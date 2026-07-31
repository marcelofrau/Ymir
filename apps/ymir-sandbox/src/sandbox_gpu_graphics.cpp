#include <ymir/gpu/gpu.hpp>

#if YMIR_PLATFORM_HAS_DIRECT3D12
    #include <ymir/gpu/api/d3d12/d3d12_params.hpp>

    #include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>
#endif

#include <ymir/util/scope_guard.hpp>

#include <SDL3/SDL.h>

using namespace ymir::gpu;

int runYmirGPUGraphicsSandbox() {
#if YMIR_PLATFORM_HAS_DIRECT3D12
    ymir::gpu::d3d12::DebugLayer::Get().Init();
    util::ScopeGuard sgShutdownDebug{[&] { ymir::gpu::d3d12::DebugLayer::Get().Shutdown(); }};
#endif

    GPUDeviceManager gpuDeviceMgr{};

#if YMIR_PLATFORM_HAS_DIRECT3D12
    auto result = gpuDeviceMgr.Create(D3D12GPUDeviceSpec{
        .debug = true,
        .featureLevel = D3D_FEATURE_LEVEL_12_0,
        .resourceHeap = {.maxDescriptors = 1024, .name = "Ymir resource heap"},
        .samplerHeap = {.maxDescriptors = 64, .name = "Ymir sampler heap"},
        .rtvHeap = {.maxDescriptors = 16, .name = "Ymir RTV heap"},
        .dsvHeap = {.maxDescriptors = 16, .name = "Ymir DSV heap"},
        .num32BitConstants = 2,
    });
    if (!result) {
        fmt::println("Failed to create D3D12 device: {}", result.Error().message);
        return EXIT_FAILURE;
    }
#endif

    IGPUDevice *pGpuDevice = gpuDeviceMgr.Get();
    if (pGpuDevice == nullptr) {
        fmt::println("GPU device not initialized");
        return EXIT_FAILURE;
    }
    IGPUDevice &gpuDevice = *pGpuDevice;

    // -----------------------------------------------------
    // Create resources

    std::unique_ptr<IGPUCommandQueue> cmdQueue = gpuDevice.CreateCommandQueue(CommandQueueType::Graphics).Value();
    std::unique_ptr<IGPUCommandList> cmdList = cmdQueue->CreateCommandList().Value();

    std::unique_ptr<IGPUSwapchain> swapchain;

    // TODO: a few textures to play with
    // TODO: vertex and index buffers probably?
    // TODO: vertex and pixel shaders

    // -----------------------------------------------------
    // Create graphics pipeline

    static constexpr const char *kVertexShaderCode = R"(
struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
)";

    static constexpr const char *kPixelShaderCode = R"(
struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target {
    return float4(input.uv, 0.0f, 1.0f);
}
)";

#ifdef NDEBUG
    static constexpr bool kShaderDebug = false;
    static constexpr bool kShaderOptimize = true;
#else
    static constexpr bool kShaderDebug = true;
    static constexpr bool kShaderOptimize = false;
#endif

    VertexShader vertexShader = CompileShader(VertexShaderCompileSpec{
                                                  .language = ShaderLanguage::HLSL,
                                                  .format = ShaderBytecodeFormat::DXIL,
                                                  .name = "vs_test",
                                                  .sourceCode = kVertexShaderCode,
                                                  .entrypoint = "VSMain",
                                                  .debug = kShaderDebug,
                                                  .optimize = kShaderOptimize,
                                              })
                                    .Value();
    PixelShader pixelShader = CompileShader(PixelShaderCompileSpec{
                                                .language = ShaderLanguage::HLSL,
                                                .format = ShaderBytecodeFormat::DXIL,
                                                .name = "ps_test",
                                                .sourceCode = kPixelShaderCode,
                                                .entrypoint = "PSMain",
                                                .debug = kShaderDebug,
                                                .optimize = kShaderOptimize,
                                            })
                                  .Value();

    std::unique_ptr<IGPUGraphicsPipeline> graphicsPipeline =
        gpuDevice
            .CreateGraphicsPipeline({.shaders = {.vertex = &vertexShader, .pixel = &pixelShader},
                                     .fillMode = FillMode::Solid,
                                     .cullMode = CullMode::None,
                                     .topology = PrimitiveTopology::Triangles,
                                     .windingCCW = true,
                                     .renderTargets = {RenderTargetSpec{.format = TextureFormat::B8G8R8A8_UNORM}}})
            .Value();

    // -----------------------------------------------------
    // Create window and swapchain, and enter main loop

    SDL_Window *window = SDL_CreateWindow("Sandbox", 640, 480, 0);
    SDL_PropertiesID windowProps = SDL_GetWindowProperties(window);
    const auto hwnd = (HWND)SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);

    const WindowParams windowParams{.windowHandle = hwnd, .width = (uint32)ww, .height = (uint32)wh};
    swapchain = gpuDevice.CreateSwapchain(windowParams, *cmdQueue, 3).Value();

    SDL_Event evt;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&evt)) {
            switch (evt.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    running = false;
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    // Release all outstanding references to the swap chain's buffers before resizing.
                    // d3d.gfx.ResizeSwapChainBuffers(evt.window.data1, evt.window.data2);
                }
            }
        }

        // -----------------------------------------------------
        // Begin command list

        cmdList->Reset();
        cmdList->Begin();

        // -----------------------------------------------------
        // TODO: render some graphics

        // -----------------------------------------------------
        // Execute command list

        cmdList->End();
        cmdQueue->CommitCommandList(*cmdList);
        cmdQueue->Wait();

        // -----------------------------------------------------
        // Present frame

        // TODO: swapchain->Present(...);
    }

    return EXIT_SUCCESS;
}
