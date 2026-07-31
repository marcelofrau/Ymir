#include <ymir/gpu/api/d3d12/d3d12_gpu_swapchain.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

namespace ymir::gpu {

D3D12Swapchain::D3D12Swapchain(d3d12::D3D12Device &device, d3d12::D3D12SwapChain swapchain)
    : IGPUSwapchain(kBackend)
    , m_device(device)
    , m_swapchain(std::move(swapchain)) {}

GPUObjectResult<IGPUSwapchain> D3D12Swapchain::Create(d3d12::D3D12Device &device, const WindowParams &windowParams,
                                                      const IGPUCommandQueue &queue, uint32 maxFramesInFlight,
                                                      bool debug, const util::PropertyBag *props) {
    auto *dx12Queue = queue.As<D3D12CommandQueue>();
    if (dx12Queue == nullptr) {
        return GPUOperationError{"Provided command queue is not a Direct3D 12 object"};
    }

    const HWND hwnd = static_cast<HWND>(windowParams.windowHandle);
    if (hwnd == nullptr) {
        return GPUOperationError{"HWND not specified"};
    }

    UINT dxgiFactoryFlags = util::PropertyBag::NullSafeGetOrDefault<props::D3D12DXGIFactoryFlags>(props);
    if (debug) {
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = windowParams.width;
    desc.Height = windowParams.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = maxFramesInFlight;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    d3d12::D3D12SwapChain swapchain{};
    if (!swapchain.Create(dxgiFactoryFlags, dx12Queue->GetCommandQueue().GetPointer(), desc, hwnd, maxFramesInFlight)) {
        return GPUOperationError{fmt::format("Failed to create swapchain")};
    }

    // TODO: create textures and views

    for (int i = 0; i < maxFramesInFlight; ++i) {
        ID3D12Resource *backbuffer = nullptr;
        HRESULT hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer));
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Could not get swapchain backbuffer #{}", i)};
        }

        const TextureSpec spec{
            .dimensions = TextureDimensions::Tex2D,
            .format = TextureFormat::R8G8B8A8_UNORM,
            .usage = TextureUsage::RenderTarget,
            .width = desc.Width,
            .height = desc.Height,
            .depthOrArrayLength = 1,
            .mipLevels = 1,
            .swapchain = true,
        };
        auto textureResult = D3D12Texture::CreateFromResource(device, spec, backbuffer);
        if (!textureResult) {
            return GPUOperationError{
                fmt::format("Could not create D3D12Texture for backbuffer #{}: {}", i, textureResult.Error().message)};
        }
    }

    return {std::make_unique<D3D12Swapchain>(device, std::move(swapchain))};
}

void D3D12Swapchain::SetName(std::string_view name) {
    // TODO: rename textures and views
}

} // namespace ymir::gpu
