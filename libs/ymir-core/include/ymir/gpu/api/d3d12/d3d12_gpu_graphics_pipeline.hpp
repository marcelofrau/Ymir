#pragma once

#include <ymir/gpu/api/base/gpu_graphics_pipeline.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_pipeline_state.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_root_signature.hpp>

#include <ymir/gpu/common/gpu_result.hpp>

namespace ymir::gpu {

class D3D12GraphicsPipeline final : public IGPUGraphicsPipeline {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12GraphicsPipeline(d3d12::D3D12PipelineState &&pipelineState, const GraphicsPipelineSpec &spec);

    static GPUObjectResult<IGPUGraphicsPipeline>
    Create(d3d12::D3D12Device &device, const d3d12::D3D12RootSignature &rootSig, const GraphicsPipelineSpec &spec);

    void SetName(std::string_view name) override;

    d3d12::D3D12PipelineState &GetPipelineState() {
        return m_pipelineState;
    }

    const d3d12::D3D12PipelineState &GetPipelineState() const {
        return m_pipelineState;
    }

private:
    d3d12::D3D12PipelineState m_pipelineState;
};

} // namespace ymir::gpu
