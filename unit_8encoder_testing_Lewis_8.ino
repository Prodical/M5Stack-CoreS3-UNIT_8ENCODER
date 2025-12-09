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
const uint16_t COLOR_YELLOW = 0xffe0;   // Replaces green in UI
const uint16_t COLOR_BLUE = 0x00ff;
const uint16_t COLOR_ORANGE = 0xfd20;
const uint16_t COLOR_RED_40 = 0x6000;

// LED color constants for encoder hardware LEDs
const uint32_t LED_COLOR_OFF = 0x000000;
const uint32_t LED_COLOR_YELLOW = 0xFFFF00;  // Replaces prior green behavior
const uint32_t LED_COLOR_RED_40 = 0x660000;  // Dim red for short-press state

// Palette index constants (for 4-bit color sprites)
const uint8_t PAL_TRANSPARENT = 0;
const uint8_t PAL_WHITE = 1;
const uint8_t PAL_BLACK = 2;
const uint8_t PAL_DARKGREY = 3;
const uint8_t PAL_MEDGREY = 4;
const uint8_t PAL_LIGHTGREY = 5;
const uint8_t PAL_RED = 6;
const uint8_t PAL_YELLOW = 7;   // Index retained; color remapped to yellow in palette init
const uint8_t PAL_BLUE = 8;
const uint8_t PAL_ORANGE = 9;
const uint8_t PAL_RED_40 = 10;

//========================================================================
// BLE MIDI CONFIGURATION
//========================================================================
bool bleMidiConnected = false;
uint8_t midiChannel = 1;  // MIDI channel (1-16)

// Track whether the current chord is actively sounding (for key fill brightness)
static bool chordPlaying = false;

// Per-encoder velocity (0-127), controlled by encoder rotation when switch is OFF
// Initialized to 127 (encoder at 0 position = full velocity)
uint8_t encoderVelocity[7] = { 127, 127, 127, 127, 127, 127, 127 };

//========================================================================
// DISPLAY CONFIGURATION CONSTANTS
//========================================================================
const uint16_t DISPLAY_WIDTH = 320;
const uint16_t DISPLAY_HEIGHT = 240;
const uint16_t INFO_SPRITE_HEIGHT = 55;
const uint16_t KEYS_SPRITE_HEIGHT = 145;
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

enum SystemMode {
  SCALE_STATE = 0,
  CHORD_STATE = 1
};

// Chord submodes inside CHORD_STATE
enum ChordSubMode : uint8_t { CHORD_NORMAL = 0, CHORD_ASSIGN = 1 };
static ChordSubMode chordSubMode = CHORD_NORMAL;

static bool encoder7ButtonWasPressed = false;
SystemMode currentMode = SCALE_STATE;  // Track current system mode

static unsigned long encoder7ButtonPressTime = 0;
static bool encoder7ModeChangePending = false;


//========================================================================
// TIMING & UPDATE CONFIGURATION
//========================================================================
const unsigned long UPDATE_INTERVAL = 50;     // 100fps for smooth updates
const unsigned long STATUS_INTERVAL = 5000;   // 5 seconds
const unsigned long BUTTON_HOLD_TIME = 1000;  // 1 second hold time
const unsigned long FLASH_INTERVAL = 200;     // 2.5Hz flash (200ms on, 200ms off)

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

//========================================================================
// CHORD SYSTEM
//========================================================================

// Chord types
enum ChordType {
  CHORD_TRIAD = 0,
  CHORD_SEVENTH,
  CHORD_MINOR_TRIAD,
  CHORD_MINOR_SEVENTH,
  CHORD_DIMINISHED,
  CHORD_DIMINISHED_SEVENTH,
  CHORD_AUGMENTED
};

// Chord structure
struct Chord {
  String name;         // e.g., "C Major", "Am7"
  int8_t rootNote;     // 0-11 (C-B)
  int8_t notes[6];     // Up to 6 notes
  uint8_t noteCount;   // Number of notes in chord
  ChordType type;      // Chord type
  int8_t scaleDegree;  // 0-6 (I-vii°)

  // Clear the chord data
  void clear() {
    name = "";
    rootNote = 0;
    noteCount = 0;
    type = CHORD_TRIAD;
    scaleDegree = 0;
    for (int i = 0; i < 6; i++) notes[i] = 0;
  }
};

// Current chord tracking
Chord currentChord;
int8_t currentChordDegree = 0;  // 0-6 for I-vii°

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
// Assign-mode slots: one degree per encoder 0–6
struct AssignSlot { int8_t degree; };
static AssignSlot assignSlots[7];

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
      circleColor = PAL_YELLOW;  // Active/locked (palette index 7 now mapped to yellow)
    } else if (this->shortPress) {
      // Encoder 7: white on short press, others: dim red instead of orange
      circleColor = (encNo == 7) ? PAL_WHITE : PAL_RED_40;
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

    // Roman numerals centered precisely using text width in CHORD_ASSIGN
    if (currentMode == CHORD_STATE && chordSubMode == CHORD_ASSIGN && encNo <= 6) {
      const char* romanNumerals[] = { "I", "ii", "iii", "IV", "V", "vi", "vii°" };
      int d = assignSlots[encNo].degree;
      if (d < 0) d = 0; if (d > 6) d = 6;
      encsSpriteBuffer.setTextSize(0.35);
      // Force light grey (match E7 graphic) regardless of background
      encsSpriteBuffer.setTextColor(PAL_LIGHTGREY);
      // Measure and center manually
      int tw = encsSpriteBuffer.textWidth(romanNumerals[d]);
      int th = encsSpriteBuffer.fontHeight();
      int cx = x - (tw / 2);
      int cy = y - (th / 2) + 1;
      encsSpriteBuffer.setTextDatum(TL_DATUM);
      encsSpriteBuffer.setCursor(cx, cy);
      encsSpriteBuffer.printf("%s", romanNumerals[d]);
    }
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
    // NEW: Highlight chord notes first (behind other elements)
    // Inline chord note checking
    bool noteInChord = false;
    int8_t normalizedKey = keyNo % 12;
    for (int i = 0; i < currentChord.noteCount; i++) {
      if ((currentChord.notes[i] % 12) == normalizedKey) {
        noteInChord = true;
        break;
      }
    }

    if (noteInChord && currentMode == CHORD_STATE) {
      // Draw chord note highlight: bright red when chord sounding, dim when idle
      uint8_t col = chordPlaying ? PAL_RED : PAL_RED_40;
      keysSpriteBuffer.setColor(col);
      keysSpriteBuffer.fillRoundRect(x, y, w, h, 5, col);
    }

    // Draw key outline (this will be on top of highlight)
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

    // Color: yellow for root, white for in-scale/chord, grey for out-of-scale/chord
    bool noteInCurrentSet;
    bool isCurrentRoot;

    if (currentMode == SCALE_STATE) {
      // Use scale-based highlighting
      noteInCurrentSet = this->inScale;
      isCurrentRoot = this->isFundamental;
    } else {
      // Use chord-based highlighting
      noteInCurrentSet = false;
      isCurrentRoot = false;
      for (int i = 0; i < currentChord.noteCount; i++) {
        if ((currentChord.notes[i] % 12) == keyNo) {
          noteInCurrentSet = true;
          if (keyNo == currentChord.rootNote % 12) {
            isCurrentRoot = true;
          }
          break;
        }
      }
    }

if (noteInCurrentSet) {
      if (isCurrentRoot) {
        keysSpriteBuffer.setTextColor(PAL_YELLOW);  // Palette index 7 (now yellow) for root
      } else {
        keysSpriteBuffer.setTextColor(PAL_WHITE);  // White for notes in current set
      }
    } else {
      keysSpriteBuffer.setTextColor(PAL_DARKGREY);  // Grey for notes not in current set
    }

    keysSpriteBuffer.printf(this->keyNoteName.c_str());

    // Draw octave number (flash between white/yellow and mid grey in MODE_OCTAVE)
    bool hideOctave = (encoder7Mode == MODE_OCTAVE && !flashState && noteInCurrentSet);
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 15);

    if (noteInCurrentSet) {
      if (hideOctave) {
        keysSpriteBuffer.setTextColor(PAL_MEDGREY);
      } else if (isCurrentRoot) {
        keysSpriteBuffer.setTextColor(PAL_YELLOW);
      } else {
        keysSpriteBuffer.setTextColor(PAL_WHITE);
      }
      keysSpriteBuffer.printf("%d", this->octaveNo);
    } else {
      keysSpriteBuffer.setTextColor(PAL_DARKGREY);
      keysSpriteBuffer.printf("-");
    }


// Draw scale interval number (semitones from previous scale note) - always shown
keysSpriteBuffer.setTextSize(0.35);
keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + 15);
// Yellow (palette idx 7) for scale root, white/mid-grey for others
if (this->isFundamental) {
  keysSpriteBuffer.setTextColor(PAL_YELLOW);
} else {
  keysSpriteBuffer.setTextColor(currentMode == SCALE_STATE ? PAL_WHITE : PAL_MEDGREY);
}
keysSpriteBuffer.printf(this->intervalS.c_str());

    // Draw chord interval in CHORD_STATE (step from previous chord note)
    if (currentMode == CHORD_STATE && noteInChord) {
      // Find step from previous note in chord
      int8_t chordStep = -1;
      for (int i = 0; i < currentChord.noteCount; i++) {
        if ((currentChord.notes[i] % 12) == normalizedKey) {
          if (i == 0) {
            chordStep = 0;  // Root is always 0
          } else {
            // Step = semitones from previous chord note
            chordStep = (currentChord.notes[i] - currentChord.notes[i-1] + 12) % 12;
          }
          break;
        }
      }
      
      if (chordStep >= 0) {
        keysSpriteBuffer.setTextSize(0.35);
        keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + 35);
        keysSpriteBuffer.setTextColor(PAL_LIGHTGREY);
        keysSpriteBuffer.printf("%d", chordStep);
      }
    }

    // Draw velocity bar if this key is in scale (only in SCALE_STATE)
    if (currentMode == SCALE_STATE && this->inScale) {
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
          barColor = PAL_YELLOW;  // Hold: palette index 7 (now yellow)
        } else if (enc[encoderIdx].shortPress) {
          barColor = PAL_RED_40;  // Short press: dim red (was orange)
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
// BUTTON ACTION DISPATCH (scaffold; not yet wired)
//========================================================================
struct ButtonAction {
  void (*onPressStart)(uint8_t encIndex);
  void (*onReleaseShort)(uint8_t encIndex);
  void (*onHold)(uint8_t encIndex);
  void (*onPressWhileLatched)(uint8_t encIndex);
  bool allowsLatch;
};

// Forward declarations for action handlers (stubs for now)
void act_note_onPressStart(uint8_t i);     // scale key-like
void act_note_onReleaseShort(uint8_t i);
void act_note_onHold(uint8_t i);
void act_note_onPressWhileLatched(uint8_t i);

void act_chord_onPressStart(uint8_t i);    // encoder 6 chord trigger
void act_chord_onReleaseShort(uint8_t i);
void act_chord_onHold(uint8_t i);
void act_chord_onPressWhileLatched(uint8_t i);

void act_panic_onPressStart(uint8_t i);    // encoder 5 panic (chord mode only)
void act_panic_onReleaseShort(uint8_t i);
void act_panic_onHold(uint8_t i);
void act_panic_onPressWhileLatched(uint8_t i);

void act_mode_onPressStart(uint8_t i);     // encoder 7 mode cycle (short only)
void act_mode_onReleaseShort(uint8_t i);
void act_mode_onHold(uint8_t i);
void act_mode_onPressWhileLatched(uint8_t i);

// Assign-mode trigger handlers
void act_assign_onPressStart(uint8_t i);
void act_assign_onReleaseShort(uint8_t i);
void act_assign_onHold(uint8_t i);
void act_assign_onPressWhileLatched(uint8_t i);

// Concrete action instances
static ButtonAction ACTION_NOTE_KEY = {
  .onPressStart = act_note_onPressStart,
  .onReleaseShort = act_note_onReleaseShort,
  .onHold = act_note_onHold,
  .onPressWhileLatched = act_note_onPressWhileLatched,
  .allowsLatch = true
};

static ButtonAction ACTION_CHORD_TRIGGER = {
  .onPressStart = act_chord_onPressStart,
  .onReleaseShort = act_chord_onReleaseShort,
  .onHold = act_chord_onHold,
  .onPressWhileLatched = act_chord_onPressWhileLatched,
  .allowsLatch = true
};

static ButtonAction ACTION_PANIC = {
  .onPressStart = act_panic_onPressStart,
  .onReleaseShort = act_panic_onReleaseShort,
  .onHold = act_panic_onHold,
  .onPressWhileLatched = act_panic_onPressWhileLatched,
  .allowsLatch = false
};

static ButtonAction ACTION_MODE_CYCLE = {
  .onPressStart = act_mode_onPressStart,
  .onReleaseShort = act_mode_onReleaseShort,
  .onHold = act_mode_onHold,
  .onPressWhileLatched = act_mode_onPressWhileLatched,
  .allowsLatch = false
};

static ButtonAction ACTION_ASSIGN_TRIGGER = {
  .onPressStart = act_assign_onPressStart,
  .onReleaseShort = act_assign_onReleaseShort,
  .onHold = act_assign_onHold,
  .onPressWhileLatched = act_assign_onPressWhileLatched,
  .allowsLatch = true
};

// No-op action (does nothing)
static ButtonAction ACTION_NOOP = {
  .onPressStart = nullptr,
  .onReleaseShort = nullptr,
  .onHold = nullptr,
  .onPressWhileLatched = nullptr,
  .allowsLatch = false
};

// Action maps per mode (SCALE_STATE, CHORD_STATE)
static ButtonAction* actionMap[2][8];

// Alternative mapping for CHORD_ASSIGN
static ButtonAction* const kChordAssignMap[8] = {
  &ACTION_ASSIGN_TRIGGER, &ACTION_ASSIGN_TRIGGER, &ACTION_ASSIGN_TRIGGER, &ACTION_ASSIGN_TRIGGER,
  &ACTION_ASSIGN_TRIGGER, &ACTION_ASSIGN_TRIGGER, &ACTION_ASSIGN_TRIGGER, &ACTION_MODE_CYCLE
};

// Configuration defaults
static ButtonAction* const kDefaultScaleMap[8] = {
  &ACTION_NOTE_KEY, &ACTION_NOTE_KEY, &ACTION_NOTE_KEY, &ACTION_NOTE_KEY,
  &ACTION_NOTE_KEY, &ACTION_NOTE_KEY, &ACTION_NOTE_KEY, &ACTION_MODE_CYCLE
};

static ButtonAction* const kDefaultChordMap[8] = {
  &ACTION_CHORD_TRIGGER, &ACTION_CHORD_TRIGGER, &ACTION_NOOP, &ACTION_NOOP,
  &ACTION_NOOP, &ACTION_NOOP, &ACTION_NOOP, &ACTION_MODE_CYCLE
};

void initActionMaps() {
  // Copy defaults into live map
  for (int j = 0; j < 8; ++j) actionMap[SCALE_STATE][j] = kDefaultScaleMap[j];
  for (int j = 0; j < 8; ++j) actionMap[CHORD_STATE][j] = kDefaultChordMap[j];
}

// Swap CHORD_STATE row based on submode
void applyChordSubModeMapping() {
  if (chordSubMode == CHORD_ASSIGN) {
    for (int j = 0; j < 8; ++j) actionMap[CHORD_STATE][j] = kChordAssignMap[j];
  } else {
    for (int j = 0; j < 8; ++j) actionMap[CHORD_STATE][j] = kDefaultChordMap[j];
  }
}

// Chord function forward declarations (ensure availability before use)
void initChordSystem();
void updateCurrentChord();
void selectChordByDegree(int8_t degree);
void changeChordType(ChordType newType);

// Generic button/LED helpers (to unify button state handling)
enum ButtonVisualState : uint8_t { VIS_IDLE_OFF = 0, VIS_SHORT_RED40 = 1, VIS_LATCH_YELLOW = 2 };
void setButtonVisual(uint8_t encIndex, ButtonVisualState state);
void btnStartPress(uint8_t encIndex);
void btnLatchHold(uint8_t encIndex);
void btnUnlatchOnPress(uint8_t encIndex);
void btnReleaseShort(uint8_t encIndex);

// Panic helpers
void panicAllNotesOff();
void handlePanicResetChordIfLatched();
void clearAllEncoderLatchesAndVisuals();

// BLE MIDI chord helpers
void sendCurrentChordNoteOn(uint8_t velocity);
void sendCurrentChordNoteOff();
uint8_t midiFromPitchClass(int8_t pitchClass);

// Assign-mode helpers
void initAssignSlotsDefaults();
ChordType determineDiatonicTriadForDegree(int8_t degree);

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

// Compute MIDI note from pitch class and current octave (C4=60)
uint8_t midiFromPitchClass(int8_t pitchClass) {
  int midi = (pitchClass % 12);
  if (midi < 0) midi += 12;
  midi += ((currentOctave + 1) * 12);
  if (midi < 0) midi = 0;
  if (midi > 127) midi = 127;
  return static_cast<uint8_t>(midi);
}

// Send NoteOn for all notes in currentChord using provided velocity
void sendCurrentChordNoteOn(uint8_t velocity) {
  if (!bleMidiConnected) return;
  for (uint8_t i = 0; i < currentChord.noteCount && i < 6; i++) {
    uint8_t note = midiFromPitchClass(currentChord.notes[i]);
    MIDI.sendNoteOn(note, velocity, midiChannel);
#if DEBUG_VERBOSE
    Serial.printf("[BLE MIDI] Chord NoteOn: %d, vel: %d, ch: %d\n", note, velocity, midiChannel);
#endif
  }
  chordPlaying = true;
}

// Send NoteOff for all notes in currentChord
void sendCurrentChordNoteOff() {
  if (!bleMidiConnected) return;
  for (uint8_t i = 0; i < currentChord.noteCount && i < 6; i++) {
    uint8_t note = midiFromPitchClass(currentChord.notes[i]);
    MIDI.sendNoteOff(note, 0, midiChannel);
#if DEBUG_VERBOSE
    Serial.printf("[BLE MIDI] Chord NoteOff: %d, ch: %d\n", note, midiChannel);
#endif
  }
  chordPlaying = false;
}

//========================================================================
// BUTTON/LED HELPERS - IMPLEMENTATIONS (no behavior change yet)
//========================================================================
void panicAllNotesOff() {
  if (!bleMidiConnected) return;
  // CC-only panic to avoid flooding BLE with 128 NoteOffs
  MIDI.sendControlChange(64, 0, midiChannel);   // Sustain off
  MIDI.sendControlChange(123, 0, midiChannel);  // All Notes Off (per channel)
  MIDI.sendControlChange(120, 0, midiChannel);  // All Sound Off (per channel)
}

void handlePanicResetChordIfLatched() {
  if (currentMode == CHORD_STATE && enc[6].isActive) {
    if (bleMidiConnected) {
      sendCurrentChordNoteOff();
    }
    chordPlaying = false;
    btnUnlatchOnPress(6);
  }
}

// Clear all encoder latch states and LEDs/visuals
void clearAllEncoderLatchesAndVisuals() {
  for (int i = 0; i < NUM_ENCODERS; ++i) {
    if (enc[i].isActive || enc[i].shortPress) {
      enc[i].isActive = false;
      enc[i].shortPress = false;
      waitingForNextPress[i] = false;
      buttonHoldProcessed[i] = true; // avoid treating release as short
      setButtonVisual(i, VIS_IDLE_OFF);
    }
  }
}

void setButtonVisual(uint8_t encIndex, ButtonVisualState state) {
  switch (state) {
    case VIS_IDLE_OFF:
      enc[encIndex].shortPress = false;
      sensor.setLEDColor(encIndex, LED_COLOR_OFF);
      break;
    case VIS_SHORT_RED40:
      enc[encIndex].shortPress = true;
      // Short press now uses dim red instead of orange
      sensor.setLEDColor(encIndex, LED_COLOR_RED_40);
      break;
    case VIS_LATCH_YELLOW:
      enc[encIndex].shortPress = false;
      // Latch now uses yellow instead of green
      sensor.setLEDColor(encIndex, LED_COLOR_YELLOW);
      break;
  }
}

void btnStartPress(uint8_t encIndex) {
  buttonPressStartTime[encIndex] = millis();
  buttonHoldProcessed[encIndex] = false;
  setButtonVisual(encIndex, VIS_SHORT_RED40);
}

void btnLatchHold(uint8_t encIndex) {
  enc[encIndex].isActive = true;
  enc[encIndex].shortPress = false;
  waitingForNextPress[encIndex] = true;
  buttonHoldProcessed[encIndex] = true;
  setButtonVisual(encIndex, VIS_LATCH_YELLOW);
}

void btnUnlatchOnPress(uint8_t encIndex) {
  enc[encIndex].isActive = false;
  enc[encIndex].shortPress = false;
  waitingForNextPress[encIndex] = false;
  // Mark as processed so release edge isn’t treated as short press
  buttonHoldProcessed[encIndex] = true;
  setButtonVisual(encIndex, VIS_IDLE_OFF);
}

void btnReleaseShort(uint8_t encIndex) {
  // Only for non-latched, non-held short-press releases
  enc[encIndex].shortPress = false;
  setButtonVisual(encIndex, VIS_IDLE_OFF);
  // Reset hold flag for next cycle
  buttonHoldProcessed[encIndex] = false;
}

//========================================================================
// BLE MIDI CALLBACKS
//========================================================================
//========================================================================
// BUTTON ACTION HANDLERS (stubs; behavior already handled elsewhere)
//========================================================================
void act_note_onPressStart(uint8_t i) {
  // Start short press: visual dim red and optional NoteOn
  btnStartPress(i);
  bool sendsMidi = (!currentSwitchState && i <= 6);
  if (bleMidiConnected && sendsMidi) {
    uint8_t midiNote = getEncoderMidiNote(i);
    uint8_t velocity = getEncoderVelocity(i);
    MIDI.sendNoteOn(midiNote, velocity, midiChannel);
#if DEBUG_VERBOSE
    Serial.printf("[BLE MIDI] NoteOn: %d, vel: %d, ch: %d\n", midiNote, velocity, midiChannel);
#endif
  }
}

void act_note_onPressWhileLatched(uint8_t i) {
  // Deactivate on press while latched
  bool sendsMidi = (!currentSwitchState && i <= 6);
  if (bleMidiConnected && sendsMidi) {
    uint8_t midiNote = getEncoderMidiNote(i);
    MIDI.sendNoteOff(midiNote, 0, midiChannel);
#if DEBUG_VERBOSE
    Serial.printf("[BLE MIDI] NoteOff (deactivate): %d, ch: %d\n", midiNote, midiChannel);
#endif
  }
  btnUnlatchOnPress(i);
}

void act_note_onHold(uint8_t i) {
  // Latch the note (LED yellow); MIDI already on from press
  btnLatchHold(i);
}

void act_note_onReleaseShort(uint8_t i) {
  // If not latched and not processed by hold, send NoteOff and clear visual
  if (!enc[i].isActive && !buttonHoldProcessed[i]) {
    bool sendsMidi = (!currentSwitchState && i <= 6);
    if (bleMidiConnected && sendsMidi) {
      uint8_t midiNote = getEncoderMidiNote(i);
      MIDI.sendNoteOff(midiNote, 0, midiChannel);
#if DEBUG_VERBOSE
      Serial.printf("[BLE MIDI] NoteOff (release): %d, ch: %d\n", midiNote, midiChannel);
#endif
    }
    btnReleaseShort(i);
  }
}

// Chord trigger (encoder 6 in chord mode)
// Chord trigger (press on configured encoders in chord mode)
void act_chord_onPressStart(uint8_t i) {
  if (enc[i].isActive) {
    if (bleMidiConnected) sendCurrentChordNoteOff();
    btnUnlatchOnPress(i);
  } else {
    if (bleMidiConnected) {
      uint8_t vel = getEncoderVelocity(6);
      sendCurrentChordNoteOn(vel);
    }
    btnStartPress(i);
  }
}
void act_chord_onReleaseShort(uint8_t i) {
  if (!enc[i].isActive && !buttonHoldProcessed[i]) {
    if (bleMidiConnected) sendCurrentChordNoteOff();
    btnReleaseShort(i);
  }
}
void act_chord_onHold(uint8_t i) { btnLatchHold(i); }
void act_chord_onPressWhileLatched(uint8_t i) { act_chord_onPressStart(i); }

// Panic (encoder 5 in chord mode)
void act_panic_onPressStart(uint8_t i) {
  panicAllNotesOff();
  handlePanicResetChordIfLatched();
  // Give LED driver a moment after turning off encoder 6 before lighting encoder 5
  delay(2);
  btnStartPress(i);
  // Reassert visual to ensure LED uses dim red (was orange)
  setButtonVisual(i, VIS_SHORT_RED40);
}
void act_panic_onReleaseShort(uint8_t i) {
  btnReleaseShort(i);
}
void act_panic_onHold(uint8_t i) {}
void act_panic_onPressWhileLatched(uint8_t i) {}

// Assign-mode trigger (encoders 0–6)
void act_assign_onPressStart(uint8_t i) {
  if (enc[i].isActive) {
    if (bleMidiConnected) sendCurrentChordNoteOff();
    btnUnlatchOnPress(i);
    return;
  }
  int8_t d = (i <= 6) ? assignSlots[i].degree : 0;
  ChordType t = determineDiatonicTriadForDegree(d);
  currentChord.type = t;
  selectChordByDegree(d);
  if (bleMidiConnected) {
    uint8_t vel = getEncoderVelocity((i <= 6) ? i : 6);
    sendCurrentChordNoteOn(vel);
  }
  btnStartPress(i);
}
void act_assign_onReleaseShort(uint8_t i) {
  if (!enc[i].isActive && !buttonHoldProcessed[i]) {
    if (bleMidiConnected) sendCurrentChordNoteOff();
    btnReleaseShort(i);
  }
}
void act_assign_onHold(uint8_t i) { btnLatchHold(i); }
void act_assign_onPressWhileLatched(uint8_t i) { act_assign_onPressStart(i); }

// Mode cycle (encoder 7)
void act_mode_onPressStart(uint8_t i) {
  buttonPressStartTime[i] = millis();
  enc[i].shortPress = true;
  // Ensure a fresh hold cycle regardless of prior state
  buttonHoldProcessed[i] = false;
}
void act_mode_onReleaseShort(uint8_t i) {
  unsigned long pressDuration = millis() - buttonPressStartTime[i];
  // If hold already handled, just clear visual
  if (buttonHoldProcessed[i]) {
    enc[i].shortPress = false;
    return;
  }
  if (pressDuration < 500) {
    // Short press cycles parameter mode
    encoder7Mode = static_cast<EncoderMode>((encoder7Mode + 1) % 4);
    modeChanged = true;
  } else if (pressDuration >= BUTTON_HOLD_TIME && currentMode == CHORD_STATE) {
    // Fallback: treat long press as submode toggle if hold path missed
    chordSubMode = (chordSubMode == CHORD_NORMAL) ? CHORD_ASSIGN : CHORD_NORMAL;
    if (chordSubMode == CHORD_ASSIGN) initAssignSlotsDefaults();
    applyChordSubModeMapping();
    modeChanged = true;
  }
  enc[i].shortPress = false;
  // Reset hold flag so next cycle can detect holds again
  buttonHoldProcessed[i] = false;
}
void act_mode_onHold(uint8_t i) {
  // Long press on encoder 7 toggles assign submode in chord mode
  if (i == 7 && currentMode == CHORD_STATE) {
    chordSubMode = (chordSubMode == CHORD_NORMAL) ? CHORD_ASSIGN : CHORD_NORMAL;
    if (chordSubMode == CHORD_ASSIGN) {
      initAssignSlotsDefaults();
    }
    applyChordSubModeMapping();
    modeChanged = true;
  }
}
void act_mode_onPressWhileLatched(uint8_t i) {}

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
  Serial.printf("Sketch build: %s %s\n", __DATE__, __TIME__);

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
  keysSprite.pushSprite(0, 53);
  infoSprite.pushSprite(0, 0);  // draw this on top
  encsSprite.pushSprite(0, 200);

  Serial.println("Setup complete");

  // Initialize chord system
  initChordSystem();

  // Initialize button action maps (not yet used for dispatch)
  initActionMaps();
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
        //Serial.print("Encoder 7 delta (mode ");
        //Serial.print(encoder7Mode);
        //Serial.print("): ");
        //Serial.println(delta);
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
                  updateCurrentChord();
                  encoderChanged = true;
                  break;
                }

              case MODE_ROOT:
                {
                  Serial.print("Root change triggered: ");
                  Serial.println(change);

                  // Snapshot previous chord notes for revoice if latched
                  int8_t prevNotes[6];
                  uint8_t prevCount = currentChord.noteCount;
                  for (uint8_t k = 0; k < prevCount && k < 6; ++k) prevNotes[k] = currentChord.notes[k];

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
                  updateCurrentChord();

                  // Revoice if chord is held in chord mode
                  if (currentMode == CHORD_STATE && enc[6].isActive && bleMidiConnected) {
                    for (uint8_t k = 0; k < prevCount && k < 6; ++k) {
                      uint8_t offNote = midiFromPitchClass(prevNotes[k]);
                      MIDI.sendNoteOff(offNote, 0, midiChannel);
                    }
                    uint8_t vel6 = getEncoderVelocity(6);
                    sendCurrentChordNoteOn(vel6);
                  }

                  updateOctaveNumbers();
                  encoderChanged = true;
                  break;
                }

              case MODE_OCTAVE:
                {
                  Serial.print("Octave change triggered: ");
                  Serial.println(change);

                  // Snapshot previous octave and chord notes
                  int prevOctave = currentOctave;
                  int8_t prevNotes[6];
                  uint8_t prevCount = currentChord.noteCount;
                  for (uint8_t k = 0; k < prevCount && k < 6; ++k) prevNotes[k] = currentChord.notes[k];

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

                  // Revoice if chord is held in chord mode (use prev octave for NoteOff)
                  if (currentMode == CHORD_STATE && enc[6].isActive && bleMidiConnected) {
                    for (uint8_t k = 0; k < prevCount && k < 6; ++k) {
                      int pc = prevNotes[k] % 12; if (pc < 0) pc += 12;
                      int midi = pc + ((prevOctave + 1) * 12);
                      if (midi < 0) midi = 0; if (midi > 127) midi = 127;
                      MIDI.sendNoteOff((uint8_t)midi, 0, midiChannel);
                    }
                    uint8_t vel6 = getEncoderVelocity(6);
                    sendCurrentChordNoteOn(vel6);
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
  static bool touchWasDown = false;
  static unsigned long lastTouchPanicMs = 0;
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

  // Global touch-to-panic (any touch)
  {
    bool touchNow = (M5.Touch.getCount() > 0);
    if (touchNow && !touchWasDown) {
      // Edge: touch began
      unsigned long nowMs = millis();
      if (nowMs - lastTouchPanicMs > 150) { // simple debounce
        panicAllNotesOff();
        handlePanicResetChordIfLatched();
        clearAllEncoderLatchesAndVisuals();
        chordPlaying = false; // ensure key highlights dim to RED_40
        modeChanged = true; // force redraw of visuals reflecting cleared latches
        lastTouchPanicMs = nowMs;
      }
    }
    touchWasDown = touchNow;
  }

  // Keep BLE MIDI stack alive
  MIDI.read();

  // ==================================================================
  // READ SWITCH STATE
  // ==================================================================
  bool currentSwitchState = sensor.getSwitchStatus();
  i2cManager.resetErrorCount();

// Handle switch state change
if (currentSwitchState != lastSwitchState) {
  // Update LED 9 based on switch state (now yellow instead of green)
  sensor.setLEDColor(8, currentSwitchState ? LED_COLOR_YELLOW : LED_COLOR_OFF);
  lastSwitchState = currentSwitchState;
  // Update system mode based on switch state
  currentMode = currentSwitchState ? CHORD_STATE : SCALE_STATE;
  if (currentMode == CHORD_STATE) applyChordSubModeMapping();


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

    keysSprite.pushSprite(0, 53);
    infoSprite.pushSprite(0, 0);  // draw this on top
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

// Handle Encoder 0 chord selection only in CHORD_STATE (disabled in assign)
    if (i == 0 && currentMode == CHORD_STATE && chordSubMode == CHORD_NORMAL) {
      static int32_t lastEncoder0Value = 60;  // Match startup value

      if (stableValue != lastEncoder0Value) {
        int32_t delta = (stableValue > lastEncoder0Value) ? 1 : -1;
        lastEncoder0Value = stableValue;

        static int32_t chordStepAccumulator = 0;
        chordStepAccumulator += delta;

        // Require 2 steps before changing (reduces sensitivity)
        if (abs(chordStepAccumulator) >= 2) {
          int chordChange = chordStepAccumulator / 2;
          chordStepAccumulator = 0;

          int8_t newDegree = currentChordDegree + chordChange;
          // If chord is latched, snapshot current notes to send NoteOff before change
          int8_t prevNotes[6];
          uint8_t prevCount = currentChord.noteCount;
          for (uint8_t k = 0; k < prevCount && k < 6; ++k) prevNotes[k] = currentChord.notes[k];

          selectChordByDegree(newDegree);

          // Retrigger chord MIDI with new notes if chord is currently sounding
          if (bleMidiConnected && chordPlaying) {
            for (uint8_t k = 0; k < prevCount && k < 6; ++k) {
              uint8_t offNote = midiFromPitchClass(prevNotes[k]);
              MIDI.sendNoteOff(offNote, 0, midiChannel);
            }
            uint8_t vel = getEncoderVelocity(6);
            sendCurrentChordNoteOn(vel);
          }

          encoderChanged = true;  // Trigger display update
        }
      }
    }


// Handle Encoder 1 chord type selection only in CHORD_STATE (disabled in assign)
    if (i == 1 && currentMode == CHORD_STATE && chordSubMode == CHORD_NORMAL) {
      static int32_t lastEncoder1Value = 60;  // Match startup value

      if (stableValue != lastEncoder1Value) {
        int32_t delta = (stableValue > lastEncoder1Value) ? 1 : -1;
        Serial.print("Encoder 1 delta: ");
        Serial.println(delta);
        lastEncoder1Value = stableValue;

        static int32_t typeStepAccumulator = 0;
        typeStepAccumulator += delta;

        // Require 2 steps before changing
        if (abs(typeStepAccumulator) >= 2) {
          int typeChange = typeStepAccumulator / 2;
          typeStepAccumulator = 0;

          // Cycle through chord types
          int currentTypeIndex = static_cast<int>(currentChord.type);
          int newTypeIndex = currentTypeIndex + typeChange;

          // Wrap around available types (0-6)
          if (newTypeIndex > 6) newTypeIndex = 0;
          if (newTypeIndex < 0) newTypeIndex = 6;

          Serial.print("Chord type change: ");
          Serial.print(currentTypeIndex);
          Serial.print(" -> ");
          Serial.println(newTypeIndex);

          // If chord is latched, snapshot current notes to send NoteOff before change
          int8_t prevNotes[6];
          uint8_t prevCount = currentChord.noteCount;
          for (uint8_t k = 0; k < prevCount && k < 6; ++k) prevNotes[k] = currentChord.notes[k];

          changeChordType(static_cast<ChordType>(newTypeIndex));

          // Retrigger chord MIDI with new notes if chord is currently sounding
          if (bleMidiConnected && chordPlaying) {
            for (uint8_t k = 0; k < prevCount && k < 6; ++k) {
              uint8_t offNote = midiFromPitchClass(prevNotes[k]);
              MIDI.sendNoteOff(offNote, 0, midiChannel);
            }
            uint8_t vel = getEncoderVelocity(6);
            sendCurrentChordNoteOn(vel);
          }

          encoderChanged = true;  // Trigger display update
        }
      }
    }

// Assign-mode degree rotation per encoder (0–6), silent UI update only
    if (currentMode == CHORD_STATE && chordSubMode == CHORD_ASSIGN && i <= 6) {
      static int32_t lastAssignVal[7] = {60,60,60,60,60,60,60};
      if (stableValue != lastAssignVal[i]) {
        int delta = (stableValue > lastAssignVal[i]) ? 1 : -1;
        lastAssignVal[i] = stableValue;
        static int32_t acc[7] = {0};
        acc[i] += delta;
        if (abs(acc[i]) >= 2) {
          int change = acc[i] / 2; acc[i] = 0;
          int nd = assignSlots[i].degree + change;
          while (nd < 0) nd += 7; while (nd > 6) nd -= 7;
          assignSlots[i].degree = (int8_t)nd;
          encoderChanged = true;
        }
      }
    }

// ==================================================================
// HANDLE BUTTON PRESS/HOLD/RELEASE
// ==================================================================

// Button press detected
if (buttonPressed && !buttonWasPressed[i]) {
  // Unified dispatch for all encoders via action map
  ButtonAction* A = actionMap[currentMode][i];
  if (A) {
    if (enc[i].isActive && waitingForNextPress[i]) {
      if (A->onPressWhileLatched) A->onPressWhileLatched(i);
    } else {
      if (A->onPressStart) A->onPressStart(i);
    }
    encoderChanged = true;
    buttonWasPressed[i] = buttonPressed;
    continue;
  }
}
// Button released
else if (!buttonPressed && buttonWasPressed[i]) {
  // Unified dispatch for all encoders on release
  ButtonAction* A = actionMap[currentMode][i];
  if (A) {
    if (A->onReleaseShort) A->onReleaseShort(i);
    encoderChanged = true;
    buttonWasPressed[i] = buttonPressed;
    continue;
  }

}
// Button held (check for 1 second hold time)
else if (buttonPressed && buttonWasPressed[i] && !buttonHoldProcessed[i] && !enc[i].isActive) {
  unsigned long now = millis();
// Encoder 7 (mode button) does not latch, but we still allow onHold action (e.g., submode toggle)
  if (i == 7) {
    if (now - buttonPressStartTime[i] >= BUTTON_HOLD_TIME) {
      ButtonAction* A = actionMap[currentMode][i];
      if (A && A->onHold) A->onHold(i);
      buttonHoldProcessed[i] = true;
      encoderChanged = true;
    }
    continue;
  }
  // Encoder 5 no longer reserved for panic; allow normal hold/press flow
// CHORD_STATE, encoder 6: latch on hold (via action)
  if (currentMode == CHORD_STATE && i == 6) {
    if (now - buttonPressStartTime[i] >= BUTTON_HOLD_TIME) {
      ButtonAction* A = actionMap[currentMode][i];
      if (A && A->onHold) A->onHold(i);
      encoderChanged = true;
    }
    continue;
  }

  // Unified hold dispatch for all other encoders (actions control whether to latch)
  if (now - buttonPressStartTime[i] >= BUTTON_HOLD_TIME) {
    ButtonAction* A = actionMap[currentMode][i];
    if (A && A->onHold) A->onHold(i);
    encoderChanged = true;
  }
}

// Update edge state
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

    keysSprite.pushSprite(0, 53);  // Moved down 5px to avoid cutoff of info sprite
    infoSprite.pushSprite(0, 0);   // draw this on top
    encsSprite.pushSprite(0, 200);

    // Reset modeChanged flag after handling
    modeChanged = false;
  }
}

//========================================================================
// HELPER FUNCTIONS - IMPLEMENTATIONS
//========================================================================

void initAssignSlotsDefaults() {
  for (int i = 0; i < 7; ++i) assignSlots[i].degree = i;
}

ChordType determineDiatonicTriadForDegree(int8_t degree) {
  int8_t d0 = degree % 7; if (d0 < 0) d0 += 7;
  auto norm12 = [](int v){ int n = v % 12; return (n < 0) ? n + 12 : n; };
  int pc0 = norm12(scalemanager.getScaleNote(d0));
  int pc1 = norm12(scalemanager.getScaleNote((d0 + 2) % 7));
  int pc2 = norm12(scalemanager.getScaleNote((d0 + 4) % 7));
  int i1 = (pc1 - pc0 + 12) % 12;
  int i2 = (pc2 - pc1 + 12) % 12;
  if (i1 == 4 && i2 == 3) return CHORD_TRIAD;
  if (i1 == 3 && i2 == 4) return CHORD_MINOR_TRIAD;
  if (i1 == 3 && i2 == 3) return CHORD_DIMINISHED;
  if (i1 == 4 && i2 == 4) return CHORD_AUGMENTED;
  return CHORD_TRIAD;
}

// Initialize all sprite buffers with color palettes
bool initializeSprites() {
  // Info sprite (top bar with scale info)
  infoSprite.setColorDepth(4);
  if (!infoSprite.createSprite(320, 55)) return false;
  infoSprite.setPaletteColor(0, 0x0001);   // Transparent
  infoSprite.setPaletteColor(1, 0xffff);   // White
  infoSprite.setPaletteColor(2, 0x0000);   // Black
  infoSprite.setPaletteColor(3, 0x39c7);   // Dark grey
  infoSprite.setPaletteColor(4, 0x7bef);   // Medium grey
  infoSprite.setPaletteColor(5, 0xd69a);   // Light grey
infoSprite.setPaletteColor(6, 0xf800);   // Red
infoSprite.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
infoSprite.setPaletteColor(8, 0x00ff);   // Blue
infoSprite.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
infoSprite.setPaletteColor(10, 0x6000);  // Red 40%
  infoSprite.setFont(&DIN_Condensed_Bold30pt7b);
  infoSprite.fillSprite(0);

  // Keys sprite (piano keyboard display)
  keysSprite.setColorDepth(4);
  if (!keysSprite.createSprite(320, 145)) return false;
  keysSprite.setPaletteColor(0, 0x0001);
  keysSprite.setPaletteColor(1, 0xffff);
  keysSprite.setPaletteColor(2, 0x0000);
  keysSprite.setPaletteColor(3, 0x39c7);
  keysSprite.setPaletteColor(4, 0x7bef);
  keysSprite.setPaletteColor(5, 0xd69a);
keysSprite.setPaletteColor(6, 0xf800);
keysSprite.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
keysSprite.setPaletteColor(8, 0x00ff);
keysSprite.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
keysSprite.setPaletteColor(10, 0x6000);  // Red 40%
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
encsSprite.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
encsSprite.setPaletteColor(8, 0x00ff);
encsSprite.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
encsSprite.setPaletteColor(10, 0x6000);  // Red 40%
  encsSprite.setFont(&DIN_Condensed_Bold30pt7b);
  encsSprite.fillSprite(0);

  // Initialize buffer sprites (same configuration as main sprites)
  infoSpriteBuffer.setColorDepth(4);
  if (!infoSpriteBuffer.createSprite(320, 55)) return false;
  infoSpriteBuffer.setPaletteColor(0, 0x0001);
  infoSpriteBuffer.setPaletteColor(1, 0xffff);
  infoSpriteBuffer.setPaletteColor(2, 0x0000);
  infoSpriteBuffer.setPaletteColor(3, 0x39c7);
  infoSpriteBuffer.setPaletteColor(4, 0x7bef);
  infoSpriteBuffer.setPaletteColor(5, 0xd69a);
infoSpriteBuffer.setPaletteColor(6, 0xf800);
infoSpriteBuffer.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
infoSpriteBuffer.setPaletteColor(8, 0x00ff);
infoSpriteBuffer.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
infoSpriteBuffer.setPaletteColor(10, 0x6000);  // Red 40%
  infoSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  infoSpriteBuffer.fillSprite(0);

  keysSpriteBuffer.setColorDepth(4);
  if (!keysSpriteBuffer.createSprite(320, 145)) return false;
  keysSpriteBuffer.setPaletteColor(0, 0x0001);
  keysSpriteBuffer.setPaletteColor(1, 0xffff);
  keysSpriteBuffer.setPaletteColor(2, 0x0000);
  keysSpriteBuffer.setPaletteColor(3, 0x39c7);
  keysSpriteBuffer.setPaletteColor(4, 0x7bef);
  keysSpriteBuffer.setPaletteColor(5, 0xd69a);
keysSpriteBuffer.setPaletteColor(6, 0xf800);
keysSpriteBuffer.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
keysSpriteBuffer.setPaletteColor(8, 0x00ff);
keysSpriteBuffer.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
keysSpriteBuffer.setPaletteColor(10, 0x6000);  // Red 40%
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
encsSpriteBuffer.setPaletteColor(7, 0xffe0);   // Yellow (replaces green)
encsSpriteBuffer.setPaletteColor(8, 0x00ff);
encsSpriteBuffer.setPaletteColor(9, 0xfd20);   // Orange (legacy; prefer PAL_RED_40 for short-press)
encsSpriteBuffer.setPaletteColor(10, 0x6000);  // Red 40%
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
  int boxWidth = 177;  // Fixed width to properly contain all scale names
  int boxHeight = 53;  // Original 50 + 3px

  // Draw root note name (without octave number) - shifted 3px down and right
  infoSpriteBuffer.setTextSize(0.55);
  infoSpriteBuffer.setTextDatum(TL_DATUM);
  infoSpriteBuffer.setCursor(3, 3);

  // working - but not required
  // // Draw mode indicator in top-left corner
  // infoSpriteBuffer.setTextSize(0.35);
  // infoSpriteBuffer.setTextDatum(TL_DATUM);
  // infoSpriteBuffer.setCursor(3, 40);  // Bottom of info area
  // infoSpriteBuffer.setTextColor(PAL_LIGHTGREY);
  // infoSpriteBuffer.printf("MODE: %s", currentMode == SCALE_STATE ? "SCALE" : "CHORD");

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
  infoSpriteBuffer.setCursor(4, 33);
  infoSpriteBuffer.setTextColor(PAL_LIGHTGREY);
  infoSpriteBuffer.printf(intvlFormula[scaleNo]);

  // Draw rounded rectangle outline around all labels
  infoSpriteBuffer.setColor(PAL_WHITE);
  infoSpriteBuffer.drawRoundRect(0, 0, boxWidth, boxHeight, 5);

  // Only show chord display in CHORD_STATE (switch ON)
  if (currentMode != CHORD_STATE) {
    return;  // Skip chord display completely in SCALE_STATE
  }

  // ==================================================================
  // IMPROVED CHORD DISPLAY (only shown in CHORD_STATE)
  // ==================================================================

  int chordAreaX = 182;      // 5px from right edge of scale info block (177 + 5)
  int chordAreaWidth = 138;  // From chord area start to screen edge (320 - 182)
  int chordAreaHeight = 57;  // Match scale info box height

  // Draw chord area as rounded rectangle
  infoSpriteBuffer.setColor(PAL_DARKGREY);
  infoSpriteBuffer.fillRoundRect(chordAreaX, 0, chordAreaWidth, chordAreaHeight - 4, 5, PAL_DARKGREY);
  infoSpriteBuffer.setColor(PAL_WHITE);
  infoSpriteBuffer.drawRoundRect(chordAreaX, 0, chordAreaWidth, chordAreaHeight - 4, 5, PAL_WHITE);

  // Roman numerals for chord degrees
  const char* romanNumerals[] = { "I", "ii", "iii", "IV", "V", "vi", "vii°" };
  const char* noteNamesSimple[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  const char* chordTypeNames[] = { "", "7", "m", "m7", "dim", "dim7", "aug" };  // Match ChordType enum

  if (currentChord.name.length() > 0) {
    // Get root note name
    int rootIndex = currentChord.rootNote % 12;
    String rootNoteName = String(noteNamesSimple[rootIndex]);

    // Display chord name: "I: C " or "I: Cm " or "I: C7 "
    infoSpriteBuffer.setTextSize(0.45);
    infoSpriteBuffer.setTextDatum(TL_DATUM);  // Top Left
    infoSpriteBuffer.setCursor(chordAreaX + 5, 5);
    infoSpriteBuffer.setTextColor(PAL_WHITE);

    String chordName = String(romanNumerals[currentChordDegree]) + ": " + rootNoteName + String(chordTypeNames[static_cast<int>(currentChord.type)]) + " ";
    infoSpriteBuffer.printf("%s", chordName.c_str());

    // //NOTES DISPLAY DISABLED - Commented out for cleaner chord display
    // // Build and display notes string
    // String notesStr = "";
    // for (int i = 0; i < currentChord.noteCount && i < 6; i++) {
    //   int noteIndex = currentChord.notes[i] % 12;
    //   notesStr += String(noteNamesSimple[noteIndex]);
    //   if (i < currentChord.noteCount - 1) notesStr += "-";
    // }

    // // Fixed position for notes (experiment with this value)
    // infoSpriteBuffer.setCursor(chordAreaX + 60, 5);  // Try different X positions
    // infoSpriteBuffer.printf("%s", notesStr.c_str());

    // // Display notes right-aligned within chord area
    // infoSpriteBuffer.setTextSize(0.45);
    // infoSpriteBuffer.setTextDatum(TR_DATUM);  // Top Right
    // infoSpriteBuffer.setCursor(chordAreaX + chordAreaWidth - 3, 5);  // 3px from right edge
    // infoSpriteBuffer.printf("%s", notesStr.c_str());

    // Display intervals
    infoSpriteBuffer.setTextSize(0.35);
    infoSpriteBuffer.setTextDatum(TL_DATUM);  // Top Left
    infoSpriteBuffer.setCursor(chordAreaX + 5, 28);
    infoSpriteBuffer.setTextColor(PAL_LIGHTGREY);

    // Short interval descriptions
    String structureStr = "";
    switch (currentChord.type) {
      case CHORD_TRIAD:
        structureStr = "R,Maj3,P5";
        break;
      case CHORD_MINOR_TRIAD:
        structureStr = "R,Min3,P5";
        break;
      case CHORD_DIMINISHED:
        structureStr = "R,Min3,Dim5";
        break;
      case CHORD_AUGMENTED:
        structureStr = "R,Maj3,Aug5";
        break;
      case CHORD_SEVENTH:
        structureStr = "R,Maj3,P5,Maj7";
        break;
      case CHORD_MINOR_SEVENTH:
        structureStr = "R,Min3,P5,Min7";
        break;
      case CHORD_DIMINISHED_SEVENTH:
        structureStr = "R,Min3,Dim5,Dim7";
        break;
      default:
        structureStr = "R,Maj3,P5";
        break;
    }

    infoSpriteBuffer.printf("%s", structureStr.c_str());
  } else {
    // No chord case
    infoSpriteBuffer.setTextSize(0.45);
    infoSpriteBuffer.setTextDatum(TL_DATUM);
    infoSpriteBuffer.setCursor(chordAreaX + 5, 5);
    infoSpriteBuffer.setTextColor(PAL_WHITE);
    infoSpriteBuffer.printf("%s: No chord", romanNumerals[currentChordDegree]);
  }

}

// Draw the toggle switch indicator
void drawSwitchIndicator(bool isOn) {
encsSpriteBuffer.fillRect(305, 8, 10, 20, PAL_TRANSPARENT);  // Clear area
encsSpriteBuffer.setColor(isOn ? PAL_YELLOW : PAL_DARKGREY);  // Yellow (palette idx 7) if on, grey if off
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

//========================================================================
// CHORD GENERATION FUNCTIONS
//========================================================================

// Get chord name based on root note and type
String getChordName(int8_t rootNote, ChordType type) {
  String rootName = getNoteName(rootNote);

  switch (type) {
    case CHORD_TRIAD: return rootName + " ";                 // Major triad
    case CHORD_MINOR_TRIAD: return rootName + "m ";          // Minor triad
    case CHORD_SEVENTH: return rootName + "7 ";              // Major 7th
    case CHORD_MINOR_SEVENTH: return rootName + "m7 ";       // Minor 7th
    case CHORD_DIMINISHED: return rootName + "dim ";         // Diminished
    case CHORD_DIMINISHED_SEVENTH: return rootName + "dim7 "; // Diminished 7th
    case CHORD_AUGMENTED: return rootName + "aug ";          // Augmented
    default: return rootName + " ";
  }
}

// Generate chord based on scale degree and respect selected type
void generateChordForDegree(int8_t degree) {
  if (degree < 0 || degree > 6) return;

  // Preserve the current type before clearing
  ChordType preservedType = currentChord.type;

  currentChord.clear();
  currentChordDegree = degree;
  currentChord.type = preservedType;  // Restore the type after clear!

  // Get the root note from scale
  int8_t scaleRoot = scalemanager.getScaleNote(degree);
  // scaleRoot already includes the fundamental offset, don't add it again
  int8_t absoluteRoot = scaleRoot % 12;
  currentChord.rootNote = absoluteRoot;

  // Use the preserved chord type
  ChordType chordType = currentChord.type;

  // Build the chord notes based on chord type (proper intervals)
  switch (chordType) {
    case CHORD_TRIAD:                                   // Major triad: R, M3, P5
      currentChord.notes[0] = absoluteRoot;             // Root
      currentChord.notes[1] = (absoluteRoot + 4) % 12;  // Major 3rd
      currentChord.notes[2] = (absoluteRoot + 7) % 12;  // Perfect 5th
      currentChord.noteCount = 3;
      break;

    case CHORD_MINOR_TRIAD:                             // Minor triad: R, m3, P5
      currentChord.notes[0] = absoluteRoot;             // Root
      currentChord.notes[1] = (absoluteRoot + 3) % 12;  // Minor 3rd
      currentChord.notes[2] = (absoluteRoot + 7) % 12;  // Perfect 5th
      currentChord.noteCount = 3;
      break;

    case CHORD_DIMINISHED:                              // Diminished triad: R, m3, d5
      currentChord.notes[0] = absoluteRoot;             // Root
      currentChord.notes[1] = (absoluteRoot + 3) % 12;  // Minor 3rd
      currentChord.notes[2] = (absoluteRoot + 6) % 12;  // Diminished 5th
      currentChord.noteCount = 3;
      break;

    case CHORD_AUGMENTED:                               // Augmented triad: R, M3, A5
      currentChord.notes[0] = absoluteRoot;             // Root
      currentChord.notes[1] = (absoluteRoot + 4) % 12;  // Major 3rd
      currentChord.notes[2] = (absoluteRoot + 8) % 12;  // Augmented 5th
      currentChord.noteCount = 3;
      break;

    case CHORD_SEVENTH:                                  // Major 7th: R, M3, P5, M7
      currentChord.notes[0] = absoluteRoot;              // Root
      currentChord.notes[1] = (absoluteRoot + 4) % 12;   // Major 3rd
      currentChord.notes[2] = (absoluteRoot + 7) % 12;   // Perfect 5th
      currentChord.notes[3] = (absoluteRoot + 11) % 12;  // Major 7th
      currentChord.noteCount = 4;
      break;

    case CHORD_MINOR_SEVENTH:                            // Minor 7th: R, m3, P5, m7
      currentChord.notes[0] = absoluteRoot;              // Root
      currentChord.notes[1] = (absoluteRoot + 3) % 12;   // Minor 3rd
      currentChord.notes[2] = (absoluteRoot + 7) % 12;   // Perfect 5th
      currentChord.notes[3] = (absoluteRoot + 10) % 12;  // Minor 7th
      currentChord.noteCount = 4;
      break;

    case CHORD_DIMINISHED_SEVENTH:                      // Diminished 7th: R, m3, d5, d7
      currentChord.notes[0] = absoluteRoot;             // Root
      currentChord.notes[1] = (absoluteRoot + 3) % 12;  // Minor 3rd
      currentChord.notes[2] = (absoluteRoot + 6) % 12;  // Diminished 5th
      currentChord.notes[3] = (absoluteRoot + 9) % 12;  // Diminished 7th
      currentChord.noteCount = 4;
      break;
  }

  // Generate chord name with correct type
  currentChord.name = getChordName(absoluteRoot, chordType);

  Serial.print("Generated chord: ");
  Serial.print(currentChord.name);
  Serial.print(" (Degree ");
  Serial.print(degree);
  Serial.print(", Type ");
  Serial.print(static_cast<int>(chordType));
  Serial.print(") Notes: ");
  for (int i = 0; i < currentChord.noteCount; i++) {
    Serial.print(getNoteName(currentChord.notes[i]));
    Serial.print(" ");
  }
  Serial.println();
}

// Initialize with I chord
void initChordSystem() {
  generateChordForDegree(0);  // Start with I chord
}

// Update the current chord when scale/root changes
void updateCurrentChord() {
  generateChordForDegree(currentChordDegree);  // Regenerate with current degree
}

// Simple chord display function that avoids linking issues
void displayCurrentChordSimple() {
  // This function just prepares data for display - doesn't call other functions
  // The actual drawing will be done in drawInfoArea
}

// Function to get Roman numeral for chord degree (self-contained)
String getRomanNumeral(int degree) {
  const char* romanNumerals[] = { "I", "ii", "iii", "IV", "V", "vi", "vii°" };
  if (degree >= 0 && degree < 7) {
    return String(romanNumerals[degree]);
  }
  return String(degree);
}

// Function to select chord by degree
void selectChordByDegree(int8_t degree) {
  if (degree < 0) degree = 6;  // Wrap to vii°
  if (degree > 6) degree = 0;  // Wrap to I

  currentChordDegree = degree;
  generateChordForDegree(degree);
  Serial.print("Selected chord degree: ");
  Serial.println(degree);
}

// Function to change chord type
void changeChordType(ChordType newType) {
  Serial.print("Changing chord type from ");
  Serial.print(static_cast<int>(currentChord.type));
  Serial.print(" to ");
  Serial.println(static_cast<int>(newType));

  currentChord.type = newType;
  generateChordForDegree(currentChordDegree);  // Regenerate with new type

  Serial.print("After regeneration, type is: ");
  Serial.println(static_cast<int>(currentChord.type));
}

// call via
//String numberIntervals = getChordIntervalsAsNumbers();
// Returns "0,4,7" for major triad, "0,3,7" for minor triad, etc.
// Get integer-based interval representation (semitones from root)
String getChordIntervalsAsNumbers() {
  String intervalsStr = "";

  switch (currentChord.type) {
    case CHORD_TRIAD:
      intervalsStr = "0,4,7";  // R, M3, P5
      break;
    case CHORD_MINOR_TRIAD:
      intervalsStr = "0,3,7";  // R, m3, P5
      break;
    case CHORD_DIMINISHED:
      intervalsStr = "0,3,6";  // R, m3, dim5
      break;
    case CHORD_AUGMENTED:
      intervalsStr = "0,4,8";  // R, M3, aug5
      break;
    case CHORD_SEVENTH:
      intervalsStr = "0,4,7,11";  // R, M3, P5, M7
      break;
    case CHORD_MINOR_SEVENTH:
      intervalsStr = "0,3,7,10";  // R, m3, P5, m7
      break;
    case CHORD_DIMINISHED_SEVENTH:
      intervalsStr = "0,3,6,9";  // R, m3, dim5, dim7
      break;
    default:
      intervalsStr = "0,4,7";  // Default to major triad
      break;
  }

  return intervalsStr;
}

// call via
//String numberIntervals = getChordIntervalsAsSteps();
// Returns "0,4,3" for major triad, "0,3,4" for minor triad, etc.
// Get intervalic representation (semitones between consecutive notes)
String getChordIntervalsAsSteps() {
  String stepsStr = "";

  switch (currentChord.type) {
    case CHORD_TRIAD:
      stepsStr = "0,4,3";  // R, +4 (M3), +3 (P5 from M3)
      break;
    case CHORD_MINOR_TRIAD:
      stepsStr = "0,3,4";  // R, +3 (m3), +4 (P5 from m3)
      break;
    case CHORD_DIMINISHED:
      stepsStr = "0,3,3";  // R, +3 (m3), +3 (dim5 from m3)
      break;
    case CHORD_AUGMENTED:
      stepsStr = "0,4,4";  // R, +4 (M3), +4 (aug5 from M3)
      break;
    case CHORD_SEVENTH:
      stepsStr = "0,4,3,4";  // R, +4 (M3), +3 (P5), +4 (M7 from P5)
      break;
    case CHORD_MINOR_SEVENTH:
      stepsStr = "0,3,4,3";  // R, +3 (m3), +4 (P5), +3 (m7 from P5)
      break;
    case CHORD_DIMINISHED_SEVENTH:
      stepsStr = "0,3,3,3";  // R, +3 (m3), +3 (dim5), +3 (dim7 from dim5)
      break;
    default:
      stepsStr = "0,4,3";  // Default to major triad
      break;
  }

  return stepsStr;
}

// Check if a given note (0-11) is in the current chord
bool isNoteInCurrentChord(int8_t note) {
  note = note % 12;  // Normalize to 0-11

  for (int i = 0; i < currentChord.noteCount; i++) {
    if ((currentChord.notes[i] % 12) == note) {
      return true;
    }
  }
  return false;
}
