#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU compute pipeline.
class IGPUComputePipeline : public GPUObject<IGPUComputePipeline> {
protected:
    IGPUComputePipeline(Backend backend, const ComputeShader &shader)
        : GPUObject(backend)
        , m_shader(shader) {}

public:
    virtual ~IGPUComputePipeline() = default;

    const ComputeShader &GetShader() const {
        return m_shader;
    }

private:
    const ComputeShader &m_shader;
};

} // namespace ymir::gpu
