# Debug Serial Output Guide

This guide explains how to use the separate Serial1 debug output for troubleshooting the 65C02 Logic Analyzer without interfering with the binary protocol communication.

## Table of Contents

1. [Overview](#overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Building Debug Firmware](#building-debug-firmware)
4. [Hardware Setup](#hardware-setup)
5. [Debug Output Reference](#debug-output-reference)
6. [Common Debugging Scenarios](#common-debugging-scenarios)
7. [Troubleshooting](#troubleshooting)
8. [Limitations](#limitations)

## Overview

The logic analyzer uses two separate serial interfaces:

- **Serial (UART0)** - GPIO1 (TX), GPIO3 (RX) - Used for binary protocol communication
- **Serial1 (UART1)** - GPIO2 (TX only) - Used for debug output

This separation allows you to monitor debug output while the binary protocol operates normally, without any cross-interference.

### Debug Philosophy

Debug output is **minimal by design** - only errors and major events are logged:

- ✅ **Logged**: Errors, protocol initialization, major state changes (streaming start/stop, mode changes), PING/GET_STATUS
- ❌ **NOT logged**: Individual packets, routine ACKs, parser state transitions, normal bus state updates

This keeps debug output readable and focused on troubleshooting actual issues.

## Hardware Requirements

### Required Components

1. **ESP8266 module** (e.g., NodeMCU, Wemos D1 Mini)
2. **USB-to-serial adapter** (FTDI, CP2102, CH340, etc.) for debug output
3. **USB cable** for main serial connection
4. **Jumper wires** for connections

### GPIO Pin Information

| Function | GPIO | NodeMCU Pin | Notes |
|----------|------|-------------|-------|
| Serial TX (Protocol) | GPIO1 | TX | Main binary protocol output |
| Serial RX (Protocol) | GPIO3 | RX | Main binary protocol input |
| Serial1 TX (Debug) | GPIO2 | D4 | Debug output only (TX-only) |
| Built-in LED | GPIO2 | D4 | **Conflict**: LED won't work in debug builds |

**Important**: GPIO2 is shared between Serial1 TX and the built-in LED. In debug builds, the LED will not function because Serial1 takes priority.

## Building Debug Firmware

### Production Build (No Debug)

Build and upload the production firmware without debug output:

```bash
# Build only
pio run -e logic-analyzer

# Build and upload
pio run -e logic-analyzer -t upload

# Build, upload, and monitor main serial
pio run -e logic-analyzer -t upload -t monitor
```

### Debug Build (With Serial1 Debug)

Build and upload the debug firmware with Serial1 output enabled:

```bash
# Build only
pio run -e logic-analyzer-debug

# Build and upload
pio run -e logic-analyzer-debug -t upload

# Build and upload
pio run -e logic-analyzer-debug -t upload
```

**Note**: Do NOT use `pio run -t monitor` with debug builds. You need separate terminal windows for each serial port (see Hardware Setup below).

### Build Flag

The debug build is enabled by the `DEBUG_PROTOCOL=1` build flag in `platformio.ini`:

```ini
[env:logic-analyzer-debug]
build_flags = -DDEBUG_PROTOCOL=1
build_src_filter =
	${env.build_src_filter}
	+<logic-analyzer/**>
```

### Enabling Verbose Mode (Optional)

For even more detailed debug output, you can enable verbose/trace mode. This adds `[TRACE]` messages for low-level operations like packet transmission.

**Option 1: Add to source file** (temporary, for development):
```cpp
// At the top of protocol.cpp or main.cpp, before #include "debug.h"
#define DEBUG_VERBOSE_ENABLED
```

**Option 2: Add build flag** (persistent, for testing):
```ini
[env:logic-analyzer-debug-verbose]
build_flags =
    -DDEBUG_PROTOCOL=1
    -DDEBUG_VERBOSE_ENABLED=1
build_src_filter =
	${env.build_src_filter}
	+<logic-analyzer/**>
```

Then build with:
```bash
pio run -e logic-analyzer-debug-verbose -t upload
```

**Note**: Verbose mode significantly increases debug output and may impact performance during high-speed streaming.

## Hardware Setup

### Wiring Diagram

Connect your USB-to-serial adapter to monitor debug output:

```
ESP8266 (NodeMCU)              USB-to-Serial Adapter
┌─────────────────┐            ┌─────────────────┐
│                 │            │                 │
│      GPIO2 (D4) ├───────────►│ RX              │
│   (Serial1 TX)  │            │                 │
│                 │            │                 │
│             GND ├────────────┤ GND             │
│                 │            │                 │
└─────────────────┘            └─────────────────┘
         │                              │
         │                              │
         ▼                              ▼
    USB Cable                      USB Cable
    (Protocol)                     (Debug Output)
```

### Connection Steps

1. **Connect main serial** (for protocol):
   - Connect ESP8266 USB port to computer
   - This provides Serial (UART0) for binary protocol communication

2. **Connect debug serial** (for debug output):
   - Connect USB-to-serial adapter's **RX pin** to ESP8266's **GPIO2 (D4)**
   - Connect USB-to-serial adapter's **GND pin** to ESP8266's **GND**
   - Connect USB-to-serial adapter to computer via USB
   - **Note**: Only connect RX and GND - TX is not needed

3. **Identify serial ports**:
   ```bash
   # List available serial ports
   ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null

   # On macOS
   ls /dev/tty.usb* /dev/cu.usb* 2>/dev/null
   ```

   You should see two ports:
   - `/dev/ttyUSB0` - Main ESP8266 (protocol)
   - `/dev/ttyUSB1` - USB-to-serial adapter (debug)

### Monitoring Both Serial Ports

#### Option 1: Two Terminal Windows

**Terminal 1** - Monitor debug output (Serial1):
```bash
# Linux/macOS with screen
screen /dev/ttyUSB1 115200

# Or with minicom
minicom -D /dev/ttyUSB1 -b 115200

# Or with PlatformIO monitor
pio device monitor -p /dev/ttyUSB1 -b 115200
```

**Terminal 2** - Run Python client (communicates via Serial):
```bash
cd tools
python3 logic_analyzer_client.py /dev/ttyUSB0
```

#### Option 2: Using tmux or screen (Split Screen)

```bash
# Start tmux
tmux

# Split window horizontally (Ctrl+b, then ")
# Top pane: monitor debug output
pio device monitor -p /dev/ttyUSB1 -b 115200

# Bottom pane: run Python client
# (Switch with Ctrl+b, then arrow keys)
cd tools
python3 logic_analyzer_client.py /dev/ttyUSB0
```

#### Option 3: Separate Tools

You can also use GUI serial terminal programs:
- **PuTTY** (Windows/Linux)
- **CoolTerm** (macOS/Windows)
- **Arduino Serial Monitor** (set to 115200 baud)

## Debug Output Reference

### Startup Messages

When the ESP8266 boots with debug firmware:

```
=== 65C02 Logic Analyzer Starting ===
Build: Jan 20 2026 14:32:45
[DEBUG] MCP23017 0x20 OK
[DEBUG] MCP23017 0x21 OK
[DEBUG] Binary protocol initialized
[DEBUG] Clock mode: AUTOMATIC, speed index: 1
```

### Protocol Command Events

Major protocol commands are logged:

```
[DEBUG] SET_CLOCK_MODE: MANUAL
[DEBUG] SET_CLOCK_SPEED: index=5
[DEBUG] Streaming STARTED
[DEBUG] GET_STATUS request
[DEBUG] RESET_COUNTER
[DEBUG] PING received
[DEBUG] Streaming STOPPED
```

### Error Messages

All protocol errors are logged with details:

```
[ERROR] Checksum mismatch: expected=0x42 got=0x43
[ERROR] Invalid command: 0xFF
[ERROR] SET_CLOCK_MODE: Invalid mode: 3
[ERROR] SET_CLOCK_SPEED: Invalid clock speed index: 99
[ERROR] START_STREAMING: Invalid payload length: 2
[ERROR] MCP23017 0x20 init failed
```

### Debug Output Format

All debug messages follow this format:

```
[DEBUG] <event description>
[ERROR] <error description with details>
```

- Messages include `\r\n` line endings
- Hex values are formatted as `0x%02X`
- No timestamps (use your terminal's timestamp feature if needed)

## Common Debugging Scenarios

### Scenario 1: Connection Issues

**Problem**: Python client can't connect to ESP8266

**Debug steps**:
1. Check debug output for startup banner
2. If no startup banner appears:
   - Check power supply
   - Verify debug serial wiring (GPIO2 → Adapter RX, GND → GND)
   - Verify baud rate is 115200
3. If startup banner appears but init fails:
   ```
   [ERROR] MCP23017 0x20 init failed
   ```
   - Check I2C wiring
   - Verify MCP23017 addresses (0x20, 0x21)
   - Check I2C pull-up resistors

### Scenario 2: Checksum Errors

**Problem**: Commands fail with checksum errors

**Debug output**:
```
[ERROR] Checksum mismatch: expected=0x42 got=0x43
```

**Causes**:
- Noisy serial connection (try shorter USB cable)
- Wrong baud rate on client side
- Serial port buffer issues
- Hardware failure

**Solutions**:
1. Verify client baud rate is 115200
2. Try different USB cable/port
3. Add delay between commands in client
4. Check for USB hub issues

### Scenario 3: Invalid Commands

**Problem**: Commands rejected by ESP8266

**Debug output**:
```
[ERROR] Invalid command: 0xFF
[ERROR] SET_CLOCK_MODE: Invalid mode: 3
```

**Causes**:
- Client sending wrong command bytes
- Protocol version mismatch
- Corrupted packet data

**Solutions**:
1. Verify client library version matches firmware
2. Check for proper packet framing (0xAA start byte)
3. Use PING command to verify basic communication
4. Review protocol specification

### Scenario 4: Streaming Not Working

**Problem**: No bus state updates received

**Debug steps**:
1. Check if streaming was started:
   ```
   [DEBUG] Streaming STARTED
   ```
2. If not started, send START_STREAMING command
3. If started but no updates:
   - Bus state may not be changing (check 65C02 clock)
   - Check automatic clock mode is enabled
   - Verify MCP23017 connections to bus

**Expected sequence**:
```
[DEBUG] SET_CLOCK_MODE: AUTOMATIC
[DEBUG] Streaming STARTED
(Bus state updates sent silently - not logged)
```

### Scenario 5: Clock Control Not Working

**Problem**: Clock pulses don't affect 65C02

**Debug output**:
```
[DEBUG] SET_CLOCK_MODE: MANUAL
[DEBUG] SET_CLOCK_SPEED: index=5
```

**Debug steps**:
1. Verify clock mode changes appear in debug output
2. Check LED pin (GPIO14) connection to clock circuit
3. Use oscilloscope to verify GPIO14 output
4. Check manual vs automatic mode setting

## Troubleshooting

### No Debug Output at All

**Possible causes**:
1. Wrong serial port selected
2. Wrong baud rate (must be 115200)
3. Debug firmware not uploaded (check build environment)
4. Wiring issue (GPIO2 → Adapter RX)
5. USB-to-serial adapter not working

**Verification steps**:
```bash
# 1. Verify correct build environment
pio run -e logic-analyzer-debug -t upload

# 2. List serial ports
ls /dev/ttyUSB* /dev/ttyACM*

# 3. Test USB-to-serial adapter (loop TX to RX and type)
screen /dev/ttyUSB1 115200

# 4. Check GPIO2 voltage with multimeter (should toggle during startup)
```

### Garbled Debug Output

**Possible causes**:
- Wrong baud rate
- Incorrect voltage levels (3.3V logic)
- Poor connection/loose wire
- Serial adapter configuration

**Solutions**:
1. Verify 115200 baud on both firmware and terminal
2. Check for 3.3V logic level on adapter (5V might work but not ideal)
3. Re-seat connections
4. Try different terminal program

### Debug Output Stops After Startup

**Normal behavior**: Debug output is minimal by design. After startup, you'll only see:
- Major events (mode changes, streaming start/stop)
- Errors (checksum failures, invalid commands)
- PING and GET_STATUS events

**Not logged** (by design):
- Individual ACK responses
- Bus state updates during streaming
- Normal packet processing

### Built-in LED Not Working

**Expected behavior**: The built-in LED (GPIO2) does not work in debug builds because Serial1 uses GPIO2 for TX.

**Solutions**:
- Use external LED on GPIO14 for visual feedback (already used for clock)
- Switch to production build if LED is needed
- Monitor debug output instead of relying on LED

## Limitations

### Hardware Limitations

1. **Serial1 is TX-only**:
   - GPIO2 can only transmit (no receiving)
   - Debug output is one-way only

2. **GPIO2 shared with built-in LED**:
   - Built-in LED won't work in debug builds
   - This is acceptable for debugging purposes

3. **Limited to 115200 baud**:
   - Both Serial and Serial1 run at 115200 baud
   - Higher rates may cause instability on ESP8266

### Software Limitations

1. **Minimal logging**:
   - Not every operation is logged (by design)
   - Focus is on errors and major events
   - Individual packets are NOT logged

2. **No log levels**:
   - Only two levels: `[DEBUG]` and `[ERROR]`
   - `DEBUG_VERBOSE` macro exists for `[TRACE]` output (enable with `-DDEBUG_VERBOSE_ENABLED=1` build flag)

3. **No timestamps**:
   - Debug messages don't include timestamps
   - Use terminal program with timestamp feature if needed

### Performance Impact

Debug builds have minimal performance impact:
- Debug output only occurs on major events/errors
- No debug output during normal streaming
- Binary protocol performance is unchanged

## Related Documentation

- [Binary Protocol Specification](binary_protocol.md) - Complete protocol reference
- [Quick Start Guide](../tools/README.md) - Getting started with Python client
- [Hardware Design](../README.md) - Circuit diagrams and connections

## Support

If you encounter issues not covered in this guide:

1. Check debug output for error messages
2. Verify hardware connections match diagrams
3. Test with PING command to verify basic communication
4. Review protocol specification for command details
5. Check serial port permissions (Linux: add user to `dialout` group)

Example permission fix (Linux):
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in for changes to take effect
```
