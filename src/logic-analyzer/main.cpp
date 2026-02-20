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
const unsigned long debounceDelay = 2; // 50ms debounce

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

#pragma pack(push, 1)
typedef union {
  uint8_t data;
  struct {
    uint8_t rwb : 1;   // Read/Write Bar
    uint8_t sync : 1;  // SYNC
    uint8_t irqb : 1;  // IRQ Bar
    uint8_t resetb : 1;  // Reset Bar
    uint8_t unused : 3;
    uint8_t clock : 1; // Clock (inverted to convert 3.3 -> 5V logic using transistor)
  } bits;
} Flags;
#pragma pack(pop)

void printBusState(uint16_t addressBus, uint8_t data, Flags flags, unsigned long clockCounter);

/////////////////////////////////
// Clock mode and pulsing logic
/////////////////////////////////

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
};

uint8_t clockIndex = 0;
int pulsePeriod = 0; // milliseconds, 0 means no pulsing

void setClock(int index) {
  clockIndex = index;
  pulsePeriod = clockPulseIntervals[clockIndex];
}


void stopClock() {
  setClock(0);
}

void incClockIndex() {
  if (clockIndex < (sizeof(clockPulseIntervals) / sizeof(clockPulseIntervals[0])) - 1) {
    clockIndex++;
    Serial.printf("Clock index increased to %d (%d ms)\r\n", clockIndex, clockPulseIntervals[clockIndex]);

    setClock(clockIndex);
  }
}

void decClockIndex() {
  if (clockIndex > 0) {
    clockIndex--;
    Serial.printf("Clock index decreased to %d (%d ms)\r\n", clockIndex, clockPulseIntervals[clockIndex]);

    setClock(clockIndex);
  }
}

enum ClockMode {
  MANUAL = 0,
  AUTOMATIC = 1
};

ClockMode clockMode = ClockMode::MANUAL;
unsigned long clockCounter = 0;

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

void singleClockPulse(unsigned long duration);

void onRight(ESPRotary& r) {
  if (clockMode == ClockMode::MANUAL) {
    singleClockPulse(100);
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


typedef struct {
    uint8_t rwb : 1;   // Read/Write Bar
    uint8_t sync : 1;  // SYNC
    uint8_t irqb : 1;  // IRQ Bar
    uint8_t resetb : 1;  // Reset Bar
    uint8_t unused : 3;
    uint8_t clock : 1; // Clock (inverted to convert 3.3 -> 5V logic using transistor)
} FlagsBits;


const char wrs [4] = { 'W', 'r', '?', 'f' };

const char noteIrqVectorRead[] = "IRQ Vector Read";
const char noteResetVectorRead[] = "Reset Vector Read";
const char noteStackRead[] = "Stack read";
const char noteStackWrite[] = "Stack write";

const char* opcodeNames[256] = {
    /* 00 */
    "BRK imp","ORA (zp,x)","NOP imp","NOP imp","TSB zp","ORA zp","ASL zp","RMB0 zp",
    "PHP imp","ORA imm","ASL acc","NOP imp","TSB abs","ORA abs","ASL abs","BBR0 rel",

    /* 10 */
    "BPL rel","ORA (zp),y","ORA (zp)","NOP imp","TRB zp","ORA zp,x","ASL zp,x","RMB1 zp",
    "CLC imp","ORA abs,y","INC acc","NOP imp","TRB abs","ORA abs,x","ASL abs,x","BBR1 rel",

    /* 20 */
    "JSR abs","AND (zp,x)","NOP imp","NOP imp","BIT zp","AND zp","ROL zp","RMB2 zp",
    "PLP imp","AND imm","ROL acc","NOP imp","BIT abs","AND abs","ROL abs","BBR2 rel",

    /* 30 */
    "BMI rel","AND (zp),y","AND (zp)","NOP imp","BIT zp,x","AND zp,x","ROL zp,x","RMB3 zp",
    "SEC imp","AND abs,y","DEC acc","NOP imp","BIT abs,x","AND abs,x","ROL abs,x","BBR3 rel",

    /* 40 */
    "RTI imp","EOR (zp,x)","NOP imp","NOP imp","NOP zp","EOR zp","LSR zp","RMB4 zp",
    "PHA imp","EOR imm","LSR acc","NOP imp","JMP abs","EOR abs","LSR abs","BBR4 rel",

    /* 50 */
    "BVC rel","EOR (zp),y","EOR (zp)","NOP imp","NOP zp,x","EOR zp,x","LSR zp,x","RMB5 zp",
    "CLI imp","EOR abs,y","PHY imp","NOP imp","NOP abs","EOR abs,x","LSR abs,x","BBR5 rel",

    /* 60 */
    "RTS imp","ADC (zp,x)","NOP imp","NOP imp","STZ zp","ADC zp","ROR zp","RMB6 zp",
    "PLA imp","ADC imm","ROR acc","NOP imp","JMP (abs)","ADC abs","ROR abs","BBR6 rel",

    /* 70 */
    "BVS rel","ADC (zp),y","ADC (zp)","NOP imp","STZ zp,x","ADC zp,x","ROR zp,x","RMB7 zp",
    "SEI imp","ADC abs,y","PLY imp","NOP imp","JMP (abs,x)","ADC abs,x","ROR abs,x","BBR7 rel",

    /* 80 */
    "BRA rel","STA (zp,x)","NOP imp","NOP imp","STY zp","STA zp","STX zp","SMB0 zp",
    "DEY imp","BIT imm","TXA imp","NOP imp","STY abs","STA abs","STX abs","BBS0 rel",

    /* 90 */
    "BCC rel","STA (zp),y","STA (zp)","NOP imp","STY zp,x","STA zp,x","STX zp,y","SMB1 zp",
    "TYA imp","STA abs,y","TXS imp","NOP imp","STZ abs","STA abs,x","STZ abs,x","BBS1 rel",

    /* A0 */
    "LDY imm","LDA (zp,x)","LDX imm","NOP imp","LDY zp","LDA zp","LDX zp","SMB2 zp",
    "TAY imp","LDA imm","TAX imp","NOP imp","LDY abs","LDA abs","LDX abs","BBS2 rel",

    /* B0 */
    "BCS rel","LDA (zp),y","LDA (zp)","NOP imp","LDY zp,x","LDA zp,x","LDX zp,y","SMB3 zp",
    "CLV imp","LDA abs,y","TSX imp","NOP imp","LDY abs,x","LDA abs,x","LDX abs,y","BBS3 rel",

    /* C0 */
    "CPY imm","CMP (zp,x)","NOP imp","NOP imp","CPY zp","CMP zp","DEC zp","SMB4 zp",
    "INY imp","CMP imm","DEX imp","WAI imp","CPY abs","CMP abs","DEC abs","BBS4 rel",

    /* D0 */
    "BNE rel","CMP (zp),y","CMP (zp)","NOP imp","NOP zp,x","CMP zp,x","DEC zp,x","SMB5 zp",
    "CLD imp","CMP abs,y","PHX imp","STP imp","NOP abs","CMP abs,x","DEC abs,x","BBS5 rel",

    /* E0 */
    "CPX imm","SBC (zp,x)","NOP imp","NOP imp","CPX zp","SBC zp","INC zp","SMB6 zp",
    "INX imp","SBC imm","NOP imp","NOP imp","CPX abs","SBC abs","INC abs","BBS6 rel",

    /* F0 */
    "BEQ rel","SBC (zp),y","SBC (zp)","NOP imp","NOP zp,x","SBC zp,x","INC zp,x","SMB7 zp",
    "SED imp","SBC abs,y","PLX imp","NOP imp","NOP abs","SBC abs,x","INC abs,x","BBS7 rel"
};

// W65C51N Serial Communication Interface (SCI) status register bits
typedef struct serial_status_reg_t {
    uint8_t parity_error : 1;
    uint8_t framing_error : 1;
    uint8_t overrun_error : 1;
    uint8_t rx_full : 1;
    uint8_t tx_empty : 1;
    uint8_t dcd: 1;   // Data Carrier Detect
    uint8_t dsr: 1;   // Data Set Ready
    uint8_t irq: 1;
};

void printBusState(uint16_t addressBus, uint8_t data, Flags flags, unsigned long clockCounter) {
  // 0 = RWB (1=Read, 0=Write)
  // 1 = SYNC (Fetching new instruction)
  // 2 = IRQB (Interrupt Request)

  Serial.printf("%5d %c addr:0x%04X data:0x%02X flags:0x%02X (%c%c%c)",
    clockCounter,
    flags.bits.clock ? ' ' : '|',
    addressBus,
    data & 0xFF,
    flags.data,
    flags.bits.rwb ? 'r' : 'W',
    flags.bits.sync ? 's' : '-',
    // flags.bits.irqb ? '-' : 'I'
    '-' // flags.bits.irqb ? '-' : 'I'
  );

  if (!flags.bits.resetb) {
    Serial.print(" RESET");
  } else if (flags.bits.clock == 0) {
    // Print note when clock pulse is active
    if (addressBus == 0xFFFE  || addressBus == 0xFFFF) {
      Serial.print(" " );
      Serial.print(noteIrqVectorRead);
    } else if (addressBus == 0xFFFC || addressBus == 0xFFFD) {
      Serial.print(" " );
      Serial.print(noteResetVectorRead);
    } else if (addressBus > 0x0100 && addressBus <= 0x01FF) {
      Serial.print(" " );
      Serial.print(flags.bits.rwb ? noteStackRead : noteStackWrite);
    } else if (addressBus == 0x6000 && flags.bits.rwb == 0) {
      // Write to IO area at 0x6000
      Serial.printf(" IO 0x6000 Write 0x%02X", data);
    } else if (addressBus == 0x5000) {
      if (flags.bits.rwb == 1) {
        // read serial data
        Serial.printf(" Serial data read 0x%02X", data);
      } else {
        // write serial data
        Serial.printf(" Serial data write 0x%02X", data);
      }
    } else if (addressBus == 0x5001) {
      // Serial status
      if (flags.bits.rwb == 1) {
        // read serial data
        union {
          uint8_t data;
          serial_status_reg_t bits;
        } u = { .data = data };

        Serial.printf(" Serial status read 0x%02X (irq:%d dsr:%d dcd:%d rx_full:%d tx_empty:%d overrun:%d)",
            data,
            u.bits.irq,
            u.bits.dsr,
            u.bits.dcd,
            u.bits.rx_full,
            u.bits.tx_empty,
            u.bits.overrun_error
          );
      }
    } else if (addressBus == 0x5002) {
      if (flags.bits.rwb == 1) {
        // read serial data
        Serial.printf(" Serial command read 0x%02X", data);
      } else {
        // write serial data
        Serial.printf(" Serial command write 0x%02X", data);
      }
    } else if (addressBus == 0x5003) {
      if (flags.bits.rwb == 1) {
        // read serial data
        Serial.printf(" Serial control read 0x%02X", data);
      } else {
        // write serial data
        Serial.printf(" Serial control write 0x%02X", data);
      }
    }

    // decode opcode when fetching instruction
    if (flags.bits.sync && flags.bits.rwb) {
      // data is the opcode
      Serial.print(" Opcode: ");
      Serial.print(data, HEX);

      auto opcodeName = opcodeNames[data];

      if (opcodeName != nullptr) {
        Serial.print(" (");
        Serial.print(opcodeName);
        Serial.print(")");
      }
    }
  }

  Serial.println();
}

void singleClockPulse(unsigned long duration) {
  digitalWrite(LED_PIN, LOW);

  auto addressBus = mcp0.readGPIOAB();
  auto dataBusFlags = mcp1.readGPIOAB();

  delay(duration);
  digitalWrite(LED_PIN, HIGH);

  Flags flags= { .data = dataBusFlags >> 8 };

  printBusState(addressBus, dataBusFlags & 0xFF, flags, clockCounter);

  // Keep clock zero if reset is active
  if (!flags.bits.resetb) {
    clockCounter = 0;
  } else {
    clockCounter++;
  }
}


bool setupOk = false;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  Serial.println("Hello from PIO - Button Interrupt Demo");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

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
  // Wire.begin();
  // Wire.setClock(400000); // 400kHz I2C

  if (!mcp0.begin_I2C(0x20, &Wire)) {
    Serial.println("Error initializing MCP23017 0x20. Check connections.");
    return;
  } else {
    Serial.println("MCP23017 0x20 initialized successfully.");
  }

  if (!mcp1.begin_I2C(0x21, &Wire)) {
    Serial.println("Error initializing MCP23017 0x21. Check connections.");
    return;
  } else {
    Serial.println("MCP23017 0x21 initialized successfully.");
  }

  Wire.setClock(400000); // 400kHz is max for WeMo D1 Mini (ESP8266 at 80MHz)

  clockMode = ClockMode::AUTOMATIC;
  setClock(1); // Start with 1 Hz pulsing

  printClockMode();

  setupOk = true;
}

/*

Some measurements:
empty loop(): ~93170 loops/sec

                100kHz  200kHz 400kHz
------------------------------------------
readGPIOA       1740   3500    6950
readGPIOAB      1430   2870    5740
readGPIOAB x 2  720    1460    2960

                                             ```````````````````````````````````
*/

void handleSerialInput() {
  if (!Serial.available()) {
    return;
  }

  char c = Serial.read();

  if (c == 'm') {
    toggleClockMode();
  } else if (c == 'p') {
    singleClockPulse(100);
  } else if (c == '+' || c == '=') {  // Increase clock speed, switch to automatic mode
    if (clockMode == ClockMode::MANUAL) {
      toggleClockMode();
    } else {
      incClockIndex();
    }
  } else if (c == '-' || c == '_') {   // Decrease clock speed (automatic mode only)
    if (clockMode == ClockMode::AUTOMATIC) {
      decClockIndex();
    }
  } else if (c == ' ') { // Single step, switch to manual mode if currently in automatic
    if (clockMode == ClockMode::AUTOMATIC) {
      toggleClockMode();
    } else {
      singleClockPulse(100);
    }
  }
  else if (c >= '1' && c <= '9') { // 1-9 change clock speed directly
    int index = c - '0';
    if (index < (sizeof(clockPulseIntervals) / sizeof(clockPulseIntervals[0]))) {
      if (clockMode == ClockMode::MANUAL) {
        toggleClockMode();
      }
      clockIndex = index;
      setClock(clockIndex);
      Serial.printf("Clock index set to %d (%d ms)\r\n", clockIndex, clockPulseIntervals[clockIndex]);
    }
  } else if (c == '0') { // Stop clock
    stopClock();
    Serial.println("Clock stopped");
  }
}

void loop() {
  if (!setupOk) {
    return;
  }

  handleSerialInput();

  // print stats once a second
#ifdef DEBUG_LOOP_COUNTER
  static unsigned long loopCounter = 0;
  loopCounter++;

  unsigned long currentTime = millis();
  static unsigned long lastPrintTime = 0;
  static unsigned long lastLoopCounter = 0;

  if (currentTime - lastPrintTime >= 1000) {

    // Clock rate
    unsigned long clocksPerSecond = loopCounter - lastLoopCounter;

    Serial.printf("Clock counter: %d (%lu clocks/sec)\r\n",
      loopCounter,
      clocksPerSecond
    );

    lastLoopCounter = loopCounter;

    lastPrintTime = currentTime;
  }
#endif


  // Pulse LED every n milliseconds, duty cycle 50%
  static unsigned long pulseStartTime = 0; // Time when the last pulse started
  unsigned long currentTime = millis();

  if (clockMode == ClockMode::AUTOMATIC && pulsePeriod > 0) {
    if ((currentTime - pulseStartTime) < (pulsePeriod / 2)) {
      // HIGH phase
      digitalWrite(LED_PIN, HIGH);
    } else if ((currentTime - pulseStartTime) < pulsePeriod) {
      // LOW phase
      digitalWrite(LED_PIN, LOW);
    } else {
      // Start new pulse cycle
      pulseStartTime = currentTime;
      clockCounter++;
    }
  }

  if (clockMode == ClockMode::AUTOMATIC) {
    // mcp0.readGPIOA();
    auto addressBus = mcp0.readGPIOAB();
    auto dataBusFlags = mcp1.readGPIOAB();

    // Dump  address and data bus every loop when it is chanaged
    static uint16_t lastAddressBus = 0xFFFF;
    static uint16_t lastDataBusFlags = 0xFFFF;

    if (addressBus != lastAddressBus || dataBusFlags != lastDataBusFlags) {
      Flags flags= { .data = dataBusFlags >> 8 };

      // Keep clock zero if reset is active
      if (!flags.bits.resetb) {
        clockCounter = 0;
      }

      if (flags.bits.clock == 0) {
        printBusState(addressBus, dataBusFlags & 0xFF, flags, clockCounter);
      }

      lastAddressBus = addressBus;
      lastDataBusFlags = dataBusFlags;
    }
  }

  rotary.loop();

  // Check if button was pressed
  if (buttonPressed) {
    buttonPressed = false; // Reset flag
    toggleClockMode();
  }

  delay(1); // Small delay to avoid busy looping
}
