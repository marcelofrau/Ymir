#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU graphics pipeline.
class IGPUGraphicsPipeline : public GPUObject<IGPUGraphicsPipeline> {
protected:
    IGPUGraphicsPipeline(Backend backend, const GraphicsPipelineSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {}

public:
    virtual ~IGPUGraphicsPipeline() = default;

    /// @brief Retrieves the graphics pipeline specifications.
    /// @return the graphics pipeline specifications
    const GraphicsPipelineSpec &GetSpec() const {
        return m_spec;
    }

private:
    GraphicsPipelineSpec m_spec;
};

} // namespace ymir::gpu
