# PAL Chip Tester

A programmable logic (PAL/GAL) chip tester using an ESP32 and two MCP23017 I²C GPIO expanders. Treats the chip as a black box — enumerates all input combinations and captures the complete truth table.

## Overview

The tester drives all possible input combinations on a PAL chip and reads back its outputs, producing a full truth table. Two MCP23017 expanders provide 32 I/O pins (16 each), which is enough to cover common PAL/GAL packages up to 24 pins (minus VCC/GND).

No knowledge of the chip's internal fuse map or equations is needed — only the role of each pin (input, output, or unused).

**Target chips:** ATF22V10C, ATF16V8, GAL16V8, GAL22V10, and similar 20/24-pin DIP PAL/GAL devices.

## Hardware

```
ESP32 ──I²C──┬── MCP23017 (0x20) ── 16 I/O pins ──┐
             │                                      ├── PAL chip under test
             └── MCP23017 (0x21) ── 16 I/O pins ──┘
```

- **ESP32** (or ESP8266) — controller, communicates with MCP23017s over I²C
- **MCP23017 #0** (address 0x20) — 16 GPIO pins (GPA0–7, GPB0–7)
- **MCP23017 #1** (address 0x21) — 16 GPIO pins (GPA0–7, GPB0–7)
- **32 I/O pins total** — mapped to the PAL chip's pins

### Example: ATF22V10C (24-pin DIP)

```
              ATF22V10C
MCP0
             ┌────╥────┐
A0   CLK/I ──┤ 1    24 ├── VCC
A1       I ──┤ 2    23 ├── I/O   A6
A2       I ──┤ 3    22 ├── I/O   A4
A3       I ──┤ 4    21 ├── I/O   A3
A4       I ──┤ 5    20 ├── I/O   A2  MCP1
A5       I ──┤ 6    19 ├── I/O   A1
A6       I ──┤ 7    18 ├── I/O   A0
A7       I ──┤ 8    17 ├── I/O   B7
B0       I ──┤ 9    16 ├── I/O   B6
B1       I ──┤ 10   15 ├── I/O   B5  MCP0
B2       I ──┤ 11   14 ├── I/O   B4
       GND ──┤ 12   13 ├── I     B3
             └─────────┘
```

22 pins to connect (24 minus VCC and GND), well within the 32 available.

## Pin Configuration

Each of the 32 MCP I/O pins is assigned a role:

| Role       | MCP Direction | Description                          |
|------------|---------------|--------------------------------------|
| **output** | OUTPUT        | Drives an input pin on the PAL chip  |
| **input**  | INPUT         | Reads an output pin from the PAL chip|
| **ignore** | INPUT (Hi-Z)  | Not connected, left floating         |

The configuration maps each MCP pin to a PAL chip pin and its role. This is chip-specific — different PAL types have different input/output pin assignments.

## How Testing Works

The tester treats the PAL chip as a black box. The only configuration needed is the role of each pin.

### Brute-Force Truth Table

The tester iterates through all 2^N combinations of the N input pins and records the output pins at each step:

1. **Configure** — set MCP pin directions based on the pin role map
2. **Enumerate** — for `i` in `0` to `2^N - 1`:
   - Write `i` across all OUTPUT pins
   - Wait for combinational logic to settle
   - Read all INPUT pins
   - Output the row: `inputs → outputs`
3. **Result** — complete truth table of the chip

### Clock Handling

The CLK pin (e.g., pin 1 on ATF22V10C) is assigned as the **least significant bit** of the input counter. As the counter increments, the CLK pin naturally toggles every step — low on even values, high on odd values. This emulates a clock signal without any special handling.

This is sufficient for simple combinational logic and chips without complex internal state machines. For chips with registered outputs, each pair of consecutive rows (CLK=0 → CLK=1) forms one clock cycle.

### Example Output

With 12 inputs (CLK + 11 data) and 10 outputs, the tester produces 2^12 = 4096 rows:

```
# inputs → outputs
0x0000 → 0x03FF
0x0001 → 0x0000
0x0002 → 0x03FF
0x0003 → 0x0001
...
```

## Status

**Current state:** functional. Configures MCP23017 pin directions from a pin role array, enumerates all 2^N input combinations, and outputs the complete truth table over serial. Default configuration targets the ATF22V10C. Send `r` over serial to run, `p` to print pin config.
