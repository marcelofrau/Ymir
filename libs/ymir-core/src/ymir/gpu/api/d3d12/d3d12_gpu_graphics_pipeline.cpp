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

    auto makeShaderBytecode = [](const auto *shader) -> D3D12_SHADER_BYTECODE {
        if (shader == nullptr) {
            return D3D12_SHADER_BYTECODE{.pShaderBytecode = nullptr, .BytecodeLength = 0};
        }
        return D3D12_SHADER_BYTECODE{.pShaderBytecode = shader->bytecode.data(),
                                     .BytecodeLength = shader->bytecode.size()};
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSig.GetPointer();
    desc.VS = makeShaderBytecode(spec.shaders.vertex);
    desc.PS = makeShaderBytecode(spec.shaders.pixel);
    desc.DS = makeShaderBytecode(spec.shaders.domain);
    desc.HS = makeShaderBytecode(spec.shaders.hull);
    desc.GS = makeShaderBytecode(spec.shaders.geometry);
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.RasterizerState.FillMode = util::ToD3D12Value(spec.fillMode);
    desc.RasterizerState.CullMode = util::ToD3D12Value(spec.cullMode);
    desc.RasterizerState.FrontCounterClockwise = spec.windingCCW;
    desc.PrimitiveTopologyType = util::ToD3D12Value(spec.topology);
    desc.SampleDesc = {.Count = 1, .Quality = 0};

    desc.NumRenderTargets = std::min<UINT>(spec.renderTargets.size(), 8u);
    for (UINT i = 0; i < desc.NumRenderTargets; ++i) {
        const RenderTargetSpec &renderTargetSpec = spec.renderTargets[i];
        desc.RTVFormats[i] = util::ToD3D12Value(renderTargetSpec.format);

        D3D12_RENDER_TARGET_BLEND_DESC &blendDesc = desc.BlendState.RenderTarget[i];
        blendDesc.SrcBlend = D3D12_BLEND_ONE;
        blendDesc.DestBlend = D3D12_BLEND_ZERO;
        blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        if (renderTargetSpec.blendMode == BlendMode::Blend) {
            blendDesc.BlendEnable = TRUE;
            blendDesc.SrcBlend = util::ToD3D12Value(renderTargetSpec.blendParams.src);
            blendDesc.DestBlend = util::ToD3D12Value(renderTargetSpec.blendParams.dst);
            blendDesc.BlendOp = util::ToD3D12Value(renderTargetSpec.blendParams.op);
            blendDesc.SrcBlendAlpha = util::ToD3D12Value(renderTargetSpec.blendParams.srcAlpha);
            blendDesc.DestBlendAlpha = util::ToD3D12Value(renderTargetSpec.blendParams.dstAlpha);
            blendDesc.BlendOpAlpha = util::ToD3D12Value(renderTargetSpec.blendParams.opAlpha);
        } else if (renderTargetSpec.blendMode == BlendMode::Logic) {
            blendDesc.LogicOpEnable = TRUE;
            blendDesc.LogicOp = util::ToD3D12Value(renderTargetSpec.logicParams.op);
        }
    }

    if (spec.depthEnable) {
        desc.DepthStencilState.DepthEnable = true;
        if (spec.depthWrite) {
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        } else {
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        }
        desc.DepthStencilState.DepthFunc = util::ToD3D12Value(spec.depthOperation);
        desc.DSVFormat = util::ToD3D12Value(spec.depthFormat);
        if (spec.depthClamp) {
            desc.RasterizerState.DepthBias = 0;
            desc.RasterizerState.DepthBiasClamp = 0.0f;
            desc.RasterizerState.SlopeScaledDepthBias = 0.0f;
        }
    }
    desc.RasterizerState.DepthClipEnable = spec.depthClip;

    d3d12::D3D12PipelineState pipelineState{};
    HRESULT hr = pipelineState.CreateGraphics(device, desc);
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to build graphics pipeline: error code {:X}", hr)};
    }

    return {std::make_unique<D3D12GraphicsPipeline>(std::move(pipelineState), spec)};
}

void D3D12GraphicsPipeline::SetName(std::string_view name) {
    m_pipelineState->SetName(util::StringToWString(name).c_str());
}

} // namespace ymir::gpu
