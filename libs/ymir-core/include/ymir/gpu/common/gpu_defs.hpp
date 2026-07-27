#pragma once

/**
@file
@brief Common Ymir GPU API definitions.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/bitmask_enum.hpp>

#include <string>
#include <vector>

namespace ymir::gpu {

class IGPUTexture;
class IGPUBuffer;

enum class CommandQueueType { Graphics, Compute, Copy, VideoEncode, VideoDecode };

// ---------------------------------------------------------------------------------------------------------------------

enum class TextureFormat {
    Undefined,

    R8G8B8A8_UINT,
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,

    R8_UINT,
    R16_UINT,
    R32_UINT,

    R32G32_UINT,

    D16_UNORM,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
    D32_FLOAT_S8X24_UINT,

    // TODO: add formats as needed
};

constexpr uint32 BytesPerPixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::Undefined: return 0u;
    case TextureFormat::R8G8B8A8_UINT: return 4u;
    case TextureFormat::R8G8B8A8_UNORM: return 4u;
    case TextureFormat::B8G8R8A8_UNORM: return 4u;
    case TextureFormat::R8_UINT: return 1u;
    case TextureFormat::R16_UINT: return 2u;
    case TextureFormat::R32_UINT: return 4u;
    case TextureFormat::R32G32_UINT: return 8u;
    case TextureFormat::D16_UNORM: return 2u;
    case TextureFormat::D24_UNORM_S8_UINT: return 4u;
    case TextureFormat::D32_FLOAT: return 4u;
    case TextureFormat::D32_FLOAT_S8X24_UINT: return 8u;
    }
    return 0u;
}

enum class TextureUsage : uint32 {
    None = 0u,

    RenderTarget = 1u << 0u,
    DepthTarget = 1u << 1u,
    Storage = 1u << 2u,
    ShaderResource = 1u << 3u,
};

enum class TextureDimensions {
    Tex1D,
    Tex2D,
    Tex3D,
};

enum class ResourceLayout {
    Undefined,
    General,      // aka UAV
    ReadOnly,     // aka SRV
    RenderTarget, // aka color attachment
    DepthStencilRead,
    DepthStencilWrite,
    TransferSource,
    TransferDestination,
    Present,
};

enum class TextureViewType { None, RenderTarget, DepthTarget, ShaderRead, ShaderWrite };

struct TextureSpec {
    TextureDimensions dimensions;
    TextureFormat format = TextureFormat::Undefined;
    TextureUsage usage = TextureUsage::None;
    uint32 width = 1;              // Valid for 1D, 2D, 3D
    uint32 height = 1;             // Valid for 2D, 3D
    uint32 depthOrArrayLength = 1; // Depth for 3D, array length for 1D and 2D
    uint32 mipLevels = 1;
};

enum class Texture2DViewMode { Normal, Multisampled, Cube };

struct TextureViewSpec {
    IGPUTexture *texture = nullptr;                  // Must not be nullptr
    TextureViewType type = TextureViewType::None;    // Must not be None
    TextureFormat format = TextureFormat::Undefined; // Undefined mirrors the texture's format
    Texture2DViewMode viewMode2D;                    // 2D texture handling mode

    uint32 arrayIndex = 0;    // Starting array index, cube texture slice index or W slice
    uint32 arraySize = 0;     // Number of array entries, cubes or W slices; 0 means maximum
    uint32 planeSlice = 0;    // Plane slice for 2D textures
    uint32 mipLevelBase = 0;  // First mip level to map to view or mip slice to use
    uint32 mipLevelCount = 0; // 0 means all remaining levels
};

// ---------------------------------------------------------------------------------------------------------------------

enum class BufferUsage : uint32 {
    None = 0u,

    Vertex = 1u << 0u,
    Index = 1u << 1u,
    Constant = 1u << 2u,
    ShaderRead = 1u << 3u,
    ShaderWrite = 1u << 4u,
    Upload = 1u << 5u,   // aka staging
    Download = 1u << 6u, // aka readback
    IndirectCommands = 1u << 7u,
};

enum class BufferViewType {
    Constant,   // CBV or equivalent
    Structured, // SRV or equivalent
    Storage,    // UAV or equivalent
};

// ---------------------------------------------------------------------------------------------------------------------

enum class ShaderLanguage {
    None, // Precompiled shader blobs
    HLSL, // D3D12, Vulkan
    MSL,  // Metal
    // TODO: add others as needed
};

enum class ShaderBytecodeFormat {
    None,
    DXIL,     // D3D12
    SPIRV,    // Vulkan
    MetalLib, // Metal
    // TODO: add others as needed
};

enum class ShaderStage {
    // Graphics pipeline
    Vertex,
    Hull,
    Domain,
    Geometry,
    Pixel,

    // Compute pipeline
    Compute,

    // TODO: Mesh pipeline
    // Amplification,
    // Mesh,

    // TODO: ray tracing stuff: (also needs BLAS/TLAS resources)
    // RTRayGeneration,
    // RTIntersection,
    // RTAnyHit,
    // RTClosestHit,
    // RTMiss,
    // RTCallable,
};

struct CompiledShader {
    ShaderStage stage;
    ShaderLanguage language = ShaderLanguage::None;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::None;
    std::vector<char> bytecode;
    std::string entrypoint;
};

/// @brief Shader macro specification.
struct ShaderMacro {
    std::string name;
    std::string value;
};

/// @brief Specifications for compiling shaders from source code.
/// You must specify a valid language and bytecode type combination:
/// - HLSL -> DXIL or SPIRV
/// - MSL -> MetalLib
struct ShaderCompileSpec {
    ShaderStage stage;
    ShaderLanguage language = ShaderLanguage::None;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::None;
    std::string name;
    std::string sourceCode;
    std::string entrypoint;
    std::vector<ShaderMacro> macros;
    bool debug = false;
    bool optimize = true;
};

// ---------------------------------------------------------------------------------------------------------------------

enum class PipelineStageMask : uint32 {
    None = 0u,

    TopOfPipe = 1u << 0u,
    DrawIndirect = 1u << 1u,
    VertexInput = 1u << 2u,
    IndexInput = 1u << 3u,
    VertexShader = 1u << 4u,
    HullShader = 1u << 5u,
    DomainShader = 1u << 6u,
    GeometryShader = 1u << 7u,
    PixelShader = 1u << 8u,
    ComputeShader = 1u << 9u,
    RenderTargetOutput = 1u << 10u,
    Resolve = 1u << 11u,
    Copy = 1u << 12u,
    BottomOfPipe = 1u << 13u,

    // TODO: raytracing stuff

    AllGraphics = 1u << 30u,
    AllCommands = 1u << 31u,
};

enum class ResourceAccessMask : uint32 {
    None = 0u,

    IndirectCommandRead = 1u << 0u,
    VertexBufferRead = 1u << 1u,
    IndexBufferRead = 1u << 2u,
    ConstantBufferRead = 1u << 3u,
    ShaderRead = 1u << 4u,
    ShaderWrite = 1u << 5u,
    RenderTargetRead = 1u << 6u,
    RenderTargetWrite = 1u << 7u,
    DepthStencilRead = 1u << 8u,
    DepthStencilWrite = 1u << 9u,
    TransferRead = 1u << 10u,
    TransferWrite = 1u << 11u,
    HostRead = 1u << 12u,
    HostWrite = 1u << 13u,
    MemoryRead = 1u << 14u,
    MemoryWrite = 1u << 15u,

    // TODO: raytracing stuff
};

// TODO: texture barrier

struct BufferBarrierSpec {
    IGPUBuffer *buffer = nullptr; // Required
    PipelineStageMask srcStage = PipelineStageMask::None;
    PipelineStageMask dstStage = PipelineStageMask::None;
    ResourceAccessMask srcAccess = ResourceAccessMask::None;
    ResourceAccessMask dstAccess = ResourceAccessMask::None;
};

struct MemoryBarrierSpec {
    PipelineStageMask srcStage = PipelineStageMask::None;
    PipelineStageMask dstStage = PipelineStageMask::None;
    ResourceAccessMask srcAccess = ResourceAccessMask::None;
    ResourceAccessMask dstAccess = ResourceAccessMask::None;
};

struct BarrierGroupSpec {
    // TODO: texture barriers
    std::vector<BufferBarrierSpec> bufferBarriers;
    std::vector<MemoryBarrierSpec> memoryBarriers;
};

} // namespace ymir::gpu

ENABLE_BITMASK_OPERATORS(ymir::gpu::TextureUsage);
ENABLE_BITMASK_OPERATORS(ymir::gpu::BufferUsage);
ENABLE_BITMASK_OPERATORS(ymir::gpu::PipelineStageMask);
ENABLE_BITMASK_OPERATORS(ymir::gpu::ResourceAccessMask);
