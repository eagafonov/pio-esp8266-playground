#include <Arduino.h>
#include <ESPRotary.h>
#include <Adafruit_MCP23X17.h>
#include <Wire.h>

#define EXTERNAL_LED_PIN 14 // GPIO14 on ESP8266, D5 on NodeMCU
// #define BUTTON_PIN 16        // GPI15 on ESP8266, D8 on NodeMCU
// #define BUTTON_PIN 2        // RX

#define BUTTON_PIN 0        // GPIO0 D3 on ESP8266, D3 on NodeMCU (aka Flash button)

// #define EXTERNAL_LED_PIN 16        // GPI15 on ESP8266, D8 on NodeMCU
// #define BUTTON_PIN 14 // GPIO14 on ESP8266, D5 on NodeMCU

#define BUILTIN_LED_PIN 2   // GPIO2 on ESP8266, D4 on NodeMCU (built-in LED)
// #define BUTTON_PIN 4        // GPIO4 on ESP8266, D2 on NodeMCU

#define ROTARY_PIN1 12    // GPIO12 on ESP8266, D6 on NodeMCU
#define ROTARY_PIN2 13    // GPIO13 on ESP8266, D7 on NodeMCU

#define LED_PIN EXTERNAL_LED_PIN

// NOTE: GPIO16 (D0) doesn't support interrupts on ESP8266
// Using GPIO4 (D2) instead for button with interrupt support

volatile bool buttonPressed = false;
volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 50; // 50ms debounce

// Interrupt Service Routine (ISR)
void IRAM_ATTR handleButtonPress() {
  unsigned long interruptTime = millis();

  // Debounce: ignore interrupts that occur too quickly
  if (interruptTime - lastInterruptTime > debounceDelay) {
    lastInterruptTime = interruptTime;

    //read the pin state to clear the interrupt (optional)
    // volatile read to ensure compiler does not optimize it away
    buttonPressed = digitalRead(BUTTON_PIN);
  }
}

void toggleLED() {
  static bool ledState = false;
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);

  Serial.println(ledState ? "ON" : "OFF");
}

void readBusState();

void pulseLED(unsigned long duration) {
  digitalWrite(LED_PIN, HIGH);
  // mcp.digitalWrite(0, HIGH);
  delay(duration);
  digitalWrite(LED_PIN, LOW);
  // mcp.digitalWrite(0, HIGH);

  readBusState();
}


/////////////////////////////////
// Clock mode and pulsing logic
/////////////////////////////////

int pulsePeriod = 0; // milliseconds, 0 means no pulsing

unsigned long clockPulseIntervals[] = {
  0, // manual mode / no pulsing
  1000, // 1 Hz
  500,  // 2 Hz
  250,  // 4 Hz
  100,  // 10 Hz
  50,   // 20 Hz
  33,   // 30 Hz
  25,   // 40 Hz
  20,   // 50 Hz
  15,   // ~66 Hz
  12,   // 80 Hz
  10,   // 100 Hz
  7,    // ~143 Hz
  5,     // 200 Hz
  3,     // ~333 Hz
  2,     // 500 Hz
};

uint8_t clockIndex = 0;

void stopClock() {
  clockIndex = 0;
  pulsePeriod = clockPulseIntervals[clockIndex];
}

void incClockIndex() {
  if (clockIndex < (sizeof(clockPulseIntervals) / sizeof(clockPulseIntervals[0])) - 1) {
    clockIndex++;
    Serial.printf("Clock index increased to %d (%d ms)\r\n", clockIndex, clockPulseIntervals[clockIndex]);
  }

  pulsePeriod = clockPulseIntervals[clockIndex];
}

void decClockIndex() {
  if (clockIndex > 0) {
    clockIndex--;
    Serial.printf("Clock index decreased to %d (%d ms)\r\n", clockIndex, clockPulseIntervals[clockIndex]);
  }

  pulsePeriod = clockPulseIntervals[clockIndex];
}

enum ClockMode {
  MANUAL = 0,
  AUTOMATIC = 1
};

ClockMode clockMode = ClockMode::MANUAL;
uint16_t clockCounter = 0;

void printClockMode() {
  Serial.print("Current clock mode: ");
  Serial.println(clockMode == ClockMode::AUTOMATIC ? "Automatic" : "Manual");
}

void toggleClockMode() {
  clockMode = (clockMode == ClockMode::AUTOMATIC) ? ClockMode::MANUAL : ClockMode::AUTOMATIC;

  // Reset clock index when switching to manual mode
  if (clockMode == ClockMode::MANUAL) {
    stopClock();
  }

  printClockMode();
}

////////////////////
// Rotary encoder
////////////////////

ESPRotary rotary;

// on change
void rotate(ESPRotary& r) {
  if (clockMode == ClockMode::MANUAL) {
    return;
  }

  // position in 0.1 Hz
  auto freq = r.getPosition();

  // convert to milliseconds period
  if (freq > 0) {
    pulsePeriod = 10000 / freq; // 0.1 Hz to ms
    Serial.printf("Freq: %d * 0.1 Hz, Pulse period set to: %d\n", freq, pulsePeriod);
  } else {
    pulsePeriod = -1; // disable pulsing
    Serial.printf("Pulse period disabled\n");
  }
}

// on left or right rotation
void showDirection(ESPRotary& r) {
  Serial.println(r.directionToString(r.getDirection()));
}

void onRight(ESPRotary& r) {
  if (clockMode == ClockMode::MANUAL) {
    pulseLED(100);
  } else {
    incClockIndex();
  }
}

void onLeft(ESPRotary& r) {
  if (clockMode == ClockMode::MANUAL) {
    if (clockCounter != 0) {
      clockCounter = 0;
      Serial.println("Clock counter reset to 0");
    }
  } else {
    decClockIndex();
  }
}


/////////
// MCP
/////////

Adafruit_MCP23X17 mcp0;
Adafruit_MCP23X17 mcp1;

typedef union {
  uint8_t data;

  // Flag bits
  struct {
    uint8_t rwb : 1;   // Read/Write Bar
    uint8_t sync : 1;  // SYNC
    uint8_t irqb : 1;  // IRQB
    uint8_t unused : 5;
  } bits;
} FlagsUnion;

const char wrs [4] = { 'W', 'r', '?', 'f' };

const char noteIrqVectorRead[] = "IRQ Addr Read";
const char noteResetVectorRead[] = "Reset Addr Read";

void readBusState() {
  // MCP #0 reads address bus (GPIOA - lower byte, GPIOB - upper byte)
  // MCP #1 GPIOA reads data bus
  // MCP #1 GPIOB reads flags
  // 0 = RWB (1=Read, 0=Write)
  // 1 = SYNC (Fetching new instruction)
  // 2 = IRQB (Interrupt Request)

  auto addressBus = mcp0.readGPIOAB();
  auto dataBus = mcp1.readGPIOA();

  FlagsUnion flags= { .data = mcp1.readGPIOB() };

  Serial.printf("%5d Bus addr:0x%04X data:0x%02X flags:0x%02X (%c%c%c)",
    clockCounter,
    addressBus,
    dataBus,
    flags.data,
    flags.bits.rwb ? 'r' : 'W',
    flags.bits.sync ? 's' : '-',
    flags.bits.irqb ? '-' : 'I'
    // '-' // flags.bits.irqb ? '-' : 'I'
  );

  // Print note if needed
  if (addressBus == 0xFFFE  || addressBus == 0xFFFF) {
    Serial.print(" <- " );
    Serial.print(noteIrqVectorRead);
  } else if (addressBus == 0xFFFC || addressBus == 0xFFFD) {
    Serial.print(" <- " );
    Serial.print(noteResetVectorRead);
  }

  Serial.println();

  clockCounter++;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  Serial.println("Hello from PIO - Button Interrupt Demo");

  pinMode(LED_PIN, OUTPUT);
  // pinMode(BUTTON_PIN, INPUT_PULLUP); // Enable internal pull-up resistor
  // Attach interrupt: trigger on FALLING edge (button press pulls to ground)
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, CHANGE);

  Serial.println("Button interrupt initialized on GPIO4 (D2)");
  Serial.println("Press the button to toggle LED state");

  rotary.begin(ROTARY_PIN1, ROTARY_PIN2, 4, 0, 4000, 0, 1);

  // rotary.setChangedHandler(rotate);
  // rotary.setLeftRotationHandler(showDirection);
  // rotary.setRightRotationHandler(showDirection);

  rotary.setRightRotationHandler(onRight);
  rotary.setLeftRotationHandler(onLeft);

  // MCP23017 setup
  Wire.pins(5, 4);
  Wire.begin();

  if (!mcp0.begin_I2C(0x20, &Wire)) {
    Serial.println("Error initializing MCP23017 0x20. Check connections.");
    // while (1);
  } else {
    Serial.println("MCP23017 0x20 initialized successfully.");
  }

  if (!mcp1.begin_I2C(0x21, &Wire)) {
    Serial.println("Error initializing MCP23017 0x21. Check connections.");
    // while (1);
  } else {
    Serial.println("MCP23017 0x21 initialized successfully.");
  }


  printClockMode();
}

void loop() {
  rotary.loop();

  // Check if button was pressed
  if (buttonPressed) {
    buttonPressed = false; // Reset flag
    toggleClockMode();
  }

  // Pulse LED every n milliseconds
  static unsigned long lastPulse = 0;
  unsigned long currentTime = millis();

  if (pulsePeriod > 0 && ((currentTime - lastPulse) >= pulsePeriod)) {
    if (clockMode == ClockMode::AUTOMATIC) {
      pulseLED(1);
    }

    lastPulse = currentTime;
  }
}