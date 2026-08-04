#pragma once

#include <ymir/gpu/api/base/gpu_swapchain.hpp>

#include <ymir/gpu/api/base/gpu_command_queue.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_commands.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_descriptor_heap.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_fence.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_resource.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_swap_chain.hpp>

#include <ymir/gpu/common/gpu_window_params.hpp>

#include <ymir/core/types.hpp>

namespace ymir::gpu {

class D3D12Swapchain : public IGPUSwapchain {
    struct Frame {
        d3d12::D3D12CommandAllocator cmdAlloc;
        d3d12::D3D12Resource renderTarget;
        d3d12::FenceCounter fenceCounter;
    };

    D3D12Swapchain(d3d12::D3D12Device &device, d3d12::D3D12SwapChain &&swapchain, d3d12::D3D12DescriptorHeap &&rtvHeap,
                   d3d12::D3D12Fence &&fence, std::vector<Frame> &&frames);

public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    static GPUObjectResult<IGPUSwapchain> Create(d3d12::D3D12Device &device, const WindowParams &windowParams,
                                                 const IGPUCommandQueue &queue, uint32 maxFramesInFlight, bool debug,
                                                 const util::PropertyBag *props);

    void SetName(std::string_view name) override;

    void Present(uint32 interval) override;

    d3d12::D3D12SwapChain &GetSwapChain() {
        return m_swapchain;
    }

    const d3d12::D3D12SwapChain &GetSwapChain() const {
        return m_swapchain;
    }

    d3d12::D3D12DescriptorHeap &GetRTVHeap() {
        return m_rtvHeap;
    }

    const d3d12::D3D12DescriptorHeap &GetRTVHeap() const {
        return m_rtvHeap;
    }

    d3d12::D3D12Fence &GetFence() {
        return m_fence;
    }

    const d3d12::D3D12Fence &GetFence() const {
        return m_fence;
    }

private:
    d3d12::D3D12Device &m_device;
    d3d12::D3D12SwapChain m_swapchain;
    d3d12::D3D12DescriptorHeap m_rtvHeap;
    d3d12::D3D12Fence m_fence;

    std::vector<Frame> m_frames;
};

} // namespace ymir::gpu
