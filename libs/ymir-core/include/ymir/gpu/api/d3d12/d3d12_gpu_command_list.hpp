#pragma once

#include <ymir/gpu/api/base/gpu_command_list.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_commands.hpp>

namespace ymir::gpu {

class D3D12GPUDevice;
class D3D12CommandQueue;

class D3D12CommandList final : public IGPUCommandList {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12CommandList(D3D12GPUDevice &device, D3D12CommandQueue &queue, d3d12::D3D12CommandAllocator &&allocator,
                     d3d12::D3D12GraphicsCommandList &&list);

    void SetName(std::string_view name) override;

    void Reset() override;
    void Begin() override;
    void End() override;

    void Barrier(const BufferBarrierSpec &spec) override;
    void Barrier(const MemoryBarrierSpec &spec) override;
    void Barrier(const BarrierGroupSpec &spec) override;

    void SetComputePipeline(const IGPUComputePipeline &pipeline) override;
    void SetComputeConstants(const void *data, size_t size) override;

    d3d12::D3D12CommandAllocator &GetAllocator() {
        return m_allocator;
    }

    const d3d12::D3D12CommandAllocator &GetAllocator() const {
        return m_allocator;
    }

    d3d12::D3D12GraphicsCommandList GetCommandList() {
        return m_list;
    }

    const d3d12::D3D12GraphicsCommandList GetCommandList() const {
        return m_list;
    }

private:
    D3D12GPUDevice &m_device;
    D3D12CommandQueue &m_queue;
    d3d12::D3D12CommandAllocator m_allocator;
    d3d12::D3D12GraphicsCommandList m_list;
};

} // namespace ymir::gpu
