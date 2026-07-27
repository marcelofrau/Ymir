#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU buffer view.
class IGPUBufferView : public GPUObject<IGPUBufferView> {
protected:
    IGPUBufferView(Backend backend, const IGPUBuffer &buffer, BufferViewType type)
        : GPUObject(backend)
        , m_buffer(buffer)
        , m_type(type) {}

public:
    virtual ~IGPUBufferView() = default;

    /// @brief Retrieves the buffer that this view is attached to.
    /// @return the owning buffer
    const IGPUBuffer &GetBuffer() const {
        return m_buffer;
    }

    /// @brief Retrieves the type of the view.
    /// @return the view type
    BufferViewType GetViewType() const {
        return m_type;
    }

    /// @brief Retrieves the bindless index assigned to this view.
    /// @return the view's bindless index
    virtual uint32 GetBindlessIndex() const = 0;

private:
    const IGPUBuffer &m_buffer;
    BufferViewType m_type;
};

} // namespace ymir::gpu
