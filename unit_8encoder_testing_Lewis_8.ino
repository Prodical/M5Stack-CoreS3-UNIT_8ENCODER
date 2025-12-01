/*
 * M5Stack CoreS3 UNIT_8ENCODER Testing Sketch
 * 
 * This sketch provides a visual interface for the 8-encoder unit with:
 * - Real-time encoder value display
 * - Musical scale visualization on keys
 * - Interactive encoder and button controls
 * - Double-buffered sprite rendering for smooth updates
 * 
 * Physical Controls:
 * - Encoders 1-5: General purpose rotary inputs
 * - Encoder 6: Octave selection (wraps -1 to 9)
 * - Encoder 7: Root note selection (12 chromatic notes)
 * - Encoder 8: Scale selection (9 scales)
 * - Button Hold (1s): Activate/lock encoder
 * - Button Press (on active encoder): Deactivate
 * - Toggle Switch: Global enable/disable
 */

//========================================================================
// LIBRARIES
//========================================================================
#include <M5Unified.h>
#include <M5GFX.h>
#include "UNIT_8ENCODER.h"
#include "ScaleManager.h"              // Chris Ball's scale manager library (tweaked)
#include "DIN_Condensed_Bold30pt7b.h"  // Custom display font
#include <esp_task_wdt.h>
#include <map>

//========================================================================
// DISPLAY & GRAPHICS OBJECTS
//========================================================================
M5GFX display;
M5Canvas infoSprite(&display);        // Info area sprite
M5Canvas keysSprite(&display);        // Keys display sprite
M5Canvas encsSprite(&display);        // Encoders display sprite
M5Canvas infoSpriteBuffer(&display);  // Double buffer for info
M5Canvas keysSpriteBuffer(&display);  // Double buffer for keys
M5Canvas encsSpriteBuffer(&display);  // Double buffer for encoders

//========================================================================
// COLOR DEFINITIONS
//========================================================================
#define DARKGREY 0x39c7

const uint16_t COLOR_TRANSPARENT = 0x0001;
const uint16_t COLOR_WHITE = 0xffff;
const uint16_t COLOR_BLACK = 0x0000;
const uint16_t COLOR_DARKGREY = 0x39c7;
const uint16_t COLOR_DARKGRAY = 0x7bef;
const uint16_t COLOR_LIGHTGRAY = 0xd69a;
const uint16_t COLOR_RED = 0xf800;
const uint16_t COLOR_GREEN = 0x7e0;
const uint16_t COLOR_BLUE = 0x00ff;
const uint16_t COLOR_ORANGE = 0xfd20;

// LED color constants for encoder hardware LEDs
const uint32_t LED_COLOR_OFF = 0x000000;
const uint32_t LED_COLOR_GREEN = 0x00FF00;
const uint32_t LED_COLOR_ORANGE = 0xFF8000;

//========================================================================
// DISPLAY CONFIGURATION CONSTANTS
//========================================================================
const uint16_t DISPLAY_WIDTH = 320;
const uint16_t DISPLAY_HEIGHT = 240;
const uint16_t INFO_SPRITE_HEIGHT = 50;
const uint16_t KEYS_SPRITE_HEIGHT = 150;
const uint16_t ENCS_SPRITE_HEIGHT = 40;

//========================================================================
// ENCODER & HARDWARE CONFIGURATION
//========================================================================
UNIT_8ENCODER sensor;

const uint8_t NUM_ENCODERS = 8;
const uint8_t NUM_KEYS = 12;
const int8_t ENCODER_MAX_VALUE = 60;
const int8_t ENCODER_MIN_VALUE = -60;

// Encoder state tracking arrays
int32_t encoderValue[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int32_t encoderValueLast[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t btn_status[8] = {0, 0, 0, 0, 0, 0, 0, 0};
bool switch_status = false;

//========================================================================
// TIMING & UPDATE CONFIGURATION
//========================================================================
const unsigned long UPDATE_INTERVAL = 10;     // 100fps for smooth updates
const unsigned long STATUS_INTERVAL = 5000;   // 5 seconds
const unsigned long BUTTON_HOLD_TIME = 1000;  // 1 second hold time

//========================================================================
// I2C ERROR RECOVERY CONFIGURATION
//========================================================================
const uint8_t I2C_MAX_RETRIES = 3;
const uint16_t I2C_RETRY_DELAY = 100;   // ms
const uint16_t I2C_ERROR_DELAY = 1000;  // ms

//========================================================================
// MUSICAL SCALE CONFIGURATION
//========================================================================
ScaleManager scalemanager;

const int8_t FUNDAMENTAL_DEFAULT = 60;
const int8_t OCTAVE_DEFAULT = 4;
const int8_t SCALE_DEFAULT = 0;

// Scale state variables
int8_t fundamentalDefault = 60;
int8_t fundamental = 60;
int8_t fundamentalLast = 60;
int8_t octaveDefault = 4;
int8_t octave = 4;
int8_t octaveLast = 4;
int8_t octaveDif = 0;
int8_t notesDif = 0;
int8_t scaleNo = 0;
int8_t scaleNoteNo[7] = {0, 0, 0, 0, 0, 0, 0};
int8_t scaleIntervals[7] = {0, 0, 0, 0, 0, 0, 0};
bool updateScale = false;

// Cached scale information for display
String cachedScaleName = "";
String cachedFundamentalName = "";
String cachedRootName = "";
bool scaleCacheNeedsUpdate = true;

// Interval formulas for each scale (displayed in info area)
String intvlFormula[9] = {
  "W-W-H-W-W-W-H", "W-H-W-W-H-W-W", "W-H-W-W-W-H-W", "W-H-W-W-H-3H-H",
  "W-H-W-W-W-H-W", "H-W-W-W-H-W-W", "W-W-W-H-W-W-H", "W-W-H-W-W-H-W", "H-W-W-H-W-W-W"
};

// Note names stored in PROGMEM to save RAM
const char noteNames[] PROGMEM = "C\0C#\0D\0D#\0E\0F\0F#\0G\0G#\0A\0A#\0B";

//========================================================================
// KEY IDENTIFICATION
//========================================================================
// Identifies white keys (1) vs black keys (0) in chromatic scale starting from C
bool whiteKeys[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};

//========================================================================
// ENCODER STRUCTURE & STATE
//========================================================================
// Defines visual representation and state of on-screen encoder widgets
struct Encoders {
  int x;                        // X position on screen
  int y;                        // Y position on screen
  int r;                        // Radius
  int encValue;                 // Current encoder value
  uint16_t colourIdle;          // Color when idle
  uint16_t colourActive;        // Color when active
  int8_t encId = -1;           // Encoder ID
  bool encPressed = false;      // Button press state
  bool isActive = false;        // Active/locked state
  bool shortPress = false;      // Short press indicator state
  String encNoteName = "";      // Note name (if applicable)
  int8_t encScaleNoteNo;        // Scale note number
  int8_t encOnceOnlySendNoteOn = 0;   // MIDI note on flag
  int8_t encOnceOnlySendNoteOff = 0;  // MIDI note off flag
  int8_t onceOnlyEncDraw = 0;         // Draw once flag

  // Clear encoder display area
  void clearEncs(int8_t encNo) {
    encsSpriteBuffer.setColor(2);
    encsSpriteBuffer.fillCircle(x - 2, y - 2, r + 4, 2);
  }

  // Draw encoder with current state (idle/active/shortPress)
  void drawEncs(int8_t encNo) {
    // Determine circle color based on state
    uint16_t circleColor;
    if (this->isActive) {
      circleColor = 7;  // Green for active/locked
    } else if (this->shortPress) {
      circleColor = 9;  // Orange for short press
    } else {
      circleColor = 3;  // Grey for idle
    }
    
    // Draw filled circle
    encsSpriteBuffer.setColor(circleColor);
    encsSpriteBuffer.fillCircle(x, y, r, circleColor);

    // Draw arc indicator starting from bottom (90 degrees)
    uint16_t arcColor = (circleColor == 3) ? 1 : 2;  // White on grey, black otherwise
    encsSpriteBuffer.setColor(arcColor);
    
    float startAngle = 90 + (this->encValue * 6) - 5;
    float endAngle = 90 + (this->encValue * 6) + 5;
    int innerRadius = r - (arcColor == 2 ? 7 : 5);
    int outerRadius = r - (arcColor == 2 ? 2 : 4);
    
    encsSpriteBuffer.drawArc(x, y, innerRadius, outerRadius, startAngle, endAngle, arcColor);
  }
};

static constexpr uint8_t nosEncs = 8;
static Encoders enc[nosEncs];
bool holdOn = false;

//========================================================================
// KEY/PIANO STRUCTURE & STATE
//========================================================================
// Defines visual representation of piano keys for scale display
struct Keys {
  int x;                        // X position
  int y;                        // Y position
  int w;                        // Width
  int h;                        // Height
  uint16_t colourIdle;          // Idle color
  uint16_t colourActive;        // Active color
  int8_t kId = -1;             // Key ID
  int8_t keyNo = -1;           // Key number (0-11)
  String keyNoteName = "";      // Note name (C, C#, D, etc.)
  uint8_t onceOnlyKeyDraw = 0;  // Draw once flag
  bool inScale = false;         // Is this key in current scale?
  bool isFundamental = false;   // Is this the root note?
  uint8_t octaveNo = -1;       // Octave number
  String octaveS = "";          // Octave string for display
  uint8_t interval;             // Interval from previous scale note
  String intervalS;             // Interval string for display

  // Clear key display area
  void clearKeys(int8_t keyNo) {
    keysSpriteBuffer.setColor(2);
    keysSpriteBuffer.fillRoundRect(x - 1, y - 1, w + 2, h + 2, 2);
  }

  // Draw key with note name, octave, and interval
  void drawKeys(int8_t keyNo) {
    // Draw key outline
    keysSpriteBuffer.setColor(this->colourIdle);
    if (whiteKeys[keyNo]) {
      keysSpriteBuffer.drawRoundRect(x, y, w, h, 5);
    } else {
      // Thicker outline for black keys
      keysSpriteBuffer.drawRoundRect(x, y, w, h, 5);
      keysSpriteBuffer.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5);
      keysSpriteBuffer.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5);
    }

    // Draw note name
    keysSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
    keysSpriteBuffer.setTextDatum(ML_DATUM);
    keysSpriteBuffer.setTextSize(0.45);
    
    if (whiteKeys[keyNo]) {
      keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 35);
    } else {
      keysSpriteBuffer.setCursor(this->x + w / 2 - 8, this->y + h - 35);
    }
    
    // Color: green for root, white for in-scale, grey for out-of-scale
    if (this->inScale == true) {
      if (this->isFundamental == true) {
        keysSpriteBuffer.setTextColor(7);  // Green for root
      } else {
        keysSpriteBuffer.setTextColor(1);  // White for scale notes
      }
    } else {
      keysSpriteBuffer.setTextColor(3);    // Grey for non-scale notes
    }
    keysSpriteBuffer.printf(this->keyNoteName.c_str());

    // Draw octave number
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 15);
    
    if (this->inScale == true) {
      if (this->isFundamental == true) {
        keysSpriteBuffer.setTextColor(7);
      } else {
        keysSpriteBuffer.setTextColor(1);
      }
      keysSpriteBuffer.printf(this->octaveS.c_str());
    } else {
      keysSpriteBuffer.setTextColor(3);
      keysSpriteBuffer.printf("-");
    }

    // Draw interval number (semitones from previous scale note)
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + 15);
    keysSpriteBuffer.setTextColor(1);
    keysSpriteBuffer.printf(this->intervalS.c_str());
  }
};

static constexpr uint8_t nosKeys = 12;
static Keys key[nosKeys];

//========================================================================
// TEXT WIDTH CACHE (Optimization)
//========================================================================
// Caches text width calculations to avoid repeated expensive operations
struct TextWidthCache {
  std::map<String, int16_t> cache;

  int16_t getWidth(const String& text) {
    auto it = cache.find(text);
    if (it != cache.end()) {
      return it->second;
    }
    int16_t width = M5.Lcd.textWidth(text.c_str());
    cache[text] = width;
    return width;
  }

  void clear() {
    cache.clear();
  }
};

TextWidthCache textWidthCache;

//========================================================================
// BUTTON STATE MANAGEMENT
//========================================================================
// State machine for button press/hold/release detection
enum ButtonState {
  BUTTON_RELEASED = 0,
  BUTTON_PRESSED = 1,
  BUTTON_HOLDING = 2
};

struct ButtonStateManager {
  ButtonState state = BUTTON_RELEASED;
  unsigned long pressStartTime = 0;
  bool processedHold = false;
  bool waitingForNextPress = false;
};

ButtonStateManager buttonStates[8];

//========================================================================
// ENCODER TRACKING STATE
//========================================================================
// Global tracking variables for encoder state changes
static int8_t currentOctave = 4;
static int32_t lastEncoderValues[8] = {0};
static int32_t encoderAccumulator[8] = {0};
static bool encoderInitialized[8] = {false};
static int32_t encoder6LastValue = 0;
static int32_t encoder7LastValue = 0;
static int32_t encoder8LastValue = 0;
static unsigned long buttonPressStartTime[8] = {0};
static bool buttonHoldProcessed[8] = {false};
static bool waitingForNextPress[8] = {false};
static uint8_t lastButtonStates[8] = {1};
static bool currentSwitchState = false;

//========================================================================
// DEBOUNCING CONFIGURATION
//========================================================================
// Encoder debouncing to reduce jitter and false readings
static const uint8_t DEBOUNCE_COUNT = 3;
static const uint16_t DEBOUNCE_TIME = 5;  // milliseconds
int32_t debounceValues[8][DEBOUNCE_COUNT] = {0};
uint8_t debounceIndex[8] = {0};
unsigned long lastDebounceTime[8] = {0};
int32_t lastStableValue[8] = {0};
bool valueChanged[8] = {false};

//========================================================================
// SCALE CACHE STRUCTURE
//========================================================================
// Caches scale information to avoid repeated lookups
struct ScaleCache {
  int8_t scaleNoteNo[7];
  int8_t scaleIntervals[7];
  bool inScale[12];
  bool isFundamental[12];
  String scaleName;
  String rootName;
  int8_t fundamental;
  int8_t scaleNo;
  bool needsUpdate;

  ScaleCache() : needsUpdate(true) {
    for (int i = 0; i < 7; i++) {
      scaleNoteNo[i] = 0;
      scaleIntervals[i] = 0;
    }
    for (int i = 0; i < 12; i++) {
      inScale[i] = false;
      isFundamental[i] = false;
    }
    fundamental = 0;
    scaleNo = 0;
  }

  void clear() {
    needsUpdate = true;
  }
};

ScaleCache scaleCache;

//========================================================================
// I2C MANAGER CLASS
//========================================================================
// Handles I2C communication with error recovery
class I2CManager {
private:
  static const uint8_t MAX_RETRIES = 3;
  static const uint16_t RETRY_DELAY = 100;
  static const uint16_t ERROR_DELAY = 1000;
  uint8_t errorCount = 0;
  static const uint8_t MAX_ERRORS = 10;

public:
  // Read encoder value with retry logic
  bool readEncoderValue(uint8_t encoderIndex, int32_t& value, uint8_t& buttonState) {
    uint8_t retries = 0;

    while (retries < MAX_RETRIES) {
      try {
        buttonState = sensor.getButtonStatus(encoderIndex);
        value = sensor.getEncoderValue(encoderIndex);
        errorCount = 0;  // Success - reset error count
        return true;
      } catch (...) {
        retries++;
        errorCount++;

        if (retries >= MAX_RETRIES) {
          Serial.print("Failed to read encoder ");
          Serial.print(encoderIndex);
          Serial.print(" after ");
          Serial.print(MAX_RETRIES);
          Serial.println(" attempts");
          return false;
        }
        delay(RETRY_DELAY);
      }
    }
    return false;
  }

  // Attempt I2C bus recovery
  bool handleErrors() {
    if (errorCount > MAX_ERRORS) {
      Serial.println("Too many I2C errors, attempting recovery...");

      // Reset I2C bus
      Wire.end();
      delay(ERROR_DELAY);
      Wire.begin();
      Wire.setClock(100000);
      delay(RETRY_DELAY);

      // Reinitialize sensor
      if (sensor.begin(&Wire, ENCODER_ADDR, G2, G1, 100000UL)) {
        Wire.setClock(400000);
        errorCount = 0;
        Serial.println("I2C recovery successful");

        // Reset all encoders
        for (int i = 0; i < NUM_ENCODERS; i++) {
          sensor.resetCounter(i);
          delay(50);
        }
        return true;
      } else {
        Serial.println("I2C recovery failed");
        return false;
      }
    }
    return true;
  }

  uint8_t getErrorCount() const { return errorCount; }
  void resetErrorCount() { errorCount = 0; }
};

I2CManager i2cManager;

//========================================================================
// ENCODER DEBOUNCER CLASS
//========================================================================
// Reduces encoder jitter through multi-sample averaging
class EncoderDebouncer {
private:
  static const uint8_t DEBOUNCE_SAMPLES = 5;
  static const uint16_t DEBOUNCE_THRESHOLD = 2;

  struct EncoderData {
    int32_t samples[DEBOUNCE_SAMPLES];
    uint8_t index = 0;
    int32_t lastStableValue = 0;
    bool initialized = false;
  };

  EncoderData encoders[8];

public:
  bool getStableValue(uint8_t encoderIndex, int32_t rawValue, int32_t& stableValue) {
    if (encoderIndex >= 8) return false;

    EncoderData& data = encoders[encoderIndex];

    // Initialize on first read
    if (!data.initialized) {
      for (int i = 0; i < DEBOUNCE_SAMPLES; i++) {
        data.samples[i] = rawValue;
      }
      data.lastStableValue = rawValue;
      data.index = 0;
      data.initialized = true;
      stableValue = rawValue;
      return true;
    }

    // Add new sample
    data.samples[data.index] = rawValue;
    data.index = (data.index + 1) % DEBOUNCE_SAMPLES;

    // Check for consistent readings
    int32_t consistentValue = checkConsistency(data.samples, rawValue);

    if (consistentValue != data.lastStableValue) {
      // Handle wrap-around at encoder limits
      if (consistentValue >= ENCODER_MAX_VALUE) {
        consistentValue = 0;
      } else if (consistentValue <= ENCODER_MIN_VALUE) {
        consistentValue = 0;
      }

      data.lastStableValue = consistentValue;
      stableValue = consistentValue;
      return true;
    }

    stableValue = data.lastStableValue;
    return false;  // No change
  }

private:
  int32_t checkConsistency(int32_t samples[], int32_t latestValue) {
    uint8_t matchCount = 0;
    for (int i = 0; i < DEBOUNCE_SAMPLES; i++) {
      if (samples[i] == latestValue) {
        matchCount++;
      }
    }

    if (matchCount >= DEBOUNCE_THRESHOLD) {
      return latestValue;
    }
    return latestValue;  // Allow some jitter
  }
};

EncoderDebouncer encoderDebouncer;

//========================================================================
// HELPER FUNCTIONS - DECLARATIONS
//========================================================================
// Forward declarations of helper functions
bool initializeSprites();
void drawInfoArea();
void drawSwitchIndicator(bool isOn);
void setScaleIntervals();
void updateScaleCache();
void updateOctaveNumbers();
String getNoteName(uint8_t index);
bool processEncoderSteps(int32_t currentValue, int32_t& lastValue, int32_t& accumulator);
void updateSpritesForEncoderChange(bool fullRedraw = false);
bool updateButtonState(uint8_t buttonIndex, bool isPressed, unsigned long currentTime);
bool isValueStable(int32_t newValue, uint8_t encoderIndex);

//========================================================================
// SETUP
//========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting setup...");

  // Initialize M5 hardware
  M5.begin();
  M5.Power.begin();
  M5.In_I2C.release();
  delay(100);

  // Initialize I2C with conservative speed
  Wire.begin();
  Wire.setClock(100000);  // Start at 100kHz
  delay(100);

  // Initialize UNIT_8ENCODER with retry mechanism
  Serial.println("Initializing UNIT_8ENCODER...");
  bool encoderInitialized = false;
  for (int retry = 0; retry < 3; retry++) {
    if (sensor.begin(&Wire, ENCODER_ADDR, G2, G1, 100000UL)) {
      encoderInitialized = true;
      Serial.println("Encoder initialization successful");
      break;
    }
    Serial.print("Retry ");
    Serial.print(retry + 1);
    Serial.println(" failed, waiting before next attempt...");
    delay(500);
  }

  if (!encoderInitialized) {
    Serial.println("Failed to initialize UNIT_8ENCODER after 3 attempts!");
    delay(1000);
    return;
  }

  // Gradually increase I2C speed for optimal performance
  Wire.setClock(200000);  // 200kHz
  delay(100);
  Wire.setClock(400000);  // 400kHz
  delay(100);

  // Initialize scale manager with default scale (Major/Ionian) and root (C)
  scalemanager.init();
  scalemanager.setScale(0);        // 0 = Major/Ionian
  scalemanager.setFundamental(0);  // 0 = C
  fundamental = 0;
  scaleNo = 0;

  // Cache initial scale information
  cachedScaleName = scalemanager.getScaleName();
  cachedFundamentalName = scalemanager.getFundamentalName();
  cachedRootName = cachedFundamentalName.substring(0, cachedFundamentalName.length() - 1);
  scaleCacheNeedsUpdate = false;

  Serial.print("Initial scale info - Root: ");
  Serial.print(cachedRootName);
  Serial.print(", Scale: ");
  Serial.println(cachedScaleName);

  // Initialize display
  display.init();
  display.setResolution(320, 240, 30);
  display.setEpdMode(epd_mode_t::epd_text);
  display.setFont(&DIN_Condensed_Bold30pt7b);
  display.startWrite();

  // Initialize sprites (double-buffered rendering)
  if (!initializeSprites()) {
    Serial.println("Sprite initialization failed!");
    delay(500);
    return;
  }

  // Initialize encoder display states
  for (int i = 0; i < NUM_ENCODERS; i++) {
    enc[i].r = 16;
    enc[i].x = 16 + i * 38;
    enc[i].y = 16;
    enc[i].isActive = false;
    enc[i].shortPress = false;
    enc[i].colourIdle = 3;
    enc[i].colourActive = 7;
    sensor.setLEDColor(i, 0x000000);  // Turn off LEDs
    delay(10);
  }

  // Initialize key/piano display states
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].w = 23;
    key[i].h = 120;
    key[i].keyNo = i;
    key[i].keyNoteName = getNoteName(i);
    key[i].colourActive = 1;
    key[i].x = i * 27;
    
    if (whiteKeys[i]) {
      key[i].y = 20;
      key[i].colourIdle = 5;
    } else {
      key[i].y = 10;
      key[i].colourIdle = 3;
    }
    
    key[i].octaveNo = currentOctave;
    key[i].octaveS = String(currentOctave);
  }

  // Set initial scale state (C Major)
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].inScale = false;
    key[i].isFundamental = false;
  }
  key[0].isFundamental = true;  // C is root
  key[0].inScale = true;        // C
  key[2].inScale = true;        // D
  key[4].inScale = true;        // E
  key[5].inScale = true;        // F
  key[7].inScale = true;        // G
  key[9].inScale = true;        // A
  key[11].inScale = true;       // B

  // Calculate and display initial scale intervals
  setScaleIntervals();

  // Clear all buffer sprites
  infoSpriteBuffer.fillSprite(0);
  keysSpriteBuffer.fillSprite(0);
  encsSpriteBuffer.fillSprite(0);

  // Draw initial display state
  drawInfoArea();
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].clearKeys(i);
    key[i].drawKeys(i);
  }
  for (int i = 0; i < NUM_ENCODERS; i++) {
    enc[i].clearEncs(i);
    enc[i].drawEncs(i);
  }
  drawSwitchIndicator(false);

  // Push to display
  infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
  keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
  encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);
  infoSprite.pushSprite(0, 0);
  keysSprite.pushSprite(0, 50);
  encsSprite.pushSprite(0, 200);

  Serial.println("Setup complete");
}

//========================================================================
// MAIN LOOP
//========================================================================
void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastStatusCheck = 0;
  static bool lastSwitchState = false;
  static int32_t lastEncoderValues[8] = {0};
  static bool buttonWasPressed[8] = {false};

  // Rate limiting: only update at specified interval
  if (millis() - lastUpdate < UPDATE_INTERVAL) {
    return;
  }
  lastUpdate = millis();

  M5.update();

  // ==================================================================
  // READ SWITCH STATE
  // ==================================================================
  bool currentSwitchState = false;
  try {
    currentSwitchState = sensor.getSwitchStatus();
    i2cManager.resetErrorCount();
  } catch (...) {
    Serial.println("Error reading switch status");
    if (!i2cManager.handleErrors()) {
      return;
    }
  }

  // Handle switch state change
  if (currentSwitchState != lastSwitchState) {
    // Update LED 9 based on switch state
    try {
      sensor.setLEDColor(8, currentSwitchState ? LED_COLOR_GREEN : LED_COLOR_OFF);
    } catch (...) {
      Serial.println("Error setting LED color");
      return;
    }
    lastSwitchState = currentSwitchState;

    // Redraw entire display for switch state change
    infoSpriteBuffer.fillSprite(0);
    keysSpriteBuffer.fillSprite(0);
    encsSpriteBuffer.fillSprite(0);

    drawInfoArea();

    for (int i = 0; i < NUM_KEYS; i++) {
      key[i].clearKeys(i);
      key[i].drawKeys(i);
    }

    for (int i = 0; i < NUM_ENCODERS; i++) {
      enc[i].clearEncs(i);
      enc[i].drawEncs(i);
    }

    drawSwitchIndicator(currentSwitchState);

    infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
    keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
    encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);

    infoSprite.pushSprite(0, 0);
    keysSprite.pushSprite(0, 50);
    encsSprite.pushSprite(0, 200);

    Serial.print("Switch state changed to: ");
    Serial.println(currentSwitchState ? "ON" : "OFF");
  }

  // ==================================================================
  // READ ENCODERS AND BUTTONS
  // ==================================================================
  bool encoderChanged = false;
  
  for (int i = 0; i < NUM_ENCODERS; i++) {
    int32_t currentValue = 0;
    uint8_t rawButtonState = 1;  // Default to not pressed (pulled up)

    // Read encoder value with I2C error handling
    if (!i2cManager.readEncoderValue(i, currentValue, rawButtonState)) {
      if (!i2cManager.handleErrors()) {
        return;
      }
      continue;
    }

    Serial.print("Encoder ");
    Serial.print(i);
    Serial.print(" rawButtonState: ");
    Serial.println(rawButtonState);

    bool buttonPressed = (rawButtonState == 0);  // Active low

    // Use raw value (bypassing debouncing for testing)
    int32_t stableValue = currentValue;
    enc[i].encValue = stableValue;  // Update display value

    // ==================================================================
    // HANDLE ENCODER VALUE CHANGES
    // ==================================================================
    if (stableValue != lastEncoderValues[i]) {
      encoderChanged = true;
      lastEncoderValues[i] = stableValue;

      // Encoders 6, 7, 8 (indices 5, 6, 7) control musical parameters
      if (i == 5 || i == 6 || i == 7) {
        Serial.print("Processing parameter encoder ");
        Serial.println(i);

        // ---------------------------------------------------------------
        // ENCODER 7 (index 6): ROOT NOTE SELECTION
        // ---------------------------------------------------------------
        if (i == 6) {
          if (!encoderInitialized[i]) {
            encoder7LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 7 initialized");
          } else {
            int32_t delta = 0;
            if (stableValue != encoder7LastValue) {
              delta = (stableValue > encoder7LastValue) ? 1 : -1;
              Serial.print("Root encoder delta: ");
              Serial.println(delta);
            }

            if (delta != 0) {
              static int32_t stepAccumulator = 0;
              stepAccumulator += delta;

              // Require 2 steps before changing (reduces sensitivity)
              if (abs(stepAccumulator) >= 2) {
                int fundamentalChange = stepAccumulator / 2;
                stepAccumulator = 0;
                Serial.print("Root change triggered: ");
                Serial.println(fundamentalChange);
                int newFundamental = fundamental + fundamentalChange;

                // Wrap at boundaries (0-11 chromatic notes)
                if (newFundamental > 11) {
                  newFundamental = 0;
                  Serial.println("Wrapping from 11 to 0");
                } else if (newFundamental < 0) {
                  newFundamental = 11;
                  Serial.println("Wrapping from 0 to 11");
                }

                fundamental = newFundamental;
                scalemanager.setFundamental(fundamental);
                scaleCacheNeedsUpdate = true;
                updateScaleCache();

                // Reset fundamental flags
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].isFundamental = false;
                }
                key[fundamental].isFundamental = true;

                // Reset scale flags
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].inScale = false;
                }

                // Get fresh scale notes
                for (int j = 0; j < 7; j++) {
                  scaleNoteNo[j] = scalemanager.getScaleNote(j);
                  if (scaleNoteNo[j] < 12) {
                    key[scaleNoteNo[j]].inScale = true;
                  } else {
                    key[scaleNoteNo[j] - 12].inScale = true;
                  }
                }

                setScaleIntervals();
                updateOctaveNumbers();
                encoderChanged = true;
              }
            }
            encoder7LastValue = stableValue;
          }
        }
        // ---------------------------------------------------------------
        // ENCODER 6 (index 5): OCTAVE SELECTION
        // ---------------------------------------------------------------
        else if (i == 5) {
          if (!encoderInitialized[i]) {
            encoder6LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 6 initialized");
          } else {
            int32_t delta = 0;
            if (stableValue != encoder6LastValue) {
              delta = (stableValue > encoder6LastValue) ? 1 : -1;
              Serial.print("Octave encoder delta: ");
              Serial.println(delta);
            }

            if (delta != 0) {
              static int32_t stepAccumulator = 0;
              stepAccumulator += delta;

              if (abs(stepAccumulator) >= 2) {
                int octaveChange = stepAccumulator / 2;
                stepAccumulator = 0;
                Serial.print("Octave change triggered: ");
                Serial.println(octaveChange);
                int newOctave = currentOctave + octaveChange;

                // Wrap at boundaries (-1 to 9)
                if (newOctave > 9) {
                  newOctave = -1;
                  Serial.println("Wrapping from 9 to -1");
                } else if (newOctave < -1) {
                  newOctave = 9;
                  Serial.println("Wrapping from -1 to 9");
                }

                currentOctave = newOctave;

                // Update octave for all keys in scale
                for (int j = 0; j < NUM_KEYS; j++) {
                  if (key[j].inScale) {
                    key[j].octaveNo = currentOctave;
                    key[j].octaveS = String(currentOctave);
                  }
                }

                encoderChanged = true;
              }
            }
            encoder6LastValue = stableValue;
          }
        }
        // ---------------------------------------------------------------
        // ENCODER 8 (index 7): SCALE SELECTION
        // ---------------------------------------------------------------
        else if (i == 7) {
          if (!encoderInitialized[i]) {
            encoder8LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 8 initialized");
          } else {
            int32_t delta = 0;
            if (stableValue != encoder8LastValue) {
              delta = (stableValue > encoder8LastValue) ? 1 : -1;
              Serial.print("Scale encoder delta: ");
              Serial.println(delta);
            }

            if (delta != 0) {
              static int32_t stepAccumulator = 0;
              stepAccumulator += delta;

              if (abs(stepAccumulator) >= 2) {
                int scaleChange = stepAccumulator / 2;
                stepAccumulator = 0;
                Serial.print("Scale change triggered: ");
                Serial.println(scaleChange);
                int newScale = scaleNo + scaleChange;

                // Wrap at boundaries (0-8 scales)
                if (newScale > 8) {
                  newScale = 0;
                  Serial.println("Wrapping from 8 to 0");
                } else if (newScale < 0) {
                  newScale = 8;
                  Serial.println("Wrapping from 0 to 8");
                }

                scaleNo = newScale;
                scalemanager.setScale(scaleNo);
                scaleCacheNeedsUpdate = true;
                updateScaleCache();

                // Reset scale flags
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].inScale = false;
                }

                // Set scale flags from scale manager
                for (int j = 0; j < 7; j++) {
                  int scaleNote = scalemanager.getScaleNote(j);
                  if (scaleNote < 12) {
                    key[scaleNote].inScale = true;
                  } else {
                    key[scaleNote - 12].inScale = true;
                  }
                }

                setScaleIntervals();
                encoderChanged = true;
              }
            }
            encoder8LastValue = stableValue;
          }
        }
      }
    }

    // ==================================================================
    // HANDLE BUTTON PRESS/HOLD/RELEASE
    // ==================================================================
    
    // Button press detected
    if (buttonPressed && !buttonWasPressed[i]) {
      if (enc[i].isActive && waitingForNextPress[i]) {
        // Pressing active encoder deactivates it
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" button pressed - deactivating");
        enc[i].isActive = false;
        enc[i].shortPress = false;
        waitingForNextPress[i] = false;
        try {
          sensor.setLEDColor(i, LED_COLOR_OFF);
        } catch (...) {
          Serial.println("Error setting LED color");
        }
        encoderChanged = true;
      } else if (!enc[i].isActive) {
        // New press on inactive encoder - start timing for hold
        buttonPressStartTime[i] = millis();
        buttonHoldProcessed[i] = false;
        enc[i].shortPress = true;  // Show orange indicator
        try {
          sensor.setLEDColor(i, LED_COLOR_ORANGE);
        } catch (...) {
          Serial.println("Error setting LED color");
        }
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" button pressed - waiting for hold");
        encoderChanged = true;
      }
    }
    // Button released
    else if (!buttonPressed && buttonWasPressed[i]) {
      if (!enc[i].isActive && !buttonHoldProcessed[i]) {
        // Short press released - turn off orange indicator
        enc[i].shortPress = false;
        try {
          sensor.setLEDColor(i, LED_COLOR_OFF);
        } catch (...) {
          Serial.println("Error setting LED color");
        }
        encoderChanged = true;
      }
    }
    // Button held (check for 1 second hold time)
    else if (buttonPressed && buttonWasPressed[i] && !buttonHoldProcessed[i] && !enc[i].isActive) {
      unsigned long currentTime = millis();
      if (currentTime - buttonPressStartTime[i] >= BUTTON_HOLD_TIME) {
        // Hold time reached - activate encoder
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" button held for 1 second - activating");
        enc[i].isActive = true;
        enc[i].shortPress = false;
        waitingForNextPress[i] = true;
        try {
          sensor.setLEDColor(i, LED_COLOR_GREEN);
        } catch (...) {
          Serial.println("Error setting LED color");
        }
        encoderChanged = true;
        buttonHoldProcessed[i] = true;
      }
    }

    buttonWasPressed[i] = buttonPressed;
  }

  // ==================================================================
  // UPDATE DISPLAY IF CHANGES OCCURRED
  // ==================================================================
  if (encoderChanged) {
    infoSpriteBuffer.fillSprite(0);
    keysSpriteBuffer.fillSprite(0);
    encsSpriteBuffer.fillSprite(0);

    drawInfoArea();

    for (int i = 0; i < NUM_KEYS; i++) {
      key[i].onceOnlyKeyDraw = 0;
      key[i].clearKeys(i);
      key[i].drawKeys(i);
    }

    for (int i = 0; i < NUM_ENCODERS; i++) {
      enc[i].onceOnlyEncDraw = 0;
      enc[i].clearEncs(i);
      enc[i].drawEncs(i);
    }

    drawSwitchIndicator(currentSwitchState);

    infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
    keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
    encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);

    infoSprite.pushSprite(0, 0);
    keysSprite.pushSprite(0, 50);
    encsSprite.pushSprite(0, 200);
  }
}

//========================================================================
// HELPER FUNCTIONS - IMPLEMENTATIONS
//========================================================================

// Initialize all sprite buffers with color palettes
bool initializeSprites() {
  // Info sprite (top bar with scale info)
  infoSprite.setColorDepth(4);
  if (!infoSprite.createSprite(320, 50)) return false;
  infoSprite.setPaletteColor(0, 0x0001);  // Transparent
  infoSprite.setPaletteColor(1, 0xffff);  // White
  infoSprite.setPaletteColor(2, 0x0000);  // Black
  infoSprite.setPaletteColor(3, 0x39c7);  // Dark grey
  infoSprite.setPaletteColor(4, 0x7bef);  // Medium grey
  infoSprite.setPaletteColor(5, 0xd69a);  // Light grey
  infoSprite.setPaletteColor(6, 0xf800);  // Red
  infoSprite.setPaletteColor(7, 0x7e0);   // Green
  infoSprite.setPaletteColor(8, 0x00ff);  // Blue
  infoSprite.setPaletteColor(9, 0xfd20);  // Orange
  infoSprite.setFont(&DIN_Condensed_Bold30pt7b);
  infoSprite.fillSprite(0);

  // Keys sprite (piano keyboard display)
  keysSprite.setColorDepth(4);
  if (!keysSprite.createSprite(320, 150)) return false;
  keysSprite.setPaletteColor(0, 0x0001);
  keysSprite.setPaletteColor(1, 0xffff);
  keysSprite.setPaletteColor(2, 0x0000);
  keysSprite.setPaletteColor(3, 0x39c7);
  keysSprite.setPaletteColor(4, 0x7bef);
  keysSprite.setPaletteColor(5, 0xd69a);
  keysSprite.setPaletteColor(6, 0xf800);
  keysSprite.setPaletteColor(7, 0x7e0);
  keysSprite.setPaletteColor(8, 0x00ff);
  keysSprite.setPaletteColor(9, 0xfd20);
  keysSprite.setFont(&DIN_Condensed_Bold30pt7b);
  keysSprite.fillSprite(0);

  // Encoders sprite (bottom bar with encoder indicators)
  encsSprite.setColorDepth(4);
  if (!encsSprite.createSprite(320, 40)) return false;
  encsSprite.setPaletteColor(0, 0x0001);
  encsSprite.setPaletteColor(1, 0xffff);
  encsSprite.setPaletteColor(2, 0x0000);
  encsSprite.setPaletteColor(3, 0x39c7);
  encsSprite.setPaletteColor(4, 0x7bef);
  encsSprite.setPaletteColor(5, 0xd69a);
  encsSprite.setPaletteColor(6, 0xf800);
  encsSprite.setPaletteColor(7, 0x7e0);
  encsSprite.setPaletteColor(8, 0x00ff);
  encsSprite.setPaletteColor(9, 0xfd20);
  encsSprite.setFont(&DIN_Condensed_Bold30pt7b);
  encsSprite.fillSprite(0);

  // Initialize buffer sprites (same configuration as main sprites)
  infoSpriteBuffer.setColorDepth(4);
  if (!infoSpriteBuffer.createSprite(320, 50)) return false;
  infoSpriteBuffer.setPaletteColor(0, 0x0001);
  infoSpriteBuffer.setPaletteColor(1, 0xffff);
  infoSpriteBuffer.setPaletteColor(2, 0x0000);
  infoSpriteBuffer.setPaletteColor(3, 0x39c7);
  infoSpriteBuffer.setPaletteColor(4, 0x7bef);
  infoSpriteBuffer.setPaletteColor(5, 0xd69a);
  infoSpriteBuffer.setPaletteColor(6, 0xf800);
  infoSpriteBuffer.setPaletteColor(7, 0x7e0);
  infoSpriteBuffer.setPaletteColor(8, 0x00ff);
  infoSpriteBuffer.setPaletteColor(9, 0xfd20);
  infoSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  infoSpriteBuffer.fillSprite(0);

  keysSpriteBuffer.setColorDepth(4);
  if (!keysSpriteBuffer.createSprite(320, 150)) return false;
  keysSpriteBuffer.setPaletteColor(0, 0x0001);
  keysSpriteBuffer.setPaletteColor(1, 0xffff);
  keysSpriteBuffer.setPaletteColor(2, 0x0000);
  keysSpriteBuffer.setPaletteColor(3, 0x39c7);
  keysSpriteBuffer.setPaletteColor(4, 0x7bef);
  keysSpriteBuffer.setPaletteColor(5, 0xd69a);
  keysSpriteBuffer.setPaletteColor(6, 0xf800);
  keysSpriteBuffer.setPaletteColor(7, 0x7e0);
  keysSpriteBuffer.setPaletteColor(8, 0x00ff);
  keysSpriteBuffer.setPaletteColor(9, 0xfd20);
  keysSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  keysSpriteBuffer.fillSprite(0);

  encsSpriteBuffer.setColorDepth(4);
  if (!encsSpriteBuffer.createSprite(320, 40)) return false;
  encsSpriteBuffer.setPaletteColor(0, 0x0001);
  encsSpriteBuffer.setPaletteColor(1, 0xffff);
  encsSpriteBuffer.setPaletteColor(2, 0x0000);
  encsSpriteBuffer.setPaletteColor(3, 0x39c7);
  encsSpriteBuffer.setPaletteColor(4, 0x7bef);
  encsSpriteBuffer.setPaletteColor(5, 0xd69a);
  encsSpriteBuffer.setPaletteColor(6, 0xf800);
  encsSpriteBuffer.setPaletteColor(7, 0x7e0);
  encsSpriteBuffer.setPaletteColor(8, 0x00ff);
  encsSpriteBuffer.setPaletteColor(9, 0xfd20);
  encsSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  encsSpriteBuffer.fillSprite(0);

  return true;
}

// Draw the top info area showing current scale/root/formula
void drawInfoArea() {
  infoSpriteBuffer.fillSprite(0);

  // Draw rounded rectangle border
  infoSpriteBuffer.setColor(1);
  infoSpriteBuffer.drawRoundRect(0, 0, 186, 50, 10);
  infoSpriteBuffer.drawRoundRect(1, 1, 186 - 2, 50 - 2, 10);

  // Draw root note name (without octave number)
  infoSpriteBuffer.setTextSize(0.55);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(10, 10);
  infoSpriteBuffer.setTextColor(1);
  String rootName = cachedRootName;
  if (rootName.endsWith("-")) {
    rootName = rootName.substring(0, rootName.length() - 1);
  }
  infoSpriteBuffer.printf(rootName.c_str());

  // Draw scale name
  infoSpriteBuffer.setTextSize(0.4);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(37, 15);
  infoSpriteBuffer.setTextColor(1);
  infoSpriteBuffer.printf(cachedScaleName.c_str());

  // Draw interval formula
  infoSpriteBuffer.setTextSize(0.35);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(190, 15);
  infoSpriteBuffer.setTextColor(1);
  infoSpriteBuffer.printf(intvlFormula[scaleNo].c_str());

  Serial.print("Drawing info - Root: ");
  Serial.print(rootName);
  Serial.print(", Scale: ");
  Serial.print(cachedScaleName);
  Serial.print(", Formula: ");
  Serial.println(intvlFormula[scaleNo]);
}

// Draw the toggle switch indicator
void drawSwitchIndicator(bool isOn) {
  encsSpriteBuffer.fillRect(305, 8, 10, 20, 0);  // Clear area
  encsSpriteBuffer.setColor(isOn ? 7 : 3);       // Green if on, grey if off
  int yPos = isOn ? 8 : 18;                      // Top position when on
  encsSpriteBuffer.fillRect(305, yPos, 10, 10);
  encsSpriteBuffer.setColor(5);  // Light grey border
  encsSpriteBuffer.drawRect(305, 8, 10, 20);
}

// Calculate intervals between successive scale notes
void setScaleIntervals() {
  Serial.println("\n--- Scale Interval Debug ---");

  // Get fresh scale notes from scale manager
  for (int i = 0; i < 7; i++) {
    scaleNoteNo[i] = scalemanager.getScaleNote(i);
    Serial.print("Scale note ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(scaleNoteNo[i]);
  }

  // Find the fundamental note index
  int fundamentalIndex = -1;
  for (int i = 0; i < NUM_KEYS; i++) {
    if (key[i].isFundamental == true) {
      fundamentalIndex = i;
      Serial.print("Found fundamental at key index: ");
      Serial.println(fundamentalIndex);
      break;
    }
  }

  if (fundamentalIndex == -1) {
    Serial.println("Error: No fundamental note found!");
    return;
  }

  // Find the index of the fundamental in the scale notes array
  int fundamentalScaleIndex = -1;
  for (int i = 0; i < 7; i++) {
    if (scaleNoteNo[i] == fundamentalIndex) {
      fundamentalScaleIndex = i;
      Serial.print("Found fundamental at scale index: ");
      Serial.println(fundamentalScaleIndex);
      break;
    }
  }

  if (fundamentalScaleIndex == -1) {
    Serial.println("Error: Fundamental not found in scale notes!");
    return;
  }

  // Calculate intervals between successive notes
  for (int i = 0; i < 7; i++) {
    int currentIndex = (fundamentalScaleIndex + i) % 7;
    int nextIndex = (fundamentalScaleIndex + i + 1) % 7;

    int currentNote = scaleNoteNo[currentIndex];
    int nextNote = scaleNoteNo[nextIndex];

    Serial.print("Calculating interval ");
    Serial.print(i);
    Serial.print(": from note ");
    Serial.print(currentNote);
    Serial.print(" to ");
    Serial.print(nextNote);

    if (nextNote < currentNote) {
      // Handle wrap around octave
      scaleIntervals[i] = (12 - currentNote) + nextNote;
      Serial.print(" (wrapped) = ");
    } else {
      scaleIntervals[i] = nextNote - currentNote;
      Serial.print(" = ");
    }
    Serial.println(scaleIntervals[i]);
  }

  // Clear all interval strings
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].intervalS = "";
  }

  // Update keys with calculated intervals
  for (int i = 0; i < 7; i++) {
    int scaleIndex = (fundamentalScaleIndex + i) % 7;
    int noteIndex = scaleNoteNo[scaleIndex];
    if (noteIndex < 12) {
      key[noteIndex].intervalS = String(scaleIntervals[i]);
      Serial.print("Key ");
      Serial.print(noteIndex);
      Serial.print(" (in scale) gets interval: ");
      Serial.println(key[noteIndex].intervalS);
    } else {
      key[noteIndex - 12].intervalS = String(scaleIntervals[i]);
      Serial.print("Key ");
      Serial.print(noteIndex - 12);
      Serial.print(" (in scale) gets interval: ");
      Serial.println(key[noteIndex - 12].intervalS);
    }
  }
  Serial.println("--- End Scale Interval Debug ---\n");
}

// Update cached scale information from scale manager
void updateScaleCache() {
  if (scaleCacheNeedsUpdate) {
    cachedScaleName = scalemanager.getScaleName();
    cachedFundamentalName = scalemanager.getFundamentalName();
    cachedRootName = cachedFundamentalName.substring(0, cachedFundamentalName.length() - 1);
    scaleCacheNeedsUpdate = false;

    Serial.print("Scale cache updated - Root: ");
    Serial.print(cachedRootName);
    Serial.print(", Scale: ");
    Serial.println(cachedScaleName);
  }
}

// Update octave numbers for all keys in current scale
void updateOctaveNumbers() {
  for (int i = 0; i < NUM_KEYS; i++) {
    if (key[i].inScale) {
      if (key[i].octaveNo < 0) {
        key[i].octaveS = String(key[i].octaveNo);  // Show negative octaves
      } else {
        key[i].octaveS = String(key[i].octaveNo);
      }
      Serial.print("Key ");
      Serial.print(i);
      Serial.print(" octave set to: ");
      Serial.println(key[i].octaveS);
    } else {
      key[i].octaveS = "-";  // Non-scale notes show dash
    }
  }
}

// Get note name from PROGMEM lookup table
String getNoteName(uint8_t index) {
  char buffer[3];
  const char* ptr = noteNames;
  for (uint8_t i = 0; i < index; i++) {
    while (pgm_read_byte(ptr) != '\0') ptr++;
    ptr++;
  }
  uint8_t j = 0;
  while (j < 2 && (buffer[j] = pgm_read_byte(ptr++)) != '\0') j++;
  buffer[j] = '\0';
  return String(buffer);
}

// Check if encoder value has stabilized (debouncing)
bool isValueStable(int32_t newValue, uint8_t encoderIndex) {
  unsigned long currentTime = millis();

  if (currentTime - lastDebounceTime[encoderIndex] > DEBOUNCE_TIME) {
    debounceValues[encoderIndex][debounceIndex[encoderIndex]] = newValue;
    debounceIndex[encoderIndex] = (debounceIndex[encoderIndex] + 1) % DEBOUNCE_COUNT;

    // Check if all values match
    bool allSame = true;
    int32_t firstValue = debounceValues[encoderIndex][0];
    for (uint8_t i = 1; i < DEBOUNCE_COUNT; i++) {
      if (debounceValues[encoderIndex][i] != firstValue) {
        allSame = false;
        break;
      }
    }

    if (allSame && firstValue != lastStableValue[encoderIndex]) {
      lastStableValue[encoderIndex] = firstValue;
      lastDebounceTime[encoderIndex] = currentTime;
      return true;
    }
  }

  return false;
}
