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

// BLE MIDI Transport
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32.h>
#include <MIDI.h>
BLEMIDI_CREATE_DEFAULT_INSTANCE();

#define DEBUG_VERBOSE 0

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

// Palette index constants (for 4-bit color sprites)
const uint8_t PAL_TRANSPARENT = 0;
const uint8_t PAL_WHITE = 1;
const uint8_t PAL_BLACK = 2;
const uint8_t PAL_DARKGREY = 3;
const uint8_t PAL_MEDGREY = 4;
const uint8_t PAL_LIGHTGREY = 5;
const uint8_t PAL_RED = 6;
const uint8_t PAL_GREEN = 7;
const uint8_t PAL_BLUE = 8;
const uint8_t PAL_ORANGE = 9;

//========================================================================
// BLE MIDI CONFIGURATION
//========================================================================
bool bleMidiConnected = false;
uint8_t midiChannel = 1;  // MIDI channel (1-16)

// Per-encoder velocity (0-127), controlled by encoder rotation when switch is OFF
// Initialized to 127 (encoder at 0 position = full velocity)
uint8_t encoderVelocity[7] = { 127, 127, 127, 127, 127, 127, 127 };

//========================================================================
// DISPLAY CONFIGURATION CONSTANTS
//========================================================================
const uint16_t DISPLAY_WIDTH = 320;
const uint16_t DISPLAY_HEIGHT = 240;
const uint16_t INFO_SPRITE_HEIGHT = 52;
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

enum EncoderMode {
  MODE_DEFAULT = 0,
  MODE_SCALE,
  MODE_ROOT,
  MODE_OCTAVE
};
EncoderMode encoder7Mode = MODE_DEFAULT;
bool modeChanged = true;  // Force initial display update

static bool encoder7ButtonWasPressed = false;
static unsigned long encoder7ButtonPressTime = 0;
static bool encoder7ModeChangePending = false;


//========================================================================
// TIMING & UPDATE CONFIGURATION
//========================================================================
const unsigned long UPDATE_INTERVAL = 50;     // 100fps for smooth updates
const unsigned long STATUS_INTERVAL = 5000;   // 5 seconds
const unsigned long BUTTON_HOLD_TIME = 1000;  // 1 second hold time
const unsigned long FLASH_INTERVAL = 100;     // 5Hz flash (100ms on, 100ms off)

// Flash state tracking
static unsigned long lastFlashToggle = 0;
static bool flashState = false;  // true = visible, false = hidden

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

// Scale state variables
int8_t fundamental = 60;
int8_t fundamentalLast = 60;
int8_t octaveDefault = 4;
int8_t octave = 4;
int8_t octaveLast = 4;
int8_t octaveDif = 0;
int8_t notesDif = 0;
int8_t scaleNo = 0;
int8_t scaleNoteNo[7] = { 0, 0, 0, 0, 0, 0, 0 };
int8_t scaleIntervals[7] = { 0, 0, 0, 0, 0, 0, 0 };
bool updateScale = false;

// Cached scale information for display
String cachedScaleName = "";
String cachedFundamentalName = "";
String cachedRootName = "";
bool scaleCacheNeedsUpdate = true;

// Interval formulas for each scale (displayed in info area)
const char* const intvlFormula[9] = {
  "W-W-H-W-W-W-H", "W-H-W-W-H-W-W", "W-H-W-W-W-H-W", "W-H-W-W-H-3H-H",
  "W-H-W-W-W-H-W", "H-W-W-W-H-W-W", "W-W-W-H-W-W-H", "W-W-H-W-W-H-W", "H-W-W-H-W-W-W"
};

// Note names stored in PROGMEM to save RAM
const char noteNames[] PROGMEM = "C\0C#\0D\0D#\0E\0F\0F#\0G\0G#\0A\0A#\0B";

//========================================================================
// KEY IDENTIFICATION
//========================================================================
// Identifies white keys (1) vs black keys (0) in chromatic scale starting from C
const bool whiteKeys[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };

//========================================================================
// ENCODER STRUCTURE & STATE
//========================================================================
// Defines visual representation and state of on-screen encoder widgets
struct Encoders {
  int x;                              // X position on screen
  int y;                              // Y position on screen
  int r;                              // Radius
  int encValue;                       // Current encoder value
  uint16_t colourIdle;                // Color when idle
  uint16_t colourActive;              // Color when active
  int8_t encId = -1;                  // Encoder ID
  bool encPressed = false;            // Button press state
  bool isActive = false;              // Active/locked state
  bool shortPress = false;            // Short press indicator state
  String encNoteName = "";            // Note name (if applicable)
  int8_t encScaleNoteNo;              // Scale note number
  int8_t encOnceOnlySendNoteOn = 0;   // MIDI note on flag
  int8_t encOnceOnlySendNoteOff = 0;  // MIDI note off flag
  int8_t onceOnlyEncDraw = 0;         // Draw once flag

  // Clear encoder display area
  void clearEncs(int8_t encNo) {
    encsSpriteBuffer.setColor(PAL_BLACK);
    encsSpriteBuffer.fillCircle(x - 2, y - 2, r + 4, PAL_BLACK);
  }

  // Draw encoder with current state (idle/active/shortPress)
  void drawEncs(int8_t encNo) {
    // Determine circle color based on state
    uint8_t circleColor;
    if (this->isActive) {
      circleColor = PAL_GREEN;  // Green for active/locked
    } else if (this->shortPress) {
      // Encoder 7: white on short press, others: orange
      circleColor = (encNo == 7) ? PAL_WHITE : PAL_ORANGE;
    } else {
      circleColor = this->colourIdle;  // Use encoder's idle color (mid grey for enc 7, dark grey for others)
    }

    // Draw filled circle
    encsSpriteBuffer.setColor(circleColor);
    encsSpriteBuffer.fillCircle(x, y, r, circleColor);

    // Draw arc indicator starting from bottom (90 degrees)
    uint8_t arcColor = (circleColor == PAL_DARKGREY) ? PAL_WHITE : PAL_BLACK;
    encsSpriteBuffer.setColor(arcColor);

    float startAngle = 90 + (this->encValue * 6) - 5;
    float endAngle = 90 + (this->encValue * 6) + 5;
    int innerRadius = r - (arcColor == PAL_BLACK ? 7 : 5);
    int outerRadius = r - (arcColor == PAL_BLACK ? 2 : 4);

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
  int8_t kId = -1;              // Key ID
  int8_t keyNo = -1;            // Key number (0-11)
  String keyNoteName = "";      // Note name (C, C#, D, etc.)
  uint8_t onceOnlyKeyDraw = 0;  // Draw once flag
  bool inScale = false;         // Is this key in current scale?
  bool isFundamental = false;   // Is this the root note?
  uint8_t octaveNo = -1;        // Octave number
  String octaveS = "";          // Octave string for display
  uint8_t interval;             // Interval from previous scale note
  String intervalS;             // Interval string for display

  // Clear key display area
  void clearKeys(int8_t keyNo) {
    keysSpriteBuffer.setColor(PAL_BLACK);
    keysSpriteBuffer.fillRoundRect(x - 1, y - 1, w + 2, h + 2, PAL_BLACK);
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
    if (this->inScale) {
      if (this->isFundamental) {
        keysSpriteBuffer.setTextColor(PAL_GREEN);  // Green for root
      } else {
        keysSpriteBuffer.setTextColor(PAL_WHITE);  // White for scale notes
      }
    } else {
      keysSpriteBuffer.setTextColor(PAL_DARKGREY);  // Grey for non-scale notes
    }
    keysSpriteBuffer.printf(this->keyNoteName.c_str());

    // Draw octave number (flash between white/green and mid grey in MODE_OCTAVE)
    bool hideOctave = (encoder7Mode == MODE_OCTAVE && !flashState && this->inScale);
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 15);

    if (this->inScale) {
      if (hideOctave) {
        keysSpriteBuffer.setTextColor(PAL_MEDGREY);
      } else if (this->isFundamental) {
        keysSpriteBuffer.setTextColor(PAL_GREEN);
      } else {
        keysSpriteBuffer.setTextColor(PAL_WHITE);
      }
      keysSpriteBuffer.printf(this->octaveS.c_str());
    } else {
      keysSpriteBuffer.setTextColor(PAL_DARKGREY);
      keysSpriteBuffer.printf("-");
    }

    // Draw interval number (semitones from previous scale note)
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + 15);
    keysSpriteBuffer.setTextColor(PAL_WHITE);
    keysSpriteBuffer.printf(this->intervalS.c_str());

    // Draw velocity bar if this key is in scale
    if (this->inScale) {
      // Find which encoder index (0-6) this scale note corresponds to
      int encoderIdx = -1;
      for (int j = 0; j < 7; j++) {
        int8_t scaleNote = scalemanager.getScaleNote(j);
        if (scaleNote >= 12) scaleNote -= 12;
        if (scaleNote == keyNo) {
          encoderIdx = j;
          break;
        }
      }

      if (encoderIdx >= 0 && encoderIdx < 7) {
        // Bar dimensions: fixed base height 85, max height 35 (within keys sprite)
        int barX = this->x + w / 2 - 2;  // Center horizontally (5px wide)
        int barBase = 85;                // Fixed base for white keys
        // Offset bar base upward for black keys (same offset as key positioning: 10px)
        if (!whiteKeys[keyNo]) {
          barBase -= 10;  // Black keys offset
        }
        int maxBarHeight = 35;  // Fixed max height
        int barTop = barBase - maxBarHeight;

        // Map velocity (0-127) to bar height
        // CW turn increases height from 0 to max, ACW decreases from max to 0
        uint8_t velocity = encoderVelocity[encoderIdx];
        int barHeight = (velocity * maxBarHeight) / 127;
        if (barHeight < 0) barHeight = 0;
        if (barHeight > maxBarHeight) barHeight = maxBarHeight;

        // Determine bar color based on button state of corresponding encoder
        uint8_t barColor = PAL_MEDGREY;  // Default: mid grey
        if (enc[encoderIdx].isActive) {
          barColor = PAL_GREEN;  // Hold: green (active/locked)
        } else if (enc[encoderIdx].shortPress) {
          barColor = PAL_ORANGE;  // Short press: orange
        }

        // Draw bar from bottom up (base to base-height)
        int barY = barBase - barHeight;
        keysSpriteBuffer.setColor(barColor);
        keysSpriteBuffer.fillRect(barX, barY, 5, barHeight, barColor);
      }
    }
  }
};

static constexpr uint8_t nosKeys = 12;
static Keys key[nosKeys];


//========================================================================
// ENCODER TRACKING STATE
//========================================================================
// Global tracking variables for encoder state changes
static int8_t currentOctave = 4;
static int32_t lastEncoderValues[8] = { 0 };
static bool encoderInitialized[8] = { false };
static int32_t encoder6LastValue = 0;
static int32_t encoder7LastValue = 0;
static int32_t encoder8LastValue = 0;
static unsigned long buttonPressStartTime[8] = { 0 };
static bool buttonHoldProcessed[8] = { false };
static bool waitingForNextPress[8] = { false };
static uint8_t lastButtonStates[8] = { 1 };
static bool currentSwitchState = false;

//========================================================================
// DEBOUNCING CONFIGURATION
//========================================================================
// Encoder debouncing to reduce jitter and false readings
static const uint8_t DEBOUNCE_COUNT = 3;
static const uint16_t DEBOUNCE_TIME = 5;  // milliseconds
int32_t debounceValues[8][DEBOUNCE_COUNT] = { 0 };
uint8_t debounceIndex[8] = { 0 };
unsigned long lastDebounceTime[8] = { 0 };
int32_t lastStableValue[8] = { 0 };
bool valueChanged[8] = { false };


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
  // Read encoder value (no retry needed - I2C library handles errors)
  bool readEncoderValue(uint8_t encoderIndex, int32_t& value, uint8_t& buttonState) {
    buttonState = sensor.getButtonStatus(encoderIndex);
    value = sensor.getEncoderValue(encoderIndex);
    errorCount = 0;  // Success - reset error count
    return true;
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

  uint8_t getErrorCount() const {
    return errorCount;
  }
  void resetErrorCount() {
    errorCount = 0;
  }
};

I2CManager i2cManager;


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
// BLE MIDI HELPER FUNCTIONS
//========================================================================
// Calculate MIDI note for encoder button (encoders 0-6 = scale notes)
// MIDI note 60 = C4, so formula is: note + ((octave + 1) * 12)
uint8_t getEncoderMidiNote(uint8_t encoderIndex) {
  if (encoderIndex > 6) return 0;  // Encoder 7 doesn't send notes

  int8_t scaleNote = scalemanager.getScaleNote(encoderIndex);
  if (scaleNote >= 12) scaleNote -= 12;

  // MIDI standard: C4 = 60, so octave 4 -> (4+1)*12 = 60 for C
  int midiNote = scaleNote + ((currentOctave + 1) * 12);
  if (midiNote < 0) midiNote = 0;
  if (midiNote > 127) midiNote = 127;

  return (uint8_t)midiNote;
}

// Get velocity for encoder 0-6
uint8_t getEncoderVelocity(uint8_t encoderIndex) {
  if (encoderIndex > 6) return 100;
  return encoderVelocity[encoderIndex];
}

//========================================================================
// BLE MIDI CALLBACKS
//========================================================================
void OnMidiConnected() {
  bleMidiConnected = true;
  Serial.println("[BLE MIDI] Connected");
}

void OnMidiDisconnected() {
  bleMidiConnected = false;
  Serial.println("[BLE MIDI] Disconnected");
}

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

  // Initialize encoder display states and set encoder values to 60 (velocity 127)
  for (int i = 0; i < NUM_ENCODERS; i++) {
    enc[i].r = 16;
    enc[i].x = 16 + i * 38;
    enc[i].y = 16;
    enc[i].isActive = false;
    enc[i].shortPress = false;
    // Encoder 7 is mid grey by default, others are dark grey
    enc[i].colourIdle = (i == 7) ? 4 : 3;  // 4 = PAL_MEDGREY, 3 = PAL_DARKGREY
    enc[i].colourActive = 7;
    sensor.setLEDColor(i, 0x000000);  // Turn off LEDs
    sensor.resetCounter(i);           // Reset encoder
    // Set initial count to 60 so velocity starts at 127
    // This way CW turn has no effect (already at max), only ACW decreases
    sensor.setEncoderValue(i, 60);
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

  // Initialize BLE MIDI
  Serial.println("Initializing BLE MIDI...");
  MIDI.begin(MIDI_CHANNEL_OMNI);
  delay(100);
  BLEMIDI.setHandleConnected(OnMidiConnected);
  BLEMIDI.setHandleDisconnected(OnMidiDisconnected);
  Serial.println("BLE MIDI initialized");

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
// ENCODER VALUE HANDLING FUNCTION
//========================================================================
bool handleEncoderValueChange(int encoderIndex, int32_t currentValue, int32_t& lastValue, bool currentSwitchState, uint8_t rawButtonState) {
  static bool encoderInitialized[8] = { false };
  static int32_t encoder8LastValue = 0;  // Only need this one now
  static bool encoder7ButtonWasPressed = false;
  static unsigned long encoder7ButtonPressTime = 0;

  bool encoderChanged = false;

  // Update the last value
  lastValue = currentValue;

  // Encoders 0-6: Switch OFF controls velocity
  if (!currentSwitchState && encoderIndex <= 6) {
    // Map encoder value to velocity: CW increases, ACW decreases
    int vel = (currentValue * 127) / 60;
    if (vel < 0) vel = 0;
    if (vel > 127) vel = 127;
    encoderVelocity[encoderIndex] = (uint8_t)vel;
    encoderChanged = true;
  }
  // Encoder 7: always show rotation, but only act on parameters when NOT in MODE_DEFAULT
  else if (encoderIndex == 7) {
    if (!encoderInitialized[encoderIndex]) {
      encoder8LastValue = currentValue;
      encoderInitialized[encoderIndex] = true;
      Serial.println("Encoder 7 initialized");
      encoderChanged = true;
    } else {
      // Handle encoder rotation based on current mode
      int32_t delta = 0;
      if (currentValue != encoder8LastValue) {
        encoderChanged = true;  // Always show rotation on GUI
        delta = (currentValue > encoder8LastValue) ? 1 : -1;
        Serial.print("Encoder 7 delta (mode ");
        Serial.print(encoder7Mode);
        Serial.print("): ");
        Serial.println(delta);
      }

      // Only process parameter changes when NOT in MODE_DEFAULT
      if (encoder7Mode != MODE_DEFAULT) {
        Serial.print("Processing parameter encoder 7, mode: ");
        switch (encoder7Mode) {
          case MODE_DEFAULT: Serial.println("DEFAULT (no action)"); break;
          case MODE_SCALE: Serial.println("SCALE"); break;
          case MODE_ROOT: Serial.println("ROOT"); break;
          case MODE_OCTAVE: Serial.println("OCTAVE"); break;
        }

        if (delta != 0) {
          static int32_t stepAccumulator = 0;
          stepAccumulator += delta;

          if (abs(stepAccumulator) >= 2) {
            int change = stepAccumulator / 2;
            stepAccumulator = 0;

            switch (encoder7Mode) {
              case MODE_SCALE:
                {
                  Serial.print("Scale change triggered: ");
                  Serial.println(change);
                  int newScale = scaleNo + change;
                  if (newScale > 8) newScale = 0;
                  else if (newScale < 0) newScale = 8;

                  scaleNo = newScale;
                  scalemanager.setScale(scaleNo);
                  scaleCacheNeedsUpdate = true;
                  updateScaleCache();

                  for (int j = 0; j < NUM_KEYS; j++) key[j].inScale = false;
                  for (int j = 0; j < 7; j++) {
                    int scaleNote = scalemanager.getScaleNote(j);
                    if (scaleNote < 12) key[scaleNote].inScale = true;
                    else key[scaleNote - 12].inScale = true;
                  }

                  setScaleIntervals();
                  encoderChanged = true;
                  break;
                }

              case MODE_ROOT:
                {
                  Serial.print("Root change triggered: ");
                  Serial.println(change);
                  int newFundamental = fundamental + change;
                  if (newFundamental > 11) newFundamental = 0;
                  else if (newFundamental < 0) newFundamental = 11;

                  fundamental = newFundamental;
                  scalemanager.setFundamental(fundamental);
                  scaleCacheNeedsUpdate = true;
                  updateScaleCache();

                  for (int j = 0; j < NUM_KEYS; j++) key[j].isFundamental = false;
                  key[fundamental].isFundamental = true;

                  for (int j = 0; j < NUM_KEYS; j++) key[j].inScale = false;
                  for (int j = 0; j < 7; j++) {
                    scaleNoteNo[j] = scalemanager.getScaleNote(j);
                    if (scaleNoteNo[j] < 12) key[scaleNoteNo[j]].inScale = true;
                    else key[scaleNoteNo[j] - 12].inScale = true;
                  }

                  setScaleIntervals();
                  updateOctaveNumbers();
                  encoderChanged = true;
                  break;
                }

              case MODE_OCTAVE:
                {
                  Serial.print("Octave change triggered: ");
                  Serial.println(change);
                  int newOctave = currentOctave + change;
                  if (newOctave > 9) newOctave = -1;
                  else if (newOctave < -1) newOctave = 9;

                  currentOctave = newOctave;
                  for (int j = 0; j < NUM_KEYS; j++) {
                    if (key[j].inScale) {
                      key[j].octaveNo = currentOctave;
                      key[j].octaveS = String(currentOctave);
                    }
                  }
                  encoderChanged = true;
                  break;
                }

              case MODE_DEFAULT:
                // No action in default mode
                break;
            }
          }
        }
      }
      encoder8LastValue = currentValue;
    }
  }

  return encoderChanged;
}

//========================================================================
// MAIN LOOP
//========================================================================
void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastStatusCheck = 0;
  static bool lastSwitchState = false;
  static int32_t lastEncoderValues[8] = { 60, 60, 60, 60, 60, 60, 60, 60 };  // Match startup values
  static bool buttonWasPressed[8] = { false };

  // Rate limiting: only update at specified interval
  if (millis() - lastUpdate < UPDATE_INTERVAL) {
    return;
  }
  lastUpdate = millis();

  M5.update();

  // Keep BLE MIDI stack alive
  MIDI.read();

  // ==================================================================
  // READ SWITCH STATE
  // ==================================================================
  bool currentSwitchState = sensor.getSwitchStatus();
  i2cManager.resetErrorCount();

  // Handle switch state change
  if (currentSwitchState != lastSwitchState) {
    // Update LED 9 based on switch state
    sensor.setLEDColor(8, currentSwitchState ? LED_COLOR_GREEN : LED_COLOR_OFF);
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

// debugging
#if DEBUG_VERBOSE
    // Serial.print("Encoder ");
    // Serial.print(i);
    // Serial.print(" rawButtonState: ");
    // Serial.println(rawButtonState);
#endif

    bool buttonPressed = (rawButtonState == 0);  // Active low

    // Use raw value (bypassing debouncing for testing)
    int32_t stableValue = currentValue;
    enc[i].encValue = stableValue;  // Update display value

    // ==================================================================
    // HANDLE ENCODER VALUE CHANGES
    // ==================================================================
    if (stableValue != lastEncoderValues[i]) {
      // Handle encoder value changes in separate function to avoid scoping issues
      if (handleEncoderValueChange(i, stableValue, lastEncoderValues[i], currentSwitchState, rawButtonState)) {
        encoderChanged = true;
      }
    }


    // ==================================================================
    // HANDLE BUTTON PRESS/HOLD/RELEASE
    // ==================================================================

    // Button press detected
    if (buttonPressed && !buttonWasPressed[i]) {
      // Encoder 7: mode cycling on short press (no MIDI)
      if (i == 7) {
        buttonPressStartTime[i] = millis();
        enc[i].shortPress = true;  // Show white indicator
        encoderChanged = true;
        buttonWasPressed[i] = buttonPressed;
        continue;
      }

      // Only encoders 0-6 send MIDI notes (switch OFF mode)
      // Switch ON: buttons don't send MIDI (encoders 5-7 control params)
      bool sendsMidi = !currentSwitchState && i <= 6;

      if (enc[i].isActive && waitingForNextPress[i]) {
        // Pressing active encoder deactivates it - send NoteOff
        if (bleMidiConnected && sendsMidi) {
          uint8_t midiNote = getEncoderMidiNote(i);
          MIDI.sendNoteOff(midiNote, 0, midiChannel);
          Serial.printf("[BLE MIDI] NoteOff (deactivate): %d, ch: %d\n", midiNote, midiChannel);
        }
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" button pressed - deactivating");
        enc[i].isActive = false;
        enc[i].shortPress = false;
        waitingForNextPress[i] = false;
        sensor.setLEDColor(i, LED_COLOR_OFF);
        encoderChanged = true;
      } else if (!enc[i].isActive) {
        // New press on inactive encoder - send NoteOn, start timing for hold
        if (bleMidiConnected && sendsMidi) {
          uint8_t midiNote = getEncoderMidiNote(i);
          uint8_t velocity = getEncoderVelocity(i);
          MIDI.sendNoteOn(midiNote, velocity, midiChannel);
          Serial.printf("[BLE MIDI] NoteOn: %d, vel: %d, ch: %d\n", midiNote, velocity, midiChannel);
        }
        buttonPressStartTime[i] = millis();
        buttonHoldProcessed[i] = false;
        enc[i].shortPress = true;  // Show orange indicator
        sensor.setLEDColor(i, LED_COLOR_ORANGE);
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" button pressed - waiting for hold");
        encoderChanged = true;
      }
    }
    // Button released
    else if (!buttonPressed && buttonWasPressed[i]) {
      // Encoder 7: cycle mode on short press release
      if (i == 7) {
        unsigned long pressDuration = millis() - buttonPressStartTime[i];
        if (pressDuration < 500) {  // Short press cycles mode
          encoder7Mode = static_cast<EncoderMode>((encoder7Mode + 1) % 4);  // 4 modes now
          modeChanged = true;
          Serial.print("Encoder 7 mode changed to: ");
          switch (encoder7Mode) {
            case MODE_DEFAULT: Serial.println("DEFAULT"); break;
            case MODE_SCALE: Serial.println("SCALE"); break;
            case MODE_ROOT: Serial.println("ROOT"); break;
            case MODE_OCTAVE: Serial.println("OCTAVE"); break;
          }
        }
        enc[i].shortPress = false;  // Turn off white indicator
        encoderChanged = true;
        buttonWasPressed[i] = buttonPressed;
        continue;
      }

      bool sendsMidi = !currentSwitchState && i <= 6;

      // Only send NoteOff on release if it was a short press (not held/active)
      if (!enc[i].isActive && !buttonHoldProcessed[i]) {
        if (bleMidiConnected && sendsMidi) {
          uint8_t midiNote = getEncoderMidiNote(i);
          MIDI.sendNoteOff(midiNote, 0, midiChannel);
          Serial.printf("[BLE MIDI] NoteOff (release): %d, ch: %d\n", midiNote, midiChannel);
        }
        // Short press released - turn off orange indicator
        enc[i].shortPress = false;
        sensor.setLEDColor(i, LED_COLOR_OFF);
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
        sensor.setLEDColor(i, LED_COLOR_GREEN);
        encoderChanged = true;
        buttonHoldProcessed[i] = true;
      }
    }

    buttonWasPressed[i] = buttonPressed;
  }

  // ==================================================================
  // HANDLE FLASHING FOR ACTIVE MODES
  // ==================================================================
  unsigned long currentTime = millis();
  if (currentTime - lastFlashToggle >= FLASH_INTERVAL) {
    flashState = !flashState;
    lastFlashToggle = currentTime;
    // Force redraw when in any active mode
    if (encoder7Mode != MODE_DEFAULT) {
      modeChanged = true;
    }
  }

  // ==================================================================
  // UPDATE DISPLAY IF CHANGES OCCURRED
  // ==================================================================
  if (encoderChanged || modeChanged) {
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
    keysSprite.pushSprite(0, 52);  // Moved down 2px to avoid cutoff of info sprite
    encsSprite.pushSprite(0, 200);

    // Reset modeChanged flag after handling
    modeChanged = false;
  }
}

//========================================================================
// HELPER FUNCTIONS - IMPLEMENTATIONS
//========================================================================

// Initialize all sprite buffers with color palettes
bool initializeSprites() {
  // Info sprite (top bar with scale info)
  infoSprite.setColorDepth(4);
  if (!infoSprite.createSprite(320, 52)) return false;
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
  if (!infoSpriteBuffer.createSprite(320, 52)) return false;
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
  // Note: sprite is already cleared by caller before drawInfoArea()

  // Determine if we should flash (hide) based on mode
  bool hideRoot = (encoder7Mode == MODE_ROOT && !flashState);
  bool hideScale = (encoder7Mode == MODE_SCALE && !flashState);

  // Calculate bounding box for scale info
  int boxWidth = 176;  // Fixed width to properly contain all scale names
  int boxHeight = 52;  // Original 50 + 2px
  
  // Draw rounded rectangle outline around all labels
  infoSpriteBuffer.setColor(PAL_WHITE);
  infoSpriteBuffer.drawRoundRect(0, 0, boxWidth, boxHeight, 5);

  // Draw root note name (without octave number) - shifted 3px down and right
  infoSpriteBuffer.setTextSize(0.55);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(3, 3);
  if (hideRoot) {
    infoSpriteBuffer.setTextColor(PAL_MEDGREY);
  } else {
    infoSpriteBuffer.setTextColor(PAL_WHITE);
  }
  String rootName = cachedRootName;
  if (rootName.endsWith("-")) {
    rootName = rootName.substring(0, rootName.length() - 1);
  }
  infoSpriteBuffer.printf(rootName.c_str());

  // Draw scale name - shifted 3px down and right
  infoSpriteBuffer.setTextSize(0.4);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(30, 8);
  if (hideScale) {
    infoSpriteBuffer.setTextColor(PAL_MEDGREY);
  } else {
    infoSpriteBuffer.setTextColor(PAL_WHITE);
  }
  infoSpriteBuffer.printf(cachedScaleName.c_str());

  // Draw interval formula - shifted 3px down and right
  infoSpriteBuffer.setTextSize(0.35);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(3, 33);
  infoSpriteBuffer.setTextColor(PAL_WHITE);
  infoSpriteBuffer.printf(intvlFormula[scaleNo]);

  // Serial.print("Drawing info - Root: ");
  // Serial.print(rootName);
  // Serial.print(", Scale: ");
  // Serial.print(cachedScaleName);
  // Serial.print(", Formula: ");
  // Serial.println(intvlFormula[scaleNo]);  // Now const char*, no .c_str() needed

  // Add mode indicator when switch is ON
  if (currentSwitchState) {
    infoSpriteBuffer.setTextSize(0.35);
    infoSpriteBuffer.setTextDatum(TR_DATUM);  // Top right
    infoSpriteBuffer.setCursor(310, 5);
    infoSpriteBuffer.setTextColor(PAL_LIGHTGREY);

    String modeText = "MODE: ";
    switch (encoder7Mode) {
      case MODE_SCALE: modeText += "SCALE"; break;
      case MODE_ROOT: modeText += "ROOT"; break;
      case MODE_OCTAVE: modeText += "OCTAVE"; break;
    }
    infoSpriteBuffer.printf(modeText.c_str());
  }
}

// Draw the toggle switch indicator
void drawSwitchIndicator(bool isOn) {
  encsSpriteBuffer.fillRect(305, 8, 10, 20, PAL_TRANSPARENT);  // Clear area
  encsSpriteBuffer.setColor(isOn ? PAL_GREEN : PAL_DARKGREY);  // Green if on, grey if off
  int yPos = isOn ? 8 : 18;                                    // Top position when on
  encsSpriteBuffer.fillRect(305, yPos, 10, 10);
  encsSpriteBuffer.setColor(PAL_LIGHTGREY);  // Light grey border
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
