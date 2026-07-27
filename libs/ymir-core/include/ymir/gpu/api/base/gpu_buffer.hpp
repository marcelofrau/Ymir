#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU buffer.
class IGPUBuffer : public GPUObject<IGPUBuffer> {
protected:
    IGPUBuffer(Backend backend, BufferUsage usage, uint64 size, uint64 count)
        : GPUObject(backend)
        , m_usage(usage)
        , m_size(size)
        , m_count(count) {}

public:
    virtual ~IGPUBuffer() = default;

    /// @brief Maps the buffer on the CPU side for reading or writing.
    /// Set start and end to 0 to map the entire buffer.
    /// @param[in] start the starting offset
    /// @param[in] end the ending offset
    /// @return a pointer to the buffer, or `nullptr` if the map operation failed
    virtual void *Map(uint64 start = 0u, uint64 end = 0u) = 0;

    /// @brief Unmaps a portion of the buffer.
    /// Set start and end to 0 to unmap the entire buffer.
    /// @param[in] start the starting offset
    /// @param[in] end the ending offset
    virtual void Unmap(uint64 start = 0u, uint64 end = 0u) = 0;

    /// @brief Returns the address of the buffer.
    /// @return the address of the buffer
    virtual uint64 GetAddress() const = 0;

    /// @brief Retrieves the buffer's usage flags
    /// @return the usage flags
    BufferUsage GetUsage() const {
        return m_usage;
    }

    /// @brief Retrieves the size (in bytes) of an individual element in the buffer.
    /// @return the element size
    uint64 GetElementSize() const {
        return m_size;
    }

    /// @brief Retrieves the number of elements in the buffer.
    /// @return the element count
    uint64 GetElementCount() const {
        return m_count;
    }

    /// @brief Retrieves the total size (in bytes) of the buffer
    /// @return the buffer size
    uint64 GetTotalSize() const {
        return m_count * m_size;
    }

private:
    BufferUsage m_usage;
    uint64 m_size;  // Element size (aka stride)
    uint64 m_count; // Element count
};

} // namespace ymir::gpu
