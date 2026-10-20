#pragma once

/**
@file
@brief Defines `D3D12SwapChain`, a wrapper for `IDXGISwapChain3` objects.
*/

#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `IDXGISwapChain3` and a waitable object for frame synchronization.
class D3D12SwapChain final : public D3D12ObjectWrapper<IDXGISwapChain3> {
public:
    D3D12SwapChain() = default;
    D3D12SwapChain(wil::com_ptr_nothrow<IDXGISwapChain3> &&ptr)
        : D3D12ObjectWrapper(std::move(ptr)) {}
    D3D12SwapChain(IDXGISwapChain3 *ptr)
        : D3D12ObjectWrapper(ptr) {}

    /// @brief Creates an `IDXGISwapChain3` with the specified parameters
    /// @param[in] dxgiFactoryFlags base flags to apply to the `IDXGIFactory6` instance
    /// @param[in] commandQueue the direct command queue to bind to the swap chain
    /// @param[in] scDesc the swap chain descriptor
    /// @param[in] hwnd the window handle to bind the swap chain to
    /// @param[in] maxLatench the maximum number of frames of presentation latency
    /// @return `true` if the swap chain was created successfully, `false` otherwise (TODO: better errors)
    bool Create(UINT dxgiFactoryFlags, ID3D12CommandQueue *commandQueue, DXGI_SWAP_CHAIN_DESC1 scDesc, HWND hwnd,
                UINT maxLatency) {
        m_windowed = true;
        return CreateImpl(dxgiFactoryFlags, scDesc, maxLatency,
                          [&](IDXGIFactory6 *factory, const DXGI_SWAP_CHAIN_DESC1 &desc, IDXGISwapChain1 **swapChain) {
                              if (FAILED(factory->CreateSwapChainForHwnd(commandQueue, hwnd, &desc, nullptr, nullptr,
                                                                         swapChain))) {
                                  return false;
                              }
                              if (m_tearingSupport) {
                                  factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
                              }
                              return true;
                          });
    }

    /// @brief Creates an `IDXGISwapChain3` bound to a UWP `CoreWindow` with the specified parameters
    /// @param[in] dxgiFactoryFlags base flags to apply to the `IDXGIFactory6` instance
    /// @param[in] commandQueue the direct command queue to bind to the swap chain
    /// @param[in] scDesc the swap chain descriptor
    /// @param[in] coreWindow the `Windows::UI::Core::CoreWindow` to bind the swap chain to
    /// @param[in] maxLatency the maximum number of frames of presentation latency
    /// @return `true` if the swap chain was created successfully, `false` otherwise (TODO: better errors)
    bool CreateForCoreWindow(UINT dxgiFactoryFlags, ID3D12CommandQueue *commandQueue, DXGI_SWAP_CHAIN_DESC1 scDesc,
                             IUnknown *coreWindow, UINT maxLatency) {
        m_windowed = false;
        return CreateImpl(dxgiFactoryFlags, scDesc, maxLatency,
                          [&](IDXGIFactory6 *factory, const DXGI_SWAP_CHAIN_DESC1 &desc, IDXGISwapChain1 **swapChain) {
                              return SUCCEEDED(factory->CreateSwapChainForCoreWindow(commandQueue, coreWindow, &desc,
                                                                                     nullptr, swapChain));
                          });
    }

    /// @brief Resizes the swap chain buffers to the specified dimensions.
    /// Callers must ensure the render targets are not referenced anywhere before calling this function.
    /// @param[in] width the new framebuffer width
    /// @param[in] height the new framebuffer height
    /// @return the result of the operation
    HRESULT ResizeBuffers(UINT width, UINT height) const {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        m_object->GetDesc1(&desc);
        return m_object->ResizeBuffers(0, width, height, desc.Format, desc.Flags);
    }

    /// @brief Retrieves the waitable object for this swap chain.
    /// @return the waitable object
    HANDLE WaitableObject() const {
        return m_waitableObject.get();
    }

    /// @brief Determines if presentation tearing is supported with this swap chain.
    /// @return `true` if presentation tearing is supported
    bool IsTearingSupported() const {
        return m_tearingSupport;
    }

private:
    wil::unique_handle m_waitableObject = nullptr;
    bool m_tearingSupport = false;
    bool m_windowed = true; ///< `true` for HWND swap chains, `false` for CoreWindow swap chains

    template <typename FnCreateSwapChain>
    bool CreateImpl(UINT dxgiFactoryFlags, DXGI_SWAP_CHAIN_DESC1 scDesc, UINT maxLatency,
                    FnCreateSwapChain &&fnCreateSwapChain) {
        wil::com_ptr_nothrow<IDXGIFactory6> factory;
        wil::com_ptr_nothrow<IDXGISwapChain1> swapChain1;

        if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(factory.put())))) {
            return false;
        }

        BOOL allowTearing = FALSE;
        factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
        m_tearingSupport = allowTearing == TRUE;
        if (m_tearingSupport) {
            scDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        if (!fnCreateSwapChain(factory.get(), scDesc, swapChain1.put())) {
            return false;
        }

        if (FAILED(swapChain1.copy_to(m_object.put()))) {
            return false;
        }
        m_object->SetMaximumFrameLatency(maxLatency);
        m_waitableObject.reset(m_object->GetFrameLatencyWaitableObject());

        return true;
    }

    void DestroyExt() {
        // Exclusive fullscreen only exists for HWND swap chains
        if (m_object && m_windowed) {
            m_object->SetFullscreenState(false, nullptr);
        }
        m_waitableObject.reset();
    }

    bool IsValidExt() const {
        return (bool)m_waitableObject;
    }
};

} // namespace ymir::gpu::d3d12
