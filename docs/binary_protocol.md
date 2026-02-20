# Logic Analyzer Binary Protocol

A compact, efficient binary protocol for controlling the ESP8266-based 65C02 logic analyzer over Serial/UART.

## Quick Start

### Upload Firmware

```bash
pio run -e logic-analyzer -t upload
```

### Test with Python Client

```bash
# Ping the device
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --ping

# Get status
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --status

# Set to manual mode and trigger a pulse
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --mode manual --pulse

# Stream bus states
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --stream
```

### Debugging

For troubleshooting protocol issues without interfering with binary communication, you can build and upload debug firmware that outputs diagnostic information via a separate Serial1 (GPIO2) interface:

```bash
# Build and upload debug firmware
pio run -e logic-analyzer-debug -t upload

# In one terminal: monitor debug output on Serial1
pio device monitor -p /dev/ttyUSB1 -b 115200

# In another terminal: run your client on main serial
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --stream
```

Debug output includes:
- Startup messages and initialization status
- Protocol errors (checksum failures, invalid commands)
- Major events (streaming start/stop, mode changes)
- Command processing (PING, GET_STATUS, etc.)

See [Debugging Guide](debugging.md) for complete setup instructions, wiring diagrams, and troubleshooting scenarios.

## Protocol Overview

**Communication:** Serial/UART at 115200 baud
**Byte Order:** Little-endian
**Error Detection:** CRC-8 checksum (polynomial 0x07)
**Framing:** Length-prefixed packets

## Packet Structure

All packets follow this format:

```
┌──────────┬──────────────┬────────┬─────────────┬──────────┐
│  START   │ MESSAGE_TYPE │ LENGTH │   PAYLOAD   │ CHECKSUM │
│  (0xAA)  │     (1B)     │  (1B)  │  (0-255B)   │   (1B)   │
└──────────┴──────────────┴────────┴─────────────┴──────────┘
```

- **START_BYTE**: `0xAA` - packet synchronization marker
- **MESSAGE_TYPE**: Command or response identifier (1 byte)
- **LENGTH**: Payload size in bytes (0-255)
- **PAYLOAD**: Message-specific data (optional, 0-255 bytes)
- **CHECKSUM**: CRC-8 (polynomial 0x07) of MESSAGE_TYPE + LENGTH + PAYLOAD bytes

**Minimum packet size:** 4 bytes (no payload)  
**Maximum packet size:** 259 bytes (255-byte payload)  
**Maximum payload size:** 255 bytes

### CRC-8 Checksum

The protocol uses **CRC-8-CCITT** (polynomial 0x07, init 0x00) for error detection. This provides:
- Detection of all single-bit errors
- Detection of all double-bit errors  
- Detection of burst errors up to 8 bits
- Detection of most multi-bit errors

**Checksum calculation:**
```
CRC = CRC8(MESSAGE_TYPE || LENGTH || PAYLOAD)
```

The checksum is calculated over the MESSAGE_TYPE byte, LENGTH byte, and all PAYLOAD bytes (if any).

## Message Types

### Commands (Host → ESP8266) [0x01-0x08]

| Type | Name | Description |
|------|------|-------------|
| `0x01` | SET_CLOCK_MODE | Switch between manual/automatic modes |
| `0x02` | SET_CLOCK_SPEED | Set clock speed index (0-11) |
| `0x03` | CLOCK_PULSE | Single clock pulse (manual mode) |
| `0x04` | START_STREAMING | Enable bus state streaming |
| `0x05` | STOP_STREAMING | Disable bus state streaming |
| `0x06` | GET_STATUS | Request current analyzer status |
| `0x07` | RESET_COUNTER | Reset clock counter to 0 |
| `0x08` | PING | Connection test |

### Responses/Events (ESP8266 → Host) [0x80-0x84]

| Type | Name | Description |
|------|------|-------------|
| `0x80` | ACK | Command acknowledged |
| `0x81` | ERROR | Command failed |
| `0x82` | STATUS_REPORT | Current analyzer status |
| `0x83` | BUS_STATE | Bus state change event |
| `0x84` | PONG | Response to PING |

## Detailed Command Reference

### SET_CLOCK_MODE (0x01)

Switch between manual and automatic clock modes.

**Command:**
```
AA 01 01 [MODE] [CS]
```
- `MODE`: `0x00` = Manual, `0x01` = Automatic

**Response:**
```
AA 80 02 01 [MODE] [CS]
```

**Example (Python):**
```python
client.set_clock_mode('automatic')
```

---

### SET_CLOCK_SPEED (0x02)

Set clock speed by index.

**Command:**
```
AA 02 01 [INDEX] [CS]
```
- `INDEX`: 0-11
  - 0 = Stopped
  - 1 = 1 Hz
  - 2 = 2 Hz
  - 3 = 4 Hz
  - 4 = 10 Hz
  - 5 = 20 Hz
  - 6 = 30 Hz
  - 7 = 40 Hz
  - 8 = 50 Hz
  - 9 = ~66 Hz
  - 10 = 80 Hz
  - 11 = 100 Hz

**Response:**
```
AA 80 02 02 [STATUS] [CS]
```
- `STATUS`: Validation status
  - `0x00` - Success (valid index, speed changed)
  - `0x01` - Error (invalid index, out of range)

**Example (Python):**
```python
success, valid = client.set_clock_speed(4)  # 10 Hz
if not valid:
    print("Error: Invalid clock speed index")
```

---

### CLOCK_PULSE (0x03)

Trigger a single clock pulse (manual mode only).

**Command:**
```
AA 03 02 [DUR_LSB] [DUR_MSB] [CS]
```
- `DURATION`: 16-bit pulse duration in milliseconds (little-endian)

**Response:**
```
AA 80 02 03 [STATUS] [CS]
```
- `STATUS`: Execution status
  - `0x00` - Success (pulse executed in manual mode)
  - `0x01` - Error (pulse ignored, device in automatic mode)

**Behavior:**
- In **manual mode**: Pulse is executed, returns `STATUS = 0x00` (success)
- In **automatic mode**: Pulse is ignored (clock running), returns `STATUS = 0x01` (error: wrong mode)

**Example (Python):**
```python
client.clock_pulse(100)  # 100ms pulse
```

---

### START_STREAMING (0x04)

Start streaming bus state changes.

**Command:**
```
AA 04 01 [FLAGS] [CS]
```
- `FLAGS`: Reserved for future filtering (currently 0x00)

**Response:**
```
AA 80 01 04 [CS]
```

After this command, the ESP8266 will send `BUS_STATE` events whenever the address, data, or flags change.

**Example (Python):**
```python
def on_bus_state(state):
    print(f"Addr: 0x{state.address_bus:04X}, Data: 0x{state.data_bus:02X}")

client.on_bus_state = on_bus_state
client.start_streaming()

while True:
    client.update()
```

---

### STOP_STREAMING (0x05)

Stop streaming bus state changes.

**Command:**
```
AA 05 00 [CS]
```

**Response:**
```
AA 80 01 05 [CS]
```

**Example (Python):**
```python
client.stop_streaming()
```

---

### GET_STATUS (0x06)

Get current analyzer status.

**Command:**
```
AA 06 00 [CS]
```

**Response (STATUS_REPORT - 0x82):**
```
AA 82 0B [MODE] [INDEX] [CNT0] [CNT1] [CNT2] [CNT3] [ADDR_LSB] [ADDR_MSB] [DATA] [FLAGS] [CS]
```
- `MODE`: 0=Manual, 1=Automatic
- `INDEX`: Current clock speed index
- `COUNTER`: 32-bit clock counter (little-endian)
- `ADDR`: 16-bit address bus (little-endian)
- `DATA`: 8-bit data bus
- `FLAGS`: 8-bit control flags

**Example (Python):**
```python
status = client.get_status()
print(f"Mode: {status.mode}, Counter: {status.clock_counter}")
print(f"Addr: 0x{status.address_bus:04X}, Data: 0x{status.data_bus:02X}")
```

---

### RESET_COUNTER (0x07)

Reset the clock counter to 0.

**Command:**
```
AA 07 00 [CS]
```

**Response:**
```
AA 80 01 07 [CS]
```

**Example (Python):**
```python
client.reset_counter()
```

---

### PING (0x08)

Test connection and get device info.

**Command:**
```
AA 08 00 [CS]
```

**Response (PONG - 0x84):**
```
AA 84 07 [VER_MAJ] [VER_MIN] [VER_PATCH] [UP0] [UP1] [UP2] [UP3] [CS]
```
- `VERSION`: 3 bytes for semantic versioning (currently 1.0.0)
- `UPTIME`: 32-bit uptime in milliseconds (little-endian)

**Example (Python):**
```python
pong = client.ping()
print(f"Version: {pong.version_major}.{pong.version_minor}.{pong.version_patch}")
print(f"Uptime: {pong.uptime_ms/1000:.1f}s")
```

---

### BUS_STATE Event (0x83)

Streamed automatically when streaming is enabled and bus state changes.

**Packet:**
```
AA 83 08 [CNT0] [CNT1] [CNT2] [CNT3] [ADDR_LSB] [ADDR_MSB] [DATA] [FLAGS] [CS]
```
- `COUNTER`: 32-bit clock counter (little-endian)
- `ADDR`: 16-bit address bus (little-endian)
- `DATA`: 8-bit data bus
- `FLAGS`: 8-bit control flags
  - Bit 0: RWB (1=Read, 0=Write)
  - Bit 1: SYNC (instruction fetch)
  - Bit 2: IRQB (interrupt request, active low)
  - Bit 3: RESETB (reset, active low)
  - Bit 7: CLOCK signal

**Example (Python):**
```python
def handle_bus_state(state):
    rw = 'R' if state.rwb else 'W'
    sync = 'S' if state.sync else '-'
    print(f"[{state.clock_counter:5d}] {rw}{sync} "
          f"0x{state.address_bus:04X} = 0x{state.data_bus:02X}")

client.on_bus_state = handle_bus_state
```

---

### ERROR Response (0x81)

Sent when a command fails.

**Packet:**
```
AA 81 02 [FAILED_CMD] [ERROR_CODE] [CS]
```
- `FAILED_CMD`: The command type that failed
- `ERROR_CODE`:
  - `0x01` - Invalid command
  - `0x02` - Invalid parameter
  - `0x03` - Wrong mode (e.g., pulse in auto mode)
  - `0x04` - Checksum error
  - `0x05` - Buffer overflow

**Example (Python):**
```python
def handle_error(error):
    print(f"Error: {error.error_code.name} for command 0x{error.failed_command:02X}")

client.on_error = handle_error
```

## Python Client Library

### Installation

```bash
pip install pyserial
```

### Basic Usage

```python
from logic_analyzer_client import LogicAnalyzerClient

# Connect
client = LogicAnalyzerClient('/dev/ttyUSB0', 115200)
client.connect()

# Test connection
pong = client.ping()
print(f"Connected! Version: {pong.version_major}.{pong.version_minor}.{pong.version_patch}")

# Get status
status = client.get_status()
print(status)

# Set to automatic mode at 10 Hz
client.set_clock_mode('automatic')
client.set_clock_speed(4)  # 10 Hz

# Stream data
def on_bus_state(state):
    if state.sync:  # Only print instruction fetches
        print(f"[{state.clock_counter:5d}] Fetch from 0x{state.address_bus:04X} = 0x{state.data_bus:02X}")

client.on_bus_state = on_bus_state
client.start_streaming()

try:
    while True:
        client.update()
except KeyboardInterrupt:
    client.stop_streaming()

client.disconnect()
```

### Command-Line Interface

The Python client includes a CLI:

```bash
# Test connection
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --ping

# Get current status
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --status

# Set clock mode
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --mode automatic

# Set clock speed
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --speed 4

# Trigger pulse (manual mode)
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --mode manual --pulse 100

# Reset counter
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --reset

# Stream bus states
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --stream
```

## Advanced Examples

### Capture First 100 Clock Cycles

```python
client = LogicAnalyzerClient('/dev/ttyUSB0')
client.connect()

# Reset and prepare
client.reset_counter()
client.set_clock_mode('automatic')
client.set_clock_speed(1)  # 1 Hz for slow capture

captures = []

def capture_state(state):
    captures.append(state)
    if len(captures) >= 100:
        client.stop_streaming()

client.on_bus_state = capture_state
client.start_streaming()

while len(captures) < 100:
    client.update()

print(f"Captured {len(captures)} states")
for state in captures[:10]:  # Print first 10
    print(state)
```

### Trigger on Specific Address

```python
def wait_for_address(target_addr):
    triggered = False

    def check_address(state):
        nonlocal triggered
        if state.address_bus == target_addr:
            print(f"Triggered at counter {state.clock_counter}")
            print(f"Address: 0x{state.address_bus:04X}, Data: 0x{state.data_bus:02X}")
            triggered = True

    client.on_bus_state = check_address
    client.start_streaming()

    while not triggered:
        client.update()

    client.stop_streaming()

# Wait for access to address 0xFFFC (reset vector)
wait_for_address(0xFFFC)
```

### Log Bus Activity to File

```python
import time

with open('bus_log.txt', 'w') as f:
    def log_state(state):
        timestamp = time.time()
        f.write(f"{timestamp:.6f},{state.clock_counter},{state.address_bus:04X},{state.data_bus:02X},{state.flags:02X}\n")
        f.flush()

    client.on_bus_state = log_state
    client.start_streaming()

    try:
        while True:
            client.update()
    except KeyboardInterrupt:
        client.stop_streaming()
```

## Backward Compatibility

The protocol parser checks for the start byte (`0xAA`). If a different byte is received, it's ignored by the protocol handler. This means the existing text-based commands can still work alongside the binary protocol for debugging purposes.

To use text commands, simply send characters as before:
- `m` - Toggle mode
- `p` - Pulse
- `+`/`-` - Speed control
- etc.

## Performance Considerations

### Serial Bandwidth

At 115200 baud, maximum throughput is ~11,520 bytes/second.

Each BUS_STATE packet is 13 bytes, so theoretical maximum is ~886 state updates/second.

In practice, expect:
- **Streaming mode**: ~500-800 updates/second depending on how often the bus state changes
- **Command/response**: < 5ms round-trip latency

### Tips for High-Speed Capture

1. **Disable text printing**: The `printBusState()` calls in the ESP8266 code add overhead. Comment them out for faster streaming.

2. **Increase baudrate** (if your USB-serial adapter supports it):
   ```cpp
   Serial.begin(230400);  // or 460800, 921600
   ```

3. **Use filtering**: When implemented, use the streaming flags to filter specific address ranges.

4. **Buffer on host**: Process incoming packets in batches rather than one at a time.

## Troubleshooting

### No response from device

1. Check serial port and baudrate
2. Try pinging: `python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --ping`
3. Check that device is powered and firmware is uploaded

### Checksum errors

1. Verify baudrate matches (115200)
2. Check cable quality
3. Try lowering baudrate if errors persist

### Missing packets during streaming

1. Check that you're calling `client.update()` frequently in a tight loop
2. Reduce clock speed to generate fewer updates
3. Consider disabling text printing in the ESP8266 code

### Device not responding to commands

1. Check that you're in the correct mode (e.g., pulse only works in manual mode)
2. Verify command parameters are within valid ranges
3. Check for error responses with `client.on_error`

## Future Enhancements

Potential additions to the protocol:

1. **Triggered capture**: Start streaming when address/data matches a pattern
2. **Buffered capture**: Store N samples in ESP8266 RAM, retrieve on demand
3. **Filtering**: Only stream specific address ranges or conditions
4. **Compression**: RLE encoding for repeated values
5. **Timestamps**: Add microsecond timestamps to events
6. **Multi-channel**: Separate channels for bus state vs. decoded instructions

## Protocol Version History

### v1.0.0 (Current)
- Initial release
- Basic command/response structure
- Real-time bus state streaming
- Clock control (manual/automatic modes)
- Status reporting

## License

Same as the main project.

## Contributing

Feel free to extend the protocol with new commands. Reserve command types 0x09-0x7F for future use. Response types 0x85-0xFF are also available.
