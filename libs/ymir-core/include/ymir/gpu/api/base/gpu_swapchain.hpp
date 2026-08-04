#pragma once

#include "gpu_object.hpp"

#include "gpu_texture.hpp"
#include "gpu_texture_view.hpp"

#include <memory>
#include <vector>

namespace ymir::gpu {

/// @brief Represents a GPU swapchain.
class IGPUSwapchain : public GPUObject<IGPUSwapchain> {
protected:
    IGPUSwapchain(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUSwapchain() = default;

    /// @brief Presents a frame.
    /// @param[in] interval the presentation interval - waits for vertical synchronization for at least this many frames
    virtual void Present(uint32 interval) = 0;
};

} // namespace ymir::gpu
