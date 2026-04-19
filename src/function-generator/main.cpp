#include <Arduino.h>

#include <driver/dac_cosine.h>

bool setupOk = false;

void setup() {
    Serial.begin(115200);

#ifdef NATIVE_USB
    while(!Serial) {
        delay(50);
    }
#endif

    Serial.println();
    Serial.println("Function generator");

    // Setup cosine
    dac_cosine_config_t cfg = {
        .chan_id=DAC_CHAN_0,
        .freq_hz=1000,
        .clk_src=DAC_COSINE_CLK_SRC_DEFAULT,
        .atten=DAC_COSINE_ATTEN_DB_0,
        .phase=DAC_COSINE_PHASE_0,
        .offset=0,
    };

    dac_cosine_handle_t handle;

    auto err = dac_cosine_new_channel(&cfg, &handle);

    if (err != ESP_OK) {
        Serial.print("Failed to create new channel: ");
        Serial.println(err);
        return;
    }

    err = dac_cosine_start(handle);

    if (err != ESP_OK) {
        Serial.print("Failed to start Cosine DAC: ");
        Serial.println(err);
        return;
    }

    setupOk = true;

    Serial.println("Setup OK");
}

void loop() {
}
