#include <ymir/gpu/api/d3d12/d3d12_gpu_command_list.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_compute_pipeline.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

namespace ymir::gpu {

D3D12CommandList::D3D12CommandList(D3D12GPUDevice &device, D3D12CommandQueue &queue,
                                   d3d12::D3D12CommandAllocator &&allocator, d3d12::D3D12GraphicsCommandList &&list)
    : IGPUCommandList(kBackend)
    , m_device(device)
    , m_queue(queue)
    , m_allocator(std::move(allocator))
    , m_list(std::move(list)) {

    m_list->Close();
}

void D3D12CommandList::SetName(std::string_view name) {
    m_list->SetName(util::StringToWString(name).c_str());
}

void D3D12CommandList::Reset() {
    // TODO: initial state?
    // TODO: handle errors
    HRESULT hr = m_allocator->Reset();
    hr = m_list->Reset(m_allocator.GetPointer(), nullptr);
}

void D3D12CommandList::Begin() {
    // TODO: heaps
    ID3D12DescriptorHeap *const heaps[] = {
        m_device.GetDescriptorManager().GetResourceHeap().GetPointer(),
        m_device.GetDescriptorManager().GetSamplerHeap().GetPointer(),
    };
    m_list->SetDescriptorHeaps(std::size(heaps), heaps);
}

void D3D12CommandList::End() {
    // TODO: handle errors
    HRESULT hr = m_list->Close();
}

void D3D12CommandList::Barrier(const BufferBarrierSpec &spec) {
    // TODO: implement
}

void D3D12CommandList::Barrier(const MemoryBarrierSpec &spec) {
    // TODO: implement
}

void D3D12CommandList::Barrier(const BarrierGroupSpec &spec) {
    // TODO: implement
}

void D3D12CommandList::SetComputeConstants(std::span<const std::byte> constData) {
    m_list->SetComputeRoot32BitConstants(0, constData.size() / sizeof(uint32), constData.data(), 0);
}

void D3D12CommandList::SetComputePipeline(const IGPUComputePipeline &pipeline) {
    if (auto *dx12Pipeline = pipeline.As<D3D12ComputePipeline>()) {
        m_list->SetPipelineState(dx12Pipeline->GetPipelineState().GetPointer());
        m_list->SetComputeRootSignature(m_device.GetRootSignature().GetPointer());
    }
    // TODO: return error
}

} // namespace ymir::gpu
