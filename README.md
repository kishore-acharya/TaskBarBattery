# TrayRing

Battery rings for your wireless keyboard, mouse and headphones, in the Windows notification area.

![TrayRing in the Windows taskbar: keyboard asleep (dimmed), mouse at 26 %, headset at 20 % (yellow)](docs/tray.png)

Each device gets its own tray icon: a device glyph inside a ring of 10 sections that fills with the battery level.
It turns yellow at 20 % and red at 10 %. TrayRing is one small native C++ executable. It talks to the devices
directly, so none of the vendor apps (Razer Synapse, the EvoFox configurator, Sony | Headphones Connect) need to run.

## Supported devices

| Device | Connection | How the level is read | Charging shown |
|---|---|---|---|
| EvoFox Ronin X75 Plus keyboard | 2.4 GHz receiver `3151:4011` | ROYUAN receiver status query `0xF7` (HID feature report) | inferred from a rising level |
| Razer DeathAdder V2 X HyperSpeed mouse | HyperSpeed dongle `1532:009C` | Razer 90-byte battery query (HID feature report) | no (AA/AAA battery) |
| Sony WH-CH720N headphones | Bluetooth | battery Windows receives over the Hands-Free profile | no (not reported) |

These are the devices I own and tested with. Others that use the same protocols, such as ROYUAN-based keyboards on a
`3151:4011` receiver or other Bluetooth headsets that report their battery to Windows, should work after a one-line
addition (see [Adding a device](#adding-a-device)).

## Features

- One icon per device. The tooltip shows the device name and status. Right-click for **Refresh now**,
  **Start with Windows** and **Exit**; left-click refreshes.
- Polls every 30 s, and re-reads 5 s after a device links up, because the first value after link-up can be stale.
- Refreshes immediately when a receiver is plugged in or removed, when a Bluetooth device connects or disconnects,
  and after resume from sleep.
- Shows when a device is asleep, disconnected or missing, instead of a stale number.
- Starts with Windows through a per-user `HKCU\...\CurrentVersion\Run` entry. No admin rights, services or drivers.
- About 2 MB of private memory, idle between polls.
- Read-only: it sends battery queries and nothing else, never changes device settings, and never reads your
  keystrokes or mouse input.

## Install

Requires Windows 10 or 11 (x64).

1. Download `TrayRing-<version>-win-x64.zip` from [Releases](../../releases) and extract it anywhere.
2. Run `TrayRing.exe`. The first launch turns on **Start with Windows**.
3. Windows hides new tray icons in the overflow menu. Drag them onto the taskbar, or turn them on under
   *Settings > Personalization > Taskbar > Other system tray icons*.

The executables are unsigned, so SmartScreen may warn on first run. If you'd rather not run a binary from the
internet, [build it yourself](#build); it takes about a minute.

To uninstall, untick **Start with Windows**, choose **Exit**, then delete the folder, the `HKCU\Software\TrayRing`
registry key and `%LOCALAPPDATA%\TrayRing`.

## Tray icon

- **Ring:** a dim track split into 10 sections, with a coloured arc that fills clockwise from 12 o'clock. The arc is
  green above 20 %, yellow from 11 to 20 % and red at 10 % or below.
- **Charging:** the arc turns green and pulses slowly (a 3 s cycle at 10 frames per second). The animation runs only
  while a device is charging.
- **Asleep or disconnected:** the icon is dimmed and the arc shows the last reading. Without a receiver, or on an
  error, only the dimmed track and glyph are shown, and the tooltip explains why.
- **Theme:** the glyph and track are white on a dark taskbar and black on a light one, and follow theme changes
  without a restart.
- **Rendering:** icons are drawn in code at the tray's DPI size (`SM_CXSMICON`) with 4×4 supersampling. Glyphs are
  filled silhouettes with cut-outs, because outlines blur to grey at 16–20 px. There are no image assets.

Run `cmake --build build --target icon-preview`, then `build\icon-preview.exe sheet.png [light]` to render every
glyph and state at 16, 20, 24 and 32 px into a PNG contact sheet.

The look is inspired by [HaloBattery](https://github.com/HeyOkay/HaloBattery) (MIT): segmented ring, dim track, green
charging pulse and taskbar-theme detection. No code was copied.

## Build

You need CMake 3.21 or later and one of the toolchains below. The output is:

- `TrayRing.exe`: the tray app (no console window)
- `ronin-battery-cli.exe` and `razer-battery-cli.exe`: console tools that print the raw exchange with a device
- `protocol_tests.exe`: unit tests

### llvm-mingw (clang, no install or admin rights needed)

The release binaries are built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20261104 (UCRT), CMake 4.5
and Ninja 1.13. Put their `bin` folders on `PATH`, then:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-rc
cmake --build build
ctest --test-dir build
```

The executables are fully static and depend only on Windows system DLLs (UCRT is part of Windows 10 and later).

### Visual Studio 2022 or later (MSVC)

You need the "Desktop development with C++" workload and a Windows 10/11 SDK. From a *Developer PowerShell*:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

The CRT is linked statically (`/MT`), so no VC++ redistributable is needed.

> The code uses only standard C++20 and Windows SDK APIs and is written to compile cleanly with MSVC
> (`/W4 /permissive-`), but so far only the llvm-mingw build has been run. Reports either way are welcome.

## Debug logging

Start `TrayRing.exe --debug`, or set the DWORD `HKCU\Software\TrayRing\DebugLog` to `1` (this also covers
autostart). The log goes to `%LOCALAPPDATA%\TrayRing\trayring.log` and records state changes and device errors.

The console tools show the raw exchange with a device:

```powershell
ronin-battery-cli.exe              # list the receiver's HID collections, query once, print raw and decoded reply
ronin-battery-cli.exe --list       # list collections and capabilities only; sends nothing
ronin-battery-cli.exe --watch 10   # query every 10 s
ronin-battery-cli.exe --log cli.log
```

`razer-battery-cli.exe` takes the same options for the Razer mouse. Both tools use these exit codes: `0` battery
read, `2` device offline, `3` receiver not found, `4` communication error or invalid reply.

## Adding a device

Each device implements `BatteryDevice` (`src/battery_device.h`): `name()`, `kind()` (which picks the glyph),
`read()`, `reset()`, `ownsInterface()` and optionally `reportsCharging()`. Register it in `src/main.cpp`:

```cpp
devices.push_back(std::make_unique<trayring::RoninKeyboard>());
devices.push_back(std::make_unique<trayring::RazerDeathAdderV2X>());
devices.push_back(std::make_unique<trayring::BluetoothHeadset>(L"Sony WH-CH720N", L"vid&0002054c_pid&0eaf"));
```

Each device gets its own tray icon (`uID` = index + 1), its own charge tracker and a status line in the context menu.
All devices are polled on one worker thread. Keyboard, mouse and headset glyphs exist in `src/icon_renderer.cpp`.

- **Another Bluetooth headset:** find its `VID&…_PID&…` in Device Manager (the Bluetooth device's *Hardware Ids*)
  and add a `BluetoothHeadset` line. This works for headsets that report their battery to Windows over the
  Hands-Free profile, which is most headsets that show a level under *Settings > Bluetooth & devices*.
- **A device that can't charge**, such as the AA-powered Razer mouse, returns `true` from `reportsCharging()` and
  never sets `charging`, so a battery swap isn't shown as charging.

## How each device is read

### EvoFox Ronin X75 Plus (2.4 GHz)

#### Source

The protocol comes from the reverse-engineered ROYUAN keyboard HID protocol documented by
**sharkfin** (JR Lanteigne, GPL-3.0):

- [docs/PROTOCOL.md](https://github.com/dniminenn/sharkfin/blob/master/docs/PROTOCOL.md): sections
  *Transport → 2.4 GHz receiver*, *Checksums* and *Battery*. The version read was commit `8c999ba`.
- [app/src-tauri/src/protocol/receiver.rs](https://github.com/dniminenn/sharkfin/blob/master/app/src-tauri/src/protocol/receiver.rs)

Upstream marks the receiver status opcode `0xF7` as hardware-verified on a `3151:4011` receiver (an Attack Shark
X86), the same receiver PID as the Ronin's. This project re-implements the documented facts and copies no upstream
code.

#### Wire format

| | |
|---|---|
| Interface | `MI_02`, usage page `0xFFFF`, usage `0x0002`; the only collection with feature reports |
| Report | Feature report, ID 0, 64-byte payload (`FeatureReportByteLength` = 65) |
| Request | `HidD_SetFeature`: `00 │ F7 00 00 00 00 00 00 08 00…` (byte 7 = `0xFF − sum(bytes 0..6)`) |
| Reply | `HidD_GetFeature` about 15 ms later (upstream: ready after about 10 ms) |

Status reply (payload bytes, after the report ID):

| byte | meaning | status here |
|---|---|---|
| 0 | `1` = a relayed keyboard reply is waiting | upstream [HW]; seen as `1` once |
| 1 | keyboard battery, percent | upstream [HW]; observed 100 → 97, see below |
| 2 | mouse battery | upstream [JS], unused |
| 3 | `0` = keyboard linked | **confirmed**: toggles as the keyboard sleeps and wakes |
| 4 | `0` = mouse linked | upstream [JS], unused |
| 5 | `1` = receiver ready to relay | **confirmed**: briefly `0` during link-up |
| 6 | paired device kind `1` keyboard, `2` mouse, `3` both | `1` here; used to recognise a status reply |
| 7, 8 | upstream: RF-boot flags | reads `01 C8` here, meaning unknown, **ignored** |

#### Interfaces on this receiver

| Collection | Usage page:usage | In / Out / Feature bytes | Role |
|---|---|---|---|
| MI_00 | 0001:0006 | 9 / 2 / 0 | boot keyboard |
| MI_01 COL01 | 000C:0001 | 3 / 0 / 0 | consumer control |
| MI_01 COL02 | 0001:0080 | 2 / 0 / 0 | system control |
| MI_01 COL03 | 0001:0006 | 16 / 0 / 0 | NKRO keyboard |
| MI_01 COL04 | 0001:0002 | 8 / 0 / 0 | mouse |
| MI_01 COL05 | FFFF:0001 | 4 / 0 / 0 | vendor input-only; nothing arrived during 10 minutes of monitoring |
| **MI_02** | **FFFF:0002** | **0 / 0 / 65** | **vendor feature channel, status / battery** |

#### Observed behaviour (polling `0xF7` once a second)

```
14:17:41  00 05 00 00 01 01 01 c8   keyboard linked, byte1 = 5
14:19:34  00 05 00 01 01 01 01 c8   keyboard asleep (byte3 = 1); receiver keeps the last value
14:20:01  00 05 00 00 01 00 01 c8   link-up, not yet ready to relay (byte5 = 0)
14:20:03  00 64 00 00 01 01 01 c8   byte1 = 100
14:24:57  00 63 …                   99
14:25:04  00 62 …                   98
14:29:52  00 61 …                   97
```

- Byte 3 tracks the link state reliably, and the receiver keeps byte 1 while the keyboard is offline.
- The value right after link-up can be stale (here 5, then 100), which is why TrayRing re-reads 5 s after link-up.

#### Charging is inferred

The `0xF7` reply has no charging field: neither sharkfin nor HaloBattery's MonsGeek provider, which uses the same
command, reports one, and a 20-minute capture showed only the level and the online flag changing. TrayRing infers
charging (`ChargeTracker`):

- A settled reading at least 2 points above the reference starts charging. A reading counts as settled only if the
  previous poll was also online, which skips stale values right after link-up.
- Charging stays on for 10 minutes after the last rise.
- A drop, or a jump of more than 10 points (a receiver correction), resets the reference.

So the pulse starts a few minutes after plugging in and stops about 10 minutes after the level stops rising. If you
find a byte that reports charging (`ronin-battery-cli.exe --watch 10` while charging), set `charging` in `read()` and
return `true` from `reportsCharging()`.

#### Safety

- The only command sent is the receiver's own read-only status query `0xF7`. Nothing is relayed to the keyboard and
  no configuration opcodes are sent.
- All handles are opened shared (`FILE_SHARE_READ | FILE_SHARE_WRITE`). Keyboard and mouse collections are opened
  with zero access, which allows capability and feature-report calls but never reads input.
- The official configurator uses the same channel. If both run at once, a read can return the configurator's relayed
  reply instead of a status reply. Byte 6 identifies it and the read is retried, so it never shows up as a level.

### Razer DeathAdder V2 X HyperSpeed (HyperSpeed dongle)

#### Source

[OpenRazer](https://github.com/openrazer/openrazer) (GPL-2.0). Only protocol facts were used; no code was copied.

- [driver/razercommon.h](https://github.com/openrazer/openrazer/blob/master/driver/razercommon.h): the 90-byte report
  layout, the status codes and the CRC.
- [driver/razerchromacommon.c](https://github.com/openrazer/openrazer/blob/master/driver/razerchromacommon.c):
  `razer_chroma_misc_get_battery_level()` is class `0x07`, id `0x80`, data size `0x02`.
- [driver/razermouse_driver.c](https://github.com/openrazer/openrazer/blob/master/driver/razermouse_driver.c): for
  `USB_DEVICE_ID_RAZER_DEATHADDER_V2_X_HYPERSPEED` (`0x009C`), the battery query uses transaction ID `0x1F`, the level
  is `arguments[1]` scaled 0–255, and `is_charging` is always 0 ("Use AA batteries").

#### Wire format

The request and the reply are both 90-byte feature reports with report ID 0, so the HID buffers are 91 bytes:

| Byte | Field | Battery request | Reply observed |
|------|-------|-----------------|----------------|
| 0 | status: `00` new, `01` busy, `02` ok, `03` fail, `04` timeout, `05` unsupported | `00` | `02` |
| 1 | transaction ID | `1F` | `1F` |
| 2–4 | remaining packets, protocol type | `00` | `00` |
| 5 | data size | `02` | `02` |
| 6–7 | command class, command ID | `07 80` | `07 80` |
| 8–9 | arguments; `[9]` is the level 0–255 | `00 00` | `00 47` |
| 88 | CRC: XOR of bytes 2–87 | `85` | `C2` |

#### Verified behaviour

- The only collection with a 91-byte feature report is `MI_00` (usage page `0x0001`, usage `0x0002`). It's the mouse
  collection, so Windows refuses read/write access (error 5). A zero-access handle works, because the feature IOCTLs
  are `FILE_ANY_ACCESS`.
- The reply was level `0x47` = 71/255 = 27.8 %, and Razer Synapse showed **27 %**. Synapse truncates, so TrayRing
  does too.
- Synapse shares the dongle's single reply buffer and overwrites our reply with its own within about 20 ms (class
  `0F`/id `03`, status `05`). So TrayRing issues GET straight after SET: the first GET returns busy (`01`), the next
  returns the level. A reply to any other command or transaction is discarded and the query re-sent. In 40
  consecutive reads with Synapse running, all 40 returned 27 % and one needed a retry.
- **Not yet verified:** the reply when the mouse is off or asleep. Following OpenRazer's status codes, a timeout
  (`04`) or a reply that stays busy is shown as *asleep or disconnected*.
- Only the battery query is sent. It changes no settings and doesn't affect the mouse's input.

### Sony WH-CH720N (Bluetooth)

The headset has no HID interface. Windows' Bluetooth stack receives its battery level over the Hands-Free profile
(the HFP battery indicator) and stores it as a device property; it's the value shown under *Settings > Bluetooth &
devices*. TrayRing reads that property through CfgMgr32 and sends nothing to the headset
(`src/bluetooth_headset.cpp`).

| Node (instance ID under `BTHENUM`) | Property | Meaning |
|---|---|---|
| `{0000111E-…}_VID&0002054C_PID&0EAF\…` (Hands-Free AG) | `{104EA319-6EE2-4701-BD47-8DDBF425BBE5} 2`, `BYTE` | battery % |
| same | `DEVPKEY_Device_ContainerId` | links it to the device node |
| `DEV_<address>\…` with the same container | `{83DA6326-97A6-4088-9453-A1923F573B29} 15`, `BOOLEAN` | connected |

- The battery property read **20** while the headset reported 20 %.
- Windows keeps the last level of paired devices that are switched off (1 % and 60 % on two such devices here), while
  their connected flag reads `FALSE`. So the level counts only while the headset is connected; otherwise the icon
  shows *disconnected*.
- HFP reports no charging state, and the headset disconnects while charging, so no charging pulse is shown.
- **Not yet verified:** how quickly the flag and level update on disconnect and reconnect. TrayRing refreshes when
  the headset's audio interfaces arrive or leave, and polls every 30 s regardless.

Bluetooth LE devices (GATT Battery Service) need a different reader and aren't supported yet.

## Layout

```
CMakeLists.txt
src/
  battery_device.*     BatteryDevice interface, readings, status text (no Windows dependencies)
  charge_tracker.*     charging inferred from a rising level (no Windows dependencies)
  icon_renderer.*      ring + glyph rasteriser, level colours, pulse curve (no Windows dependencies)
  ronin_protocol.*     ROYUAN 0xF7 request/reply encoding and validation (no Windows dependencies)
  razer_protocol.*     Razer 90-byte report: battery request, CRC, reply classification (no Windows dependencies)
  bluetooth_battery.*  Hands-Free battery/connection state -> reading (no Windows dependencies)
  hid_device.*         CfgMgr32 enumeration, HID caps, shared handle, Get/SetFeature
  ronin_keyboard.*     RoninKeyboard device: receiver discovery, status exchange, retries
  razer_mouse.*        RazerDeathAdderV2X device: dongle discovery, polled exchange, retries
  bluetooth_headset.*  BluetoothHeadset device: CfgMgr32 Bluetooth property lookup
  logger.*             optional file log
  tray_icon.*          rendered pixels -> HICON, taskbar theme lookup
  autostart.*          HKCU Run entry and settings
  tray_app.*           hidden window, one Shell_NotifyIcon v4 icon per device, pulse timer, menu, worker thread
  main.cpp             single instance, logging, device list, startup
tools/ronin_cli.cpp    console tool (keyboard)
tools/razer_cli.cpp    console tool (Razer mouse)
tools/icon_preview.cpp icon contact sheet (dev tool, not built by default)
tests/protocol_tests.cpp  protocols, headset state mapping, colour thresholds, pulse range, charge inference
resources/             manifest (PerMonitorV2, asInvoker), version info, app icon
```

## Limitations

- The keyboard is read only through its 2.4 GHz receiver. Upstream reports that Bluetooth mode doesn't expose the
  vendor collection, and that a wired keyboard has no battery command.
- The receiver ID `3151:4011` is shared by other ROYUAN-based keyboards, which are reported the same way.
- The Razer mouse is read only through its HyperSpeed dongle. In Bluetooth mode it shows as disconnected.
- The headset level has whatever resolution the headset sends over HFP, and Windows updates it only while connected.

## Credits

- [sharkfin](https://github.com/dniminenn/sharkfin) by JR Lanteigne: the ROYUAN receiver protocol.
- [OpenRazer](https://github.com/openrazer/openrazer): the Razer report format and battery command.
- [HaloBattery](https://github.com/HeyOkay/HaloBattery): the ring design.

Not affiliated with EvoFox/Amkette, Razer or Sony. Product names are trademarks of their owners.

## License

[MIT](LICENSE)
