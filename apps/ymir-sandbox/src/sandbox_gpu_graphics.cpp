#include <ymir/gpu/gpu.hpp>

#if YMIR_PLATFORM_HAS_DIRECT3D12
    #include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>

    #include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>
#endif

#include <ymir/util/scope_guard.hpp>

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

    std::unique_ptr<IGPUCommandQueue> cmdQueue = gpuDevice.CreateCommandQueue(CommandQueueType::Compute).Value();
    std::unique_ptr<IGPUCommandList> cmdList = cmdQueue->CreateCommandList().Value();

    // TODO: surface/swapchain
    // TODO: a few textures to play with
    // TODO: vertex and index buffers probably?
    // TODO: vertex and pixel shaders

    // -----------------------------------------------------
    // Create graphics pipeline

    static constexpr const char *kVertexShaderCode = R"(
struct VSInput {
    float4 position : POSITION;
    float2 uv       : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = input.position;
    output.uv = input.uv;
    return output;
}
)";

    static constexpr const char *kPixelShaderCode = R"(
cbuffer Constants : register(b0) {
    uint textureIndex;
    uint samplerIndex;
}

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET0 {
    Texture2D    texture = ResourceDescriptorHeap[textureIndex];
    SamplerState sampler = SamplerDescriptorHeap[samplerIndex];
    return texture.Sample(sampler, input.uv);
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
                                     .windingCCW = true})
            .Value();

    // -----------------------------------------------------
    // TODO: create window and swapchain, enter main loop

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

    return EXIT_SUCCESS;
}
