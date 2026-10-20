// Shader compilation and validation for UWP (Xbox Dev Mode) builds.
//
// UWP builds do not link against the DirectX Shader Compiler (dxcompiler.dll) to avoid shipping it in the package.
// All shaders used at runtime are precompiled to DXIL at build time with the Windows SDK DXC, which also signs them
// with dxil.dll, so runtime validation is skipped and runtime compilation is unavailable.
#include <ymir/gpu/shaders/gpu_shaders.hpp>

namespace ymir::gpu {

util::ValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return util::ErrorMessage{"Runtime shader compilation is not available on UWP"};
}

util::ValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return util::ErrorMessage{"Runtime shader compilation is not available on UWP"};
}

util::ValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return util::ErrorMessage{"Runtime shader compilation is not available on UWP"};
}

// -----------------------------------------------------------------------------

template <ShaderStage stage>
static util::VoidResult<> DoValidateShader(CompiledShader<stage> &shader) {
    if (shader.format != ShaderBytecodeFormat::DXIL) {
        return util::ErrorMessage{"Only DXIL shaders are supported on UWP"};
    }
    // Precompiled DXIL is validated and signed at build time
    return {};
}

util::VoidResult<> ValidateShader(VertexShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(PixelShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(ComputeShader &shader) {
    return DoValidateShader(shader);
}

} // namespace ymir::gpu
