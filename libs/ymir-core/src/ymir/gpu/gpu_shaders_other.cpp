#include <ymir/gpu/gpu_shaders.hpp>

namespace ymir::gpu {

GPUValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

GPUValueResult<HullShader> CompileShader(const HullShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

GPUValueResult<DomainShader> CompileShader(const DomainShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

GPUValueResult<GeometryShader> CompileShader(const GeometryShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

GPUValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

GPUValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

// -----------------------------------------------------------------------------

std::optional<GPUOperationError> ValidateShader(VertexShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(HullShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(DomainShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(GeometryShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(PixelShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(ComputeShader &shader) {
    return GPUOperationError{"Unsupported platform"};
}

} // namespace ymir::gpu
