// Copyright 2026
// Licensed under the Apache License, Version 2.0.
//
// A deliberately small Win32 tray application. It samples Windows interface
// byte counters once per second and draws a click-through meter directly on
// the taskbar (inside the taskbar strip, left of the tray/clock) rather than
// injecting anything into Explorer.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// iphlpapi.h includes netioapi.h in user mode; provide its Winsock types first.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <strsafe.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "resource.h"

namespace {

constexpr wchar_t kApplicationName[] = L"Internet Speed Meter";
constexpr wchar_t kWindowClassName[] = L"InternetSpeedMeter.OverlayWindow";
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\InternetSpeedMeter.SingleInstance";
constexpr wchar_t kSettingsRegistryKey[] = L"Software\\InternetSpeedMeter";
constexpr wchar_t kOverlayVisibleValue[] = L"ShowOverlay";
constexpr wchar_t kRunRegistryKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunRegistryValue[] = L"InternetSpeedMeter";

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kUpdateTimerId = 1;
constexpr UINT kUpdateIntervalMs = 1000;
constexpr UINT kTrayIconId = 1;

constexpr UINT kMenuToggleOverlay = 1001;
constexpr UINT kMenuToggleStartup = 1002;
constexpr UINT kMenuAbout = 1003;
constexpr UINT kMenuExit = 1004;

struct InterfaceCounters {
    std::uint64_t receivedBytes = 0;
    std::uint64_t sentBytes = 0;
    bool valid = false;
};

struct SpeedReading {
    double downloadBytesPerSecond = 0.0;
    double uploadBytesPerSecond = 0.0;
};

bool IsUsableInterface(const MIB_IF_ROW2& row) {
    return row.OperStatus == IfOperStatusUp &&
           row.Type != IF_TYPE_SOFTWARE_LOOPBACK;
}

// Prefer hardware interfaces. This avoids counting the same traffic twice on
// common virtual adapters. If a device only exposes a virtual active adapter,
// the caller falls back to all non-loopback interfaces.
InterfaceCounters ReadInterfaceCounters() {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return {};
    }

    bool hasActiveHardwareInterface = false;
    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IF_ROW2& row = table->Table[index];
        if (IsUsableInterface(row) &&
            row.InterfaceAndOperStatusFlags.HardwareInterface) {
            hasActiveHardwareInterface = true;
            break;
        }
    }

    InterfaceCounters counters{};
    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const MIB_IF_ROW2& row = table->Table[index];
        if (!IsUsableInterface(row)) {
            continue;
        }

        if (hasActiveHardwareInterface &&
            !row.InterfaceAndOperStatusFlags.HardwareInterface) {
            continue;
        }

        counters.receivedBytes += row.InOctets;
        counters.sentBytes += row.OutOctets;
    }

    FreeMibTable(table);
    counters.valid = true;
    return counters;
}

class SpeedSampler {
public:
    bool Sample(SpeedReading* reading) {
        const InterfaceCounters current = ReadInterfaceCounters();
        if (!current.valid) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!hasPreviousSample_) {
            previousCounters_ = current;
            previousSampleTime_ = now;
            hasPreviousSample_ = true;
            *reading = {};
            return true;
        }

        const double elapsedSeconds =
            std::chrono::duration<double>(now - previousSampleTime_).count();
        if (elapsedSeconds <= 0.0) {
            return false;
        }

        reading->downloadBytesPerSecond =
            BytesSince(current.receivedBytes, previousCounters_.receivedBytes) /
            elapsedSeconds;
        reading->uploadBytesPerSecond =
            BytesSince(current.sentBytes, previousCounters_.sentBytes) /
            elapsedSeconds;

        previousCounters_ = current;
        previousSampleTime_ = now;
        return true;
    }

    void Reset() {
        hasPreviousSample_ = false;
    }

private:
    static double BytesSince(std::uint64_t current, std::uint64_t previous) {
        // An adapter can reconnect or reset its counter. Treat that sample as
        // zero instead of showing a very large, incorrect speed.
        if (current < previous) {
            return 0.0;
        }
        return static_cast<double>(current - previous);
    }

    InterfaceCounters previousCounters_{};
    std::chrono::steady_clock::time_point previousSampleTime_{};
    bool hasPreviousSample_ = false;
};

std::wstring FormatSpeed(double bytesPerSecond) {
    constexpr double kKilobyte = 1024.0;
    constexpr double kMegabyte = kKilobyte * 1024.0;
    constexpr double kGigabyte = kMegabyte * 1024.0;

    const double value = std::max(0.0, bytesPerSecond);
    wchar_t buffer[64]{};

    if (value < kKilobyte) {
        StringCchPrintfW(buffer, _countof(buffer), L"%.0f B/s", value);
    } else if (value < kMegabyte) {
        StringCchPrintfW(buffer, _countof(buffer), L"%.1f KB/s", value / kKilobyte);
    } else if (value < kGigabyte) {
        StringCchPrintfW(buffer, _countof(buffer), L"%.2f MB/s", value / kMegabyte);
    } else {
        StringCchPrintfW(buffer, _countof(buffer), L"%.2f GB/s", value / kGigabyte);
    }

    return buffer;
}

int ScaleForDpi(int pixelsAt96Dpi, UINT dpi) {
    return MulDiv(pixelsAt96Dpi, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

UINT GetWindowDpi(HWND window) {
    const UINT dpi = GetDpiForWindow(window);
    return dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
}

BOOL CALLBACK FindPrimaryMonitorCallback(HMONITOR monitor,
                                         HDC,
                                         LPRECT,
                                         LPARAM userData) {
    auto* primaryMonitor = reinterpret_cast<HMONITOR*>(userData);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo) &&
        (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0) {
        *primaryMonitor = monitor;
        return FALSE;
    }
    return TRUE;
}

HMONITOR FindPrimaryMonitor() {
    HMONITOR primaryMonitor = nullptr;
    EnumDisplayMonitors(nullptr, nullptr, FindPrimaryMonitorCallback,
                        reinterpret_cast<LPARAM>(&primaryMonitor));
    return primaryMonitor;
}

// The primary taskbar window. The meter is positioned directly on top of this
// window's area (inside the taskbar strip) instead of floating above it, so it
// looks like part of the taskbar without injecting anything into Explorer.
HWND FindTaskbarWindow() {
    return FindWindowW(L"Shell_TrayWnd", nullptr);
}

bool TryGetWindowRect(HWND window, RECT* rect) {
    return window != nullptr && rect != nullptr &&
           GetWindowRect(window, rect) != 0;
}

// True when the taskbar is actually on screen. Explorer hides the window when
// the taskbar is turned off, and auto-hide slides it almost entirely off the
// monitor edge, which leaves only a few pixels visible.
bool IsTaskbarOnScreen(HWND taskbar, const RECT& taskbarRect) {
    if (!IsWindowVisible(taskbar)) {
        return false;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo)) {
        return true;
    }

    RECT visible{};
    if (!IntersectRect(&visible, &taskbarRect, &monitorInfo.rcMonitor)) {
        return false;
    }

    const long long taskbarArea =
        static_cast<long long>(taskbarRect.right - taskbarRect.left) *
        static_cast<long long>(taskbarRect.bottom - taskbarRect.top);
    const long long visibleArea =
        static_cast<long long>(visible.right - visible.left) *
        static_cast<long long>(visible.bottom - visible.top);
    return visibleArea * 2 >= taskbarArea;
}

// A fullscreen app (game, video player) covers the taskbar, so the meter has
// no business being on top of it. Only the monitor holding the taskbar counts,
// so a fullscreen app on a second display does not hide the meter.
bool IsFullScreenAppActiveOn(HMONITOR monitor) {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || !IsWindowVisible(foreground) ||
        foreground == GetShellWindow()) {
        return false;
    }

    if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST) != monitor) {
        return false;
    }

    RECT windowRect{};
    if (!GetWindowRect(foreground, &windowRect)) {
        return false;
    }

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return false;
    }

    return windowRect.left <= monitorInfo.rcMonitor.left &&
           windowRect.top <= monitorInfo.rcMonitor.top &&
           windowRect.right >= monitorInfo.rcMonitor.right &&
           windowRect.bottom >= monitorInfo.rcMonitor.bottom;
}

bool IsLightTheme() {
    DWORD value = 0;
    DWORD valueSize = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &valueSize);
    return status == ERROR_SUCCESS && value != 0;
}

std::wstring GetExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;) {
        const DWORD copied = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return L"";
        }

        if (static_cast<std::size_t>(copied) < buffer.size() - 1) {
            return std::wstring(buffer.data(), copied);
        }

        // A long-path-aware executable can live beyond MAX_PATH. Grow the
        // buffer until Windows returns a complete path.
        if (buffer.size() >= 32768) {
            return L"";
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool IsStartupEnabled() {
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunRegistryKey, 0, KEY_QUERY_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) {
        return false;
    }

    DWORD valueType = 0;
    const LSTATUS queryStatus = RegQueryValueExW(
        key, kRunRegistryValue, nullptr, &valueType, nullptr, nullptr);
    RegCloseKey(key);
    return queryStatus == ERROR_SUCCESS &&
           (valueType == REG_SZ || valueType == REG_EXPAND_SZ);
}

bool SetStartupEnabled(bool enabled) {
    HKEY key = nullptr;

    if (enabled) {
        const LSTATUS createStatus = RegCreateKeyExW(
            HKEY_CURRENT_USER, kRunRegistryKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (createStatus != ERROR_SUCCESS) {
            return false;
        }

        const std::wstring executablePath = GetExecutablePath();
        if (executablePath.empty()) {
            RegCloseKey(key);
            return false;
        }

        const std::wstring commandLine = L"\"" + executablePath + L"\"";
        const DWORD bytes = static_cast<DWORD>(
            (commandLine.size() + 1) * sizeof(wchar_t));
        const LSTATUS setStatus = RegSetValueExW(
            key, kRunRegistryValue, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(commandLine.c_str()), bytes);
        RegCloseKey(key);
        return setStatus == ERROR_SUCCESS;
    }

    const LSTATUS openStatus = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunRegistryKey, 0, KEY_SET_VALUE, &key);
    if (openStatus == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (openStatus != ERROR_SUCCESS) {
        return false;
    }

    const LSTATUS deleteStatus = RegDeleteValueW(key, kRunRegistryValue);
    RegCloseKey(key);
    return deleteStatus == ERROR_SUCCESS || deleteStatus == ERROR_FILE_NOT_FOUND;
}

bool LoadOverlayVisibility() {
    DWORD value = 1;
    DWORD valueSize = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, kSettingsRegistryKey, kOverlayVisibleValue,
        RRF_RT_REG_DWORD, nullptr, &value, &valueSize);
    return status == ERROR_SUCCESS ? value != 0 : true;
}

void SaveOverlayVisibility(bool visible) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsRegistryKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    const DWORD value = visible ? 1U : 0U;
    RegSetValueExW(key, kOverlayVisibleValue, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

void ConfigureDpiAwareness() {
    // Per-monitor V2 gives the small taskbar meter crisp text when it is used
    // on a secondary display with a different scale. Fall back cleanly on older
    // Windows versions where this entry point does not exist.
    using SetProcessDpiAwarenessContextFunction =
        BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const FARPROC rawSetContext = user32 == nullptr
                                      ? nullptr
                                      : GetProcAddress(user32,
                                                       "SetProcessDpiAwarenessContext");

    // GetProcAddress deliberately returns FARPROC instead of a typed function
    // pointer. This Win32 ABI-compatible cast is required for optional APIs.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#endif
    const auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
        rawSetContext);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

    if (setContext != nullptr &&
        setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }

    SetProcessDPIAware();
}

class SpeedMeterApplication {
public:
    int Run(HINSTANCE instance) {
        instance_ = instance;
        singleton_ = this;

        if (!CreateMainWindow()) {
            singleton_ = nullptr;
            return 1;
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        singleton_ = nullptr;
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window,
                                             UINT message,
                                             WPARAM wParam,
                                             LPARAM lParam) {
        if (singleton_ != nullptr) {
            return singleton_->HandleMessage(window, message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool CreateMainWindow() {
        taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SPEED_METER));
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&windowClass) == 0) {
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                WS_EX_TRANSPARENT,
            kWindowClassName, kApplicationName, WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);
        if (window_ == nullptr) {
            return false;
        }

        // Fully opaque keeps the meter crisp over the taskbar (and avoids
        // redraw ghosting when the shell repaints behind it).
        SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        dpi_ = GetWindowDpi(window_);
        CreateFonts();

        overlayVisible_ = LoadOverlayVisibility();
        startupEnabled_ = IsStartupEnabled();
        AddTrayIcon();

        // The first sample establishes the baseline, so the first real rate
        // appears on the following one-second tick.
        UpdateSpeed();
        PositionMeter();
        if (overlayVisible_) {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            UpdateWindow(window_);
        }

        SetTimer(window_, kUpdateTimerId, kUpdateIntervalMs, nullptr);
        return true;
    }

    LRESULT HandleMessage(HWND window,
                          UINT message,
                          WPARAM wParam,
                          LPARAM lParam) {
        if (message == taskbarCreatedMessage_) {
            // Explorer has restarted and removed the notification icon and
            // the taskbar window. Re-add the icon and re-place the meter on
            // the new taskbar.
            trayIconAdded_ = false;
            AddTrayIcon();
            hasLastMeterPosition_ = false;
            PositionMeter();
            return 0;
        }

        switch (message) {
            case WM_TIMER:
                if (wParam == kUpdateTimerId) {
                    UpdateSpeed();
                }
                return 0;

            case WM_PAINT:
                PaintOverlay();
                return 0;

            case WM_ERASEBKGND:
                return 1;

            case WM_NCHITTEST:
                // The meter is informational. Clicks go to the taskbar (or the
                // window below) rather than taking focus away from the user's
                // work. WS_EX_TRANSPARENT covers the rest of the hit test.
                return HTTRANSPARENT;

            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;

            case WM_DPICHANGED:
                dpi_ = HIWORD(wParam);
                CreateFonts();
                hasLastMeterPosition_ = false;
                PositionMeter();
                InvalidateRect(window_, nullptr, TRUE);
                return 0;

            case WM_DISPLAYCHANGE:
            case WM_SETTINGCHANGE:
                hasLastMeterPosition_ = false;
                PositionMeter();
                InvalidateRect(window_, nullptr, FALSE);
                return 0;

            case WM_POWERBROADCAST:
                if (wParam == PBT_APMRESUMEAUTOMATIC ||
                    wParam == PBT_APMRESUMESUSPEND) {
                    sampler_.Reset();
                }
                return TRUE;

            case kTrayCallbackMessage:
                HandleTrayCallback(lParam);
                return 0;

            case WM_COMMAND:
                HandleCommand(LOWORD(wParam));
                return 0;

            case WM_CLOSE:
                // There is no normal application window to close. Keep the
                // process running and hide only the meter; Exit is in the tray
                // menu so it is always an intentional action.
                SetOverlayVisible(false);
                return 0;

            case WM_DESTROY:
                KillTimer(window_, kUpdateTimerId);
                RemoveTrayIcon();
                DeleteFonts();
                PostQuitMessage(0);
                return 0;

            default:
                return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    void HandleTrayCallback(LPARAM callbackData) {
        // With NOTIFYICON_VERSION_4 the shell puts the event code in the low
        // word and the icon ID in the high word of lParam.
        const UINT event = LOWORD(static_cast<DWORD_PTR>(callbackData));
        switch (event) {
            case WM_LBUTTONUP:
                SetOverlayVisible(!overlayVisible_);
                break;

            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                ShowTrayMenu();
                break;

            default:
                break;
        }
    }

    void HandleCommand(UINT command) {
        switch (command) {
            case kMenuToggleOverlay:
                SetOverlayVisible(!overlayVisible_);
                break;

            case kMenuToggleStartup:
                if (SetStartupEnabled(!startupEnabled_)) {
                    startupEnabled_ = !startupEnabled_;
                } else {
                    MessageBoxW(window_,
                                L"Could not update the Windows startup setting.",
                                kApplicationName, MB_OK | MB_ICONERROR);
                }
                break;

            case kMenuAbout:
                MessageBoxW(
                    window_,
                    L"Internet Speed Meter\n\n"
                    L"Native Win32 network speed display.\n"
                    L"The meter sits on the taskbar, left of the tray icons.\n"
                    L"Left-click the tray icon to show or hide the meter.\n"
                    L"Right-click it for startup and exit options.",
                    kApplicationName, MB_OK | MB_ICONINFORMATION);
                break;

            case kMenuExit:
                DestroyWindow(window_);
                break;

            default:
                break;
        }
    }

    void AddTrayIcon() {
        if (window_ == nullptr || trayIconAdded_) {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = window_;
        iconData.uID = kTrayIconId;
        iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        iconData.uCallbackMessage = kTrayCallbackMessage;
        iconData.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SPEED_METER));
        StringCchCopyW(iconData.szTip, _countof(iconData.szTip),
                       L"Internet Speed Meter");

        if (Shell_NotifyIconW(NIM_ADD, &iconData)) {
            iconData.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &iconData);
            trayIconAdded_ = true;
        }
    }

    void RemoveTrayIcon() {
        if (!trayIconAdded_) {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = window_;
        iconData.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &iconData);
        trayIconAdded_ = false;
    }

    void UpdateTrayTooltip() {
        if (!trayIconAdded_) {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = window_;
        iconData.uID = kTrayIconId;
        iconData.uFlags = NIF_TIP;
        StringCchPrintfW(iconData.szTip, _countof(iconData.szTip),
                         L"Download: %s\nUpload: %s",
                         downloadText_.c_str(), uploadText_.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &iconData);
    }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }

        AppendMenuW(menu, MF_STRING | (overlayVisible_ ? MF_CHECKED : MF_UNCHECKED),
                    kMenuToggleOverlay, L"Show speed on taskbar");
        AppendMenuW(menu, MF_STRING | (startupEnabled_ ? MF_CHECKED : MF_UNCHECKED),
                    kMenuToggleStartup, L"Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuAbout, L"About");
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                       cursor.x, cursor.y, 0, window_, nullptr);
        DestroyMenu(menu);

        // Required by the shell so a subsequent tray click behaves correctly.
        PostMessageW(window_, WM_NULL, 0, 0);
    }

    void SetOverlayVisible(bool visible) {
        overlayVisible_ = visible;
        SaveOverlayVisibility(overlayVisible_);

        if (overlayVisible_) {
            PositionMeter();
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            UpdateWindow(window_);
        } else {
            ShowWindow(window_, SW_HIDE);
        }
    }

    void UpdateSpeed() {
        // At login Explorer can take a moment to create its notification area.
        // Retry on the normal timer if the first NIM_ADD happened too early.
        if (!trayIconAdded_) {
            AddTrayIcon();
        }

        SpeedReading reading{};
        if (!sampler_.Sample(&reading)) {
            downloadText_ = L"Unavailable";
            uploadText_ = L"Unavailable";
        } else {
            downloadText_ = FormatSpeed(reading.downloadBytesPerSecond);
            uploadText_ = FormatSpeed(reading.uploadBytesPerSecond);
        }

        // Follow the taskbar if it moved/resized and hide with it when it
        // auto-hides. This runs once per second on the existing timer.
        PositionMeter();
        InvalidateRect(window_, nullptr, FALSE);
        UpdateTrayTooltip();
    }

    void PositionMeter() {
        if (window_ == nullptr) {
            return;
        }

        const int width = ScaleForDpi(150, dpi_);
        const int gap = ScaleForDpi(6, dpi_);
        const int defaultHeight = ScaleForDpi(36, dpi_);

        const HWND taskbar = FindTaskbarWindow();
        RECT taskbarRect{};
        if (taskbar != nullptr && TryGetWindowRect(taskbar, &taskbarRect)) {
            // Hide the meter with the taskbar. With auto-hide the taskbar is
            // not "invisible": it slides mostly off the screen edge, so also
            // treat a mostly off-screen taskbar as hidden. The next timer tick
            // brings the meter back as soon as the taskbar reappears.
            const HMONITOR taskbarMonitor =
                MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);

            if (!IsTaskbarOnScreen(taskbar, taskbarRect) ||
                IsFullScreenAppActiveOn(taskbarMonitor)) {
                if (IsWindowVisible(window_)) {
                    ShowWindow(window_, SW_HIDE);
                }
                return;
            }

            const int taskbarWidth = taskbarRect.right - taskbarRect.left;
            const int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
            const bool horizontal = taskbarWidth > taskbarHeight;

            if (horizontal) {
                if (taskbarHeight < ScaleForDpi(20, dpi_)) {
                    if (IsWindowVisible(window_)) {
                        ShowWindow(window_, SW_HIDE);
                    }
                    return;
                }

                // Fit the meter inside the taskbar strip, including the
                // "use small taskbar buttons" setting.
                int height = defaultHeight;
                const int maxHeight = taskbarHeight - ScaleForDpi(2, dpi_);
                if (height > maxHeight) {
                    height = maxHeight;
                }

                const int y = taskbarRect.top + (taskbarHeight - height) / 2;

                // Default: leave room for the clock and show-desktop button
                // at the far right of the taskbar.
                int x = taskbarRect.right - ScaleForDpi(110, dpi_) - width - gap;

                // Prefer the empty space just left of the tray area so the
                // meter never covers tray icons or the clock.
                const HWND tray =
                    FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
                RECT trayRect{};
                if (tray != nullptr &&
                    TryGetWindowRect(tray, &trayRect) &&
                    trayRect.right > trayRect.left) {
                    x = trayRect.left - width - gap;
                } else {
                    HWND clock =
                        FindWindowExW(taskbar, nullptr, L"TrayClockWClass", nullptr);
                    if (clock == nullptr && tray != nullptr) {
                        clock = FindWindowExW(tray, nullptr, L"TrayClockWClass",
                                              nullptr);
                    }
                    RECT clockRect{};
                    if (clock != nullptr &&
                        TryGetWindowRect(clock, &clockRect)) {
                        x = clockRect.left - width - gap;
                    }
                }

                x = std::max(static_cast<int>(taskbarRect.left) + gap, x);

                PlaceMeter(x, y, width, height);
                if (overlayVisible_ && !IsWindowVisible(window_)) {
                    ShowWindow(window_, SW_SHOWNOACTIVATE);
                    UpdateWindow(window_);
                }
                return;
            }
            // A vertical (left/right) taskbar is too narrow to hold the
            // meter, so hug its inner edge instead of dropping the meter into
            // a desktop corner.
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (taskbarMonitor != nullptr &&
                GetMonitorInfoW(taskbarMonitor, &monitorInfo)) {
                const int availableWidth = static_cast<int>(
                    monitorInfo.rcWork.right - monitorInfo.rcWork.left);
                int meterWidth = width;
                if (meterWidth > availableWidth - (gap * 2)) {
                    meterWidth = availableWidth - (gap * 2);
                }

                const int y = taskbarRect.top + (taskbarHeight - defaultHeight) / 2;
                const int x =
                    (taskbarRect.left <= monitorInfo.rcMonitor.left)
                        ? taskbarRect.right + gap
                        : taskbarRect.left - meterWidth - gap;

                PlaceMeter(x, y, meterWidth, defaultHeight);
                if (overlayVisible_ && !IsWindowVisible(window_)) {
                    ShowWindow(window_, SW_SHOWNOACTIVATE);
                    UpdateWindow(window_);
                }
                return;
            }
        }

        // Fallback while Explorer is not ready and there is no taskbar to sit
        // on yet: use the work-area corner until the next timer tick.
        HMONITOR monitor = FindPrimaryMonitor();
        if (monitor == nullptr) {
            monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY);
        }

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo)) {
            return;
        }

        const int height = defaultHeight;
        const RECT& workArea = monitorInfo.rcWork;
        const RECT& fullArea = monitorInfo.rcMonitor;
        const int workLeft = static_cast<int>(workArea.left);
        const int workTop = static_cast<int>(workArea.top);
        const int workRight = static_cast<int>(workArea.right);
        const int workBottom = static_cast<int>(workArea.bottom);
        const int fullTop = static_cast<int>(fullArea.top);
        const int fullBottom = static_cast<int>(fullArea.bottom);

        int x = workRight - width - gap;
        int y = workBottom - height - gap;

        if (workBottom == fullBottom && workTop > fullTop) {
            y = workTop + gap;
        }

        x = std::max(workLeft + gap, x);
        y = std::max(workTop + gap, y);

        PlaceMeter(x, y, width, height);
        if (overlayVisible_ && !IsWindowVisible(window_)) {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            UpdateWindow(window_);
        }
    }

    void PlaceMeter(int x, int y, int width, int height) {
        const bool sizeChanged = !hasLastMeterPosition_ ||
                                 lastMeterWidth_ != width ||
                                 lastMeterHeight_ != height;

        lastMeterWidth_ = width;
        lastMeterHeight_ = height;
        hasLastMeterPosition_ = true;

        // Re-assert the position on every tick, not just when it changes: the
        // taskbar is a topmost window too and can end up above the meter after
        // user interaction. Moving it back to the top of the topmost band is
        // enough to keep the meter visible on the taskbar.
        SetWindowPos(window_, HWND_TOPMOST, x, y, width, height,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);

        if (sizeChanged) {
            ApplyRoundedRegion(width, height);
        }
    }

    void ApplyRoundedRegion(int width, int height) {
        const int corner = std::max(ScaleForDpi(6, dpi_), 2);
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                         corner, corner);
        if (region == nullptr) {
            return;
        }

        if (SetWindowRgn(window_, region, TRUE) == 0) {
            DeleteObject(region);
        }
        // On success Windows owns the region handle.
    }

    void CreateFonts() {
        DeleteFonts();

        LOGFONTW font{};
        font.lfHeight = -ScaleForDpi(9, dpi_);
        font.lfWeight = FW_SEMIBOLD;
        font.lfQuality = CLEARTYPE_QUALITY;
        StringCchCopyW(font.lfFaceName, _countof(font.lfFaceName), L"Segoe UI");
        textFont_ = CreateFontIndirectW(&font);
    }

    void DeleteFonts() {
        if (textFont_ != nullptr) {
            DeleteObject(textFont_);
            textFont_ = nullptr;
        }
    }

    void PaintOverlay() {
        PAINTSTRUCT paint{};
        HDC deviceContext = BeginPaint(window_, &paint);

        RECT client{};
        GetClientRect(window_, &client);
        const int corner = std::max(ScaleForDpi(6, dpi_), 2);

        // Match the taskbar light/dark theme so the meter stays readable on
        // both. The registry value is re-read on every paint, so a theme
        // change applies on the next one-second tick.
        const bool light = IsLightTheme();
        const COLORREF backgroundColor = light ? RGB(245, 245, 245) : RGB(20, 25, 34);
        const COLORREF borderColor = light ? RGB(190, 190, 190) : RGB(60, 76, 99);
        const COLORREF downloadColor = light ? RGB(0, 90, 180) : RGB(94, 211, 255);
        const COLORREF uploadColor = light ? RGB(0, 130, 60) : RGB(120, 232, 163);

        HBRUSH background = CreateSolidBrush(backgroundColor);
        HPEN border = CreatePen(PS_SOLID, 1, borderColor);
        HGDIOBJ oldBrush = SelectObject(deviceContext, background);
        HGDIOBJ oldPen = SelectObject(deviceContext, border);
        RoundRect(deviceContext, client.left, client.top,
                  client.right, client.bottom, corner, corner);
        SelectObject(deviceContext, oldBrush);
        SelectObject(deviceContext, oldPen);
        DeleteObject(border);
        DeleteObject(background);

        SetBkMode(deviceContext, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(deviceContext, textFont_);
        const int left = ScaleForDpi(8, dpi_);
        const int right = client.right - ScaleForDpi(6, dpi_);
        // Split the client area in half so the two rates fit any taskbar size.
        const int lineHeight = (client.bottom - client.top) / 2;

        std::wstring downloadLine = L"\x2193  " + downloadText_;
        std::wstring uploadLine = L"\x2191  " + uploadText_;

        RECT downloadRect{left, client.top, right, client.top + lineHeight};
        SetTextColor(deviceContext, downloadColor);
        DrawTextW(deviceContext, downloadLine.c_str(), -1, &downloadRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        RECT uploadRect{left, client.top + lineHeight, right, client.bottom};
        SetTextColor(deviceContext, uploadColor);
        DrawTextW(deviceContext, uploadLine.c_str(), -1, &uploadRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        SelectObject(deviceContext, oldFont);
        EndPaint(window_, &paint);
    }

    inline static SpeedMeterApplication* singleton_ = nullptr;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT textFont_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    UINT taskbarCreatedMessage_ = 0;
    bool trayIconAdded_ = false;
    bool overlayVisible_ = true;
    bool startupEnabled_ = false;
    bool hasLastMeterPosition_ = false;
    int lastMeterWidth_ = 0;
    int lastMeterHeight_ = 0;
    SpeedSampler sampler_{};
    std::wstring downloadText_ = L"0 B/s";
    std::wstring uploadText_ = L"0 B/s";
};

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance,
                      HINSTANCE,
                      PWSTR,
                      int) {
    ConfigureDpiAwareness();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (mutex == nullptr) {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    SpeedMeterApplication application;
    const int result = application.Run(instance);

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
