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
