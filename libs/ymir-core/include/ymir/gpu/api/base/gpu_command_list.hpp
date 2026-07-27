#pragma once

#include "gpu_object.hpp"

#include "gpu_compute_pipeline.hpp"

#include <span>

namespace ymir::gpu {

/// @brief Represents a GPU command list.
class IGPUCommandList : public GPUObject<IGPUCommandList> {
protected:
    IGPUCommandList(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUCommandList() = default;

    /// @brief Resets the command list, clearing all enqueued commands.
    virtual void Reset() = 0;

    /// @brief Begins the command list.
    virtual void Begin() = 0;

    /// @brief Ends the command list.
    virtual void End() = 0;

    /// @brief Inserts a buffer barrier.
    /// @param[in] barrier barrier specifications
    virtual void Barrier(const BufferBarrierSpec &spec) = 0;

    /// @brief Inserts a memory barrier.
    /// @param[in] barrier barrier specifications
    virtual void Barrier(const MemoryBarrierSpec &spec) = 0;

    /// @brief Inserts a barrier group.
    /// @param[in] barrier barrier group specifications
    virtual void Barrier(const BarrierGroupSpec &spec) = 0;

    /// @brief Uses the specified graphics pipeline.
    /// @param[in] pipeline the graphics pipeline to apply
    /// @param[in,opt] constData constant buffer data
    // TODO: virtual void SetGraphicsPipeline(const IGPUGraphicsPipeline &pipeline,
    //                                  std::span<const std::byte> constData = {}) = 0;

    /// @brief Uses the specified compute pipeline.
    /// @param[in] pipeline the compute pipeline to apply
    virtual void SetComputePipeline(const IGPUComputePipeline &pipeline) = 0;

    /// @brief Sets compute constants. A compute pipeline must be set prior to this.
    /// @param[in] data pointer to the constant buffer
    /// @param[in] size size of the constant buffer
    virtual void SetComputeConstants(const void *data, size_t size) = 0;

    // TODO: add whatever is needed on demand
};

} // namespace ymir::gpu
