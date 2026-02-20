#include <FastLED.h>

//                    GPIO      Dx
#define LED_PIN         14   // D5
#define POWER_CTRL_PIN  16   // D0
#define BUTTON_PIN_0    0    // D3 (Flash button)
#define BUTTON_PIN_1    13   // D7
#define BUTTON_PIN_2    12   // D6

#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
// #define NUM_LEDS    (8)

// #define NUM_LEDS    (8 * 32) // single panel 8x32 matrix
#define NUM_LEDS    (16 * 32) // double panel 16x32 matrix

#define BRIGHTNESS  10
#define BRIGHTNESS_MAX 100
#define FRAMES_PER_SECOND 20

bool gReverseDirection = false;

CRGB leds[NUM_LEDS];

CRGBPalette16 gPal;

// Fire2012 by Mark Kriegsman, July 2012
// as part of "Five Elements" shown here: http://youtu.be/knWiGsmgycY
////
// This basic one-dimensional 'fire' simulation works roughly as follows:
// There's a underlying array of 'heat' cells, that model the temperature
// at each point along the line.  Every cycle through the simulation,
// four steps are performed:
//  1) All cells cool down a little bit, losing heat to the air
//  2) The heat from each cell drifts 'up' and diffuses a little
//  3) Sometimes randomly new 'sparks' of heat are added at the bottom
//  4) The heat from each cell is rendered as a color into the leds array
//     The heat-to-color mapping uses a black-body radiation approximation.
//
// Temperature is in arbitrary units from 0 (cold black) to 255 (white hot).
//
// This simulation scales it self a bit depending on NUM_LEDS; it should look
// "OK" on anywhere from 20 to 100 LEDs without too much tweaking.
//
// I recommend running this simulation at anywhere from 30-100 frames per second,
// meaning an interframe delay of about 10-35 milliseconds.
//
// Looks best on a high-density LED setup (60+ pixels/meter).
//
//
// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (used in step 1 above), and SPARKING (used
// in step 3 above).
//
// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 55, suggested range 20-100
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120


void Fire2012WithPalette()
{
// Array of temperature readings at each simulation cell
  static uint8_t heat[NUM_LEDS];

  // Step 1.  Cool down every cell a little
    for( int i = 0; i < NUM_LEDS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }

    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= NUM_LEDS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }

    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
      // Scale the heat value from 0-255 down to 0-240
      // for best results with color palettes.
      uint8_t colorindex = scale8( heat[j], 240);
      CRGB color = ColorFromPalette( gPal, colorindex);
      int pixelnumber;
      if( gReverseDirection ) {
        pixelnumber = (NUM_LEDS-1) - j;
      } else {
        pixelnumber = j;
      }
      leds[pixelnumber] = color;
    }
}

const int panelWidth = 8; // zigzag width of the panel
const int panelHeight = 32; // height of the panel
const int panelLength = panelWidth * panelHeight; // total number of LEDs in the panel

// screen is two panels on a side
const int width = panelHeight; // width of the panel
const int height = panelWidth * 2; // height of two the panel

// Convert (x, y) coordinates to a pixel index in the zigzag pattern
int panelXYtoN(int x, int y) {
  if (y & 0x01) {
    // return y * panelWidth + panelWidth - 1 - x;
    return (y + 1) * panelWidth - 1 - x;
  } else {
    return y * panelWidth + x;
  }
}

int XYtoN(int x, int y) {
  if (x < 8) {
    // First panel top row
    return panelXYtoN(x, y);
  } else {
    // Second panel bottom row
    x -= 8; // Adjust x for the second panel
    return panelXYtoN(panelWidth - 1 - x, panelHeight - 1 - y) + panelLength;
  }
}

void RunningPixel() {
  static int currentPixel = 0;

  // Clear the previous pixel
  leds[currentPixel] = CRGB::Black;

  currentPixel = (currentPixel + 1) % NUM_LEDS;
  // Set the current pixel to a color
  // leds[currentPixel] = CRGB::Red;
  leds[currentPixel] = 0x101010;
}

void RunningPixelFullPanel() {
  static int x = 0;
  static int y = 0;

  leds[XYtoN(x, y)] = CRGB::Black;

  x += 1;

  // If we reach the end of the row, move to the next row
  if (x >= height) {
    x = 0;

    // Move to the next row
    y += 1;
    if (y >= width) {
      y = 0; // Reset to the first row
    }
  }

  auto color = 0x101010;

  if (x == 0) {
    color = CRGB::Red;
  }

  leds[XYtoN(x, y)] = color;
}

void Pong() {
  static int x = 0;
  static int y = 0;

  static int dx = 1;
  static int dy = 1;

  // Clear the previous pixel
  leds[XYtoN(x, y)] = CRGB::Black;

  // check if next position is out of bounds
  // If it is, reverse the direction
  if (((x + dx) >= height) || ((x + dx) < 0)) {
    dx = -dx; // Reverse direction on x-axis

    // occasionally change the y direction
    if (random8(10) < 2) {
      dy = -dy; // Randomly reverse y direction
    }
  }

  if (((y + dy) >= width) || ((y + dy) < 0)) {
    dy = -dy; // Reverse direction on y-axis

    // occasionally change the x direction
    if (random8(10) < 2) {
      dx = -dx; // Randomly reverse x direction
    }
  }

  // Update the position of the pixel
  x += dx;
  y += dy;

  // Set the current pixel to a color
  // leds[XYtoN(x, y)] = CRGB::Red;
  leds[XYtoN(x, y)] = 0x101010;
}


void RunningLine() {
  static int currentLine = 0;

  // Clear the previous line
  for (int i = 0; i < width; i++) {
    int pixelIndex = XYtoN(i, currentLine);
    leds[pixelIndex] = CRGB::Black;
  }

  currentLine = (currentLine + 1) % (NUM_LEDS / width);
  // Set the current line to a color
  for (int i = 0; i < width; i++) {
    int pixelIndex = XYtoN(i, currentLine);
    // leds[pixelIndex] = CRGB::Red;
    leds[pixelIndex] = 0x101010;
  }
}

// Fire2012 with programmable Color Palette
//
// This code is the same fire simulation as the original "Fire2012",
// but each heat cell's temperature is translated to color through a FastLED
// programmable color palette, instead of through the "HeatColor(...)" function.
//
// Four different static color palettes are provided here, plus one dynamic one.
//
// The three static ones are:
//   1. the FastLED built-in HeatColors_p -- this is the default, and it looks
//      pretty much exactly like the original Fire2012.
//
//  To use any of the other palettes below, just "uncomment" the corresponding code.
//
//   2. a gradient from black to red to yellow to white, which is
//      visually similar to the HeatColors_p, and helps to illustrate
//      what the 'heat colors' palette is actually doing,
//   3. a similar gradient, but in blue colors rather than red ones,
//      i.e. from black to blue to aqua to white, which results in
//      an "icy blue" fire effect,
//   4. a simplified three-step gradient, from black to red to white, just to show
//      that these gradients need not have four components; two or
//      three are possible, too, even if they don't look quite as nice for fire.
//
// The dynamic palette shows how you can change the basic 'hue' of the
// color palette every time through the loop, producing "rainbow fire".

void IRAM_ATTR handleButtonPress();
void readButtonState();

int currentBrightness = BRIGHTNESS;

void setup() {
  delay(1000); // sanity delay
  Serial.begin(115200);
  Serial.println("Fire2012 with Palette");
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( currentBrightness );

  // This first palette is the basic 'black body radiation' colors,
  // which run from black to red to bright yellow to white.
  // gPal = HeatColors_p;

  // These are other ways to set up the color palette for the 'fire'.
  // First, a gradient from black to red to yellow to white -- similar to HeatColors_p
  //   gPal = CRGBPalette16( CRGB::Black, CRGB::Red, CRGB::Yellow, CRGB::White);

  // Second, this palette is like the heat colors, but blue/aqua instead of red/yellow
  gPal = CRGBPalette16( CRGB::Black, CRGB::Blue, CRGB::Aqua,  CRGB::White);

  // Third, here's a simpler, three-step gradient, from black to red to white
  //   gPal = CRGBPalette16( CRGB::Black, CRGB::Red, CRGB::White);

  // LED Power control pin
  pinMode(POWER_CTRL_PIN, OUTPUT);
  digitalWrite(POWER_CTRL_PIN, LOW); //Disable power to LEDs initially
  delay(1000);
  digitalWrite(POWER_CTRL_PIN, HIGH); //Enable power to LEDs

  // Button pins
  pinMode(BUTTON_PIN_0, INPUT_PULLUP);
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_0), handleButtonPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_1), handleButtonPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), handleButtonPress, FALLING);

  readButtonState();
}

// Button
// volatile bool buttonPressed0 = false;
// volatile bool buttonPressed1 = false;
// volatile bool buttonPressed2 = false;

volatile  uint8_t buttonsState = 0;

volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 50; // 50ms debounce

// Interrupt Service Routine (ISR)
void IRAM_ATTR handleButtonPress() {
  unsigned long interruptTime = millis();

  // Debounce: ignore interrupts that occur too quickly
  if (interruptTime - lastInterruptTime > debounceDelay) {
    lastInterruptTime = interruptTime;

    // read the pin state to clear the interrupt (optional)
    // volatile read to ensure compiler does not optimize it away
    readButtonState();
  }
}

void readButtonState() {
    // Button inputs are pulled HIGH, pressed state is LOW

    buttonsState = ((digitalRead(BUTTON_PIN_0) ? 0 : 1) << 0) |
                   ((digitalRead(BUTTON_PIN_1) ? 0 : 1) << 1) |
                   ((digitalRead(BUTTON_PIN_2) ? 0 : 1) << 2);
}

bool powerEnabled = true;

void setLedPower(bool powerState) {
  powerEnabled = powerState;
  Serial.printf("LED Power: %s\n", powerEnabled ? "ON" : "OFF");
  digitalWrite(POWER_CTRL_PIN, powerEnabled ? HIGH : LOW);
}

void toggleLedPower() {
  setLedPower(!powerEnabled);
}

void adjustBrightness(int change) {
  currentBrightness += change;

  if (currentBrightness < 0) {
    currentBrightness = 0;
  } else if (currentBrightness > BRIGHTNESS_MAX) {
    currentBrightness = BRIGHTNESS_MAX;
  }

  if (currentBrightness == 0) {
    setLedPower(false); // Disable power to LEDs
  } else {
    setLedPower(true); // Enable power to LEDs
  }

  FastLED.setBrightness(currentBrightness);
  Serial.printf("Brightness set to: %d\n", currentBrightness);
}


int c = 0;

void loop()
{
  // Add entropy to random number generator; we use a lot of it.
  random16_add_entropy( random());

  // Fourth, the most sophisticated: this one sets up a new palette every
  // time through the loop, based on a hue that changes every time.
  // The palette is a gradient from black, to a dark color based on the hue,
  // to a light color based on the hue, to white.
  //

  if (currentBrightness > 0) {
    static uint8_t hue = 0;
    hue++;
    CRGB darkcolor  = CHSV(hue,255,192); // pure hue, three-quarters brightness
    CRGB lightcolor = CHSV(hue,128,255); // half 'whitened', full brightness
    gPal = CRGBPalette16( CRGB::Black, darkcolor, lightcolor, CRGB::White);

    Fire2012WithPalette(); // run simulation frame, using palette colors
    // RunningPixel(); // run a simple running pixel effect
    // RunningPixelFullPanel(); // run a simple running line effect
    // Pong();
  }

  FastLED.show(); // display this frame
  // FastLED.delay(1000 / FRAMES_PER_SECOND);

  // Check if button was pressed
  if (buttonsState & 0x01) { // Button 0 pressed
    // Reset
    buttonsState &= ~0x01; // Clear flag
    Serial.printf("%d: Button #0 pressed\r\n", c++);
    toggleLedPower();
  }

  if (buttonsState & 0x02) { // Button 1 pressed
    buttonsState &= ~0x02; // Clear flag
    Serial.printf("%d: Button #1 pressed\r\n", c++);

    // Serial.println("Button #1 pressed");
    adjustBrightness(5);
  }

  if (buttonsState & 0x04) { // Button 2 pressed
    buttonsState &= ~0x04; // Clear flag
    Serial.printf("%d: Button #2 pressed\r\n", c++);
    adjustBrightness(-5);
  }

  // Calculate frame time
  static unsigned long lastFrameTime = 0;
  unsigned long currentTime = millis();
  unsigned long frameTime = currentTime - lastFrameTime;
  lastFrameTime = currentTime;

  // Print frame time for debugging once every second
  static unsigned long lastDebugTime = 0;
  if (currentTime - lastDebugTime >= 1000) {
    lastDebugTime = currentTime;
    Serial.printf("FPS: %d (Frame Time: %lu ms)\r\n", 1000 / frameTime, frameTime);
  }
}