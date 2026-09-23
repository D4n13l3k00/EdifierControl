#include "d3d_renderer.hpp"
#include <wincodec.h>
#include <filesystem>
#include <vector>

namespace edifier::app {

D3dRenderer::~D3dRenderer() {
    shutdown();
}

void D3dRenderer::createTarget() {
    if (!m_swapChain || !m_device) return;
    ID3D11Texture2D* buffer{nullptr};
    if (SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer))) && buffer) {
        m_device->CreateRenderTargetView(buffer, nullptr, &m_target);
        buffer->Release();
    }
}

void D3dRenderer::cleanupTarget() {
    if (m_target) {
        m_target->Release();
        m_target = nullptr;
    }
}

bool D3dRenderer::init(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL selected{};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &desc, &m_swapChain, &m_device, &selected, &m_context))) {
        return false;
    }
    createTarget();
    return true;
}

void D3dRenderer::shutdown() {
    cleanupTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

bool D3dRenderer::resize(int width, int height) {
    if (!m_swapChain) return false;
    cleanupTarget();
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createTarget();
    return SUCCEEDED(hr);
}

void D3dRenderer::beginFrame(float r, float g, float b, float a) {
    if (!m_context || !m_target) return;
    const float clearColor[4]{r, g, b, a};
    m_context->OMSetRenderTargets(1, &m_target, nullptr);
    m_context->ClearRenderTargetView(m_target, clearColor);
}

void D3dRenderer::endFrame(bool vsync) {
    if (!m_swapChain) return;
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

bool D3dRenderer::captureScreenshot(const wchar_t* filename) {
    if (!m_swapChain || !m_device || !m_context) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging))) {
        backBuffer->Release();
        return false;
    }
    m_context->CopyResource(staging, backBuffer);
    backBuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        m_context->Unmap(staging, 0);
        staging->Release();
        return false;
    }

    IWICBitmapEncoder* encoder = nullptr;
    factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    IWICStream* stream = nullptr;
    factory->CreateStream(&stream);

    const auto absPath = std::filesystem::absolute(filename).wstring();
    std::filesystem::create_directories(std::filesystem::path(absPath).parent_path());
    if (FAILED(stream->InitializeFromFilename(absPath.c_str(), GENERIC_WRITE))) {
        if (stream) stream->Release();
        if (encoder) encoder->Release();
        factory->Release();
        m_context->Unmap(staging, 0);
        staging->Release();
        return false;
    }

    encoder->Initialize(stream, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* frame = nullptr;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);

    std::vector<uint32_t> bgra(desc.Width * desc.Height);
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = src + y * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            uint8_t r = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t b = row[x * 4 + 2];
            uint8_t a = 255;
            bgra[y * desc.Width + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(bgra.size() * 4), reinterpret_cast<BYTE*>(bgra.data()));
    frame->Commit();
    encoder->Commit();
    frame->Release();
    stream->Release();
    encoder->Release();
    factory->Release();
    m_context->Unmap(staging, 0);
    staging->Release();
    return true;
}

} // namespace edifier::app
