#pragma once

#include <ymir/gpu/common/gpu_defs.hpp>

#include <d3d12.h>

#include <string>
#include <string_view>

namespace util {

/// @brief Converts the specified command queue type to the corresponding Direct3D 12 enumeration value.
/// @param[in] type the command queue type
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_COMMAND_LIST_TYPE ToD3D12Value(ymir::gpu::CommandQueueType type) {
    switch (type) {
    case ymir::gpu::CommandQueueType::Graphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case ymir::gpu::CommandQueueType::Compute: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case ymir::gpu::CommandQueueType::Copy: return D3D12_COMMAND_LIST_TYPE_COPY;
    case ymir::gpu::CommandQueueType::VideoEncode: return D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
    default: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

/// @brief Converts the specified texture format to the corresponding Direct3D 12 enumeration value.
/// @param[in] type the texture format
/// @return the corresponding value in the Direct3D 12 API
inline DXGI_FORMAT ToD3D12Value(ymir::gpu::TextureFormat format) {
    switch (format) {
    case ymir::gpu::TextureFormat::Undefined: return DXGI_FORMAT_UNKNOWN;
    case ymir::gpu::TextureFormat::R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
    case ymir::gpu::TextureFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ymir::gpu::TextureFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case ymir::gpu::TextureFormat::R8_UINT: return DXGI_FORMAT_R8_UINT;
    case ymir::gpu::TextureFormat::R16_UINT: return DXGI_FORMAT_R16_UINT;
    case ymir::gpu::TextureFormat::R32_UINT: return DXGI_FORMAT_R32_UINT;
    case ymir::gpu::TextureFormat::R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
    case ymir::gpu::TextureFormat::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
    case ymir::gpu::TextureFormat::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case ymir::gpu::TextureFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    case ymir::gpu::TextureFormat::D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/// @brief Converts the specified resource layout to the corresponding Direct3D 12 enumeration value.
/// @param[in] layout the resource layout
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_BARRIER_LAYOUT ToD3D12Value(ymir::gpu::ResourceLayout layout) {
    switch (layout) {
    case ymir::gpu::ResourceLayout::Undefined: return D3D12_BARRIER_LAYOUT_UNDEFINED;
    case ymir::gpu::ResourceLayout::General: return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    case ymir::gpu::ResourceLayout::ReadOnly: return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    case ymir::gpu::ResourceLayout::RenderTarget: return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    case ymir::gpu::ResourceLayout::DepthStencilRead: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
    case ymir::gpu::ResourceLayout::DepthStencilWrite: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
    case ymir::gpu::ResourceLayout::TransferSource: return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    case ymir::gpu::ResourceLayout::TransferDestination: return D3D12_BARRIER_LAYOUT_COPY_DEST;
    case ymir::gpu::ResourceLayout::Present: return D3D12_BARRIER_LAYOUT_PRESENT;
    default: return D3D12_BARRIER_LAYOUT_UNDEFINED;
    }
}

/// @brief Converts the specified pipeline stage mask to the corresponding Direct3D 12 enumeration values.
/// @param[in] stage the pipeline stage mask
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_BARRIER_SYNC ToD3D12Value(ymir::gpu::PipelineStageMask stage) {
    using Mask = ymir::gpu::PipelineStageMask;
    BitmaskEnum bmStage{stage};

    D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;

    if (bmStage.AnyOf(Mask::TopOfPipe)) {
        sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
    }
    if (bmStage.AnyOf(Mask::DrawIndirect)) {
        sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
    }
    if (bmStage.AnyOf(Mask::VertexInput)) {
        sync |= D3D12_BARRIER_SYNC_DRAW;
    }
    if (bmStage.AnyOf(Mask::IndexInput)) {
        sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
    }
    if (bmStage.AnyOf(Mask::VertexShader)) {
        sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
    }
    if (bmStage.AnyOf(Mask::HullShader | Mask::DomainShader | Mask::GeometryShader)) {
        sync |= D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
    }
    if (bmStage.AnyOf(Mask::PixelShader)) {
        sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
    }
    if (bmStage.AnyOf(Mask::ComputeShader)) {
        sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    }
    if (bmStage.AnyOf(Mask::RenderTargetOutput)) {
        sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
    }
    if (bmStage.AnyOf(Mask::Resolve)) {
        sync |= D3D12_BARRIER_SYNC_RESOLVE;
    }
    if (bmStage.AnyOf(Mask::Copy)) {
        sync |= D3D12_BARRIER_SYNC_COPY;
    }
    if (bmStage.AnyOf(Mask::BottomOfPipe)) {
        sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
    }

    if (bmStage.AnyOf(Mask::AllGraphics)) {
        sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
    }
    if (bmStage.AnyOf(Mask::AllCommands)) {
        sync |= D3D12_BARRIER_SYNC_ALL;
    }

    return sync;
}

/// @brief Converts the specified resource access mask to the corresponding Direct3D 12 enumeration values.
/// @param[in] access the resource access mask
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_BARRIER_ACCESS ToD3D12Value(ymir::gpu::ResourceAccessMask access) {
    using Mask = ymir::gpu::ResourceAccessMask;
    BitmaskEnum bmAccess{access};

    if (bmAccess.None()) {
        return D3D12_BARRIER_ACCESS_NO_ACCESS;
    }

    D3D12_BARRIER_ACCESS flags = D3D12_BARRIER_ACCESS_COMMON;

    if (bmAccess.AnyOf(Mask::IndirectCommandRead)) {
        flags |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
    }
    if (bmAccess.AnyOf(Mask::VertexBufferRead)) {
        flags |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
    }
    if (bmAccess.AnyOf(Mask::IndexBufferRead)) {
        flags |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
    }
    if (bmAccess.AnyOf(Mask::ConstantBufferRead)) {
        flags |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
    }
    if (bmAccess.AnyOf(Mask::ShaderRead)) {
        flags |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    }
    if (bmAccess.AnyOf(Mask::ShaderWrite)) {
        flags |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    }
    if (bmAccess.AnyOf(Mask::RenderTargetRead)) {
        flags |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
    }
    if (bmAccess.AnyOf(Mask::RenderTargetWrite)) {
        flags |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
    }
    if (bmAccess.AnyOf(Mask::DepthStencilRead)) {
        flags |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
    }
    if (bmAccess.AnyOf(Mask::DepthStencilWrite)) {
        flags |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
    }
    if (bmAccess.AnyOf(Mask::TransferRead)) {
        flags |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
    }
    if (bmAccess.AnyOf(Mask::TransferWrite)) {
        flags |= D3D12_BARRIER_ACCESS_COPY_DEST;
    }
    if (bmAccess.AnyOf(Mask::HostRead) || bmAccess.AnyOf(Mask::HostWrite)) {
        flags |= D3D12_BARRIER_ACCESS_NO_ACCESS;
    }
    if (bmAccess.AnyOf(Mask::MemoryRead)) {
        flags |= D3D12_BARRIER_ACCESS_COMMON;
    }
    if (bmAccess.AnyOf(Mask::MemoryWrite)) {
        flags |= D3D12_BARRIER_ACCESS_COMMON;
    }

    return flags;
}

/// @brief Converts the specified fill mode to the corresponding Direct3D 12 enumeration value.
/// @param[in] fillMode the fill mode
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_FILL_MODE ToD3D12Value(ymir::gpu::FillMode fillMode) {
    switch (fillMode) {
    case ymir::gpu::FillMode::Solid: return D3D12_FILL_MODE_SOLID;
    case ymir::gpu::FillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
    default: return D3D12_FILL_MODE_SOLID;
    }
}

/// @brief Converts the specified cull mode to the corresponding Direct3D 12 enumeration value.
/// @param[in] cullMode the cull mode
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_CULL_MODE ToD3D12Value(ymir::gpu::CullMode cullMode) {
    switch (cullMode) {
    case ymir::gpu::CullMode::None: return D3D12_CULL_MODE_NONE;
    case ymir::gpu::CullMode::Back: return D3D12_CULL_MODE_BACK;
    case ymir::gpu::CullMode::Front: return D3D12_CULL_MODE_FRONT;
    default: return D3D12_CULL_MODE_NONE;
    }
}

/// @brief Converts the specified primitive topology to the corresponding Direct3D 12 enumeration value.
/// @param[in] primitiveTopology the primitive topology
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12Value(ymir::gpu::PrimitiveTopology primitiveTopology) {
    switch (primitiveTopology) {
    case ymir::gpu::PrimitiveTopology::Undefined: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    case ymir::gpu::PrimitiveTopology::Triangles: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case ymir::gpu::PrimitiveTopology::Lines: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case ymir::gpu::PrimitiveTopology::Points: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    }
}

/// @brief Converts the specified depth operation to the corresponding Direct3D 12 enumeration value.
/// @param[in] depthOperation the depth operation
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_COMPARISON_FUNC ToD3D12Value(ymir::gpu::DepthOperation depthOperation) {
    switch (depthOperation) {
    case ymir::gpu::DepthOperation::None: return D3D12_COMPARISON_FUNC_NONE;
    case ymir::gpu::DepthOperation::Never: return D3D12_COMPARISON_FUNC_NEVER;
    case ymir::gpu::DepthOperation::Less: return D3D12_COMPARISON_FUNC_LESS;
    case ymir::gpu::DepthOperation::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case ymir::gpu::DepthOperation::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case ymir::gpu::DepthOperation::Greater: return D3D12_COMPARISON_FUNC_GREATER;
    case ymir::gpu::DepthOperation::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case ymir::gpu::DepthOperation::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case ymir::gpu::DepthOperation::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    default: return D3D12_COMPARISON_FUNC_NONE;
    }
}

/// @brief Converts the specified blend factor to the corresponding Direct3D 12 enumeration value.
/// @param[in] blendFactor the blend factor
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_BLEND ToD3D12Value(ymir::gpu::BlendFactor blendFactor) {
    switch (blendFactor) {
    case ymir::gpu::BlendFactor::Zero: return D3D12_BLEND_ZERO;
    case ymir::gpu::BlendFactor::One: return D3D12_BLEND_ONE;
    case ymir::gpu::BlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
    case ymir::gpu::BlendFactor::InvSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
    case ymir::gpu::BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
    case ymir::gpu::BlendFactor::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
    case ymir::gpu::BlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
    case ymir::gpu::BlendFactor::InvDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    case ymir::gpu::BlendFactor::DstColor: return D3D12_BLEND_DEST_COLOR;
    case ymir::gpu::BlendFactor::InvDstColor: return D3D12_BLEND_INV_DEST_COLOR;
    case ymir::gpu::BlendFactor::SrcAlphaSat: return D3D12_BLEND_SRC_ALPHA_SAT;
    case ymir::gpu::BlendFactor::BlendFactor: return D3D12_BLEND_BLEND_FACTOR;
    case ymir::gpu::BlendFactor::InvBlendFactor: return D3D12_BLEND_INV_BLEND_FACTOR;
    case ymir::gpu::BlendFactor::Src1Color: return D3D12_BLEND_SRC1_COLOR;
    case ymir::gpu::BlendFactor::InvSrc1Color: return D3D12_BLEND_INV_SRC1_COLOR;
    case ymir::gpu::BlendFactor::Src1Alpha: return D3D12_BLEND_SRC1_ALPHA;
    case ymir::gpu::BlendFactor::InvSrc1Alpha: return D3D12_BLEND_INV_SRC1_ALPHA;
    case ymir::gpu::BlendFactor::AlphaFactor: return D3D12_BLEND_ALPHA_FACTOR;
    case ymir::gpu::BlendFactor::InvAlphaFactor: return D3D12_BLEND_INV_ALPHA_FACTOR;
    default: return D3D12_BLEND_ZERO;
    }
}

/// @brief Converts the specified blend operation to the corresponding Direct3D 12 enumeration value.
/// @param[in] blendOp the blend operation
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_BLEND_OP ToD3D12Value(ymir::gpu::BlendOp blendOp) {
    switch (blendOp) {
    case ymir::gpu::BlendOp::Add: return D3D12_BLEND_OP_ADD;
    case ymir::gpu::BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
    case ymir::gpu::BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case ymir::gpu::BlendOp::Min: return D3D12_BLEND_OP_MIN;
    case ymir::gpu::BlendOp::Max: return D3D12_BLEND_OP_MAX;
    default: return D3D12_BLEND_OP_ADD;
    }
}

/// @brief Converts the specified logic operation to the corresponding Direct3D 12 enumeration value.
/// @param[in] logicOp the logic operation
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_LOGIC_OP ToD3D12Value(ymir::gpu::LogicOp logicOp) {
    switch (logicOp) {
    case ymir::gpu::LogicOp::Clear: return D3D12_LOGIC_OP_CLEAR;
    case ymir::gpu::LogicOp::Set: return D3D12_LOGIC_OP_SET;
    case ymir::gpu::LogicOp::Copy: return D3D12_LOGIC_OP_COPY;
    case ymir::gpu::LogicOp::CopyInverted: return D3D12_LOGIC_OP_COPY_INVERTED;
    case ymir::gpu::LogicOp::Noop: return D3D12_LOGIC_OP_NOOP;
    case ymir::gpu::LogicOp::Invert: return D3D12_LOGIC_OP_INVERT;
    case ymir::gpu::LogicOp::And: return D3D12_LOGIC_OP_AND;
    case ymir::gpu::LogicOp::Nand: return D3D12_LOGIC_OP_NAND;
    case ymir::gpu::LogicOp::Or: return D3D12_LOGIC_OP_OR;
    case ymir::gpu::LogicOp::Nor: return D3D12_LOGIC_OP_NOR;
    case ymir::gpu::LogicOp::Xor: return D3D12_LOGIC_OP_XOR;
    case ymir::gpu::LogicOp::Equiv: return D3D12_LOGIC_OP_EQUIV;
    case ymir::gpu::LogicOp::AndReversed: return D3D12_LOGIC_OP_AND_REVERSE;
    case ymir::gpu::LogicOp::AndInverted: return D3D12_LOGIC_OP_AND_INVERTED;
    case ymir::gpu::LogicOp::OrReversed: return D3D12_LOGIC_OP_OR_REVERSE;
    case ymir::gpu::LogicOp::OrInverted: return D3D12_LOGIC_OP_OR_INVERTED;
    default: return D3D12_LOGIC_OP_NOOP;
    }
}

/// @brief Converts the given UTF-8-encoded string to a wide string.
/// @param[in] str the string to convert
/// @return the string converted to `std::wstring`
inline std::wstring StringToWString(std::string_view str) {
    if (str.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;
}

} // namespace util
