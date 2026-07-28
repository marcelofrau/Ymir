#include <ymir/gpu/gpu_shaders.hpp>

// TODO: consider using shaderc

namespace ymir::gpu {

template <ShaderStage stage>
GPUValueResult<CompiledShader<stage>> DoCompileShader(const ShaderCompileSpec<stage> &spec) {
    if (spec.language != ShaderLanguage::HLSL) {
        return GPUOperationError{"Unsupported shader language provided to compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return GPUOperationError{"Shader compilation is unimplemented"};
}

template <ShaderStage stage>
std::optional<GPUOperationError> DoValidateShader(CompiledShader<stage> &spec) {
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to compiler"};
    }

    // TODO: validate bytecode

    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------------

GPUValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

GPUValueResult<HullShader> CompileShader(const HullShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

GPUValueResult<DomainShader> CompileShader(const DomainShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

GPUValueResult<GeometryShader> CompileShader(const GeometryShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

GPUValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

GPUValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

// -----------------------------------------------------------------------------

std::optional<GPUOperationError> ValidateShader(VertexShader &shader) {
    return DoValidateShader(shader);
}

std::optional<GPUOperationError> ValidateShader(HullShader &shader) {
    return DoValidateShader(shader);
}

std::optional<GPUOperationError> ValidateShader(DomainShader &shader) {
    return DoValidateShader(shader);
}

std::optional<GPUOperationError> ValidateShader(GeometryShader &shader) {
    return DoValidateShader(shader);
}

std::optional<GPUOperationError> ValidateShader(PixelShader &shader) {
    return DoValidateShader(shader);
}

std::optional<GPUOperationError> ValidateShader(ComputeShader &shader) {
    return DoValidateShader(shader);
}

} // namespace ymir::gpu
