#include "capture/sources/winrt/capture_source_winrt.hpp"

#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"
#include "capture/sources/winrt/winrt_capture_session.hpp"
#include "capture/sources/winrt/winrt_frame_sink.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#endif

namespace vf {

WinrtCaptureSource::WinrtCaptureSource()
#ifdef _WIN32
    : session(std::make_unique<WinrtCaptureSession>())
#endif
{
}

WinrtCaptureSource::~WinrtCaptureSource() {
    const std::expected<void, std::error_code> result = stop();
    if (!result) {
        VF_WARN("WinrtCaptureSource stop during destruction failed: {}", result.error().message());
    }
}

void WinrtCaptureSource::setFrameSink(std::shared_ptr<IWinrtFrameSink> nextFrameSink) {
    std::scoped_lock lock(stateMutex);
    frameSink = std::move(nextFrameSink);
}

std::expected<void, std::error_code> WinrtCaptureSource::start(const CaptureConfig& config) {
#ifndef _WIN32
    static_cast<void>(config);
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == CaptureState::Running) {
            return {};
        }
        if (state == CaptureState::Starting || state == CaptureState::Stopping) {
            return std::unexpected(makeErrorCode(CaptureError::InvalidState));
        }
        state = CaptureState::Starting;
    }

    auto markFault = [this]() {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Fault;
    };

    if (session == nullptr) {
        markFault();
        return std::unexpected(makeErrorCode(CaptureError::InvalidState));
    }

    try {
        const std::expected<void, std::error_code> initializeResult =
            session->initializeDeviceAndItem(config.preferredDisplayIndex);
        if (!initializeResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during device/item initialization: {}",
                     initializeResult.error().message());
            return std::unexpected(initializeResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during device/item initialization: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::DeviceInitializationFailed));
    }

    try {
        const std::expected<void, std::error_code> framePoolResult = session->initializeFramePool(
            [this](const auto& sender, const auto& args) { onFrameArrived(sender, args); });
        if (!framePoolResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during frame pool initialization: {}",
                     framePoolResult.error().message());
            return std::unexpected(framePoolResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during frame pool initialization: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::FramePoolInitializationFailed));
    }

    try {
        const std::expected<void, std::error_code> sessionResult = session->startSession();
        if (!sessionResult) {
            markFault();
            VF_ERROR("WinrtCaptureSource start failed during session start: {}",
                     sessionResult.error().message());
            return std::unexpected(sessionResult.error());
        }
    } catch (const winrt::hresult_error& ex) {
        markFault();
        VF_ERROR("WinrtCaptureSource start failed during session start: {}",
                 winrt::to_string(ex.message()));
        return std::unexpected(makeErrorCode(CaptureError::SessionStartFailed));
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Running;
    }

    VF_INFO("WinrtCaptureSource started (display index: {})", config.preferredDisplayIndex);
    return {};
#endif
}

std::expected<void, std::error_code> WinrtCaptureSource::stop() {
#ifndef _WIN32
    return std::unexpected(makeErrorCode(CaptureError::PlatformNotSupported));
#else
    {
        std::scoped_lock lock(stateMutex);
        if (state == CaptureState::Idle) {
            return {};
        }
        state = CaptureState::Stopping;
    }

    std::error_code stopError;
    if (session != nullptr) {
        const std::expected<void, std::error_code> sessionStopResult = session->stop();
        if (!sessionStopResult) {
            stopError = sessionStopResult.error();
        }
    }

    {
        std::scoped_lock lock(stateMutex);
        state = CaptureState::Idle;
    }

    if (stopError) {
        return std::unexpected(stopError);
    }

    VF_INFO("WinrtCaptureSource stopped");
    return {};
#endif
}

#ifdef _WIN32

void WinrtCaptureSource::onFrameArrived(
    const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable& args) {
    static_cast<void>(args);

    std::shared_ptr<IWinrtFrameSink> frameSinkSnapshot;
    {
        std::scoped_lock lock(stateMutex);
        if (state != CaptureState::Running) {
            return;
        }
        frameSinkSnapshot = frameSink;
    }
    if (frameSinkSnapshot == nullptr) {
        return;
    }

    {
        std::scoped_lock lock(stateMutex);
        if (state != CaptureState::Running) {
            return;
        }
    }

    try {
        const auto frame = sender.TryGetNextFrame();
        if (!frame) {
            return;
        }

        const auto surface = frame.Surface();
        if (!surface) {
            return;
        }

        auto access =
            surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        void* textureRaw = nullptr;
        const HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D), &textureRaw);
        if (SUCCEEDED(hr)) {
            texture.attach(static_cast<ID3D11Texture2D*>(textureRaw));
        }
        if (FAILED(hr) || !texture) {
            return;
        }

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        CaptureFrameInfo info;
        info.width = desc.Width;
        info.height = desc.Height;
        info.systemRelativeTime100ns = frame.SystemRelativeTime().count();

        {
            std::scoped_lock lock(stateMutex);
            if (state != CaptureState::Running) {
                return;
            }
        }

        frameSinkSnapshot->onFrame(texture.get(), info);
    } catch (const winrt::hresult_error& ex) {
        VF_WARN("WinrtCaptureSource frame processing failed with WinRT exception: {}",
                winrt::to_string(ex.message()));
    } catch (const std::exception& ex) {
        VF_WARN("WinrtCaptureSource frame processing failed with exception: {}", ex.what());
    } catch (...) {
        VF_WARN("WinrtCaptureSource frame processing failed with unknown exception");
    }
}

#endif

} // namespace vf
