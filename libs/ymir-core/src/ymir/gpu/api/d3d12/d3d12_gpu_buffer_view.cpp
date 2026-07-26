#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer_view.hpp>

#include <ymir/util/unreachable.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12BufferView::D3D12BufferView(D3D12DescriptorManager &descMgr, const BufferViewSpec &spec,
                                 D3D12DescriptorAllocation alloc)
    : IGPUBufferView(kBackend, spec)
    , m_descMgr(descMgr)
    , m_alloc(alloc) {}

D3D12BufferView::~D3D12BufferView() {
    m_descMgr.Free(m_alloc);
}

GPUObjectResult<IGPUBufferView> D3D12BufferView::Create(D3D12DescriptorManager &descMgr, const BufferViewSpec &spec) {
    auto result = [&]() -> GPUValueResult<D3D12DescriptorAllocation> {
        switch (spec.type) {
        case BufferViewType::None: return GPUOperationError{"No buffer view type provided"};
        case BufferViewType::Constant: return descMgr.AllocateCBV(spec);
        case BufferViewType::Structured: return descMgr.AllocateSRV(spec);
        case BufferViewType::Storage: return descMgr.AllocateUAV(spec);
        default: return GPUOperationError{"Illegal buffer view type"};
        }
    }();
    if (!result) {
        return GPUOperationError{fmt::format("Failed to allocate buffer view: {}", result.Error().message)};
    }
    return {std::make_unique<D3D12BufferView>(descMgr, spec, result.Value())};
}

void D3D12BufferView::SetName(std::string_view name) {
    // Resource views cannot be named in D3D12
}

} // namespace ymir::gpu
