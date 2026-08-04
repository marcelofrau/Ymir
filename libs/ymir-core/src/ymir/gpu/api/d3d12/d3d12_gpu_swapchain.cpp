#include <ymir/gpu/api/d3d12/d3d12_gpu_swapchain.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12Swapchain::D3D12Swapchain(d3d12::D3D12Device &device, d3d12::D3D12SwapChain &&swapchain,
                               d3d12::D3D12DescriptorHeap &&rtvHeap, d3d12::D3D12Fence &&fence,
                               std::vector<Frame> &&frames)
    : IGPUSwapchain(kBackend)
    , m_device(device)
    , m_swapchain(std::move(swapchain))
    , m_rtvHeap(std::move(rtvHeap))
    , m_frames(std::move(frames)) {}

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
        return GPUOperationError{"Failed to create swapchain"};
    }

    d3d12::D3D12DescriptorHeap rtvHeap{};
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = maxFramesInFlight;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(rtvHeap.Create(device, rtvHeapDesc))) {
        return GPUOperationError("Failed to create swapchain RTV descriptor heap");
    }

    d3d12::D3D12Fence fence{};
    if (FAILED(fence.Create(device, 0, D3D12_FENCE_FLAG_NONE))) {
        return GPUOperationError("Failed to create swapchain fence");
    }

    std::vector<Frame> frames{maxFramesInFlight};

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap.GetCPUStart());
    for (int i = 0; i < maxFramesInFlight; ++i) {
        Frame &frame = frames[i];
        ID3D12Resource *backbuffer = nullptr;
        if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&backbuffer)))) {
            return GPUOperationError{fmt::format("Could not get swapchain backbuffer #{}", i)};
        }
        if (FAILED(frame.cmdAlloc.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT))) {
            return GPUOperationError(fmt::format("Failed to create command allocator for swapchain frame #{}", i));
        }
        device->CreateRenderTargetView(backbuffer, nullptr, rtvHandle);
        frame.renderTarget.Attach(backbuffer);
        frame.fenceCounter.Bind(fence);
        rtvHandle.ptr += rtvHeap.GetDescriptorSize();
    }

    // TODO: figure out how to use the command allocators with command lists

    return {std::unique_ptr<D3D12Swapchain>{
        new D3D12Swapchain(device, std::move(swapchain), std::move(rtvHeap), std::move(fence), std::move(frames))}};
}

void D3D12Swapchain::SetName(std::string_view name) {
    m_rtvHeap->SetName(util::StringToWString(fmt::format("{} RTV heap", name)).c_str());
    for (size_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].renderTarget->SetName(util::StringToWString(fmt::format("{} buffer #{}", name, i)).c_str());
    }
}

void D3D12Swapchain::Present(uint32 interval) {
    const UINT flags = interval == 0 && m_swapchain.IsTearingSupported() ? DXGI_PRESENT_ALLOW_TEARING : 0;
    m_swapchain->Present(interval, flags);
}

} // namespace ymir::gpu
