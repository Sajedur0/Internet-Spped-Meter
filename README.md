# Internet Speed Meter for Windows

A **native C++ / Win32 API** app that shows live download and upload speed directly on the Windows taskbar (inside the taskbar strip, just left of the tray icons and clock). When the app starts, it does not open a large main window — it lives in the tray and keeps running in the background.

> **Why C++/Win32?** It is the best choice for low RAM and CPU usage. There is no Electron, browser engine, Java, or .NET runtime here; only Windows' own APIs, GDI, and the IP Helper API are used. As a result, the app is small, fast, and very light when idle. Actual RAM usage depends on the Windows version, DPI, and other running apps.

## Features

- Updates the upload/download rate every **1 second**
- Reads Windows network byte counters via `GetIfTable2`; it does not use test traffic, packet capture, or external servers
- Skips common virtual/loopback adapters and sums traffic from physical active adapters, reducing the chance of counting the same traffic twice
- Small, always-on-top, click-through meter sitting directly on the taskbar (inside the taskbar strip, left of the tray icons/clock), with light/dark theme aware colors
- Follows the taskbar when it moves or resizes, hides with it when auto-hide is on, re-places itself after Explorer restarts, and gets out of the way while a fullscreen app/game is in the foreground
- Tray icon:
  - **Left-click:** Show/hide the meter
  - **Right-click:** Show speed on taskbar, Start with Windows, About, Exit
- When **Start with Windows** is enabled, it starts automatically at login for the current user; no administrator permission is required
- Only one instance runs, even if launched multiple times
- Tries to re-add the tray icon even if Explorer restarts
- Ready for high-DPI displays and the primary taskbar on multiple monitors
- No telemetry, ads, cloud calls, or third-party dependencies

## An important Windows limitation

Windows 10/11 does not provide a supported public API for placing another app's live text **inside** the notification area, and it is not possible to append a custom button to the taskbar without injecting code into Explorer. Injection or undocumented taskbar hacks risk crashes/breakage after Windows updates and are also bad from a security perspective.

Therefore, this project uses a safe and stable approach: a tiny borderless, click-through window is positioned **inside the taskbar strip itself** — the same place the taskbar occupies, just left of the tray icons and clock — instead of floating above the work area. It is sized to the taskbar height and re-positioned every second, so it moves with the taskbar, hides with it when auto-hide is enabled, and comes back after Explorer restarts. Nothing is injected into or modified inside the Windows shell.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 with **Desktop development with C++** workload
  - This provides the MSVC compiler, Windows SDK, and CMake
- CMake 3.21 or newer

## Build

Open Visual Studio Developer PowerShell and run from the repository root:

```powershell
.\scripts\build-release.ps1
```

Or manual build:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

Built executable:

```text
build\Release\InternetSpeedMeter.exe
```

## Run and usage

```powershell
.\build\Release\InternetSpeedMeter.exe
```

1. In the first second, a baseline is taken from the counters; actual speed will be shown from the next update.
2. You will see the meter sitting **on the taskbar** itself, just left of the tray icons/clock, with two lines:
   - `↓` Download
   - `↑` Upload
3. Right-click the tray icon and tick **Start with Windows**, and the app will keep running in the background after your next Windows login.
4. Left-click to hide the meter. To stop the process, choose **Exit** from the tray menu.

## Where startup and settings live

The app only uses the current user's registry:

- Meter visibility: `HKCU\Software\InternetSpeedMeter\ShowOverlay`
- Start with Windows: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\InternetSpeedMeter`

No administrator permission, service, or scheduled task is used.

## Project layout

```text
.
├── assets/
│   └── speed-meter.ico          # tray/application icon
├── scripts/
│   └── build-release.ps1        # release build helper
├── src/
│   ├── main.cpp                 # Win32 UI, tray, registry, speed sampling
│   ├── resource.h
│   └── speed_meter.rc.in
├── .github/workflows/
│   └── windows-build.yml        # Windows CI build
└── CMakeLists.txt
```

## Technical design

| Topic | Decision |
| --- | --- |
| Language | C++17 |
| UI | Direct Win32 + GDI; no GUI framework |
| Network measurement | Windows IP Helper API (`GetIfTable2`) |
| Update model | One UI timer, 1000 ms interval; no separate busy worker thread |
| Background mode | Tray process + small topmost taskbar-level window |
| Memory/CPU goal | Minimize runtime/framework and limit idle wake-ups |
| Taskbar integration | Meter window aligned to `Shell_TrayWnd` next to the tray/clock; no Explorer injection |

## Development notes

- This project is Windows-only. CMake configure will intentionally fail on Linux/macOS.
- The GitHub Actions Windows workflow builds the x64 Release executable on every push/PR.
- Speed unit is bytes per second (`KB/s`, `MB/s`). It shows the total traffic of all active physical adapters on the internet connection; not traffic for a specific process.

Licensed under [Apache-2.0](LICENSE).
