#pragma once

#include <cstdint>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

#include "VisionFlow/capture/capture_error.hpp"
#include "VisionFlow/core/logger.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <d3d12.h>
#endif

namespace vf::dx_utils {

template <typename Fn> class ScopeExit {
  public:
    explicit ScopeExit(Fn&& fn) : fn(std::forward<Fn>(fn)) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&& other) noexcept : fn(std::move(other.fn)), active(other.active) {
        other.active = false;
    }
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() {
        if (active) {
            fn();
        }
    }

    void dismiss() { active = false; }

  private:
    Fn fn;
    bool active = true;
};

template <typename Fn> [[nodiscard]] ScopeExit<std::decay_t<Fn>> makeScopeExit(Fn&& fn) {
    return ScopeExit<std::decay_t<Fn>>(std::forward<Fn>(fn));
}

#ifdef _WIN32
struct DxCallError {
    HRESULT hr = S_OK;
    const char* apiName = "";
    bool isWin32 = false;
};

class UniqueWin32Handle {
  public:
    UniqueWin32Handle() = default;
    explicit UniqueWin32Handle(HANDLE handle) : handle(handle) {}
    UniqueWin32Handle(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle(UniqueWin32Handle&& other) noexcept : handle(other.release()) {}
    UniqueWin32Handle& operator=(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle& operator=(UniqueWin32Handle&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset(other.release());
        return *this;
    }
    ~UniqueWin32Handle() { close(); }

    [[nodiscard]] HANDLE get() const { return handle; }

    [[nodiscard]] HANDLE* put() {
        reset();
        return &handle;
    }

    [[nodiscard]] HANDLE release() {
        HANDLE out = handle;
        handle = nullptr;
        return out;
    }

    void reset(HANDLE newHandle = nullptr) {
        close();
        handle = newHandle;
    }

    explicit operator bool() const { return handle != nullptr; }

  private:
    void close() {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle));
            handle = nullptr;
        }
    }

    HANDLE handle = nullptr;
};

[[nodiscard]] inline std::expected<void, DxCallError> checkD3d(HRESULT hr, const char* apiName) {
    if (SUCCEEDED(hr)) {
        return {};
    }
    return std::unexpected(DxCallError{.hr = hr, .apiName = apiName, .isWin32 = false});
}

[[nodiscard]] inline std::expected<void, DxCallError> checkWin32(bool ok, const char* apiName) {
    if (ok) {
        return {};
    }
    const DWORD lastError = GetLastError();
    return std::unexpected(
        DxCallError{.hr = HRESULT_FROM_WIN32(lastError), .apiName = apiName, .isWin32 = true});
}

[[nodiscard]] inline std::expected<void, std::error_code>
toError(std::expected<void, DxCallError> result, CaptureError errorCode) {
    if (result) {
        return {};
    }

    const DxCallError err = result.error();
    if (err.isWin32) {
        VF_ERROR("{} failed (Win32Error=0x{:08X})", err.apiName,
                 static_cast<std::uint32_t>(HRESULT_CODE(err.hr)));
    } else {
        VF_ERROR("{} failed (HRESULT=0x{:08X})", err.apiName, static_cast<std::uint32_t>(err.hr));
    }
    return std::unexpected(makeErrorCode(errorCode));
}

[[nodiscard]] inline std::expected<void, std::error_code> callD3d(HRESULT hr, const char* apiName,
                                                                  CaptureError errorCode) {
    return toError(checkD3d(hr, apiName), errorCode);
}

[[nodiscard]] inline std::expected<void, std::error_code> callWin32(bool ok, const char* apiName,
                                                                    CaptureError errorCode) {
    return toError(checkWin32(ok, apiName), errorCode);
}

[[nodiscard]] inline D3D12_HEAP_PROPERTIES makeDefaultHeapProperties() {
    return D3D12_HEAP_PROPERTIES{
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 1,
        .VisibleNodeMask = 1,
    };
}

[[nodiscard]] inline D3D12_RESOURCE_DESC makeRawBufferDesc(UINT64 widthBytes,
                                                           D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = widthBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

inline void transitionResource(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                               D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (list == nullptr || resource == nullptr || before == after) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    // D3D12_RESOURCE_BARRIER is a tagged union in the Win32 API.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    list->ResourceBarrier(1, &barrier);
}
#endif

} // namespace vf::dx_utils
