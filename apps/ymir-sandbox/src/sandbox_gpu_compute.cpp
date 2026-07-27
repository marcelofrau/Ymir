#include <ymir/gpu/gpu.hpp>

#if YMIR_PLATFORM_HAS_DIRECT3D12
    #include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>

    #include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>
#endif

#include <ymir/util/scope_guard.hpp>

using namespace ymir::gpu;

int runYmirGPUComputeSandbox() {
#if YMIR_PLATFORM_HAS_DIRECT3D12
    ymir::gpu::d3d12::DebugLayer::Get().Init();
    util::ScopeGuard sgShutdownDebug{[&] { ymir::gpu::d3d12::DebugLayer::Get().Shutdown(); }};
#endif

    GPUDeviceManager gpuDeviceMgr{};

#if YMIR_PLATFORM_HAS_DIRECT3D12
    auto result = gpuDeviceMgr.Create(D3D12GPUDeviceSpec{
        .debug = true,
        .num32BitConstants = 2,
        .heaps =
            {
                .maxResources = 1024,
                .maxSamplers = 64,
                .maxRTVs = 16,
                .maxDSVs = 16,

                .resourceHeapName = "Ymir resource heap",
                .samplerHeapName = "Ymir sampler heap",
                .rtvHeapName = "Ymir RTV heap",
                .dsvHeapName = "Ymir DSV heap",
            },
        .featureLevel = D3D_FEATURE_LEVEL_12_0,
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

    std::unique_ptr<IGPUBuffer> bufIn = gpuDevice.CreateBuffer(BufferUsage::ShaderRead, 64, 1).Value();
    std::unique_ptr<IGPUBuffer> bufOut = gpuDevice.CreateBuffer(BufferUsage::ShaderWrite, 64, 1).Value();
    std::unique_ptr<IGPUBuffer> bufUpload = gpuDevice.CreateBuffer(BufferUsage::Upload, 64, 1).Value();
    std::unique_ptr<IGPUBuffer> bufDownload = gpuDevice.CreateBuffer(BufferUsage::Download, 64, 1).Value();

    std::unique_ptr<IGPUBufferView> bufInSRV = gpuDevice.CreateBufferView(*bufIn, BufferViewType::Structured).Value();
    std::unique_ptr<IGPUBufferView> bufOutUAV = gpuDevice.CreateBufferView(*bufOut, BufferViewType::Storage).Value();

    // -----------------------------------------------------
    // Create compute pipeline

    static constexpr const char *kShaderCode = R"(
cbuffer Globals : register(b0) {
    uint bufInIndex;
    uint bufOutIndex;
}

#ifndef AMOUNT
#define AMOUNT 1
#endif

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    ByteAddressBuffer bufIn = ResourceDescriptorHeap[bufInIndex];
    RWByteAddressBuffer bufOut = ResourceDescriptorHeap[bufOutIndex];
    const uint value = bufIn.Load(id.x * 4);
#if SUBTRACT
    bufOut.Store(id.x * 4, value - AMOUNT);
#else
    bufOut.Store(id.x * 4, value + AMOUNT);
#endif
}
)";

#ifdef NDEBUG
    static constexpr bool kShaderDebug = false;
    static constexpr bool kShaderOptimize = true;
#else
    static constexpr bool kShaderDebug = true;
    static constexpr bool kShaderOptimize = false;
#endif

    CompiledShader shader = CompileShader({
                                              .stage = ShaderStage::Compute,
                                              .language = ShaderLanguage::HLSL,
                                              .format = ShaderBytecodeFormat::DXIL,
                                              .name = "Test",
                                              .sourceCode = kShaderCode,
                                              .entrypoint = "CSMain",
                                              .macros =
                                                  {
                                                      {"AMOUNT", "123"},
                                                      {"SUBTRACT"},
                                                  },
                                              .debug = kShaderDebug,
                                              .optimize = kShaderOptimize,
                                          })
                                .Value();

    std::unique_ptr<IGPUComputePipeline> computePipeline = gpuDevice.CreateComputePipeline(shader).Value();

    // -----------------------------------------------------
    // Upload buffer

    // TODO: barrier on bufUpload + bufIn

    void *bufUploadData = bufUpload->Map();
    if (bufUploadData != nullptr) {
        auto *data = static_cast<char *>(bufUploadData);
        const size_t size = bufUpload->GetTotalSize();
        for (int i = 0; i < size; i++) {
            data[i] = i;
        }
        bufUpload->Unmap();
    }

    // TODO: barrier on bufUpload + bufIn

    // -----------------------------------------------------
    // Dispatch compute shader

    struct ComputeConstants {
        uint32 bufInIndex;
        uint32 bufOutIndex;
    } constants = {
        bufInSRV->GetBindlessIndex(),
        bufOutUAV->GetBindlessIndex(),
    };

    cmdList->Reset();
    cmdList->Begin();
    cmdList->SetComputePipeline(*computePipeline);
    cmdList->SetComputeConstants(&constants, sizeof(constants));
    cmdList->Dispatch(1, 1, 1);

    // -----------------------------------------------------
    // Download buffer

    // TODO: barrier on bufDownload + bufOut

    // TODO: download

    // TODO: barrier on bufDownload + bufOut

    cmdList->End();

    // -----------------------------------------------------
    // Execute command list

    cmdQueue->CommitCommandList(*cmdList);
    cmdQueue->Wait();

    // -----------------------------------------------------
    // Print result

    void *bufDownloadData = bufDownload->Map();
    if (bufDownloadData != nullptr) {
        auto *data = static_cast<char *>(bufDownloadData);
        const size_t size = bufDownload->GetTotalSize();
        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);
        for (uint32 i = 0; i < size; ++i) {
            if (i % 16 == 0) {
                fmt::format_to(out, "{:02X} |", i);
            }

            fmt::format_to(out, " {:02X}", data[i]);

            if (i % 16 == 15) {
                /*fmt::format_to(out, " | ");
                for (uint32 j = 0; j < 16; ++j) {
                    char ch = data[i + j - 15];
                    if (ch < 0x20 || ch > 0x7F) {
                        ch = '.';
                    }
                    fmt::format_to(out, "{:c}", ch);
                }*/
                fmt::println("{}", fmt::to_string(buf));
                buf.clear();
            }
        }
        bufDownload->Unmap();
    }

    return EXIT_SUCCESS;
}
