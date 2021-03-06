// Copyright 2017 The NXT Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Pipeline.h"

#include "Device.h"
#include "InputState.h"
#include "PipelineLayout.h"
#include "ShaderModule.h"

namespace backend {

    // PipelineBase

    PipelineBase::PipelineBase(PipelineBuilder* builder)
        : device(builder->device), stageMask(builder->stageMask), layout(std::move(builder->layout)),
          inputState(std::move(builder->inputState)) {

        if (stageMask != (nxt::ShaderStageBit::Vertex | nxt::ShaderStageBit::Fragment) &&
            stageMask != nxt::ShaderStageBit::Compute) {
            device->HandleError("Wrong combination of stage for pipeline");
            return;
        }

        auto FillPushConstants = [](const ShaderModuleBase* module, PushConstantInfo* info) {
            const auto& moduleInfo = module->GetPushConstants();
            info->mask = moduleInfo.mask;

            for (uint32_t i = 0; i < moduleInfo.names.size(); i++) {
                unsigned int size = moduleInfo.sizes[i];
                if (size == 0) {
                    continue;
                }

                for (uint32_t offset = 0; offset < size; offset++) {
                    info->types[i + offset] = moduleInfo.types[i];
                }
                i += size - 1;
            }
        };

        for (auto stageBit : IterateStages(builder->stageMask)) {
            if (!builder->stages[stageBit].module->IsCompatibleWithPipelineLayout(layout.Get())) {
                device->HandleError("Stage not compatible with layout");
                return;
            }

            FillPushConstants(builder->stages[stageBit].module.Get(), &pushConstants[stageBit]);
        }

        if (!IsCompute()) {
            if ((builder->stages[nxt::ShaderStage::Vertex].module->GetUsedVertexAttributes() & ~inputState->GetAttributesSetMask()).any()) {
                device->HandleError("Pipeline vertex stage uses inputs not in the input state");
                return;
            }
        }
    }

    const PipelineBase::PushConstantInfo& PipelineBase::GetPushConstants(nxt::ShaderStage stage) const {
        return pushConstants[stage];
    }

    nxt::ShaderStageBit PipelineBase::GetStageMask() const {
        return stageMask;
    }

    PipelineLayoutBase* PipelineBase::GetLayout() {
        return layout.Get();
    }

    InputStateBase* PipelineBase::GetInputState() {
        return inputState.Get();
    }

    bool PipelineBase::IsCompute() const {
        return stageMask == nxt::ShaderStageBit::Compute;
    }

    // PipelineBuilder

    PipelineBuilder::PipelineBuilder(DeviceBase* device)
        : device(device), stageMask(static_cast<nxt::ShaderStageBit>(0)) {
    }

    bool PipelineBuilder::WasConsumed() const {
        return consumed;
    }

    const PipelineBuilder::StageInfo& PipelineBuilder::GetStageInfo(nxt::ShaderStage stage) const {
        ASSERT(stageMask & StageBit(stage));
        return stages[stage];
    }

    PipelineBase* PipelineBuilder::GetResult() {
        // TODO(cwallez@chromium.org): the layout should be required, and put the default objects in the device
        if (!layout) {
            layout = device->CreatePipelineLayoutBuilder()->GetResult();
        }
        if (!inputState) {
            inputState = device->CreateInputStateBuilder()->GetResult();
        }

        consumed = true;
        return device->CreatePipeline(this);
    }

    void PipelineBuilder::SetLayout(PipelineLayoutBase* layout) {
        this->layout = layout;
    }

    void PipelineBuilder::SetStage(nxt::ShaderStage stage, ShaderModuleBase* module, const char* entryPoint) {
        if (entryPoint != std::string("main")) {
            device->HandleError("Currently the entry point has to be main()");
            return;
        }

        if (stage != module->GetExecutionModel()) {
            device->HandleError("Setting module with wrong execution model");
            return;
        }

        nxt::ShaderStageBit bit = StageBit(stage);
        if (stageMask & bit) {
            device->HandleError("Setting already set stage");
            return;
        }
        stageMask |= bit;

        stages[stage].module = module;
        stages[stage].entryPoint = entryPoint;
    }

    void PipelineBuilder::SetInputState(InputStateBase* inputState) {
        this->inputState = inputState;
    }


}
