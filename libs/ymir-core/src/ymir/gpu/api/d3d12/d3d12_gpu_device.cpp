#include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>

#include <ymir/gpu/api/d3d12/d3d12_descriptor_manager.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer_view.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_compute_pipeline.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_surface.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

#include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

GPUObjectResult<IGPUDevice> D3D12GPUDevice::Create(const D3D12GPUDeviceSpec &spec) {
    auto device = std::make_unique<d3d12::D3D12Device>();
    if (!device) {
        return GPUOperationError{"Could not allocate D3D12Device"};
    }

    HRESULT hr;

    hr = device->Create(spec.adapter, spec.featureLevel);
    if (FAILED(hr)) {
        // TODO: get human-readable error message from HRESULT
        // TODO: get debug messages if available
        return GPUOperationError{fmt::format("Failed to create Direct3D 12 device: error code {:X}", hr)};
    }
    if (spec.debug) {
        ymir::gpu::d3d12::DebugLayer::Get().BreakOnWarnings(device->GetPointer(), true);
    }

    auto descMgrResult =
        D3D12DescriptorManager::Create(*device, spec.resourceHeap.maxDescriptors, spec.samplerHeap.maxDescriptors,
                                       spec.rtvHeap.maxDescriptors, spec.dsvHeap.maxDescriptors);
    if (!descMgrResult) {
        return GPUOperationError{
            fmt::format("Failed to create Direct3D 12 device heaps: {}", descMgrResult.Error().message)};
    }

    auto &descMgr = *descMgrResult.Value();
    if (!spec.resourceHeap.name.empty()) {
        descMgr.SetResourceHeapName(spec.resourceHeap.name);
    }
    if (!spec.samplerHeap.name.empty()) {
        descMgr.SetSamplerHeapName(spec.samplerHeap.name);
    }
    if (!spec.rtvHeap.name.empty()) {
        descMgr.SetRTVHeapName(spec.rtvHeap.name);
    }
    if (!spec.dsvHeap.name.empty()) {
        descMgr.SetDSVHeapName(spec.dsvHeap.name);
    }

    auto rootSig = std::make_unique<d3d12::D3D12RootSignature>();
    if (!rootSig) {
        return GPUOperationError{"Could not allocate D3D12RootSignature"};
    }
    auto builder = rootSig->Builder();
    builder.Add32BitConstants(0, spec.num32BitConstants);
    builder.Flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED |
                  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    hr = builder.Build(*device);
    if (FAILED(hr)) {
        // TODO: get human-readable error message from HRESULT
        // TODO: get debug messages if available
        return GPUOperationError{fmt::format("Failed to create root signature: error code {:X}", hr)};
    }

    return {std::make_unique<D3D12GPUDevice>(std::move(device), std::move(rootSig), std::move(descMgrResult.Value()),
                                             spec.debug)};
}

void D3D12GPUDevice::SetName(std::string_view name) {
    (*m_device)->SetName(util::StringToWString(name).c_str());
}

GPUObjectResult<IGPUCommandQueue> D3D12GPUDevice::CreateCommandQueue(CommandQueueType type,
                                                                     const util::PropertyBag *props) {
    return D3D12CommandQueue::Create(*this, type, props);
}

GPUObjectResult<IGPUSurface> D3D12GPUDevice::CreateSurface(const WindowParams &windowParams,
                                                           const IGPUCommandQueue &queue, uint32 maxFramesInFlight,
                                                           const util::PropertyBag *props) {
    return D3D12Surface::Create(*m_device, windowParams, queue, maxFramesInFlight, m_debug, props);
}

GPUObjectResult<IGPUTexture> D3D12GPUDevice::CreateTexture(const TextureSpec &spec) {
    return D3D12Texture::Create(*m_device, spec);
}

GPUObjectResult<IGPUTextureView> D3D12GPUDevice::CreateTextureView(const TextureViewSpec &spec) {
    return D3D12TextureView::Create(*m_descMgr, spec);
}

GPUObjectResult<IGPUBuffer> D3D12GPUDevice::CreateBuffer(BufferUsage usage, uint64 size, uint64 count) {
    return D3D12Buffer::Create(*m_device, usage, size, count);
}

GPUObjectResult<IGPUBufferView> D3D12GPUDevice::CreateBufferView(const IGPUBuffer &buffer, BufferViewType type) {
    return D3D12BufferView::Create(*m_descMgr, buffer, type);
}

GPUObjectResult<IGPUGraphicsPipeline> D3D12GPUDevice::CreateGraphicsPipeline(const GraphicsPipelineSpec &spec) {
    // return D3D12GraphicsPipeline::Create(*m_device, *m_rootSig, spec);
    return GPUOperationError{"Unimplemented"};
}

GPUObjectResult<IGPUComputePipeline> D3D12GPUDevice::CreateComputePipeline(const ComputeShader &shader) {
    return D3D12ComputePipeline::Create(*m_device, *m_rootSig, shader);
}

} // namespace ymir::gpu
