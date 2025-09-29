#include <Arduino.h>
#include <Wire.h>

void setup() {
    delay(1000); // sanity delay
    Serial.begin(115200);
    Serial.println();
    Serial.println("I2C Scanner");
}

// returns number of devices found
int scan() {
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

    return nDevices;
}


// List of pins to scan. Each pin can be used as SDA or SCL.
uint8_t allowed_pins[] = {
    // GPIO     Wemos D1 Mini   NodeMCU
    4,  //        D2              D2
    5,  //        D1              D1
    12, //        D6              D6
    13, //        D7              D7
    14  //        D5              D5
};

void loop() {
    for (auto scl : allowed_pins) {
        for (auto sda : allowed_pins) {
            if (sda == scl) {
                continue; // skip same pin for SDA and SCL
            }

            Wire.pins(sda, scl);
            Wire.begin();

            auto devices_found = scan();

            if (devices_found) {
                Serial.printf("%d I2C devices found on SDA: %d, SCL: %d\n", devices_found, sda, scl);
            }
        }
    }

    Serial.println();
    delay(1000); // Wait before next scan
}