#include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

#include <ymir/util/unreachable.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12TextureView::D3D12TextureView(D3D12DescriptorManager &descMgr, const TextureViewSpec &spec,
                                   D3D12DescriptorAllocation alloc)
    : IGPUTextureView(kBackend, spec)
    , m_descMgr(descMgr)
    , m_alloc(alloc) {}

D3D12TextureView::~D3D12TextureView() {
    m_descMgr.Free(m_alloc);
}

GPUObjectResult<IGPUTextureView> D3D12TextureView::Create(D3D12DescriptorManager &descMgr,
                                                          const TextureViewSpec &spec) {
    auto result = [&]() -> GPUValueResult<D3D12DescriptorAllocation> {
        switch (spec.type) {
        case TextureViewType::None: return GPUOperationError{"No texture view type provided"};
        case TextureViewType::RenderTarget: return descMgr.AllocateRTV(spec);
        case TextureViewType::DepthTarget: return descMgr.AllocateDSV(spec);
        case TextureViewType::ShaderRead: return descMgr.AllocateSRV(spec);
        case TextureViewType::ShaderWrite: return descMgr.AllocateUAV(spec);
        default: return GPUOperationError{"Illegal texture view type"};
        }
    }();
    if (!result) {
        return GPUOperationError{fmt::format("Failed to allocate texture view: {}", result.Error().message)};
    }
    return {std::make_unique<D3D12TextureView>(descMgr, spec, result.Value())};
}

void D3D12TextureView::SetName(std::string_view name) {
    // Resource views cannot be named in D3D12
}

} // namespace ymir::gpu
