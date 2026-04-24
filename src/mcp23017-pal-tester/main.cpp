#include <Adafruit_MCP23X17.h>
#include <Wire.h>
#include <Arduino.h>

#ifndef BUILTIN_LED_PIN
#error "BUILTIN_LED_PIN is not defined for this board. Please define it in platformio.ini or in the code."
#endif

#ifndef BUILTIN_LED_PIN_MODE
#define BUILTIN_LED_PIN_MODE OUTPUT
#endif

// ─── Pin role definitions ───────────────────────────────────────────

enum PinRole : uint8_t {
  ROLE_IGNORE = 0,  // not connected, left as Hi-Z input
  ROLE_OUTPUT = 1,  // MCP output → drives PAL input
  ROLE_INPUT  = 2,  // MCP input  ← reads PAL output
  ROLE_CLK    = 3,  // MCP output → drives PAL clock pin (explicit toggle per combination)
};

struct PinConfig {
  PinRole role;
  const char *name;  // friendly name (e.g. "A15", "ROM /CS"), NULL for unnamed
};

// 32 pins total: 0–15 = MCP0, 16–31 = MCP1
// Configure this array for your chip under test.
// Pins are numbered sequentially: MCP0.GPA0=0 ... MCP0.GPB7=15, MCP1.GPA0=16 ... MCP1.GPB7=31

// ─── ATF22V10C default configuration ────────────────────────────────
// 24-pin DIP: pin 12=GND, pin 24=VCC → 22 I/O pins to connect
// PAL pin 1 = CLK (dedicated clock, not enumerated)
// PAL pins 2–11, 13 = inputs (11 total)
// PAL pins 14–23 = outputs (10 total)
// Uses MCP pins 0–21, leaves 22–31 ignored.
// CLK is mapped to MCP pin 0 — toggled explicitly per input combination.

static PinConfig pinRolesATF22V10C[32] = {
  // MCP0 GPA0–GPA7 (indices 0–7)
  { ROLE_CLK,    "CLK"  },  // 0  → PAL pin 1
  { ROLE_OUTPUT, "I2"   },  // 1  → PAL pin 2
  { ROLE_OUTPUT, "I3"   },  // 2  → PAL pin 3
  { ROLE_OUTPUT, "I4"   },  // 3  → PAL pin 4
  { ROLE_OUTPUT, "I5"   },  // 4  → PAL pin 5
  { ROLE_OUTPUT, "I6"   },  // 5  → PAL pin 6
  { ROLE_OUTPUT, "I7"   },  // 6  → PAL pin 7
  { ROLE_OUTPUT, "I8"   },  // 7  → PAL pin 8

  // MCP0 GPB0–GPB7 (indices 8–15)
  { ROLE_OUTPUT, "I9"   },  // 8  → PAL pin 9
  { ROLE_OUTPUT, "I10"  },  // 9  → PAL pin 10
  { ROLE_OUTPUT, "I11"  },  // 10 → PAL pin 11
  { ROLE_OUTPUT, "I13"  },  // 11 → PAL pin 13
  { ROLE_INPUT,  "IO14" },  // 12 → PAL pin 14
  { ROLE_INPUT,  "IO15" },  // 13 → PAL pin 15
  { ROLE_INPUT,  "IO16" },  // 14 → PAL pin 16
  { ROLE_INPUT,  "IO17" },  // 15 → PAL pin 17

  // MCP1 GPA0–GPA7 (indices 16–23)
  { ROLE_INPUT,  "IO18" },  // 16 → PAL pin 18
  { ROLE_INPUT,  "IO19" },  // 17 → PAL pin 19
  { ROLE_INPUT,  "IO20" },  // 18 → PAL pin 20
  { ROLE_INPUT,  "IO21" },  // 19 → PAL pin 21
  { ROLE_INPUT,  "IO22" },  // 20 → PAL pin 22
  { ROLE_INPUT,  "IO23" },  // 21 → PAL pin 23
  { ROLE_IGNORE, NULL   },  // 22
  { ROLE_IGNORE, NULL   },  // 23

  // MCP1 GPB0–GPB7 (indices 24–31)
  { ROLE_IGNORE, NULL   },  // 24
  { ROLE_IGNORE, NULL   },  // 25
  { ROLE_IGNORE, NULL   },  // 26
  { ROLE_IGNORE, NULL   },  // 27
  { ROLE_IGNORE, NULL   },  // 28
  { ROLE_IGNORE, NULL   },  // 29
  { ROLE_IGNORE, NULL   },  // 30
  { ROLE_IGNORE, NULL   },  // 31
};

// Address decoder for Ben Eater's 6502 breadboard computer

static PinConfig pinRolesBE6502[32] = {
  // MCP0 GPA0–GPA7 (indices 0–7)
  { ROLE_CLK,    "CLK"        },  // 0  → PAL pin 1
  { ROLE_OUTPUT, "A15"        },  // 1  → PAL pin 2
  { ROLE_OUTPUT, "A14"        },  // 2  → PAL pin 3
  { ROLE_OUTPUT, "A13"        },  // 3  → PAL pin 4
  { ROLE_OUTPUT, "A12"        },  // 4  → PAL pin 5
  { ROLE_IGNORE, NULL         },  // 5  → PAL pin 6
  { ROLE_IGNORE, NULL         },  // 6  → PAL pin 7
  { ROLE_IGNORE, NULL         },  // 7  → PAL pin 8

  // MCP0 GPB0–GPB7 (indices 8–15)
  { ROLE_IGNORE, NULL         },  // 8  → PAL pin 9
  { ROLE_IGNORE, NULL         },  // 9  → PAL pin 10
  { ROLE_IGNORE, NULL         },  // 10 → PAL pin 11
  { ROLE_IGNORE, NULL         },  // 11 → PAL pin 13
  { ROLE_IGNORE, NULL         },  // 12 → PAL pin 14
  { ROLE_IGNORE, NULL         },  // 13 → PAL pin 15
  { ROLE_IGNORE, NULL         },  // 14 → PAL pin 16
  { ROLE_IGNORE, NULL         },  // 15 → PAL pin 17

  // MCP1 GPA0–GPA7 (indices 16–23)
  { ROLE_IGNORE, NULL         },  // 16 → PAL pin 18
  { ROLE_IGNORE, NULL         },  // 17 → PAL pin 19
  { ROLE_INPUT,  "SERIAL /CS" },  // 18 → PAL pin 20
  { ROLE_INPUT,  "VIA /CS"    },  // 19 → PAL pin 21
  { ROLE_INPUT,  "RAM /CS"    },  // 20 → PAL pin 22
  { ROLE_INPUT,  "ROM /CS"    },  // 21 → PAL pin 23
  { ROLE_IGNORE, NULL         },  // 22
  { ROLE_IGNORE, NULL         },  // 23

  // MCP1 GPB0–GPB7 (indices 24–31)
  { ROLE_IGNORE, NULL         },  // 24
  { ROLE_IGNORE, NULL         },  // 25
  { ROLE_IGNORE, NULL         },  // 26
  { ROLE_IGNORE, NULL         },  // 27
  { ROLE_IGNORE, NULL         },  // 28
  { ROLE_IGNORE, NULL         },  // 29
  { ROLE_IGNORE, NULL         },  // 30
  { ROLE_IGNORE, NULL         },  // 31
};

// ─── Globals ────────────────────────────────────────────────────────

Adafruit_MCP23X17 mcp0;
Adafruit_MCP23X17 mcp1;

// Maps: which bit position in the input/output packed value corresponds to each MCP pin
// outputMap[i] = { mcpIndex (0 or 1), pin (0–15) } for the i-th output bit
// inputMap[i]  = { mcpIndex (0 or 1), pin (0–15) } for the i-th input bit

struct PinMapping {
  uint8_t mcpIndex;   // 0 or 1
  uint8_t mcpPin;     // 0–15
  const char *name;   // friendly name from PinConfig, NULL if unnamed
};

PinMapping outputMap[32];
PinMapping inputMap[32];
PinMapping clkPin;        // clock pin mapping (valid when hasClk == true)
uint8_t numOutputs = 0;
uint8_t numInputs  = 0;
bool hasClk = false;

bool mcpSetupOk = false;
bool testDone = false;

// ─── Setup helpers ──────────────────────────────────────────────────

void buildPinMaps(PinConfig *pins) {
  numOutputs = 0;
  numInputs  = 0;
  hasClk = false;

  for (uint8_t i = 0; i < 32; i++) {
    uint8_t mcpIdx = i / 16;
    uint8_t mcpPin = i % 16;
    const char *name = pins[i].name;

    if (pins[i].role == ROLE_OUTPUT) {
      outputMap[numOutputs++] = { mcpIdx, mcpPin, name };
    } else if (pins[i].role == ROLE_INPUT) {
      inputMap[numInputs++] = { mcpIdx, mcpPin, name };
    } else if (pins[i].role == ROLE_CLK) {
      clkPin = { mcpIdx, mcpPin, name };
      hasClk = true;
    }
  }
}

void configureMcpPins(PinConfig *pins) {
  // Set pin directions on both MCPs
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t mcpPin = i % 16;
    Adafruit_MCP23X17 &mcp = (i < 16) ? mcp0 : mcp1;

    if (pins[i].role == ROLE_OUTPUT || pins[i].role == ROLE_CLK) {
      mcp.pinMode(mcpPin, OUTPUT);
    } else {
      // INPUT and IGNORE both set as inputs
      mcp.pinMode(mcpPin, INPUT);
    }
  }
}

// ─── Test logic ─────────────────────────────────────────────────────

void printPinName(const PinMapping &pin) {
  if (pin.name)
    Serial.print(pin.name);
  else
    Serial.printf("MCP%d.%d", pin.mcpIndex, pin.mcpPin);
}

void writeClk(bool high) {
  Adafruit_MCP23X17 &mcp = (clkPin.mcpIndex == 0) ? mcp0 : mcp1;
  mcp.digitalWrite(clkPin.mcpPin, high ? HIGH : LOW);
}

void writeOutputs(uint32_t value) {
  // Scatter bits from 'value' to the output pins across both MCPs
  uint16_t mcp0Val = 0;
  uint16_t mcp1Val = 0;

  for (uint8_t i = 0; i < numOutputs; i++) {
    if (value & (1UL << i)) {
      if (outputMap[i].mcpIndex == 0)
        mcp0Val |= (1 << outputMap[i].mcpPin);
      else
        mcp1Val |= (1 << outputMap[i].mcpPin);
    }
  }

  // Write only output bits, preserving input pin state (inputs are Hi-Z anyway)
  mcp0.writeGPIOAB(mcp0Val);
  mcp1.writeGPIOAB(mcp1Val);
}

uint32_t readInputs() {
  // Gather input pin values into a packed value
  uint16_t mcp0Val = mcp0.readGPIOAB();
  uint16_t mcp1Val = mcp1.readGPIOAB();

  uint32_t result = 0;
  for (uint8_t i = 0; i < numInputs; i++) {
    uint16_t mcpVal = (inputMap[i].mcpIndex == 0) ? mcp0Val : mcp1Val;
    if (mcpVal & (1 << inputMap[i].mcpPin)) {
      result |= (1UL << i);
    }
  }
  return result;
}

void printHeader() {
  Serial.println();
  Serial.println("# PAL Truth Table");
  Serial.printf("# PAL inputs (active drive):  %d\r\n", numOutputs);
  Serial.printf("# PAL outputs (active read):  %d\r\n", numInputs);
  Serial.printf("# Clock pin: %s\r\n", hasClk
    ? (String(clkPin.name ? clkPin.name : "CLK") + " (dual read: before + after rising edge)").c_str()
    : "none (combinational only)");
  Serial.printf("# Total combinations: %lu\r\n", (unsigned long)(1UL << numOutputs));
  Serial.println("#");

  // Print drive pin mapping (MCP outputs → PAL inputs)
  Serial.print("# PAL input bits (LSB first):  ");
  for (uint8_t i = 0; i < numOutputs; i++) {
    if (i > 0) Serial.print(", ");
    printPinName(outputMap[i]);
  }
  Serial.println();

  // Print read pin mapping (MCP inputs ← PAL outputs)
  Serial.print("# PAL output bits (LSB first): ");
  for (uint8_t i = 0; i < numInputs; i++) {
    if (i > 0) Serial.print(", ");
    printPinName(inputMap[i]);
  }
  Serial.println();

  Serial.println("#");
  if (hasClk) {
    Serial.println("# INPUT: OUTPUT [CLK unchanged]");
    Serial.println("# INPUT: BEFORE_CLK: AFTER_CLK [CLK changed output]");
  } else {
    Serial.println("# INPUT: OUTPUT");
  }
}

void runTest() {
  uint32_t totalCombinations = 1UL << numOutputs;

  printHeader();

  // Ensure CLK starts low
  if (hasClk) writeClk(false);

  uint32_t registeredMask = 0;  // bits that ever changed on clock edge

  unsigned long startTime = millis();

  for (uint32_t i = 0; i < totalCombinations; i++) {
    writeOutputs(i);

    if (hasClk) {
      // CLK is already low (pre-condition from previous iteration or init)
      delayMicroseconds(10);  // combinational settling with CLK low
      uint32_t beforeClk = readInputs();

      writeClk(true);         // rising edge — registered outputs latch here
      delayMicroseconds(10);  // clock-to-output propagation
      uint32_t afterClk = readInputs();

      writeClk(false);        // restore CLK low for next iteration

      uint32_t changedBits = beforeClk ^ afterClk;
      registeredMask |= changedBits;

      if (changedBits) {
        Serial.printf("0x%04lX: 0x%04lX: 0x%04lX\r\n",
          (unsigned long)i, (unsigned long)beforeClk, (unsigned long)afterClk);
      } else {
        Serial.printf("0x%04lX: 0x%04lX\r\n",
          (unsigned long)i, (unsigned long)beforeClk);
      }
    } else {
      delayMicroseconds(10);  // combinational settling time
      uint32_t outputs = readInputs();
      Serial.printf("0x%04lX: 0x%04lX\r\n", (unsigned long)i, (unsigned long)outputs);
    }
  }

  unsigned long elapsed = millis() - startTime;

  Serial.println("#");
  Serial.printf("# Done. %lu rows in %lu ms\r\n", (unsigned long)totalCombinations, (unsigned long)elapsed);

  if (hasClk) {
    Serial.println("#");
    if (registeredMask == 0) {
      Serial.println("# All outputs are combinational (no change on clock edge)");
    } else {
      Serial.print("# Registered output bits: ");
      bool first = true;
      for (uint8_t i = 0; i < numInputs; i++) {
        if (registeredMask & (1UL << i)) {
          if (!first) Serial.print(", ");
          printPinName(inputMap[i]);
          first = false;
        }
      }
      Serial.println();

      Serial.print("# Combinational output bits: ");
      first = true;
      for (uint8_t i = 0; i < numInputs; i++) {
        if (!(registeredMask & (1UL << i))) {
          if (!first) Serial.print(", ");
          printPinName(inputMap[i]);
          first = false;
        }
      }
      Serial.println();
    }
  }

  // Reset all outputs to 0
  writeOutputs(0);

  testDone = true;
}

// ─── Arduino entry points ───────────────────────────────────────────

void setup() {
  Serial.begin(115200);

#ifdef NATIVE_USB
  while (!Serial) {
    delay(50);
  }
#endif

  Serial.println();
  Serial.println("PAL Tester");

  pinMode(BUILTIN_LED_PIN, BUILTIN_LED_PIN_MODE);
  digitalWrite(BUILTIN_LED_PIN, HIGH);

  Wire.begin(HW_I2C_PIN_SDA, HW_I2C_PIN_SCL);
  Wire.setClock(400000);

  // MCP23017 setup
  if (!mcp0.begin_I2C(0x20, &Wire)) {
    Serial.println("Error initializing MCP23017 0x20.");
    return;
  }
  Serial.println("MCP23017 0x20 OK");

  if (!mcp1.begin_I2C(0x21, &Wire)) {
    Serial.println("Error initializing MCP23017 0x21.");
    return;
  }
  Serial.println("MCP23017 0x21 OK");

  mcpSetupOk = true;

  // PinConfig *currentPinRoles = pinRolesATF22V10C;
  PinConfig *currentPinRoles = pinRolesBE6502;

  // Build pin maps and configure MCP directions
  buildPinMaps(currentPinRoles);

  if (numOutputs == 0) {
    Serial.println("Error: no output pins configured.");
    mcpSetupOk = false;
    return;
  }

  if (numOutputs > 20) {
    Serial.printf("Error: %d output pins configured. Max 20 (2^20 = 1M combinations).\r\n", numOutputs);
    mcpSetupOk = false;
    return;
  }

  Serial.printf("Configured: %d PAL inputs, %d PAL outputs\r\n", numOutputs, numInputs);

  configureMcpPins(currentPinRoles);

  Serial.println("Send 'r' to run test, 'p' to print config.");
}

void loop() {
  if (!mcpSetupOk) {
    // Error blink
    digitalWrite(BUILTIN_LED_PIN, LOW);
    delay(50);
    digitalWrite(BUILTIN_LED_PIN, HIGH);
    delay(500);
    return;
  }

  if (testDone) {
    // Slow blink to indicate done
    digitalWrite(BUILTIN_LED_PIN, LOW);
    delay(500);
    digitalWrite(BUILTIN_LED_PIN, HIGH);
    delay(500);
  }

  // Handle serial commands
  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'r' || c == 'R') {
      testDone = false;
      runTest();
    } else if (c == 'p' || c == 'P' || c == '\r' || c == '\n') {
      printHeader();
    }
  }
}
