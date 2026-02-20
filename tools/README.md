# Binary Protocol for Logic Analyzer

This directory contains the implementation of a compact binary protocol for controlling the ESP8266 logic analyzer.

## Files

### Firmware (ESP8266)
- **src/logic-analyzer/protocol.h** - Protocol definitions and structures
- **src/logic-analyzer/protocol.cpp** - Protocol implementation (parser, command handlers)
- **src/logic-analyzer/main.cpp** - Integration with logic analyzer (updated)

### Client Libraries
- **tools/logic_analyzer_client.py** - Full-featured Python client library with CLI
- **tools/example_client.py** - Simple example demonstrating basic usage

### Documentation
- **docs/binary_protocol.md** - Complete protocol specification and usage guide
- **docs/debugging.md** - Debug serial output guide and troubleshooting

## Quick Start

### 1. Build and Upload Firmware

```bash
~/.platformio/penv/bin/pio run -e logic-analyzer -t upload
```

### 2. Test the Protocol

```bash
# Install Python dependencies
pip install -r tools/requirements.txt

# Or install individually:
# pip install pyserial crc

# Ping the device
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --ping

# Get status
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --status

# Stream bus states
python3 tools/logic_analyzer_client.py /dev/ttyUSB0 --stream
```

### 3. Run Example Client

```bash
python3 tools/example_client.py
```

## Protocol Features

- **Compact binary format** - Efficient data transmission
- **Bidirectional** - Commands from host, responses + events from device
- **Real-time streaming** - Bus state changes sent automatically
- **Clock control** - Manual stepping and automatic modes with adjustable speed
- **Status monitoring** - Query current state at any time
- **Error handling** - Checksum validation and error reporting
- **Backward compatible** - Text commands still work alongside binary protocol

## Key Commands

| Command | Description |
|---------|-------------|
| SET_CLOCK_MODE | Switch between manual/automatic |
| SET_CLOCK_SPEED | Set clock frequency (0-11) |
| CLOCK_PULSE | Single step (manual mode) |
| START_STREAMING | Enable bus state events |
| STOP_STREAMING | Disable bus state events |
| GET_STATUS | Query current state |
| RESET_COUNTER | Reset clock counter to 0 |
| PING | Test connection, get version |

## Python Client API

```python
from logic_analyzer_client import LogicAnalyzerClient

# Connect
client = LogicAnalyzerClient('/dev/ttyUSB0', 115200)
client.connect()

# Control clock
client.set_clock_mode('automatic')
client.set_clock_speed(4)  # 10 Hz

# Stream data
def on_bus_state(state):
    print(f"Addr: 0x{state.address_bus:04X}, Data: 0x{state.data_bus:02X}")

client.on_bus_state = on_bus_state
client.start_streaming()

while True:
    client.update()
```

## Documentation

See **docs/binary_protocol.md** for complete protocol specification, message formats, and advanced examples.

For troubleshooting and debugging, see **docs/debugging.md** for instructions on using the separate Serial1 debug output.

## Build Status

✅ **Compiles successfully**
- RAM usage: 41.3% (33,856 bytes)
- Flash usage: 27.1% (283,115 bytes)

## Performance

- **Baudrate**: 115200 bps (configurable)
- **Packet size**: 4-13 bytes typical
- **Streaming rate**: ~500-800 updates/second
- **Latency**: < 5ms command/response round-trip

## Next Steps

Potential enhancements:
- Triggered capture (start on condition)
- Buffered capture (store in RAM, download later)
- Address range filtering
- Compression for repeated values
- Timestamp support
