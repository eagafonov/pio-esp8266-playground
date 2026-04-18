#include <Arduino.h>

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

#ifdef NATIVE_USB
  while(!Serial) {
    delay(10); // wait for serial port to connect. Needed for native USB
  }
#endif

  Serial.println();
  Serial.println("Hello from PIO");
}


void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("Hello, World!");
  delay(1000);
}
