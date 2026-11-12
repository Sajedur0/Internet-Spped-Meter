# Internet Speed Meter for Windows

একটি **native C++ / Win32 API** অ্যাপ যা Windows-এর taskbar-এর ডান পাশে (notification area-এর ঠিক উপরে) live download ও upload speed দেখায়। অ্যাপটি চালু হলে কোনো বড় main window খোলে না—tray-তে থাকে এবং background-এ চলতে থাকে।

> **কেন C++/Win32?** কম RAM ও CPU ব্যবহারের জন্য এটি সবচেয়ে উপযুক্ত পছন্দ। এখানে Electron, browser engine, Java বা .NET runtime নেই; শুধু Windows-এর নিজস্ব API, GDI এবং IP Helper API ব্যবহার করা হয়েছে। ফলে অ্যাপটি ছোট, দ্রুত এবং idle অবস্থায় খুব হালকা। প্রকৃত RAM ব্যবহার Windows সংস্করণ, DPI ও অন্যান্য চলমান app-এর ওপর নির্ভর করে।

## কী আছে

- প্রতি **১ সেকেন্ডে** upload/download rate আপডেট করে
- `GetIfTable2` দিয়ে Windows-এর network byte counter পড়ে; কোনো test traffic, packet capture বা external server ব্যবহার করে না
- সাধারণ virtual/loopback adapter বাদ দিয়ে physical active adapter-এর traffic যোগ করে, যাতে একই traffic দুইবার গণনার সম্ভাবনা কমে
- taskbar-এর ডান দিকের notification area-এর উপরে ছোট, always-on-top, click-through overlay
- tray icon:
  - **Left-click:** meter দেখানো/লুকানো
  - **Right-click:** Show speed meter, Start with Windows, About, Exit
- `Start with Windows` চালু করলে বর্তমান user-এর জন্য login-এর সময় স্বয়ংক্রিয়ভাবে চালু হবে; administrator permission লাগে না
- একাধিকবার চালালেও একটিই instance চলবে
- Explorer restart হলেও tray icon আবার যোগ করার চেষ্টা করে
- high-DPI display এবং multiple monitor-এর primary taskbar-এর জন্য প্রস্তুত
- কোনো telemetry, বিজ্ঞাপন, cloud call বা third-party dependency নেই

## একটি গুরুত্বপূর্ণ Windows সীমাবদ্ধতা

Windows 10/11 notification area-এর **ভেতরে** অন্য app-এর live text বসানোর কোনো supported public API দেয় না। Explorer-এ code injection বা undocumented taskbar hack ব্যবহার করলে Windows update-এর পরে crash/ভাঙার ঝুঁকি থাকে এবং নিরাপত্তার দিক থেকেও খারাপ।

তাই এই project নিরাপদ ও স্থিতিশীল পদ্ধতি ব্যবহার করে: taskbar-এর notification area-এর **ঠিক উপরে** ছোট overlay দেখায়। দেখতে taskbar-এর right side-এর মতোই থাকে, কিন্তু Windows shell modify করে না।

## Requirements

- Windows 10 বা Windows 11
- Visual Studio 2022 with **Desktop development with C++** workload
  - এতে MSVC compiler, Windows SDK এবং CMake পাওয়া যায়
- CMake 3.21 বা নতুন

## Build

Visual Studio Developer PowerShell খুলে repository root-এ চালান:

```powershell
.\scripts\build-release.ps1
```

অথবা manual build:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

তৈরি হওয়া executable:

```text
build\Release\InternetSpeedMeter.exe
```

## Run ও ব্যবহার

```powershell
.\build\Release\InternetSpeedMeter.exe
```

1. প্রথম second-এ counter-এর baseline নেওয়া হয়; পরের update থেকে actual speed দেখা যাবে।
2. Desktop-এর নিচের ডান দিকে দুই line দেখা যাবে:
   - `↓` Download
   - `↑` Upload
3. Tray icon-এ right-click করে **Start with Windows** tick করুন, তাহলে পরবর্তী Windows login-এর পরেও app background-এ চলবে।
4. Meter লুকাতে left-click করুন। Process বন্ধ করতে চাইলে tray menu থেকে **Exit** বেছে নিন।

## Startup ও settings কোথায় থাকে

অ্যাপটি শুধুমাত্র current user-এর registry ব্যবহার করে:

- Overlay visibility: `HKCU\Software\InternetSpeedMeter\ShowOverlay`
- Start with Windows: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\InternetSpeedMeter`

কোনো administrator permission, service বা scheduled task ব্যবহার করা হয় না।

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

## প্রযুক্তিগত নকশা

| বিষয় | সিদ্ধান্ত |
| --- | --- |
| Language | C++17 |
| UI | সরাসরি Win32 + GDI; কোনো GUI framework নয় |
| Network measurement | Windows IP Helper API (`GetIfTable2`) |
| Update model | একটি UI timer, 1000 ms interval; আলাদা busy worker thread নেই |
| Background mode | tray process + hidden popup overlay |
| Memory/CPU লক্ষ্য | runtime/framework কমানো এবং idle wake-up সীমিত রাখা |
| Taskbar integration | supported tray API + overlay; Explorer injection নয় |

## Development notes

- প্রকল্পটি Windows-only। Linux/macOS থেকে CMake configure ইচ্ছাকৃতভাবে fail করবে।
- GitHub Actions-এর Windows workflow প্রতিটি push/PR-এ x64 Release executable build করে।
- Speed unit byte per second (`KB/s`, `MB/s`)। এটি internet connection-এর সব active physical adapter-এর মোট traffic দেখায়; কোনো নির্দিষ্ট process-এর traffic নয়।

Licensed under [Apache-2.0](LICENSE).
