#include <Arduino.h>
#include <inttypes.h>
#include <soc/soc_caps.h>

#if SOC_DAC_SUPPORTED
#include <driver/dac_cosine.h>
#endif

static constexpr uint8_t SQUARE_PIN = 4;
static constexpr uint8_t PULSE_PIN = 5;
static constexpr uint32_t DEFAULT_FREQ_HZ = 1000;

// Max resolution: freq * 2^res < 40 MHz (LEDC base clock)
// Clamped to hardware maximum (14 bits)
static uint8_t maxLedcResolution(uint32_t freq) {
    uint8_t res = 0;
    uint32_t ratio = 40'000'000 / freq;
    while (ratio >>= 1) {
        res++;
    }
    return res > 14 ? 14 : res;
}

static uint32_t outputFreqHz = DEFAULT_FREQ_HZ;
static uint8_t  squareResolution = 0;
static uint32_t squareDuty50 = 0;

#ifndef BUILTIN_LED_PIN
#error "BUILTIN_LED_PIN is not defined. Define it in platformio.ini."
#endif

#ifndef BUILTIN_LED_PIN_MODE
#define BUILTIN_LED_PIN_MODE OUTPUT
#endif

enum class Mode {
#if SOC_DAC_SUPPORTED
    COSINE,
#endif
    SQUARE,
    PULSE,
};

#if SOC_DAC_SUPPORTED
static dac_cosine_handle_t cosineHandle = nullptr;
#endif
static Mode activeMode =
#if SOC_DAC_SUPPORTED
    Mode::COSINE;
#else
    Mode::SQUARE;
#endif
static bool generationActive = false;

static void setLed(bool on) {
    // Open-drain: LOW = on; Push-pull: HIGH = on
    if (BUILTIN_LED_PIN_MODE == OUTPUT_OPEN_DRAIN) {
        digitalWrite(BUILTIN_LED_PIN, on ? LOW : HIGH);
    } else {
        digitalWrite(BUILTIN_LED_PIN, on ? HIGH : LOW);
    }
}

#if SOC_DAC_SUPPORTED
static void startCosine() {
    dac_cosine_start(cosineHandle);
}

static void stopCosine() {
    dac_cosine_stop(cosineHandle);
}
#endif

static bool applySquareFreq() {
    squareResolution = maxLedcResolution(outputFreqHz);
    ledcDetach(SQUARE_PIN);
    // Reduce resolution until hardware accepts it
    while (squareResolution > 1 && !ledcAttach(SQUARE_PIN, outputFreqHz, squareResolution)) {
        squareResolution--;
    }
    if (squareResolution <= 1) {
        // Last attempt with minimum resolution
        if (!ledcAttach(SQUARE_PIN, outputFreqHz, 1)) {
            Serial.printf("LEDC: %" PRIu32 " Hz not achievable (base clock 40 MHz, divider overflow)\r\n", outputFreqHz);
            return false;
        }
    }
    squareDuty50 = (1 << squareResolution) / 2;
    Serial.printf("LEDC: %" PRIu32 " Hz, %" PRIu8 "-bit resolution, duty %" PRIu32 "/%" PRIu32 "\r\n",
        outputFreqHz, squareResolution, squareDuty50, (uint32_t)(1 << squareResolution));
    return true;
}

static void startSquare() {
    ledcWrite(SQUARE_PIN, squareDuty50);
}

static void stopSquare() {
    ledcWrite(SQUARE_PIN, 0);
}

static void setSquareFreq(uint32_t freq) {
    uint32_t prevFreq = outputFreqHz;
    outputFreqHz = freq;
    bool wasActive = generationActive && activeMode == Mode::SQUARE;
    if (wasActive) {
        stopSquare();
    }
    if (!applySquareFreq()) {
        // Revert to previous frequency
        outputFreqHz = prevFreq;
        applySquareFreq();
        if (wasActive) {
            startSquare();
        }
        return;
    }
    if (wasActive) {
        startSquare();
    }
}

// --- Pulse mode (software-driven, push-pull) ---
static uint32_t pulseHalfPeriodUs = 500'000 / DEFAULT_FREQ_HZ;
static unsigned long pulseNextToggleUs = 0;
static bool pulseState = false;
static bool pulseActive = false;

static void startPulse() {
    pinMode(PULSE_PIN, OUTPUT);
    pulseHalfPeriodUs = 500'000 / outputFreqHz;
    pulseState = false;
    digitalWrite(PULSE_PIN, HIGH);
    pulseNextToggleUs = micros() + pulseHalfPeriodUs;
    pulseActive = true;
    Serial.printf("Pulse: %" PRIu32 " Hz, half-period %" PRIu32 " us, GPIO %" PRIu8 "\r\n",
        outputFreqHz, pulseHalfPeriodUs, (uint8_t)PULSE_PIN);
}

static void stopPulse() {
    pulseActive = false;
    digitalWrite(PULSE_PIN, LOW);
}

static void loopPulse() {
    if (!pulseActive) return;
    unsigned long now = micros();
    if ((long)(now - pulseNextToggleUs) >= 0) {
        pulseState = !pulseState;
        digitalWrite(PULSE_PIN, pulseState ? HIGH : LOW);
        pulseNextToggleUs += pulseHalfPeriodUs;
    }
}

// --- Single impulse (synchronous) ---
static void triggerImpulse() {
    pinMode(PULSE_PIN, OUTPUT);
    uint32_t durationUs = 500'000 / outputFreqHz;
    digitalWrite(PULSE_PIN, HIGH);
    delayMicroseconds(durationUs);
    digitalWrite(PULSE_PIN, LOW);
    Serial.printf("Impulse: %" PRIu32 " us pulse on GPIO %" PRIu8 "\r\n", durationUs, (uint8_t)PULSE_PIN);
}

static void setFreq(uint32_t freq) {
    setSquareFreq(freq);
    if (generationActive && activeMode == Mode::PULSE) {
        pulseHalfPeriodUs = 500'000 / outputFreqHz;
        Serial.printf("Pulse: %" PRIu32 " Hz, half-period %" PRIu32 " us\r\n",
            outputFreqHz, pulseHalfPeriodUs);
    }
}

static void stopAll() {
#if SOC_DAC_SUPPORTED
    stopCosine();
#endif
    stopSquare();
    stopPulse();
    generationActive = false;
    setLed(false);
}

static void switchMode(Mode mode) {
    if (mode == activeMode && generationActive) {
        return;
    }

    stopAll();

    // Start new
    switch (mode) {
#if SOC_DAC_SUPPORTED
        case Mode::COSINE: startCosine(); break;
#endif
        case Mode::SQUARE: startSquare(); break;
        case Mode::PULSE:  startPulse(); break;
    }

    activeMode = mode;
    generationActive = true;
    setLed(true);
}

static void printHelp() {
    Serial.println("Commands:");
#if SOC_DAC_SUPPORTED
    Serial.println("  c - Cosine wave (DAC, GPIO 25, fixed 1 kHz)");
#endif
    Serial.println("  s - Square wave (LEDC, GPIO 4)");
    Serial.println("  p - Pulse (GPIO 5)");
    Serial.println("  i - Single impulse (GPIO 5)");
    Serial.println("  0-7 - Set frequency to 10^N Hz");
    Serial.println("  x - Stop output");
    Serial.println("  ? - Show this help");
}

void setup() {
    Serial.begin(115200);

#ifdef NATIVE_USB
    while(!Serial) {
        delay(50);
    }
#endif

    Serial.println();
    Serial.println("Function generator");

    // Setup LED indicator
    pinMode(BUILTIN_LED_PIN, BUILTIN_LED_PIN_MODE);
    setLed(false);

#if SOC_DAC_SUPPORTED
    // Setup cosine (DAC channel 0 = GPIO 25)
    dac_cosine_config_t cfg = {
        .chan_id=DAC_CHAN_0,
        .freq_hz=1000,
        .clk_src=DAC_COSINE_CLK_SRC_DEFAULT,
        .atten=DAC_COSINE_ATTEN_DB_0,
        .phase=DAC_COSINE_PHASE_0,
        .offset=0,
    };

    auto err = dac_cosine_new_channel(&cfg, &cosineHandle);
    if (err != ESP_OK) {
        Serial.printf("Failed to create cosine channel: %d\r\n", err);
        return;
    }
#endif

    // Setup square wave (LEDC on GPIO 4)
    applySquareFreq();

    stopSquare();

    Serial.println("Setup OK");
    printHelp();
}

void loop() {
    loopPulse();

    if (!Serial.available()) return;

    char ch = Serial.read();
    switch (ch) {
#if SOC_DAC_SUPPORTED
        case 'c':
        case 'C':
            switchMode(Mode::COSINE);
            Serial.println("→ Cosine wave (DAC)");
            break;
#endif
        case 's':
        case 'S':
            switchMode(Mode::SQUARE);
            Serial.println("→ Square wave (LEDC)");
            break;
        case 'p':
        case 'P':
            switchMode(Mode::PULSE);
            Serial.println("→ Pulse");
            break;
        case 'i':
        case 'I':
            stopAll();
            triggerImpulse();
            break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            uint32_t freq = 1;
            for (int i = 0; i < (ch - '0'); i++) freq *= 10;
            setFreq(freq);
            Serial.printf("→ Frequency: %" PRIu32 " Hz\r\n", freq);
            break;
        }
        case 'x':
        case 'X':
            stopAll();
            Serial.println("→ Stopped");
            break;
        case '?':
        case '\r':
        case '\n':
            printHelp();
            break;
        default:
            break;
    }
}
