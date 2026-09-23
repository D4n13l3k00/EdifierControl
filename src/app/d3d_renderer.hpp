#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace edifier::app {

class D3dRenderer {
public:
    D3dRenderer() = default;
    ~D3dRenderer();

    D3dRenderer(const D3dRenderer&) = delete;
    D3dRenderer& operator=(const D3dRenderer&) = delete;

    bool init(HWND window);
    void shutdown();

    bool resize(int width, int height);
    void beginFrame(float r = 16.f / 255.f, float g = 17.f / 255.f, float b = 19.f / 255.f, float a = 1.0f);
    void endFrame(bool vsync = true);

    bool captureScreenshot(const wchar_t* filename);

    ID3D11Device* getDevice() const { return m_device; }
    ID3D11DeviceContext* getContext() const { return m_context; }
    IDXGISwapChain* getSwapChain() const { return m_swapChain; }
    ID3D11RenderTargetView* getTarget() const { return m_target; }

private:
    void createTarget();
    void cleanupTarget();

    ID3D11Device* m_device{nullptr};
    ID3D11DeviceContext* m_context{nullptr};
    IDXGISwapChain* m_swapChain{nullptr};
    ID3D11RenderTargetView* m_target{nullptr};
};

} // namespace edifier::app
