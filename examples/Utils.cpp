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

#include <nxt/nxt.h>
#include <nxt/nxtcpp.h>
#include <shaderc/shaderc.hpp>
#include "GLFW/glfw3.h"

#include "BackendBinding.h"
#include "../src/wire/TerribleCommandBuffer.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>

BackendBinding* CreateMetalBinding();

namespace backend {
    void RegisterSynchronousErrorCallback(nxtDevice device, void(*)(const char*, void*), void* userData);

    namespace opengl {
        void Init(void* (*getProc)(const char*), nxtProcTable* procs, nxtDevice* device);
        void HACKCLEAR();
    }
}

class OpenGLBinding : public BackendBinding {
    public:
        void SetupGLFWWindowHints() override {
            #ifdef __APPLE__
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            #else
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            #endif
        }
        void GetProcAndDevice(nxtProcTable* procs, nxtDevice* device) override {
            glfwMakeContextCurrent(window);
            backend::opengl::Init(reinterpret_cast<void*(*)(const char*)>(glfwGetProcAddress), procs, device);
        }
        void SwapBuffers() override {
            glfwSwapBuffers(window);
            backend::opengl::HACKCLEAR();
        }
};

enum class BackendType {
    OpenGL,
    Metal,
};

enum class CmdBufType {
    None,
    Terrible,
};

static BackendType backendType = BackendType::OpenGL;
static CmdBufType cmdBufType = CmdBufType::Terrible;
static BackendBinding* binding = nullptr;

static GLFWwindow* window = nullptr;

static nxt::wire::CommandHandler* wireServer = nullptr;
static nxt::wire::TerribleCommandBuffer* cmdBuf = nullptr;

void HandleSynchronousError(const char* errorMessage, void* userData) {
    std::cerr << errorMessage << std::endl;

    if (userData != nullptr) {
        auto wireServer = reinterpret_cast<nxt::wire::CommandHandler*>(userData);
        wireServer->OnSynchronousError();
    }
}

void GetProcTableAndDevice(nxtProcTable* procs, nxt::Device* device) {
    switch (backendType) {
        case BackendType::OpenGL:
            binding = new OpenGLBinding;
            break;
        case BackendType::Metal:
            #if defined(__APPLE__)
                binding = CreateMetalBinding();
            #else
                fprintf(stderr, "Metal backend no present on this platform\n");
            #endif
            break;
    }

    if (!glfwInit()) {
        return;
    }

    binding->SetupGLFWWindowHints();
    window = glfwCreateWindow(640, 480, "NXT window", nullptr, nullptr);
    if (!window) {
        return;
    }

    binding->SetWindow(window);

    nxtDevice backendDevice;
    nxtProcTable backendProcs;
    binding->GetProcAndDevice(&backendProcs, &backendDevice);

    switch (cmdBufType) {
        case CmdBufType::None:
            *procs = backendProcs;
            *device = nxt::Device::Acquire(backendDevice);
            break;

        case CmdBufType::Terrible:
            {
                wireServer = nxt::wire::CreateCommandHandler(backendDevice, backendProcs);
                cmdBuf = new nxt::wire::TerribleCommandBuffer(wireServer);

                nxtDevice clientDevice;
                nxtProcTable clientProcs;
                nxt::wire::NewClientDevice(&clientProcs, &clientDevice, cmdBuf);

                *procs = clientProcs;
                *device = nxt::Device::Acquire(clientDevice);
            }
            break;
    }

    backend::RegisterSynchronousErrorCallback(backendDevice, HandleSynchronousError, wireServer);
}

nxt::ShaderModule CreateShaderModule(const nxt::Device& device, nxt::ShaderStage stage, const char* source) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    shaderc_shader_kind kind;
    switch (stage) {
        case nxt::ShaderStage::Vertex:
            kind = shaderc_glsl_vertex_shader;
            break;
        case nxt::ShaderStage::Fragment:
            kind = shaderc_glsl_fragment_shader;
            break;
        case nxt::ShaderStage::Compute:
            kind = shaderc_glsl_compute_shader;
            break;
    }

    auto result = compiler.CompileGlslToSpv(source, strlen(source), kind, "myshader?", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << result.GetErrorMessage();
        return {};
    }

    size_t size = (result.cend() - result.cbegin());

#ifdef DUMP_SPIRV_ASSEMBLY
    {
        auto resultAsm = compiler.CompileGlslToSpvAssembly(source, strlen(source), kind, "myshader?", options);
        size_t sizeAsm = (resultAsm.cend() - resultAsm.cbegin());

        char* buffer = reinterpret_cast<char*>(malloc(sizeAsm + 1));
        memcpy(buffer, resultAsm.cbegin(), sizeAsm);
        buffer[sizeAsm] = '\0';
        printf("SPIRV ASSEMBLY DUMP START\n%s\nSPIRV ASSEMBLY DUMP END\n", buffer);
        free(buffer);
    }
#endif

#ifdef DUMP_SPIRV_JS_ARRAY
    printf("SPIRV JS ARRAY DUMP START\n");
    for (size_t i = 0; i < size; i++) {
        printf("%#010x", result.cbegin()[i]);
        if ((i + 1) % 4 == 0) {
            printf(",\n");
        } else {
            printf(", ");
        }
    }
    printf("\n");
    printf("SPIRV JS ARRAY DUMP END\n");
#endif

    return device.CreateShaderModuleBuilder()
        .SetSource(size, result.cbegin())
        .GetResult();
}

extern "C" {
    bool InitUtils(int argc, const char** argv) {
        for (int i = 0; i < argc; i++) {
            if (std::string("-b") == argv[i] || std::string("--backend") == argv[i]) {
                i++;
                if (i < argc && std::string("opengl") == argv[i]) {
                    backendType = BackendType::OpenGL;
                    continue;
                }
                if (i < argc && std::string("metal") == argv[i]) {
                    backendType = BackendType::Metal;
                    continue;
                }
                fprintf(stderr, "--backend expects a backend name (opengl, metal)\n");
                return false;
            }
            if (std::string("-c") == argv[i] || std::string("--comand-buffer") == argv[i]) {
                i++;
                if (i < argc && std::string("none") == argv[i]) {
                    cmdBufType = CmdBufType::None;
                    continue;
                }
                if (i < argc && std::string("terrible") == argv[i]) {
                    cmdBufType = CmdBufType::Terrible;
                    continue;
                }
                fprintf(stderr, "--command-buffer expects a command buffer name (none, terrible)\n");
                return false;
            }
            if (std::string("-h") == argv[i] || std::string("--help") == argv[i]) {
                printf("Usage: %s [-b BACKEND] [-c COMMAND_BUFFER]\n", argv[0]);
                printf("  BACKEND is one of: opengl, metal\n");
                printf("  COMMAND_BUFFER is one of: none, terrible\n");
                return false;
            }
        }
        return true;
    }

    void GetProcTableAndDevice(nxtProcTable* procs, nxtDevice* device) {
        nxt::Device cppDevice;
        GetProcTableAndDevice(procs, &cppDevice);
        *device = cppDevice.Release();
    }

    nxtShaderModule CreateShaderModule(nxtDevice device, nxtShaderStage stage, const char* source) {
        return CreateShaderModule(device, static_cast<nxt::ShaderStage>(stage), source).Release();
    }

    void SwapBuffers() {
        if (cmdBuf) {
            cmdBuf->Flush();
        }
        glfwPollEvents();
        binding->SwapBuffers();
    }

    bool ShouldQuit() {
        return glfwWindowShouldClose(window);
    }

    GLFWwindow* GetWindow() {
        return window;
    }
}
