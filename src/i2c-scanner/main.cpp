#include <Arduino.h>
#include <Wire.h>

void setup() {
    delay(1000); // sanity delay
    Serial.begin(115200);
    Serial.println();
    Serial.println("I2C Scanner");

    // Initialize I2C
    Wire.pins(4, 5); // SDA = GPIO4, SCL = GPIO5
    Wire.begin();
    Serial.println("I2C Scanner started");
}
void loop() {
    Serial.println("Scanning I2C bus...");

    // Scan for devices
    byte error, address;
    int nDevices = 0;

    for (address = 1; address < 127; address++) {
        // Try to begin transmission
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            Serial.print(address, HEX);
            Serial.println(" !");
            nDevices++;
        } else if (error == 4) {
            Serial.print("Unknown error at address 0x");
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0) {
        Serial.println("No I2C devices found");
    } else {
        Serial.print(nDevices);
        Serial.println(" I2C devices found");
    }

    delay(5000); // Wait before next scan
}