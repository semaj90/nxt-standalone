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

#ifndef BACKEND_COMMON_COMMANDBUFFERGL_H_
#define BACKEND_COMMON_COMMANDBUFFERGL_H_

#include "nxt/nxtcpp.h"

#include "CommandAllocator.h"
#include "RefCounted.h"

#include <set>
#include <utility>

namespace backend {

    class BindGroupBase;
    class BufferBase;
    class DeviceBase;
    class PipelineBase;
    class TextureBase;

    class CommandBufferBuilder;

    class CommandBufferBase : public RefCounted {
        public:
            CommandBufferBase(CommandBufferBuilder* builder);
            bool ValidateResourceUsagesImmediate();

        private:
            DeviceBase* device;
            std::set<BufferBase*> buffersTransitioned;
            std::set<TextureBase*> texturesTransitioned;
    };

    class CommandBufferBuilder : public RefCounted {
        public:
            CommandBufferBuilder(DeviceBase* device);
            ~CommandBufferBuilder();

            bool WasConsumed() const;
            bool ValidateGetResult();

            CommandIterator AcquireCommands();

            // NXT API
            CommandBufferBase* GetResult();

            void CopyBufferToTexture(BufferBase* buffer, TextureBase* texture, uint32_t x, uint32_t y, uint32_t z,
                                     uint32_t width, uint32_t height, uint32_t depth, uint32_t level);
            void Dispatch(uint32_t x, uint32_t y, uint32_t z);
            void DrawArrays(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
            void DrawElements(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t firstInstance);
            void SetPushConstants(nxt::ShaderStageBit stage, uint32_t offset, uint32_t count, const void* data);
            void SetPipeline(PipelineBase* pipeline);
            void SetBindGroup(uint32_t groupIndex, BindGroupBase* group);
            void SetIndexBuffer(BufferBase* buffer, uint32_t offset, nxt::IndexFormat format);

            template<typename T>
            void SetVertexBuffers(uint32_t startSlot, uint32_t count, T* const* buffers, uint32_t const* offsets) {
                static_assert(std::is_base_of<BufferBase, T>::value, "");
                SetVertexBuffers(startSlot, count, reinterpret_cast<BufferBase* const*>(buffers), offsets);
            }
            void SetVertexBuffers(uint32_t startSlot, uint32_t count, BufferBase* const* buffers, uint32_t const* offsets);

            void TransitionBufferUsage(BufferBase* buffer, nxt::BufferUsageBit usage);
            void TransitionTextureUsage(TextureBase* texture, nxt::TextureUsageBit usage);

        private:
            friend class CommandBufferBase;

            void MoveToIterator();

            DeviceBase* device;
            CommandAllocator allocator;
            CommandIterator iterator;
            bool consumed = false;
            bool movedToIterator = false;
            // These pointers will remain valid since they are referenced by
            // the bind groups which are referenced by this command buffer.
            std::set<BufferBase*> buffersTransitioned;
            std::set<TextureBase*> texturesTransitioned;
    };

}

#endif // BACKEND_COMMON_COMMANDBUFFERGL_H_
