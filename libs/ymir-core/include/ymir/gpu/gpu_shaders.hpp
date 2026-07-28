#pragma once

/**
@file
@brief GPU shader operations.

Shaders can be loaded in Ymir in source code or precompiled forms. Ymir uses the following compilers:
- DXC on Windows
- shaderc on Linux and FreeBSD
- Metal shader compiler on macOS

`ymir::gpu::CompileShader(const ShaderCompilerSpec &spec)` is used to compile a shader from source code. Ymir will
attempt to pick the best available compiler automatic from the given combination of language and bytecode type:
- HLSL -> DXIL   : DXC or shaderc
- HLSL -> SPIR-V : DXC or shaderc
- MSL  -> MetaLib: Metal shader compiler

Direct3D 12 uses DXIL shaders.
Vulkan uses SPIR-V shaders.
Metal uses MetaLib shaders.
*/

#include <ymir/gpu/common/gpu_defs.hpp>
#include <ymir/gpu/common/gpu_result.hpp>

#include <optional>

namespace ymir::gpu {

/// @brief Compiles the specified vertex shader.
/// @param[in] spec vertex shader compilation specifications
/// @return the compiled vertex shader or an error
GPUValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec);

/// @brief Compiles the specified hull shader.
/// @param[in] spec hull shader compilation specifications
/// @return the compiled hull shader or an error
GPUValueResult<HullShader> CompileShader(const HullShaderCompileSpec &spec);

/// @brief Compiles the specified domain shader.
/// @param[in] spec domain shader compilation specifications
/// @return the compiled domain shader or an error
GPUValueResult<DomainShader> CompileShader(const DomainShaderCompileSpec &spec);

/// @brief Compiles the specified geometry shader.
/// @param[in] spec geometry shader compilation specifications
/// @return the compiled geometry shader or an error
GPUValueResult<GeometryShader> CompileShader(const GeometryShaderCompileSpec &spec);

/// @brief Compiles the specified pixel shader.
/// @param[in] spec pixel shader compilation specifications
/// @return the compiled pixel shader or an error
GPUValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec);

/// @brief Compiles the specified compute shader.
/// @param[in] spec compute shader compilation specifications
/// @return the compiled compute shader or an error
GPUValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec);

// -----------------------------------------------------------------------------

/// @brief Validates the specified vertex shader bytecode.
/// @param[in,out] shader the vertex shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(VertexShader &shader);

/// @brief Validates the specified hull shader bytecode.
/// @param[in,out] shader the hull shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(HullShader &shader);

/// @brief Validates the specified domain shader bytecode.
/// @param[in,out] shader the domain shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(DomainShader &shader);

/// @brief Validates the specified geometry shader bytecode.
/// @param[in,out] shader the geometry shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(GeometryShader &shader);

/// @brief Validates the specified pixel shader bytecode.
/// @param[in,out] shader the pixel shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(PixelShader &shader);

/// @brief Validates the specified compute shader bytecode.
/// @param[in,out] shader the compute shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(ComputeShader &shader);

} // namespace ymir::gpu
