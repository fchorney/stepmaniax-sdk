# AGENTS.md: StepManiaX SDK

## Project Overview
The StepManiaX SDK is a hardware SDK for dance pad controllers. It provides two implementations:
- **Full SDK** (`sdk/Windows/`): Rich Windows-only C++ DLL with configuration, animations, and diagnostics
- **SDK Lite** (`sdk-lite/`): Minimal cross-platform C++ library with device enumeration and input reading
- **Config Tool** (`smx-config/`): C# WPF application for pad calibration and configuration (depends on full SDK DLL)

## Architecture: Two SDKs, One API

### API Layer Pattern
Both SDKs share the **same public C API** (`sdk/SMX.h`). This enables:
- C# P/Invoke bindings to work with either implementation
- Games/apps to target a single interface
- `sdk-lite` as a minimal alternative for non-Windows platforms

**Key insight:** Changes to `sdk/SMX.h` must be backward-compatible and reflected in both implementations. The full SDK wraps its internal `SMXManager`/`SMXDevice` classes to expose the public API.

### Device Architecture: Manager → Device → Connection

**Windows Full SDK** (`sdk/Windows/`):
```
SMXManager (singleton)
  ├─ SMXDevice[2] (per-pad abstraction)
  │   ├─ SMXDeviceConnection (HID I/O)
  │   └─ SMXConfigPacket (config protocol)
  └─ SMXDeviceSearch (enumeration)
```

**SDK Lite** (`sdk-lite/`):
```
SMXManager
  ├─ USB Polling Thread (every ~1ms, configurable)
  │   └─ PollUSBData() → parses Report 3 inline, buffers Report 6
  ├─ Main I/O Thread (every ~50ms, configurable)
  │   └─ Update() → processes Report 6, sends commands, manages connections
  ├─ SMXDevice[2]
  └─ SMXDeviceConnection (cross-platform via hidapi)
```

This three-layer design separates concerns:
- **Manager**: Lifecycle, threading, device enumeration
- **Device**: Per-pad state, configuration, command sequencing
- **Connection**: Raw HID I/O, packet buffering

## Threading Model: Async Background I/O

**Critical pattern:** All I/O is nonblocking and asynchronous:
- `SMX_Start()` spawns **two** background threads:
  - **USB Polling Thread**: Reads HID data every ~1ms (configurable via `SMX_SetPollingRate`). Parses Report 3 (input state) inline and updates an atomic. Buffers Report 6 packets for the main thread.
  - **Main I/O Thread**: Processes Report 6 (commands/config), manages connections, sends commands. Sleeps ~50ms (configurable) or wakes when Report 6 data arrives.
- Callbacks (`SMXUpdateCallback`) fire from these background threads (NOT the application thread)
- **User responsibility:** Callbacks must return quickly and not call `SMX_Stop()` or block on I/O
- Setters (`SMX_SetConfig`, `SMX_SetLights`) return immediately; work happens in background
- **Main thread sleep caveat:** The device connection handshake requires multiple main thread iterations (send device info request → process response → send config request → process response). High sleep values (e.g., 200ms+) can delay connection establishment and cause race conditions where the device appears uninitialized. Keep the main thread sleep at 50ms or below for reliable behavior.

**Callback reasons** are bitmask flags (always includes `SMXUpdateCallback_Updated`):
- `SMXUpdateCallback_Updated` — always set
- `SMXUpdateCallback_InputState` — panel press/release changed
- `SMXUpdateCallback_Connected` — device fully connected
- `SMXUpdateCallback_Disconnected` — device disconnected
- `SMXUpdateCallback_ConfigUpdated` — config received or updated

Use `SMX_REASON_IS(reason, flag)` macro to check flags.

## Protocol Details: Command-Response over HID

Pads communicate via **USB HID** with simple text commands:
- `SMX_GetInputState()` reads a bitmask (9 panels = bits 0-8)
- Device info retrieved with `"I"` command → returns firmware version, serial
- Configuration sent as binary structures (see `SMXConfig` at offset calculations in `SMX.h`)
- **Packed struct use:** All device-facing config uses `#pragma pack(1)` to ensure exact byte alignment with firmware

**Important:** The `SMXConfig` struct is **250 bytes exactly** (padded for ABI stability). See `static_assert` in `SMX.h` line 278.

## Language Bindings & Marshalling

### C# Integration (SMXConfig)
The C# wrapper (`smx-config/SMX.cs`) uses P/Invoke to call the native DLL:
- Struct layout matches C++ exactly via `[StructLayout(LayoutKind.Sequential, Pack=1)]`
- `[MarshalAs(UnmanagedType.I1)]` required for bool fields (C# bug workaround)
- Serial numbers are char arrays marshalled as fixed-size byte buffers
- Platform detection: The config tool only references the full SDK; SDK Lite is not bound to C#

## Building & Deployment

### Full SDK (Windows only)
```bash
# Open SMX.sln in Visual Studio (v15+)
# Projects:
#   - sdk/Windows/SMX.vcxproj → outputs SMX.dll
#   - sample/SMXSample.vcxproj → sample app
#   - smx-config/SMXConfig.csproj → config UI (depends on SMX.dll)
```

### SDK Lite (Cross-platform)
```bash
cd sdk-lite
mkdir build && cd build
cmake ..     # auto-detects platform & hidapi
make
./smx-sample
```

For shared library builds:
```bash
cmake .. -DBUILD_SHARED_LIBS=ON
make
# produces: libsmx-lite.so / libsmx-lite.dylib / smx-lite.dll
```

**Key detail:** SDK Lite uses CMake and `hidapi-hidraw` (Linux) or `hidapi` (macOS/Windows). Build system probes multiple pkg-config variants for compatibility. On macOS, builds universal (x86_64 + arm64) by default.

## Configuration Versioning

`SMXConfig::configVersion` (byte at offset 1) tracks firmware compatibility:
- `0xFF`: Pre-configVersion era (firmware v1)
- `0x00`: configVersion added
- `0x02`: Per-panel individual thresholds
- `0x03`: Debounce delay added
- `0x05`: Current version

**Pattern:** New fields are added at the **end** of the struct, with `configVersion` incremented. Old firmware ignores unrecognized fields. This is why the struct pads to a fixed 250 bytes.

## Input State Bitmask

`SMX_GetInputState()` returns a 16-bit value where bit 0-8 map to panels:
```
Layout: 0 1 2
        3 4 5
        6 7 8
```
(Bit 9-15 unused; always check `SMXInfo::m_bConnected` before reading.)

## Sensor Configuration: FSR vs Load Cell

Hardware can use either **Force Sensitive Resistors (FSR)** or **load cells**:
- Flag: `SMXConfigFlags::PlatformFlags_FSR` in `SMXConfig::flags`
- FSR: 4 thresholds per panel (array `fsrLowThreshold[4]`, `fsrHighThreshold[4]`)
- Load cell: 1 low/high threshold pair per panel
- **Mixed thresholds:** `combinedLowThreshold` / `combinedHighThreshold` apply when both sensor types are used

This affects calibration logic in the config tool (see `SetCustomSensors.xaml.cs`).

## Testing & Diagnostics

- `SMX_SetTestMode()`: Requests raw sensor telemetry (uncalibrated values, noise, tare calibration)
- `SMX_GetTestData()`: Returns per-panel sensor readings with DIP switch state and bad-sensor detection
- `SMX_SetPanelTestMode()`: Panel-side diagnostics (pressure test, LED diagnostics); disables normal input
- `SMX_ForceRecalibration()`: Immediate fast recalibration (normally pads auto-calibrate)

The config tool uses these extensively in diagnostics UI.

## Common Modifications

### Adding a New Config Field
1. **Add field to `SMXConfig`** in `sdk/SMX.h` (before padding)
2. **Update `configVersion`** strategy: if field's default (0xFF) is ambiguous, increment `configVersion`
3. **Mirror in SDK Lite** `sdk-lite/SMXConfigPacket.h` if protocol differs for old firmware
4. **Update C# binding** `smx-config/SMX.cs` with matching marshalling
5. **Test with both SDKs** using `sample.cpp` and `SMXSample.cpp`

### Adding a New Command
1. **Implement in `SMXDevice`** (Windows) and `SMX.cpp` (Lite)
2. **Expose via `sdk/SMX.h` public API** if multi-platform, else Windows-only
3. **Handle in `SMXDeviceConnection`** packet parsing if it returns data (else just send)
4. **Update samples** to demonstrate usage

### Porting to New Platform
- Start with **SDK Lite**; it's designed for portability
- Replace `SMXDeviceConnection` HID layer while keeping device/manager unchanged
- hidapi already abstracts most platform differences; check USB device enumeration for your OS
- A subset of features may be unsupported depending on platform (e.g., animations are Windows-only)

## Key Files by Role

| File | Purpose |
|------|---------|
| `sdk/SMX.h` | Public C API (source of truth) |
| `sdk/Windows/SMXManager.h` | Threading & device lifecycle |
| `sdk/Windows/SMXDevice.h` | Per-pad state machine |
| `sdk-lite/SMX.cpp` | Consolidated lite implementation |
| `sdk-lite/SMXDeviceConnection.cpp` | Cross-platform HID via hidapi |
| `smx-config/SMX.cs` | C# P/Invoke layer |
| `smx-config/MainWindow.xaml.cs` | UI orchestration |

## Debugging Tips

- **Logs:** Call `SMX_SetLogCallback()` early to capture all startup messages
- **Connection issues:** Check `SMXInfo::m_bConnected` status on each callback
- **Config sync:** Read config back with `SMX_GetConfig()` after `SMX_SetConfig()` to verify
- **Platform detection:** Look at `hidapi.h` to understand available HID functions for your platform
- **Struct packing:** Use `static_assert(sizeof(SMXConfig) == 250, ...)` patterns when modifying structs

## Development Strategies for SDK Lite

### C++ Standard & Compilation

**Target:** C++14 (GNU++14 with GCC, Clang)
- Set by CMakeLists.txt: `set(CMAKE_CXX_STANDARD 14)`
- Avoid C++17+ features (lambdas with capture, auto params, fold expressions, etc.)
- Use `std::function`, `std::shared_ptr`, `std::make_shared` freely—they're C++11 baseline
- String handling: Use `std::string` and `std::move()` for efficiency

**Compatibility rationale:** C++14 ensures compatibility with older embedded systems and legacy build environments while still offering modern conveniences.

### Cross-Platform Code Patterns

**Platform detection in CMake:**
- `if(APPLE)` for macOS-specific paths (Homebrew prefixes)
- `if(UNIX AND NOT APPLE)` for Linux
- `if(WIN32)` for Windows
- hidapi variant probing is automatic; don't hardcode library names

**Minimal platform-specific code:**
- Prefer standard library solutions over OS-specific APIs
- Encapsulate platform differences in `SMXDeviceConnection` (HID layer only)
- Keep manager/device logic platform-agnostic

### Guidelines for AI Assistants

**When modifying SDK Lite code:**

1. **Keep changes minimal and focused.** Changes should affect the narrowest scope possible. Avoid refactoring unrelated code.

2. **Do NOT generate comprehensive documentation or explanations** unless explicitly asked. Just implement the change concisely.

3. **Preserve the consolidation pattern.** `sdk-lite/SMX.cpp` intentionally merges multiple logical components (helpers, device, manager, API) into one file for easier distribution. Don't split it arbitrarily.

4. **Thread-safety is implicit.** Background I/O thread context is already handled by `SMXManager`. Don't add extra locking unless specifically needed. The USB polling thread and main I/O thread share a recursive mutex; input state uses lock-free atomics.

5. **Test both SDKs when touching protocol.** Changes to HID packet handling or device enumeration must work on Windows Full SDK too. Binary compatibility is critical.

6. **Keep the public API (`sdk/SMX.h`) pristine.** Never modify it without ensuring backward compatibility. Changes here impact both C# bindings and game integrations.

7. **When debugging builds fail:** Check hidapi detection first (it varies by OS and package manager). The CMakeLists.txt already handles this; ensure dependencies are installed before reporting build issues.

8. **For cross-platform testing:** Prioritize Linux testing (most common embedded target), then macOS (for development), then Windows (full features already supported by main SDK).

9. **Report 3 Input State data should be the absolute top priority.** The main point of the SDK Lite is to absolutely prioritize the input state data path. The USB polling thread handles Report 3 with zero allocations on the hot path — keep it that way.
