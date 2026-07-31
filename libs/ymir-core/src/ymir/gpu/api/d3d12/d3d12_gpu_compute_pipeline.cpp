#include <ymir/gpu/api/d3d12/d3d12_gpu_compute_pipeline.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12ComputePipeline::D3D12ComputePipeline(d3d12::D3D12PipelineState &&pipelineState, const ComputeShader &shader)
    : IGPUComputePipeline(kBackend, shader)
    , m_pipelineState(std::move(pipelineState)) {}

GPUObjectResult<IGPUComputePipeline> D3D12ComputePipeline::Create(d3d12::D3D12Device &device,
                                                                  const d3d12::D3D12RootSignature &rootSig,
                                                                  const ComputeShader &shader) {
    if (shader.format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported shader format"};
    }

    d3d12::D3D12PipelineState pipelineState{};
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc{
        .pRootSignature = rootSig.GetPointer(),
        .CS = {.pShaderBytecode = shader.bytecode.data(), .BytecodeLength = shader.bytecode.size()},
        // TODO: NodeMask, CachedPSO, Flags
    };
    HRESULT hr = pipelineState.CreateCompute(device, desc);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to build compute pipeline: error code {:X}", hr)};
    }

    return {std::make_unique<D3D12ComputePipeline>(std::move(pipelineState), shader)};
}

void D3D12ComputePipeline::SetName(std::string_view name) {
    m_pipelineState->SetName(util::StringToWString(name).c_str());
}

} // namespace ymir::gpu
