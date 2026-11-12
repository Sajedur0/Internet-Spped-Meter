# Internet Speed Meter for Windows

A **native C++ / Win32 API** app that shows live download and upload speed on the right side of the Windows taskbar (just above the notification area). When the app starts, it does not open a large main window — it lives in the tray and keeps running in the background.

> **Why C++/Win32?** It is the best choice for low RAM and CPU usage. There is no Electron, browser engine, Java, or .NET runtime here; only Windows' own APIs, GDI, and the IP Helper API are used. As a result, the app is small, fast, and very light when idle. Actual RAM usage depends on the Windows version, DPI, and other running apps.

## Features

- Updates the upload/download rate every **1 second**
- Reads Windows network byte counters via `GetIfTable2`; it does not use test traffic, packet capture, or external servers
- Skips common virtual/loopback adapters and sums traffic from physical active adapters, reducing the chance of counting the same traffic twice
- Small, always-on-top, click-through overlay above the notification area on the right side of the taskbar
- Tray icon:
  - **Left-click:** Show/hide the meter
  - **Right-click:** Show speed meter, Start with Windows, About, Exit
- When **Start with Windows** is enabled, it starts automatically at login for the current user; no administrator permission is required
- Only one instance runs, even if launched multiple times
- Tries to re-add the tray icon even if Explorer restarts
- Ready for high-DPI displays and the primary taskbar on multiple monitors
- No telemetry, ads, cloud calls, or third-party dependencies

## An important Windows limitation

Windows 10/11 does not provide a supported public API for placing another app's live text **inside** the notification area. Using code injection into Explorer or undocumented taskbar hacks risks crashes/breakage after Windows updates and is also bad from a security perspective.

Therefore, this project uses a safe and stable approach: it shows a small overlay **just above** the taskbar's notification area. It looks like it is on the right side of the taskbar, but it does not modify the Windows shell.

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
2. You will see two lines at the bottom-right of the desktop:
   - `↓` Download
   - `↑` Upload
3. Right-click the tray icon and tick **Start with Windows**, and the app will keep running in the background after your next Windows login.
4. Left-click to hide the meter. To stop the process, choose **Exit** from the tray menu.

## Where startup and settings live

The app only uses the current user's registry:

- Overlay visibility: `HKCU\Software\InternetSpeedMeter\ShowOverlay`
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
| Background mode | Tray process + hidden popup overlay |
| Memory/CPU goal | Minimize runtime/framework and limit idle wake-ups |
| Taskbar integration | Supported tray API + overlay; no Explorer injection |

## Development notes

- This project is Windows-only. CMake configure will intentionally fail on Linux/macOS.
- The GitHub Actions Windows workflow builds the x64 Release executable on every push/PR.
- Speed unit is bytes per second (`KB/s`, `MB/s`). It shows the total traffic of all active physical adapters on the internet connection; not traffic for a specific process.

Licensed under [Apache-2.0](LICENSE).
