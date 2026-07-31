#pragma once

#include "gpu_object.hpp"

#include "gpu_texture.hpp"
#include "gpu_texture_view.hpp"

#include <memory>
#include <vector>

namespace ymir::gpu {

/// @brief Container for the texture and view of a surface.
struct Surface {
    std::unique_ptr<IGPUTexture> texture;
    std::unique_ptr<IGPUTextureView> view;
};

/// @brief Represents a GPU swapchain.
class IGPUSwapchain : public GPUObject<IGPUSwapchain> {
protected:
    IGPUSwapchain(Backend backend, std::vector<Surface> &&surfaces)
        : GPUObject(backend)
        , m_surfaces(std::move(surfaces)) {}

public:
    virtual ~IGPUSwapchain() = default;

    /// @brief Presents a frame.
    /// @param[in] interval the presentation interval - waits for vertical synchronization after this many frames
    virtual void Present(uint32 interval) = 0;

    /// @brief Retrieves the number of surfaces allocated in this swapchain.
    /// @return the number of surfaces in the swapchain
    uint32 GetSurfaceCount() const {
        return m_surfaces.size();
    }

    /// @brief Retrieves the surface at the specified index.
    /// @param[in] index surface index
    /// @return a pointer to the surface, or `nullptr` if the index is out of range
    const Surface *GetSurface(uint32 index) {
        if (index < m_surfaces.size()) {
            return &m_surfaces[index];
        }
        return nullptr;
    }

private:
    std::vector<Surface> m_surfaces;
};

} // namespace ymir::gpu
