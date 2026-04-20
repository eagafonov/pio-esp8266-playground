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
};

// 32 pins total: 0–15 = MCP0, 16–31 = MCP1
// Configure this array for your chip under test.
// Pins are numbered sequentially: MCP0.GPA0=0 ... MCP0.GPB7=15, MCP1.GPA0=16 ... MCP1.GPB7=31

// ─── ATF22V10C default configuration ────────────────────────────────
// 24-pin DIP: pin 12=GND, pin 24=VCC → 22 I/O pins to connect
// PAL pins 1 (CLK), 2–11, 13 = inputs (12 total)
// PAL pins 14–23 = outputs (10 total)
// Uses MCP pins 0–21, leaves 22–31 ignored.
// CLK is mapped to MCP pin 0 (lowest bit of the output counter).

static PinRole pinRolesATF22V10C[32] = {
  // MCP0 GPA0–GPA7 (indices 0–7)
  ROLE_OUTPUT,  // 0  → PAL pin 1  (CLK/I) — LSB of counter
  ROLE_OUTPUT,  // 1  → PAL pin 2  (I)
  ROLE_OUTPUT,  // 2  → PAL pin 3  (I)
  ROLE_OUTPUT,  // 3  → PAL pin 4  (I)
  ROLE_OUTPUT,  // 4  → PAL pin 5  (I)
  ROLE_OUTPUT,  // 5  → PAL pin 6  (I)
  ROLE_OUTPUT,  // 6  → PAL pin 7  (I)
  ROLE_OUTPUT,  // 7  → PAL pin 8  (I)

  // MCP0 GPB0–GPB7 (indices 8–15)
  ROLE_OUTPUT,  // 8  → PAL pin 9  (I)
  ROLE_OUTPUT,  // 9  → PAL pin 10 (I)
  ROLE_OUTPUT,  // 10 → PAL pin 11 (I)
  ROLE_OUTPUT,  // 11 → PAL pin 13 (I)
  ROLE_INPUT,   // 12 → PAL pin 14 (I/O)
  ROLE_INPUT,   // 13 → PAL pin 15 (I/O)
  ROLE_INPUT,   // 14 → PAL pin 16 (I/O)
  ROLE_INPUT,   // 15 → PAL pin 17 (I/O)

  // MCP1 GPA0–GPA7 (indices 16–23)
  ROLE_INPUT,   // 16 → PAL pin 18 (I/O)
  ROLE_INPUT,   // 17 → PAL pin 19 (I/O)
  ROLE_INPUT,   // 18 → PAL pin 20 (I/O)
  ROLE_INPUT,   // 19 → PAL pin 21 (I/O)
  ROLE_INPUT,   // 20 → PAL pin 22 (I/O)
  ROLE_INPUT,   // 21 → PAL pin 23 (I/O)
  ROLE_IGNORE,  // 22
  ROLE_IGNORE,  // 23

  // MCP1 GPB0–GPB7 (indices 24–31)
  ROLE_IGNORE,  // 24
  ROLE_IGNORE,  // 25
  ROLE_IGNORE,  // 26
  ROLE_IGNORE,  // 27
  ROLE_IGNORE,  // 28
  ROLE_IGNORE,  // 29
  ROLE_IGNORE,  // 30
  ROLE_IGNORE,  // 31
};

// Address decoder for Ben Eater's 6502 breadboard computer

static PinRole pinRolesBE6502[32] = {
  // MCP0 GPA0–GPA7 (indices 0–7)
  ROLE_OUTPUT,  // 0  → PAL pin 1  (I, CLK)
  ROLE_OUTPUT,  // 1  → PAL pin 2  (I, A15)
  ROLE_OUTPUT,  // 2  → PAL pin 3  (I, A14)
  ROLE_OUTPUT,  // 3  → PAL pin 4  (I, A13)
  ROLE_OUTPUT,  // 4  → PAL pin 5  (I, A12)
  ROLE_IGNORE,  // 5  → PAL pin 6  (I)
  ROLE_IGNORE,  // 6  → PAL pin 7  (I)
  ROLE_IGNORE,  // 7  → PAL pin 8  (I)

  // MCP0 GPB0–GPB7 (indices 8–15)
  ROLE_IGNORE,  // 8  → PAL pin 9  (I)
  ROLE_IGNORE,  // 9  → PAL pin 10 (I)
  ROLE_IGNORE,  // 10 → PAL pin 11 (I)
  ROLE_IGNORE,  // 11 → PAL pin 13 (I)
  ROLE_IGNORE,  // 12 → PAL pin 14 (I/O)
  ROLE_IGNORE,  // 13 → PAL pin 15 (I/O)
  ROLE_IGNORE,  // 14 → PAL pin 16 (I/O)
  ROLE_IGNORE,  // 15 → PAL pin 17 (I/O)

  // MCP1 GPA0–GPA7 (indices 16–23)
  ROLE_IGNORE,  // 16 → PAL pin 18 (I/O)
  ROLE_IGNORE,  // 17 → PAL pin 19 (I/O)
  ROLE_INPUT,   // 18 → PAL pin 20 (O, SERIAL /CS)
  ROLE_INPUT,   // 19 → PAL pin 21 (O, VIA /CS)
  ROLE_INPUT,   // 20 → PAL pin 22 (O, RAM /CS)
  ROLE_INPUT,   // 21 → PAL pin 23 (O, ROM /CS)
  ROLE_IGNORE,  // 22
  ROLE_IGNORE,  // 23

  // MCP1 GPB0–GPB7 (indices 24–31)
  ROLE_IGNORE,  // 24
  ROLE_IGNORE,  // 25
  ROLE_IGNORE,  // 26
  ROLE_IGNORE,  // 27
  ROLE_IGNORE,  // 28
  ROLE_IGNORE,  // 29
  ROLE_IGNORE,  // 30
  ROLE_IGNORE,  // 31
};

// ─── Globals ────────────────────────────────────────────────────────

Adafruit_MCP23X17 mcp0;
Adafruit_MCP23X17 mcp1;

// Maps: which bit position in the input/output packed value corresponds to each MCP pin
// outputMap[i] = { mcpIndex (0 or 1), pin (0–15) } for the i-th output bit
// inputMap[i]  = { mcpIndex (0 or 1), pin (0–15) } for the i-th input bit

struct PinMapping {
  uint8_t mcpIndex;  // 0 or 1
  uint8_t mcpPin;    // 0–15
};

PinMapping outputMap[32];
PinMapping inputMap[32];
uint8_t numOutputs = 0;
uint8_t numInputs  = 0;

bool mcpSetupOk = false;
bool testDone = false;

// ─── Setup helpers ──────────────────────────────────────────────────

void buildPinMaps(PinRole *pinRoles) {
  numOutputs = 0;
  numInputs  = 0;

  for (uint8_t i = 0; i < 32; i++) {
    uint8_t mcpIdx = i / 16;
    uint8_t mcpPin = i % 16;

    if (pinRoles[i] == ROLE_OUTPUT) {
      outputMap[numOutputs++] = { mcpIdx, mcpPin };
    } else if (pinRoles[i] == ROLE_INPUT) {
      inputMap[numInputs++] = { mcpIdx, mcpPin };
    }
  }
}

void configureMcpPins(PinRole *pinRoles) {
  // Set pin directions on both MCPs
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t mcpPin = i % 16;
    Adafruit_MCP23X17 &mcp = (i < 16) ? mcp0 : mcp1;

    if (pinRoles[i] == ROLE_OUTPUT) {
      mcp.pinMode(mcpPin, OUTPUT);
    } else {
      // INPUT and IGNORE both set as inputs
      mcp.pinMode(mcpPin, INPUT);
    }
  }
}

// ─── Test logic ─────────────────────────────────────────────────────

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
  Serial.printf("# Total combinations: %lu\r\n", (unsigned long)(1UL << numOutputs));
  Serial.println("#");

  // Print drive pin mapping (MCP outputs → PAL inputs)
  Serial.print("# PAL input bits (LSB first):  ");
  for (uint8_t i = 0; i < numOutputs; i++) {
    if (i > 0) Serial.print(", ");
    Serial.printf("MCP%d.%d", outputMap[i].mcpIndex, outputMap[i].mcpPin);
  }
  Serial.println();

  // Print read pin mapping (MCP inputs ← PAL outputs)
  Serial.print("# PAL output bits (LSB first): ");
  for (uint8_t i = 0; i < numInputs; i++) {
    if (i > 0) Serial.print(", ");
    Serial.printf("MCP%d.%d", inputMap[i].mcpIndex, inputMap[i].mcpPin);
  }
  Serial.println();

  Serial.println("#");
  Serial.println("# pal_inputs_hex → pal_outputs_hex");
}

void runTest() {
  uint32_t totalCombinations = 1UL << numOutputs;

  printHeader();

  unsigned long startTime = millis();

  for (uint32_t i = 0; i < totalCombinations; i++) {
    writeOutputs(i);
    delayMicroseconds(10);  // allow PAL combinational logic to settle
    uint32_t outputs = readInputs();

    Serial.printf("0x%04lX → 0x%04lX\r\n", (unsigned long)i, (unsigned long)outputs);
  }

  unsigned long elapsed = millis() - startTime;

  Serial.println("#");
  Serial.printf("# Done. %lu rows in %lu ms\r\n", (unsigned long)totalCombinations, (unsigned long)elapsed);

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

  PinRole *currentPinRoles = pinRolesATF22V10C;
  // PinRole *currentPinRoles = pinRolesBE6502;

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
