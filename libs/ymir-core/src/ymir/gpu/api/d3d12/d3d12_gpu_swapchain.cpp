#include <ymir/gpu/api/d3d12/d3d12_gpu_swapchain.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

#include <fmt/format.h>

#include <cassert>

namespace ymir::gpu {

D3D12Swapchain::D3D12Swapchain(d3d12::D3D12Device &device, d3d12::D3D12SwapChain swapchain,
                               std::vector<Surface> &&surfaces)
    : IGPUSwapchain(kBackend, std::move(surfaces))
    , m_device(device)
    , m_swapchain(std::move(swapchain)) {}

GPUObjectResult<IGPUSwapchain> D3D12Swapchain::Create(d3d12::D3D12Device &device, D3D12DescriptorManager &descMgr,
                                                      const WindowParams &windowParams, const IGPUCommandQueue &queue,
                                                      uint32 maxFramesInFlight, bool debug,
                                                      const util::PropertyBag *props) {
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
        return GPUOperationError{"Failed to create swapchain"};
    }

    std::vector<Surface> surfaces{maxFramesInFlight};

    for (int i = 0; i < maxFramesInFlight; ++i) {
        ID3D12Resource *backbuffer = nullptr;
        HRESULT hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer));
        if (FAILED(hr)) {
            return GPUOperationError{fmt::format("Could not get swapchain backbuffer #{}", i)};
        }

        const TextureSpec texSpec{
            .dimensions = TextureDimensions::Tex2D,
            .format = TextureFormat::R8G8B8A8_UNORM,
            .usage = TextureUsage::RenderTarget,
            .width = desc.Width,
            .height = desc.Height,
            .depthOrArrayLength = 1,
            .mipLevels = 1,
            .swapchain = true,
        };
        auto textureResult = D3D12Texture::CreateFromResource(device, texSpec, backbuffer);
        if (!textureResult) {
            return GPUOperationError{
                fmt::format("Could not create D3D12Texture for backbuffer #{}: {}", i, textureResult.Error().message)};
        }
        surfaces[i].texture = textureResult.Value();

        const TextureViewSpec viewSpec{
            .texture = surfaces[i].texture.get(),
            .type = TextureViewType::RenderTarget,
        };

        auto viewResult = D3D12TextureView::Create(descMgr, viewSpec);
        if (!viewResult) {
            return GPUOperationError{
                fmt::format("Could not create D3D12TextureView for backbuffer #{}: {}", i, viewResult.Error().message)};
        }
        surfaces[i].view = viewResult.Value();
    }

    return {std::make_unique<D3D12Swapchain>(device, std::move(swapchain), std::move(surfaces))};
}

void D3D12Swapchain::SetName(std::string_view name) {
    for (uint32 i = 0; i < GetSurfaceCount(); ++i) {
        const Surface *surface = GetSurface(i);
        assert(surface != nullptr);
        assert(surface->texture.get() != nullptr);
        assert(surface->view.get() != nullptr);
        surface->texture->SetName(fmt::format("{} texture #{}", name, i));
        surface->view->SetName(fmt::format("{} texture RTV #{}", name, i));
    }
}

} // namespace ymir::gpu
