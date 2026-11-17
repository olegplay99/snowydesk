# SnowyDesk

Snow on your Windows desktop, painted by a transparent overlay. Two native C++20
processes: a small state service and a rendering UI. No DLL injection, no
screenshots, no hooks — just a layered topmost window that lets every click
through.

* Transparent, click-through snow overlay on **all monitors**
* Snow accumulates on the **taskbar** and on the **top and bottom edges of visible windows**
* The scene reacts to windows: resize → excess snow falls; close → its snow falls;
  move → the snow travels with the window
* Optional **garland** (flickering bulbs along the top edge of every monitor)
* Density 0–200 (a target particle count, not a probability)
* Runtime **language switch** (Русский / English) without restarting the process
* State survives reboot: settings **and** the snow field live in
  `%ProgramData%\SnowyDesk\state.json`
* Tray icon, main window, settings window, MIT licensed

---

## Requirements

| Component | Version |
|---|---|
| Windows | 10 1809+ / 11 (x64) |
| CMake | 3.25+ |
| Compiler | MSVC 2022 (v143 toolset), x64 |
| Dependencies | `nlohmann/json` 3.11.3, `spdlog` 1.15.3 |

### Dependency decision: FetchContent, not vcpkg

Both dependencies are pulled by CMake `FetchContent` (see `cmake/Dependencies.cmake`).
Rationale: a fresh clone builds with CMake + MSVC + a network connection only; no
vcpkg bootstrap, no triplet files, no `CMAKE_TOOLCHAIN_FILE` juggling. To build
offline, populate the FetchContent cache once and reconfigure with
`-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.

## Build

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Artifacts land in `build\bin\Release\`:

```
SnowyDeskService.exe   state service (owns state.json, never draws)
SnowyDeskUI.exe        tray, windows, snow overlay, garland
snowydesk_tests.exe    serialization / protocol / simulation tests
```

## Install

The UI works without the service (it falls back to a read-only state load), but
**settings are only persisted when the service is running**.

Service (elevated prompt required):

```bat
build\bin\Release\SnowyDeskService.exe --install
build\bin\Release\SnowyDeskService.exe --uninstall
```

Equivalent with `sc`:

```bat
sc create SnowyDesk binPath= "\"C:\path\to\SnowyDeskService.exe\"" start= auto obj= LocalSystem
sc start SnowyDesk
sc stop SnowyDesk
sc delete SnowyDesk
```

Foreground debugging without the SCM:

```bat
SnowyDeskService.exe --console 30
```

### Why LocalSystem?

* it must create and write `%ProgramData%\SnowyDesk\state.json` before a user logs on;
* the optional "start via service" autostart mode needs `WTSQueryUserToken` +
  `CreateProcessAsUser`, which requires SYSTEM privileges;
* the service **never renders** and exposes exactly one named pipe, restricted by
  SDDL to SYSTEM, Administrators and Authenticated Users.

If you prefer a lower-privilege setup, run the service as `NT AUTHORITY\LocalService`
and switch autostart to the Run-key mode (the service then no longer launches the UI).

## Autostart

Two mutually exclusive modes, selected in the settings window:

1. **Run key (default)** — the UI writes
   `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SnowyDeskUI = "<path>\SnowyDeskUI.exe"`.
   No admin rights needed. Rollback: untick "Start with Windows", or run
   `reg delete HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v SnowyDeskUI /f`.
2. **Service** — the UI removes the Run value; on boot the service detects
   `autostartMode == 1` and starts the UI inside the active console session.
   Requires the service to run as LocalSystem. Rollback: untick the checkbox and
   `sc config SnowyDesk start= demand`.

The service always starts with Windows (`start= auto`); stopping the service only
stops state persistence — the overlay keeps running with the settings it already has.

## Architecture

```
SnowyDeskService.exe                         SnowyDeskUI.exe
+---------------------------+   JSON/LF     +------------------------------------+
| SCM host                  |  named pipe   | tray icon + main/settings windows   |
| state.json owner          |<------------->| snow simulation (60 Hz)             |
| 30 s atomic checkpoint    | \\.\pipe\    | Direct2D overlay, 1 window/monitor  |
| WTS user-session launch   |   SnowyDesk   | garland renderer                    |
+---------------------------+               +------------------------------------+
```

Module layout (shared static library per concern, no duplicated types):

| Directory | Contents |
|---|---|
| `common/` | `Settings`, `Particle`, `WindowAnchor`, `AppState`, JSON layer, atomic state store, paths, logging |
| `ipc/` | versioned protocol, pipe server, pipe client |
| `snow/` | particle simulation, window anchoring, `SnowTuning.h` constants |
| `garland/` | bulb layout/flicker for multiple monitors and DPI |
| `i18n/` | every user-visible string (ru/en), live switching |
| `overlay/` | transparent layered windows, Direct2D renderer, tray, main window, app glue |
| `settings/` | settings window (ComboBox/Trackbar/Checkboxes/buttons) |
| `service/` | service host, SCM install/uninstall, session launch |

### Overlay window flags

```
WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
```

Per-pixel alpha is uploaded with `UpdateLayeredWindow`, so fully transparent pixels
are click-through by definition, and `WS_EX_TRANSPARENT` makes the whole window
non-interactive. `WM_NCHITTEST` answers `HTTRANSPARENT` as a third safety net.
Rendering is Direct2D into a 32-bpp premultiplied DIB, one DIB per monitor, at a
capped 60 FPS (`SetTimer` + `timeBeginPeriod(1)`).

## IPC protocol (v1)

Transport: `\\.\pipe\SnowyDesk`, byte-mode, one JSON object per line, UTF-8.
Every frame carries `"v"` (protocol version) and `"id"` (correlation id).
Frames larger than 4 MiB are rejected.

```json
{"v":1,"id":3,"type":"get_state","payload":{}}
{"v":1,"id":3,"ok":true,"payload":{"state":{...}}}
{"v":1,"id":4,"ok":false,"error":"unsupported protocol version 2 (expected 1)"}
```

| type | payload in | payload out |
|---|---|---|
| `ping` | `{}` | `{"pong":true,"service":"SnowyDeskService","protocol":1,"schema":1}` |
| `get_state` | `{}` | `{"state":{schema v1 document}}` |
| `set_state` | `{"state":{...}}` | `{"saved":true,"particles":123}` |
| `apply_settings` | `{"settings":{...}}` | `{"saved":true,"settings":{...}}` |
| `clear_taskbar_snow` | `{}` | `{"removed":17}` |

Validation happens on the server: an unknown `type`, a wrong `"v"`, or a state that
violates schema v1 is answered with `ok:false` and a human-readable `error`.

## state.json

Location: `%ProgramData%\SnowyDesk\state.json` (written only by the service).

```json
{
  "schemaVersion": 1,
  "protocolVersion": 1,
  "savedAtUnixMs": 1763392800000,
  "settings": {
    "language": 0,
    "density": 80,
    "snowSticks": true,
    "garlandEnabled": false,
    "autostart": false,
    "autostartMode": 0,
    "topmostOverGames": false
  },
  "particles": [
    {
      "x": 812.5, "y": 1043.0, "baseX": 810.0, "baseY": 1042.0,
      "vx": 0.0, "vy": 0.0, "size": 2.1,
      "swayPhase": 1.2, "swaySpeed": 0.9, "swayAmplitude": 7.5,
      "age": 42.0, "meltTime": -1.0,
      "state": 3, "anchorHwnd": 0, "anchorEdge": 3
    }
  ]
}
```

* `state`: 0 = falling, 1 = top edge, 2 = bottom edge, 3 = taskbar
* `anchorHwnd` is a session-local handle. On restore, anchors that no longer
  resolve are converted to falling particles — that is the documented graceful
  degradation, snow never gets stuck on a window that no longer exists.
* **Atomic writes**: `state.json.tmp` is written, `fflush` + `_commit`-ed, then
  `ReplaceFileW` (fallback `MoveFileExW`) renames it over the real file. A crash
  can never truncate a valid state.
* **Corruption policy**: an unreadable/invalid document is moved aside as
  `state.json.corrupt-YYYYMMDD-HHMMSS.json` and defaults are used. Unknown extra
  fields are ignored, so an older build can read a newer file.
* Checkpoints: every 30 s, on stop/shutdown, and immediately after
  `set_state` / `apply_settings` / `clear_taskbar_snow`.

## Snow model

Particle: position, fall line (`baseX`), speed, radius, sway phase/speed/amplitude,
lifetime, anchor (HWND + edge). All tunables live in
`snow/include/snowydesk/snow/SnowTuning.h`, each with a comment:

| Constant | Value | Meaning |
|---|---|---|
| `kMaxParticles` | 1200 | hard cap regardless of density |
| `kGravity` / `kTerminalVelocity` | 130 / 150 DIP·s⁻¹ | fall acceleration and cap |
| `kSwayAmplitudeMin/Max` | 3 / 14 DIP | sideways drift |
| `kSpawnIntervalSeconds` | 0.015 | one flake per interval until density is reached |
| `kPileSpillThreshold` | 55 | flakes on one surface before it spills |
| `kPileSpillCount` | 16 | flakes that slide off at once |
| `kPileMaxThickness` | 9 DIP | pile stops accepting snow |
| `kMeltRatePerSecond` | 14 | melt speed when "snow stays" is off |
| `kWindowPollHz` | 10 | window tracking rate |

Window tracking (10 Hz) rebuilds the surface list with `EnumWindows` +
`IsWindowVisible`, skipping `WS_EX_TOOLWINDOW`, `WS_EX_NOACTIVATE`, minimized,
cloaked (other virtual desktop) and zero-size windows, our own process, and
anything outside the virtual screen. The taskbar is `Shell_TrayWnd`, measured with
`DWMWA_EXTENDED_FRAME_BOUNDS`.

Reactions: a window that shrinks releases every flake outside its new bounds; a
closed window releases its whole pile; a moved window shifts its pile by the same
delta. Bottom edges hold a thin "shelf" that spills downwards when it gets too
thick. With "snow stays on windows and the taskbar" disabled, nothing sticks and
existing piles melt at `kMeltRatePerSecond`.

Density is a target count: the controller spawns flakes from above the virtual
screen until the target is reached and removes surplus flakes (falling ones first,
then piles) when it is lowered. 0 clears the field immediately.

## Settings window semantics

| Button | Behaviour |
|---|---|
| **Apply** | validate → apply live (language, density, garland, autostart, topmost) → persist via IPC; window stays open; the snapshot for Cancel is updated |
| **Save** | same as Apply, then close the window |
| **Cancel** | restore the snapshot captured when the window opened (so an earlier Apply is rolled back), re-apply it, close |

Language switching is live: `i18n::SetLanguage` then every window calls
`RefreshTexts()`; no restart, no reload of the simulation.

## Garland, monitors and DPI

One chain per monitor. Bulbs are spaced every 42 DIPs, radius 4.2 DIPs, and all
metrics are multiplied by `dpi/96`, so a 4K chain and a 1080p chain look the same
physically. Each monitor gets a phase offset so the chains do not blink in
lockstep. Flicker is a sine wave (1.6–2.1 rad/s per bulb) mapped onto a five-colour
palette; brightness never drops below 18 % to keep it from strobing.

## "Above everything, including games"

Default behaviour is a topmost layered window plus a periodic
`SetWindowPos(HWND_TOPMOST)` re-assert (once per second), which also recovers the
overlay after a borderless game or a shell restart grabs the top of the z-order.

The checkbox does **not** change the mechanism — there is no DLL injection, no
`SetWindowsHookEx`, no D3D detour. What it documents and acknowledges is the hard
limit: **exclusive fullscreen is not supported** (the display is owned by the game,
and reaching into it would require exactly the injection this project refuses to
do). Borderless/windowed fullscreen works. The UI and the README both say so; when
the box is checked a hint is shown next to it.

## FAQ

**Why does Windows Defender/SmartScreen complain?**
A downloaded, unsigned binary that creates a background service and a topmost
window looks like a classic adware pattern to heuristics. SnowyDesk opens no
sockets, injects nothing, hooks nothing, reads no documents and only writes its own
`state.json` and log files. Verify by reading the source, or build it yourself and
run your own binaries — those never trigger SmartScreen.

**Why no DLL injection for exclusive fullscreen?**
Because injection into other people's processes is what malware does; it breaks
anti-cheat, requires disabling security features and would make the project
undistributable. SnowyDesk stays outside every process it decorates.

**Does the overlay steal focus or block clicks?**
No. It is `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`, its pixels carry per-pixel alpha,
and hit-testing returns `HTTRANSPARENT`. You can click, drag and type underneath it.

**Where are the logs?**
`%ProgramData%\SnowyDesk\service.log` (service) and
`%LOCALAPPDATA%\SnowyDesk\ui.log` (UI), rotating, 1 MiB × 3.

**Settings do not say "connected".**
The service is not running: `sc query SnowyDesk`, then `sc start SnowyDesk` (or
`SnowyDeskService.exe --install`). Everything else still works, but nothing is
persisted.

**How do I remove everything?**
`SnowyDeskService.exe --uninstall`, delete the `HKCU\...\Run\SnowyDeskUI` value if
present, then delete `%ProgramData%\SnowyDesk` and `%LOCALAPPDATA%\SnowyDesk`.

## Tests

```bat
ctest --test-dir build -C Release --output-on-failure
```

`tests/main.cpp` covers: settings/particle/state JSON round-trips, schema rejection
(version mismatch, broken JSON), density clamping, protocol encode/decode for every
message type, protocol version rejection, atomic save + corrupt-file backup, snow
simulation fall, HWND graceful degradation, taskbar clear, density 0.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 SnowyDesk contributors.

---

## Кратко по-русски

SnowyDesk — снег на рабочем столе. Служба `SnowyDeskService` (LocalSystem) хранит
настройки и состояние в `%ProgramData%\SnowyDesk\state.json`, UI-процесс
`SnowyDeskUI` рисует прозрачный оверлей поверх всех мониторов через Direct2D и
принимает настройки через именованный канал `\\.\pipe\SnowyDesk`.

Сборка: `cmake -B build -G "Visual Studio 17 2022" -A x64`, затем
`cmake --build build --config Release`. Установка службы — от администратора:
`SnowyDeskService.exe --install`, удаление — `--uninstall`. Инъекция DLL не
используется, эксклюзивный полноэкранный режим не поддерживается — только
безрамочный. Автозапуск переключается между ключом Run и запуском из службы;
как откатить — см. раздел Autostart выше.
