#include <ymir/gpu/api/d3d12/d3d12_gpu_graphics_pipeline.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12GraphicsPipeline::D3D12GraphicsPipeline(d3d12::D3D12PipelineState &&pipelineState,
                                             const GraphicsPipelineSpec &spec)
    : IGPUGraphicsPipeline(kBackend, spec)
    , m_pipelineState(std::move(pipelineState)) {}

GPUObjectResult<IGPUGraphicsPipeline> D3D12GraphicsPipeline::Create(d3d12::D3D12Device &device,
                                                                    const d3d12::D3D12RootSignature &rootSig,
                                                                    const GraphicsPipelineSpec &spec) {
    if (spec.shaders.vertex == nullptr || spec.shaders.pixel == nullptr) {
        return GPUOperationError{"Vertex and pixel shaders are required"};
    }

    if (spec.shaders.vertex->format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported vertex shader format"};
    }
    if (spec.shaders.hull != nullptr && spec.shaders.hull->format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported hull shader format"};
    }
    if (spec.shaders.domain != nullptr && spec.shaders.domain->format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported domain shader format"};
    }
    if (spec.shaders.geometry != nullptr && spec.shaders.geometry->format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported geometry shader format"};
    }
    if (spec.shaders.pixel->format != ShaderBytecodeFormat::DXIL) {
        return GPUOperationError{"Unsupported pixel shader format"};
    }

    return GPUOperationError{"Unimplemented"};

    // TODO: implement
    // d3d12::D3D12PipelineState pipelineState{};
    // auto builder = pipelineState.GraphicsBuilder();
    // TODO: builder.NodeMask(nodeMask);
    // TODO: builder.Flags(...)?
    // TODO: builder.CachedPSO()?
    // HRESULT hr = builder.Build(device, rootSig, shader.bytecode.data(), shader.bytecode.size());
    // if (FAILED(hr)) {
    //     return GPUOperationError{fmt::format("Failed to build graphics pipeline: error code {:X}", hr)};
    // }
    //
    // return {std::make_unique<D3D12GraphicsPipeline>(std::move(pipelineState), spec)};
}

void D3D12GraphicsPipeline::SetName(std::string_view name) {
    m_pipelineState->SetName(util::StringToWString(name).c_str());
}

} // namespace ymir::gpu
