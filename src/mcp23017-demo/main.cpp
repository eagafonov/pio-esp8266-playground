#include <Adafruit_MCP23X17.h>
#include <Wire.h>
#include <Arduino.h>

#ifndef BUILTIN_LED_PIN
#error "BUILTIN_LED_PIN is not defined for this board. Please define it in platformio.ini or in the code."
#endif

#ifndef BUILTIN_LED_PIN_MODE
#define BUILTIN_LED_PIN_MODE OUTPUT
#endif


/////////
// MCP
/////////

Adafruit_MCP23X17 mcp0;
Adafruit_MCP23X17 mcp1;

int clockCounter = 0;

void readBusState() {
  auto ab0 = mcp0.readGPIOAB();
  auto ab1 = mcp1.readGPIOAB();

  Serial.printf("%5d 0x%04X 0x%04X\r\n",
    clockCounter,
    ab0,
    ab1
  );

  clockCounter++;
}

bool mcpSetupOk = false;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial) {
      delay(50);
  }

  Serial.println();
  Serial.println("Stating demo");

  pinMode(BUILTIN_LED_PIN, BUILTIN_LED_PIN_MODE);
  digitalWrite(BUILTIN_LED_PIN, HIGH);

  Wire.begin(HW_I2C_PIN_SDA, HW_I2C_PIN_SCL);
  Wire.setClock(400000); // 400kHz is max for WeMo D1 Mini (ESP8266 at 80MHz)

  // MCP23017 setup
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

  // MCP23017 is is configured as inputs by default

  mcpSetupOk = true;
}

void loop() {
  if (!mcpSetupOk) {
    digitalWrite(BUILTIN_LED_PIN, LOW);
    delay(50);
    digitalWrite(BUILTIN_LED_PIN, HIGH);
    delay(500);

    return;
  }

  readBusState();

  delay(100);
}
