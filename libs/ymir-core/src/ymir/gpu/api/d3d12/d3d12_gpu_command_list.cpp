#include <ymir/gpu/api/d3d12/d3d12_gpu_command_list.hpp>

#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>
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

static bool SetupBarrier(const BufferBarrierSpec &spec, D3D12_BUFFER_BARRIER &outBarrier) {
    if (spec.buffer == nullptr) {
        // TODO: report error
        return false;
    }
    auto *dx12Buffer = spec.buffer->As<D3D12Buffer>();
    if (dx12Buffer == nullptr) {
        // TODO: report error
        return false;
    }
    outBarrier.SyncBefore = util::ToD3D12Value(spec.srcStage);
    outBarrier.SyncAfter = util::ToD3D12Value(spec.dstStage);
    outBarrier.AccessBefore = util::ToD3D12Value(spec.srcAccess);
    outBarrier.AccessAfter = util::ToD3D12Value(spec.dstAccess);
    outBarrier.pResource = dx12Buffer->GetResource().GetPointer();
    outBarrier.Offset = 0;
    outBarrier.Size = spec.buffer->GetTotalSize();
    return true;
}

static void SetupBarrier(const MemoryBarrierSpec &spec, D3D12_GLOBAL_BARRIER &outBarrier) {
    outBarrier.SyncBefore = util::ToD3D12Value(spec.srcStage);
    outBarrier.SyncAfter = util::ToD3D12Value(spec.dstStage);
    outBarrier.AccessBefore = util::ToD3D12Value(spec.srcAccess);
    outBarrier.AccessAfter = util::ToD3D12Value(spec.dstAccess);
}

void D3D12CommandList::Barrier(const BufferBarrierSpec &spec) {
    D3D12_BUFFER_BARRIER barrier{};
    if (!SetupBarrier(spec, barrier)) {
        // TODO: report error
        return;
    }
    if (barrier.AccessBefore == barrier.AccessAfter) {
        return;
    }

    D3D12_BARRIER_GROUP group{
        .Type = D3D12_BARRIER_TYPE_BUFFER,
        .NumBarriers = 1,
        .pBufferBarriers = &barrier,
    };
    m_list->Barrier(1, &group);
}

void D3D12CommandList::Barrier(const MemoryBarrierSpec &spec) {
    D3D12_GLOBAL_BARRIER barrier{};
    SetupBarrier(spec, barrier);
    if (barrier.AccessBefore == barrier.AccessAfter) {
        return;
    }

    D3D12_BARRIER_GROUP group{
        .Type = D3D12_BARRIER_TYPE_GLOBAL,
        .NumBarriers = 1,
        .pGlobalBarriers = &barrier,
    };
    m_list->Barrier(1, &group);
}

void D3D12CommandList::Barrier(const BarrierGroupSpec &spec) {
    std::vector<D3D12_GLOBAL_BARRIER> globalBarriers{};
    std::vector<D3D12_TEXTURE_BARRIER> textureBarriers{};
    std::vector<D3D12_BUFFER_BARRIER> bufferBarriers{};
    std::vector<D3D12_BARRIER_GROUP> groups{};

    for (const MemoryBarrierSpec &barrier : spec.memoryBarriers) {
        SetupBarrier(barrier, globalBarriers.emplace_back());
    }
    // TODO: implement texture barriers
    for (const BufferBarrierSpec &barrier : spec.bufferBarriers) {
        SetupBarrier(barrier, bufferBarriers.emplace_back());
    }

    if (!globalBarriers.empty()) {
        D3D12_BARRIER_GROUP &group = groups.emplace_back();
        group.Type = D3D12_BARRIER_TYPE_GLOBAL;
        group.NumBarriers = globalBarriers.size();
        group.pGlobalBarriers = globalBarriers.data();
    }
    if (!textureBarriers.empty()) {
        D3D12_BARRIER_GROUP &group = groups.emplace_back();
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = textureBarriers.size();
        group.pTextureBarriers = textureBarriers.data();
    }

    if (!bufferBarriers.empty()) {
        D3D12_BARRIER_GROUP &group = groups.emplace_back();
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = bufferBarriers.size();
        group.pBufferBarriers = bufferBarriers.data();
    }

    if (!groups.empty()) {
        m_list->Barrier(groups.size(), groups.data());
    }
}

void D3D12CommandList::CopyBufferToBuffer(IGPUBuffer &src, IGPUBuffer &dst) {
    auto *dx12Src = src.As<D3D12Buffer>();
    auto *dx12Dst = dst.As<D3D12Buffer>();
    if (dx12Src == nullptr || dx12Dst == nullptr) {
        // TODO: output error
        return;
    }

    m_list->CopyBufferRegion(dx12Dst->GetResource().GetPointer(), 0, dx12Src->GetResource().GetPointer(), 0,
                             src.GetTotalSize());
}

void D3D12CommandList::SetComputePipeline(const IGPUComputePipeline &pipeline) {
    if (auto *dx12Pipeline = pipeline.As<D3D12ComputePipeline>()) {
        m_list->SetPipelineState(dx12Pipeline->GetPipelineState().GetPointer());
        m_list->SetComputeRootSignature(m_device.GetRootSignature().GetPointer());
    }
    // TODO: return error
}

void D3D12CommandList::SetComputeConstants(const void *data, size_t size) {
    m_list->SetComputeRoot32BitConstants(0, size / sizeof(uint32), data, 0);
}

void D3D12CommandList::Dispatch(uint32 x, uint32 y, uint32 z) {
    m_list->Dispatch(x, y, z);
}

} // namespace ymir::gpu
