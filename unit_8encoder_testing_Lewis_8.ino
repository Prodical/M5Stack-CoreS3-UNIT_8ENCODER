///////////////////////////////////////
/// LIBRARIES
///////////////////////////////////////


#include <M5Unified.h>
#include <M5GFX.h>

#include "UNIT_8ENCODER.h"

// output to Serial Monitor using C++ like cout <<
//#include <Streaming.h>

// Chris Ball's scale manager library - tweaked
#include "ScaleManager.h"

// custom font
#include "DIN_Condensed_Bold30pt7b.h"

// Add at the top with other includes
#include <esp_task_wdt.h>

///////////////////////////////////////
/// Library objects + global variables
///////////////////////////////////////

// declare M5GFX library object
M5GFX display;
// declare sprites and push to display
M5Canvas infoSprite(&display);
M5Canvas keysSprite(&display);
M5Canvas encsSprite(&display);

// Add double buffer sprites
M5Canvas infoSpriteBuffer(&display);
M5Canvas keysSpriteBuffer(&display);
M5Canvas encsSpriteBuffer(&display);

// custom colours
#define DARKGREY 0x39c7

// UNIT_8ENCODER
UNIT_8ENCODER sensor;
int32_t encoderValue[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int32_t encoderValueLast[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
uint8_t btn_status[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
bool switch_status = false;

// declare + initialise variable to hold width of labels used to centre them on display
int16_t labelWidth = 0;

// on display encoders
static constexpr uint8_t nosEncs = 8;
// declare structure
struct Encoders {
  int x;
  int y;
  int r;
  int encValue;
  uint16_t colourIdle;
  uint16_t colourActive;
  int8_t encId = -1;
  bool encPressed = false;
  bool isActive = false;    // Add active state tracking
  bool shortPress = false;  // Add short press state tracking
  String encNoteName = "";
  int8_t encScaleNoteNo;
  int8_t encOnceOnlySendNoteOn = 0;
  int8_t encOnceOnlySendNoteOff = 0;
  int8_t onceOnlyEncDraw = 0;

  void clearEncs(int8_t encNo) {
    encsSpriteBuffer.setColor(2);  // Use background color
    encsSpriteBuffer.fillCircle(x - 2, y - 2, r + 4, 2);
  }

  void drawEncs(int8_t encNo) {
    // Draw the encoder circle first
    uint16_t circleColor;
    if (this->isActive) {
      circleColor = 7;  // Green for active
    } else if (this->shortPress) {
      circleColor = 9;  // Orange for short press
    } else {
      circleColor = 3;  // Grey for idle
    }
    encsSpriteBuffer.setColor(circleColor);
    encsSpriteBuffer.fillCircle(x, y, r, circleColor);

    // Draw the encoder arc starting from bottom (90 degrees)
    // Use black (2) for arc when circle is not grey, white (1) when grey
    uint16_t arcColor = (circleColor == 3) ? 1 : 2;  // 1 = white, 2 = black
    encsSpriteBuffer.setColor(arcColor);
    // Start at 90 degrees (bottom) and rotate based on encoder value
    float startAngle = 90 + (this->encValue * 6) - 5;
    float endAngle = 90 + (this->encValue * 6) + 5;
    // Draw arc with different thickness based on color
    int innerRadius = r - (arcColor == 2 ? 7 : 5);  // Wider when black
    int outerRadius = r - (arcColor == 2 ? 2 : 4);  // Wider when black
    encsSpriteBuffer.drawArc(x, y, innerRadius, outerRadius, startAngle, endAngle, arcColor);
  }
};
static Encoders enc[nosEncs];
bool holdOn = false;

// Button timing arrays - add with other global variables
static unsigned long buttonPressTime[8] = { 0 };
static bool buttonHoldActivated[8] = { false };
static bool lastButtonState[8] = { true, true, true, true, true, true, true, true };


// on display keys
static constexpr uint8_t nosKeys = 12;
// to identify white keys starting from C
bool whiteKeys[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };

struct Keys {
  int x;
  int y;
  int w;
  int h;
  uint16_t colourIdle;
  uint16_t colourActive;
  int8_t kId = -1;
  int8_t keyNo = -1;
  String keyNoteName = "";
  uint8_t onceOnlyKeyDraw = 0;
  bool inScale = false;
  bool isFundamental = false;
  uint8_t octaveNo = -1;
  String octaveS = "";
  uint8_t interval;
  String intervalS;

  void clearKeys(int8_t keyNo) {
    keysSpriteBuffer.setColor(2);
    keysSpriteBuffer.fillRoundRect(x - 1, y - 1, w + 2, h + 2, 2);
  }

  void drawKeys(int8_t keyNo) {
    // Draw key outline
    keysSpriteBuffer.setColor(this->colourIdle);
    if (whiteKeys[keyNo]) {
      keysSpriteBuffer.drawRoundRect(x, y, w, h, 5);
    } else {
      // Draw thicker outline for non-white keys
      keysSpriteBuffer.drawRoundRect(x, y, w, h, 5);
      keysSpriteBuffer.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5);
      keysSpriteBuffer.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 5);
    }

    // Draw key note name
    keysSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
    keysSpriteBuffer.setTextDatum(ML_DATUM);
    keysSpriteBuffer.setTextSize(0.45);
    // Adjust cursor position based on key type
    if (whiteKeys[keyNo]) {
      keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 35);  // Position for white keys
    } else {
      keysSpriteBuffer.setCursor(this->x + w / 2 - 8, this->y + h - 35);  // Moved 3px left for black keys
    }
    if (this->inScale == true) {
      if (this->isFundamental == true) {
        keysSpriteBuffer.setTextColor(7);
      } else {
        keysSpriteBuffer.setTextColor(1);
      }
    } else {
      keysSpriteBuffer.setTextColor(3);
    }
    keysSpriteBuffer.printf(this->keyNoteName.c_str());

    // Draw key octave #
    keysSpriteBuffer.setTextSize(0.35);
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + h - 15);
    if (this->inScale == true) {
      if (this->isFundamental == true) {
        keysSpriteBuffer.setTextColor(7);
      } else {
        keysSpriteBuffer.setTextColor(1);
      }
      // Debug output for octave display
      Serial.print("Drawing octave for key ");
      Serial.print(keyNo);
      Serial.print(": ");
      Serial.println(this->octaveS);
      keysSpriteBuffer.printf(this->octaveS.c_str());
    } else {
      keysSpriteBuffer.setTextColor(3);
      keysSpriteBuffer.printf("-");
    }

    // Draw intervals
    keysSpriteBuffer.setTextSize(0.35);  // Changed from 0.8 to match octave size
    keysSpriteBuffer.setCursor(this->x + w / 2 - 4, this->y + 15);
    keysSpriteBuffer.setTextColor(1);
    keysSpriteBuffer.printf(this->intervalS.c_str());
  }
};
static Keys key[nosKeys];

// Add text width cache structure before Keys structure
#include <map>

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

// Define the global text width cache instance
TextWidthCache textWidthCache;

// Constants for encoder configuration
const int8_t ENCODER_MAX_VALUE = 60;
const int8_t ENCODER_MIN_VALUE = -60;
const uint8_t NUM_ENCODERS = 8;
const uint8_t NUM_KEYS = 12;

// Constants for display configuration
const uint16_t DISPLAY_WIDTH = 320;
const uint16_t DISPLAY_HEIGHT = 240;
const uint16_t INFO_SPRITE_HEIGHT = 50;
const uint16_t KEYS_SPRITE_HEIGHT = 150;
const uint16_t ENCS_SPRITE_HEIGHT = 40;

// Constants for colors
const uint16_t COLOR_TRANSPARENT = 0x0001;
const uint16_t COLOR_WHITE = 0xffff;
const uint16_t COLOR_BLACK = 0x0000;
const uint16_t COLOR_DARKGREY = 0x39c7;
const uint16_t COLOR_DARKGRAY = 0x7bef;
const uint16_t COLOR_LIGHTGRAY = 0xd69a;
const uint16_t COLOR_RED = 0xf800;
const uint16_t COLOR_GREEN = 0x7e0;
const uint16_t COLOR_BLUE = 0x00ff;
const uint16_t COLOR_ORANGE = 0xfd20;  // Add orange color

// Constants for update intervals
const unsigned long UPDATE_INTERVAL = 10;    // 100fps for smoother updates
const unsigned long STATUS_INTERVAL = 5000;  // 5 seconds

// Constants for scale configuration
const int8_t FUNDAMENTAL_DEFAULT = 60;
const int8_t OCTAVE_DEFAULT = 4;
const int8_t SCALE_DEFAULT = 0;

// Button state management
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

// Button state managers - add with other global variables
ButtonStateManager buttonStates[8];


// uint8_t variables/arrays for running code blocks once only
uint8_t onceOnlySendNoteOn[7] = { 0, 0, 0, 0, 0, 0, 0 };
uint8_t onceOnlySendNoteOff[7] = { 0, 0, 0, 0, 0, 0, 0 };

int count = 0;
int indexF = 0;

// Add cached values for scale manager
String cachedScaleName = "";
String cachedFundamentalName = "";
String cachedRootName = "";
bool scaleCacheNeedsUpdate = true;

// UNIT_8ENCODER LEDs
// TO DO - work up code to integrate LEDs
//------------------------------------------------------------------------
void show_rgb_led(void) {
  sensor.setAllLEDColor(0xff0000);
  delay(1000);
  sensor.setAllLEDColor(0x00ff00);
  delay(1000);
  sensor.setAllLEDColor(0x0000ff);
  delay(1000);
  sensor.setAllLEDColor(0x000000);
}

// Add structure for batch encoder reading
struct EncoderBatch {
  int32_t values[8];
  uint8_t buttonStatus[8];
  bool hasChanged;
  bool encoderChanged[8];  // Track which encoders changed
};

// Global variable for encoder batch
EncoderBatch encoderBatch = { 0 };

// Add I2C error recovery constants
const uint8_t I2C_MAX_RETRIES = 3;
const uint16_t I2C_RETRY_DELAY = 100;   // ms
const uint16_t I2C_ERROR_DELAY = 1000;  // ms

// Add I2C error recovery function
bool recoverI2C() {
  Serial.println("Attempting I2C recovery...");

  // Reset I2C bus
  Wire.end();
  delay(I2C_ERROR_DELAY);
  Wire.begin();
  Wire.setClock(100000);  // Start with lower speed
  delay(I2C_RETRY_DELAY);

  // Try to reinitialize sensor
  if (!sensor.begin(&Wire, ENCODER_ADDR, G2, G1, 100000UL)) {
    Serial.println("I2C recovery failed!");
    return false;
  }

  // Reset all encoders
  for (int i = 0; i < NUM_ENCODERS; i++) {
    sensor.resetCounter(i);
    delay(50);
  }

  // Gradually increase speed
  Wire.setClock(200000);  // Back to stable 200kHz
  Serial.println("I2C recovery successful");
  return true;
}

// Add debouncing variables to global scope
static const uint8_t DEBOUNCE_COUNT = 3;            // Number of consistent readings required
static const uint16_t DEBOUNCE_TIME = 5;            // Debounce time in milliseconds
int32_t debounceValues[8][DEBOUNCE_COUNT] = { 0 };  // Array to store last N readings
uint8_t debounceIndex[8] = { 0 };                   // Current index in debounce array
unsigned long lastDebounceTime[8] = { 0 };          // Last time the value changed
int32_t lastStableValue[8] = { 0 };                 // Last stable value
bool valueChanged[8] = { false };                   // Whether value has changed during debounce period

// Add isValueStable function to global scope
bool isValueStable(int32_t newValue, uint8_t encoderIndex) {
  unsigned long currentTime = millis();

  // If this is the first reading or enough time has passed
  if (currentTime - lastDebounceTime[encoderIndex] > DEBOUNCE_TIME) {
    // Store new value in debounce array
    debounceValues[encoderIndex][debounceIndex[encoderIndex]] = newValue;
    debounceIndex[encoderIndex] = (debounceIndex[encoderIndex] + 1) % DEBOUNCE_COUNT;

    // Check if all values in debounce array are the same
    bool allSame = true;
    int32_t firstValue = debounceValues[encoderIndex][0];
    for (uint8_t i = 1; i < DEBOUNCE_COUNT; i++) {
      if (debounceValues[encoderIndex][i] != firstValue) {
        allSame = false;
        break;
      }
    }

    // If values are stable and different from last stable value
    if (allSame && firstValue != lastStableValue[encoderIndex]) {
      lastStableValue[encoderIndex] = firstValue;
      lastDebounceTime[encoderIndex] = currentTime;
      return true;
    }
  }

  return false;
}

// Modify readEncoderBatch to use global isValueStable
void readEncoderBatch() {
  static int32_t lastValues[8] = { 0 };
  static uint8_t lastButtonStatus[8] = { 0 };
  encoderBatch.hasChanged = false;

  // Read all encoder values with retry mechanism
  for (int i = 0; i < NUM_ENCODERS; i++) {
    uint8_t retries = 0;
    bool success = false;

    while (!success && retries < I2C_MAX_RETRIES) {
      // Read button status first
      uint8_t buttonState = sensor.getButtonStatus(i);

      // Debug button status
      if (buttonState != lastButtonStatus[i]) {
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.print(" button state changed from ");
        Serial.print(lastButtonStatus[i]);
        Serial.print(" to ");
        Serial.println(buttonState);
      }

      // Read encoder value
      int32_t rawValue = sensor.getEncoderValue(i);

      if (rawValue == 0 && lastValues[i] != 0) {
        retries++;
        if (retries >= I2C_MAX_RETRIES) {
          if (!recoverI2C()) {
            ESP.restart();
          }
          buttonState = sensor.getButtonStatus(i);
          rawValue = sensor.getEncoderValue(i);
        } else {
          delay(I2C_RETRY_DELAY);
          continue;
        }
      }

      success = true;

      // Handle full rotation
      if (rawValue >= ENCODER_MAX_VALUE) {
        sensor.setEncoderValue(i, 0);
        rawValue = 0;
      } else if (rawValue <= ENCODER_MIN_VALUE) {
        sensor.setEncoderValue(i, 0);
        rawValue = 0;
      }

      // Check if value is stable through debouncing
      bool valueStable = isValueStable(rawValue, i);

      // Only process changes if value is stable
      if (valueStable) {
        encoderBatch.hasChanged = true;
        encoderBatch.encoderChanged[i] = true;
        encoderBatch.values[i] = rawValue;
        encoderBatch.buttonStatus[i] = buttonState;
      } else {
        encoderBatch.encoderChanged[i] = false;
      }

      // Button state changes are processed immediately
      if (buttonState != lastButtonStatus[i]) {
        encoderBatch.hasChanged = true;
        encoderBatch.encoderChanged[i] = true;
        encoderBatch.buttonStatus[i] = buttonState;

        // Debug button press
        if (buttonState == 1) {
          Serial.print("Encoder ");
          Serial.print(i);
          Serial.println(" button PRESSED");
        } else {
          Serial.print("Encoder ");
          Serial.print(i);
          Serial.println(" button RELEASED");
        }
      }

      lastValues[i] = encoderBatch.values[i];
      lastButtonStatus[i] = encoderBatch.buttonStatus[i];
    }
  }
}

// Add EncoderManager class before setup
class EncoderManager {
private:
  UNIT_8ENCODER& sensor;
  int32_t values[8] = { 0 };
  int32_t lastValues[8] = { 0 };
  uint8_t buttonStatus[8] = { 0 };
  uint8_t lastButtonStatus[8] = { 0 };
  bool hasChanged = false;
  bool encoderChanged[8] = { false };

  // Debouncing variables
  static const uint8_t DEBOUNCE_COUNT = 3;            // Number of consistent readings required
  static const uint16_t DEBOUNCE_TIME = 5;            // Debounce time in milliseconds
  int32_t debounceValues[8][DEBOUNCE_COUNT] = { 0 };  // Array to store last N readings
  uint8_t debounceIndex[8] = { 0 };                   // Current index in debounce array
  unsigned long lastDebounceTime[8] = { 0 };          // Last time the value changed
  int32_t lastStableValue[8] = { 0 };                 // Last stable value
  bool valueChanged[8] = { false };                   // Whether value has changed during debounce period

  static const uint8_t I2C_MAX_RETRIES = 2;
  static const uint16_t I2C_RETRY_DELAY = 50;
  static const uint16_t I2C_ERROR_DELAY = 500;

  bool recoverI2C() {
    Serial.println("Attempting I2C recovery...");

    Wire.end();
    delay(I2C_ERROR_DELAY);
    Wire.begin();
    Wire.setClock(200000);
    delay(I2C_RETRY_DELAY);

    if (!sensor.begin(&Wire, ENCODER_ADDR, G2, G1, 200000UL)) {
      Serial.println("I2C recovery failed!");
      return false;
    }

    for (int i = 0; i < NUM_ENCODERS; i++) {
      sensor.resetCounter(i);
      delay(25);
    }

    Serial.println("I2C recovery successful");
    return true;
  }

  bool isValueStable(int32_t newValue, uint8_t encoderIndex) {
    unsigned long currentTime = millis();

    // If this is the first reading or enough time has passed
    if (currentTime - lastDebounceTime[encoderIndex] > DEBOUNCE_TIME) {
      // Store new value in debounce array
      debounceValues[encoderIndex][debounceIndex[encoderIndex]] = newValue;
      debounceIndex[encoderIndex] = (debounceIndex[encoderIndex] + 1) % DEBOUNCE_COUNT;

      // Check if all values in debounce array are the same
      bool allSame = true;
      int32_t firstValue = debounceValues[encoderIndex][0];
      for (uint8_t i = 1; i < DEBOUNCE_COUNT; i++) {
        if (debounceValues[encoderIndex][i] != firstValue) {
          allSame = false;
          break;
        }
      }

      // If values are stable and different from last stable value
      if (allSame && firstValue != lastStableValue[encoderIndex]) {
        lastStableValue[encoderIndex] = firstValue;
        lastDebounceTime[encoderIndex] = currentTime;
        return true;
      }
    }

    return false;
  }

public:
  EncoderManager(UNIT_8ENCODER& encoderSensor)
    : sensor(encoderSensor) {
    // Initialize debounce arrays
    for (int i = 0; i < NUM_ENCODERS; i++) {
      for (int j = 0; j < DEBOUNCE_COUNT; j++) {
        debounceValues[i][j] = 0;
      }
      debounceIndex[i] = 0;
      lastDebounceTime[i] = 0;
      lastStableValue[i] = 0;
      valueChanged[i] = false;
    }
  }

  bool begin() {
    Wire.begin();
    Wire.setClock(200000);
    delay(100);

    Serial.println("Initializing UNIT_8ENCODER...");
    if (!sensor.begin(&Wire, ENCODER_ADDR, G2, G1, 200000UL)) {
      Serial.println("Failed to initialize UNIT_8ENCODER!");
      delay(1000);
      ESP.restart();
      return false;
    }
    delay(100);

    // Reset all encoders to 0
    for (int i = 0; i < NUM_ENCODERS; i++) {
      sensor.resetCounter(i);
      delay(25);
    }
    delay(100);

    // Test encoder reading
    Serial.println("Testing encoder readings...");
    for (int i = 0; i < NUM_ENCODERS; i++) {
      int32_t value = sensor.getEncoderValue(i);
      Serial.print("Encoder ");
      Serial.print(i);
      Serial.print(" initial value: ");
      Serial.println(value);
    }
    return true;
  }

  void readBatch() {
    hasChanged = false;

    // Remove debug output for button states
    for (int i = 0; i < NUM_ENCODERS; i++) {
      uint8_t retries = 0;
      bool success = false;

      while (!success && retries < I2C_MAX_RETRIES) {
        // Read button status first
        uint8_t buttonState = sensor.getButtonStatus(i);

        // Read encoder value
        int32_t rawValue = sensor.getEncoderValue(i);

        if (rawValue == 0 && lastValues[i] != 0) {
          retries++;
          if (retries >= I2C_MAX_RETRIES) {
            if (!recoverI2C()) {
              ESP.restart();
            }
            buttonState = sensor.getButtonStatus(i);
            rawValue = sensor.getEncoderValue(i);
          } else {
            delay(I2C_RETRY_DELAY);
            continue;
          }
        }

        success = true;

        // Handle full rotation
        if (rawValue >= ENCODER_MAX_VALUE) {
          sensor.setEncoderValue(i, 0);
          rawValue = 0;
        } else if (rawValue <= ENCODER_MIN_VALUE) {
          sensor.setEncoderValue(i, 0);
          rawValue = 0;
        }

        // Check if value is stable through debouncing
        bool valueStable = isValueStable(rawValue, i);

        // Only process changes if value is stable
        if (valueStable) {
          hasChanged = true;
          encoderChanged[i] = true;
          values[i] = rawValue;
        } else {
          encoderChanged[i] = false;
        }

        // Button state changes are processed immediately
        if (buttonState != lastButtonStatus[i]) {
          hasChanged = true;
          encoderChanged[i] = true;
          buttonStatus[i] = buttonState;
        }

        lastValues[i] = values[i];
        lastButtonStatus[i] = buttonStatus[i];
      }
    }
  }

  bool hasAnyChanged() const {
    return hasChanged;
  }
  bool hasEncoderChanged(uint8_t index) const {
    return encoderChanged[index];
  }
  int32_t getValue(uint8_t index) const {
    return values[index];
  }
  uint8_t getButtonStatus(uint8_t index) const {
    return buttonStatus[index];
  }
};

// Create global encoder manager instance
EncoderManager encoderManager(sensor);

// Add scale cache structure
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

  ScaleCache()
    : needsUpdate(true) {
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

// Global scale cache
ScaleCache scaleCache;

// Chris Ball's tweaked scalemanager library
ScaleManager scalemanager;
int8_t fundamentalDefault = 60;
int8_t fundamental = 60;
int8_t fundamentalLast = 60;
String octaveS = "";
int8_t octaveDefault = 4;
int8_t octave = 4;
int8_t octaveLast = 4;
int8_t octaveDif = 0;
int8_t notesDif = 0;
String rootName = "";
int8_t scaleNo = 0;
bool updateScale = false;
int8_t scaleNoteNo[7] = { 0, 0, 0, 0, 0, 0, 0 };
int8_t scaleIntervals[7] = { 0, 0, 0, 0, 0, 0, 0 };

// Chris Ball's scaleManager library is crashing out the CoreS3 on getScaleNoteName(int NOTENUMBER) - so moving this to the main sketch
//TO DO - try and resolve this now I've sorted repeated drawing of the encs and keys
const char noteNames[] PROGMEM = "C\0C#\0D\0D#\0E\0F\0F#\0G\0G#\0A\0A#\0B";

// Helper function to get note name from PROGMEM
String getNoteName(uint8_t index) {
  char buffer[3];  // Max length of note name + null terminator
  const char* ptr = noteNames;
  for (uint8_t i = 0; i < index; i++) {
    while (pgm_read_byte(ptr) != '\0') ptr++;
    ptr++;  // Skip the null terminator
  }
  uint8_t j = 0;
  while (j < 2 && (buffer[j] = pgm_read_byte(ptr++)) != '\0') j++;
  buffer[j] = '\0';
  return String(buffer);
}

// Add LED color constants
const uint32_t LED_COLOR_OFF = 0x000000;
const uint32_t LED_COLOR_GREEN = 0x00FF00;
const uint32_t LED_COLOR_ORANGE = 0xFF8000;  // Add orange LED color

// Add function to update encoder LED
void updateEncoderLED(uint8_t encoderIndex, bool isActive) {
  if (isActive) {
    sensor.setLEDColor(encoderIndex, LED_COLOR_GREEN);
  } else {
    sensor.setLEDColor(encoderIndex, LED_COLOR_OFF);
  }
}

// Add function to draw switch indicator
void drawSwitchIndicator(bool isOn) {
  encsSpriteBuffer.fillRect(305, 8, 10, 20, 0);  // Clear with background color
  encsSpriteBuffer.setColor(isOn ? 7 : 3);       // Green when on, dark grey when off
  int yPos = isOn ? 8 : 18;                      // Top when on, bottom when off
  encsSpriteBuffer.fillRect(305, yPos, 10, 10);
  encsSpriteBuffer.setColor(5);  // Light grey
  encsSpriteBuffer.drawRect(305, 8, 10, 20);
}

// Add with other global variables
static uint8_t lastButtonStates[8] = { 1 };  // Store last button states (pulled up)
static bool currentSwitchState = false;      // Add global switch state variable

// Add at the top with other global variables
static int8_t currentOctave = 4;  // Track current octave globally

// Add at the top with other global variables
static int32_t lastEncoderValues[8] = { 0 };           // Track last encoder values
static int32_t encoderAccumulator[8] = { 0 };          // Track accumulated encoder changes
static bool encoderInitialized[8] = { false };         // Track if encoders are initialized
static int32_t encoder6LastValue = 0;                  // Track encoder 6's last value
static int32_t encoder7LastValue = 0;                  // Track encoder 7's last value
static int32_t encoder8LastValue = 0;                  // Track encoder 8's last value
static unsigned long buttonPressStartTime[8] = { 0 };  // Track when each button was pressed
static bool buttonHoldProcessed[8] = { false };        // Track if hold has been processed
static bool waitingForNextPress[8] = { false };        // Track if we're waiting for next press to deactivate
static const unsigned long BUTTON_HOLD_TIME = 1000;    // 1 second hold time

// Add with other global variables
String intvlFormula[9] = {
  "W-W-H-W-W-W-H", "W-H-W-W-H-W-W", "W-H-W-W-W-H-W", "W-H-W-W-H-3H-H", "W-H-W-W-W-H-W", "H-W-W-W-H-W-W", "W-W-W-H-W-W-H", "W-W-H-W-W-H-W", "H-W-W-H-W-W-W"
};

// Helper function to process encoder step accumulation - add at very end of file
//-------------------------------------------------------------
bool processEncoderSteps(int32_t currentValue, int32_t& lastValue, int32_t& accumulator) {
  int32_t delta = 0;

  if (currentValue != lastValue) {
    delta = (currentValue > lastValue) ? 1 : -1;
  }

  if (delta != 0) {
    accumulator += delta;

    if (abs(accumulator) >= 2) {
      int change = accumulator / 2;
      accumulator = 0;
      return true;
    }
  }

  lastValue = currentValue;
  return false;
}

// Helper function to update sprites efficiently - only what changed
//-------------------------------------------------------------
void updateSpritesForEncoderChange(bool fullRedraw = false) {
  static bool keyStates[NUM_KEYS];
  static bool encoderStates[NUM_ENCODERS];
  static bool infoNeedsUpdate = true;

  // Initialize static arrays on first call
  static bool initialized = false;
  if (!initialized) {
    for (int i = 0; i < NUM_KEYS; i++) {
      keyStates[i] = key[i].inScale || key[i].isFundamental;
    }
    for (int i = 0; i < NUM_ENCODERS; i++) {
      encoderStates[i] = enc[i].isActive || enc[i].shortPress;
    }
    initialized = true;
  }

  bool actuallyRedraw = false;

  // Update info area if needed
  if (infoNeedsUpdate || fullRedraw) {
    infoSpriteBuffer.fillSprite(0);
    drawInfoArea();
    infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
    infoNeedsUpdate = false;
    actuallyRedraw = true;
  }

  // For full redraw, update everything
  if (fullRedraw) {
    // Clear and redraw all keys
    keysSpriteBuffer.fillSprite(0);
    for (int i = 0; i < NUM_KEYS; i++) {
      key[i].clearKeys(i);
      key[i].drawKeys(i);
    }
    keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);

    // Clear and redraw all encoders
    encsSpriteBuffer.fillSprite(0);
    for (int i = 0; i < NUM_ENCODERS; i++) {
      enc[i].clearEncs(i);
      enc[i].drawEncs(i);
    }
    drawSwitchIndicator(currentSwitchState);  // Pass actual switch state
    encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);

    actuallyRedraw = true;
  }
  // For partial redraw, only update what changed
  else {
    bool keysUpdated = false;
    bool encodersUpdated = false;

    // Check and update individual keys that changed
    for (int i = 0; i < NUM_KEYS; i++) {
      bool currentState = key[i].inScale || key[i].isFundamental;
      if (currentState != keyStates[i]) {
        keyStates[i] = currentState;
        key[i].clearKeys(i);
        key[i].drawKeys(i);
        keysUpdated = true;
      }
    }

    // Check and update individual encoders that changed
    for (int i = 0; i < NUM_ENCODERS; i++) {
      bool currentState = enc[i].isActive || enc[i].shortPress;
      if (currentState != encoderStates[i]) {
        encoderStates[i] = currentState;
        enc[i].clearEncs(i);
        enc[i].drawEncs(i);
        encodersUpdated = true;
      }
    }

    // Push updated sprites if anything changed
    if (keysUpdated) {
      keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
      actuallyRedraw = true;
    }

    if (encodersUpdated) {
      drawSwitchIndicator(currentSwitchState);
      encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);
      actuallyRedraw = true;
    }
  }

  // Actually push to display if anything was updated
  if (actuallyRedraw) {
    if (infoNeedsUpdate || fullRedraw) {
      infoSprite.pushSprite(0, 0);
    }
    if (fullRedraw || true) {  // Always push for now to be safe
      keysSprite.pushSprite(0, 50);
      encsSprite.pushSprite(0, 200);
    }
  }

  // Mark info as needing update for next time
  infoNeedsUpdate = true;
}

// Helper function to update button state - returns true if state changed
bool updateButtonState(uint8_t buttonIndex, bool isPressed, unsigned long currentTime) {
  if (buttonIndex >= 8) return false;

  ButtonStateManager& button = buttonStates[buttonIndex];
  ButtonState oldState = button.state;

  if (isPressed && button.state == BUTTON_RELEASED) {
    // Button pressed
    button.state = BUTTON_PRESSED;
    button.pressStartTime = currentTime;
    button.processedHold = false;
    button.waitingForNextPress = false;
  } else if (!isPressed && button.state != BUTTON_RELEASED) {
    // Button released
    button.state = BUTTON_RELEASED;
    button.waitingForNextPress = false;
  } else if (isPressed && button.state == BUTTON_PRESSED) {
    // Button held
    if (currentTime - button.pressStartTime >= BUTTON_HOLD_TIME && !button.processedHold) {
      button.state = BUTTON_HOLDING;
      button.processedHold = true;
      button.waitingForNextPress = true;
    }
  }

  return (button.state != oldState);
}

// Improved I2C error handling and recovery
class I2CManager {
private:
  static const uint8_t MAX_RETRIES = 3;
  static const uint16_t RETRY_DELAY = 100;
  static const uint16_t ERROR_DELAY = 1000;
  uint8_t errorCount = 0;
  static const uint8_t MAX_ERRORS = 10;

public:
  bool readEncoderValue(uint8_t encoderIndex, int32_t& value, uint8_t& buttonState) {
    uint8_t retries = 0;

    while (retries < MAX_RETRIES) {
      try {
        buttonState = sensor.getButtonStatus(encoderIndex);
        value = sensor.getEncoderValue(encoderIndex);

        // Success - reset error count
        errorCount = 0;
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

  bool handleErrors() {
    if (errorCount > MAX_ERRORS) {
      Serial.println("Too many I2C errors, attempting recovery...");

      // Reset I2C bus
      Wire.end();
      delay(ERROR_DELAY);
      Wire.begin();
      Wire.setClock(100000);
      delay(RETRY_DELAY);

      // Try to reinitialize sensor
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

// Global I2C manager instance
I2CManager i2cManager;


// Encoder debouncing to reduce jitter
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

    // Check if we have consistent readings
    int32_t consistentValue = checkConsistency(data.samples, rawValue);

    if (consistentValue != data.lastStableValue) {
      // Handle full rotation wrapping
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
    // Count how many samples match the latest value
    uint8_t matchCount = 0;
    for (int i = 0; i < DEBOUNCE_SAMPLES; i++) {
      if (samples[i] == latestValue) {
        matchCount++;
      }
    }

    // If enough samples match, return the consistent value
    if (matchCount >= DEBOUNCE_THRESHOLD) {
      return latestValue;
    }

    // Otherwise, return the most recent value (allow some jitter)
    return latestValue;
  }
};

// Global debouncer instance
EncoderDebouncer encoderDebouncer;






///////////////////////////////////////
/// SET UP
///////////////////////////////////////
//------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting setup...");

  // Initialize M5 with basic settings
  M5.begin();
  M5.Power.begin();
  M5.In_I2C.release();
  delay(100);

  // Initialize I2C with lower speed first
  Wire.begin();
  Wire.setClock(100000);  // Start with 100kHz
  delay(100);

  // Initialize encoder sensor with retry mechanism
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

  // Gradually increase I2C speed
  Wire.setClock(200000);  // Increase to 200kHz
  delay(100);
  Wire.setClock(400000);  // Finally to 400kHz
  delay(100);

  // Initialize button state arrays
  for (int i = 0; i < NUM_ENCODERS; i++) {
    lastButtonState[i] = true;        // Buttons are pulled up (1 = released)
    buttonHoldActivated[i] = false;   // No holds activated initially
    buttonPressTime[i] = millis();    // Initialize with current time
    enc[i].isActive = false;          // Make sure encoders are inactive
    enc[i].shortPress = false;        // Make sure no short presses
  }



  // Initialize scale manager and set initial scale (Major/Ionian) and fundamental (C)
  scalemanager.init();
  scalemanager.setScale(0);        // 0 = Major/Ionian
  scalemanager.setFundamental(0);  // 0 = C
  fundamental = 0;                 // Set global fundamental to C
  scaleNo = 0;                     // Set global scale to Major/Ionian

  // Initialize cached scale info
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

  // Initialize sprites
  if (!initializeSprites()) {
    Serial.println("Sprite initialization failed!");
    delay(500);
    return;
  }

  // Initialize encoder states
  for (int i = 0; i < NUM_ENCODERS; i++) {
    enc[i].r = 16;
    enc[i].x = 16 + i * 38;
    enc[i].y = 16;
    enc[i].isActive = false;
    enc[i].shortPress = false;
    enc[i].colourIdle = 3;
    enc[i].colourActive = 7;
    sensor.setLEDColor(i, 0x000000);
    delay(10);  // Small delay between LED operations
  }

  // Initialize button state arrays - THIS IS CRITICAL
  for (int i = 0; i < NUM_ENCODERS; i++) {
    lastButtonState[i] = true;        // Buttons are pulled up (1 = released)
    buttonHoldActivated[i] = false;   // No holds activated initially
    buttonPressTime[i] = millis();    // Initialize with current time
  }

  // Initialize key states
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
    // Set initial octave number
    key[i].octaveNo = currentOctave;
    key[i].octaveS = String(currentOctave);
  }

  // Set initial scale state
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].inScale = false;
    key[i].isFundamental = false;
  }
  key[0].isFundamental = true;  // Set C as fundamental
  key[0].inScale = true;        // C
  key[2].inScale = true;        // D
  key[4].inScale = true;        // E
  key[5].inScale = true;        // F
  key[7].inScale = true;        // G
  key[9].inScale = true;        // A
  key[11].inScale = true;       // B

  // Calculate initial scale intervals
  setScaleIntervals();

  // Clear all buffer sprites first
  infoSpriteBuffer.fillSprite(0);
  keysSpriteBuffer.fillSprite(0);
  encsSpriteBuffer.fillSprite(0);

  // Draw initial state
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

  // Push initial state to display
  infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
  keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
  encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);
  infoSprite.pushSprite(0, 0);
  keysSprite.pushSprite(0, 50);
  encsSprite.pushSprite(0, 200);

  Serial.println("Setup complete");
}

// Add helper function for sprite initialization
bool initializeSprites() {
  // Initialize main sprites
  infoSprite.setColorDepth(4);
  if (!infoSprite.createSprite(320, 50)) return false;
  infoSprite.setPaletteColor(0, 0x0001);
  infoSprite.setPaletteColor(1, 0xffff);
  infoSprite.setPaletteColor(2, 0x0000);
  infoSprite.setPaletteColor(3, 0x39c7);
  infoSprite.setPaletteColor(4, 0x7bef);
  infoSprite.setPaletteColor(5, 0xd69a);
  infoSprite.setPaletteColor(6, 0xf800);
  infoSprite.setPaletteColor(7, 0x7e0);
  infoSprite.setPaletteColor(8, 0x00ff);
  infoSprite.setPaletteColor(9, 0xfd20);  // Add orange
  infoSprite.setFont(&DIN_Condensed_Bold30pt7b);
  infoSprite.fillSprite(0);

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
  keysSprite.setPaletteColor(9, 0xfd20);  // Add orange
  keysSprite.setFont(&DIN_Condensed_Bold30pt7b);
  keysSprite.fillSprite(0);

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
  encsSprite.setPaletteColor(9, 0xfd20);  // Add orange
  encsSprite.setFont(&DIN_Condensed_Bold30pt7b);
  encsSprite.fillSprite(0);

  // Initialize buffer sprites
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
  infoSpriteBuffer.setPaletteColor(9, 0xfd20);  // Add orange
  infoSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  infoSpriteBuffer.fillSprite(0);

  keysSpriteBuffer.setColorDepth(4);
  if (!keysSpriteBuffer.createSprite(320, 150)) return false;
  keysSprite.setPaletteColor(1, 0xffff);
  keysSpriteBuffer.setPaletteColor(2, 0x0000);
  keysSpriteBuffer.setPaletteColor(3, 0x39c7);
  keysSpriteBuffer.setPaletteColor(4, 0x7bef);
  keysSpriteBuffer.setPaletteColor(5, 0xd69a);
  keysSpriteBuffer.setPaletteColor(6, 0xf800);
  keysSpriteBuffer.setPaletteColor(7, 0x7e0);
  keysSpriteBuffer.setPaletteColor(8, 0x00ff);
  keysSpriteBuffer.setPaletteColor(9, 0xfd20);  // Add orange
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
  encsSpriteBuffer.setPaletteColor(9, 0xfd20);  // Add orange
  encsSpriteBuffer.setFont(&DIN_Condensed_Bold30pt7b);
  encsSpriteBuffer.fillSprite(0);

  return true;
}

// Add helper function for info area drawing
void drawInfoArea() {
  // Clear the area first
  infoSpriteBuffer.fillSprite(0);

  // Draw the border
  infoSpriteBuffer.setColor(1);
  infoSpriteBuffer.drawRoundRect(0, 0, 186, 50, 10);
  infoSpriteBuffer.drawRoundRect(1, 1, 186 - 2, 50 - 2, 10);

  // Draw root name - remove the octave number
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

  // Debug output
  Serial.print("Drawing info - Root: ");
  Serial.print(rootName);
  Serial.print(", Scale: ");
  Serial.print(cachedScaleName);
  Serial.print(", Formula: ");
  Serial.println(intvlFormula[scaleNo]);
}

///////////////////////////////////////
/// MAIN LOOP
///////////////////////////////////////
//------------------------------------------------------------------------
void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastStatusCheck = 0;
  static bool lastSwitchState = false;
  static int32_t lastEncoderValues[8] = { 0 };
  static bool buttonWasPressed[8] = { false };


  if (millis() - lastUpdate < UPDATE_INTERVAL) {
    return;
  }
  lastUpdate = millis();

  M5.update();



  // Check switch state with improved error handling
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





  if (currentSwitchState != lastSwitchState) {
    // Update LED 9 based on switch state
    try {
      sensor.setLEDColor(8, currentSwitchState ? LED_COLOR_GREEN : LED_COLOR_OFF);
    } catch (...) {
      Serial.println("Error setting LED color");
      return;
    }
    lastSwitchState = currentSwitchState;

    // Update display for switch state change
    updateSpritesForEncoderChange(true);  // Full redraw for switch change
  }


  // Read encoder values and button states with error handling
  bool encoderChanged = false;
  for (int i = 0; i < NUM_ENCODERS; i++) {

    int32_t currentValue = 0;
    uint8_t rawButtonState = 1;  // Default to not pressed



    // Use improved I2C manager
    if (!i2cManager.readEncoderValue(i, currentValue, rawButtonState)) {
      if (!i2cManager.handleErrors()) {
        return;
      }
      continue;
    }

        // Debug button state
    Serial.print("Encoder ");
    Serial.print(i);
    Serial.print(" rawButtonState: ");
    Serial.println(rawButtonState);

    bool buttonPressed = (rawButtonState == 0);

    // Temporarily bypass debouncing for testing
    int32_t stableValue = currentValue;

    // Always update display value for smooth visual feedback
    enc[i].encValue = stableValue;

    // Check for significant encoder value change (using raw value)
    if (stableValue != lastEncoderValues[i]) {
      encoderChanged = true;
      lastEncoderValues[i] = stableValue;

      // Handle parameter changes for specific encoders
      // Only for encoders 5, 6, 7 (physical 6, 7, 8)
      if (i == 5 || i == 6 || i == 7) {
        Serial.print("Processing parameter encoder ");
        Serial.println(i);

        if (i == 6) {  // Encoder 7 (0-based index) - Root change
          // Initialize encoder if not done yet
          if (!encoderInitialized[i]) {
            encoder7LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 7 initialized");

          } else {
            // Simple delta calculation for debugging
            int32_t delta = 0;
            if (stableValue != encoder7LastValue) {
              delta = (stableValue > encoder7LastValue) ? 1 : -1;
              Serial.print("Root encoder delta: ");
              Serial.println(delta);
            }

            if (delta != 0) {
              static int32_t stepAccumulator = 0;
              stepAccumulator += delta;

              if (abs(stepAccumulator) >= 2) {
                int fundamentalChange = stepAccumulator / 2;
                stepAccumulator = 0;
                Serial.print("Root change triggered: ");
                Serial.println(fundamentalChange);
                int newFundamental = fundamental + fundamentalChange;

                // Wrap around at boundaries (0-11)
                if (newFundamental > 11) {
                  newFundamental = 0;
                  Serial.println("Wrapping from 11 to 0");
                } else if (newFundamental < 0) {
                  newFundamental = 11;
                  Serial.println("Wrapping from 0 to 11");
                }

                // Update fundamental
                fundamental = newFundamental;

                // Update scale manager
                scalemanager.setFundamental(fundamental);
                scaleCacheNeedsUpdate = true;
                updateScaleCache();

                // Reset the isFundamental flag for all keys
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].isFundamental = false;
                }
                // Set the isFundamental flag for the fundamental key
                key[fundamental].isFundamental = true;

                // Reset the inScale bool for all keys
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].inScale = false;
                }

                // Get fresh scale notes from scale manager
                for (int j = 0; j < 7; j++) {
                  scaleNoteNo[j] = scalemanager.getScaleNote(j);
                  if (scaleNoteNo[j] < 12) {
                    key[scaleNoteNo[j]].inScale = true;
                  } else {
                    key[scaleNoteNo[j] - 12].inScale = true;
                  }
                }

                // Calculate new intervals
                setScaleIntervals();

                // Update octave numbers
                updateOctaveNumbers();

                // Force display update
                encoderChanged = true;
              }
            }
            encoder7LastValue = stableValue;
          }
        } else if (i == 5) {  // Physical encoder 6 (0-based index) - Octave change
          // Initialize encoder if not done yet
          if (!encoderInitialized[i]) {
            encoder6LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 6 initialized");

          } else {
            // Simple delta calculation for debugging
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

                // Wrap around at boundaries
                if (newOctave > 9) {
                  newOctave = -1;
                  Serial.println("Wrapping from 9 to -1");
                } else if (newOctave < -1) {
                  newOctave = 9;
                  Serial.println("Wrapping from -1 to 9");
                }

                // Update octave
                currentOctave = newOctave;

                // Update octave number for all keys in scale
                for (int j = 0; j < NUM_KEYS; j++) {
                  if (key[j].inScale) {
                    key[j].octaveNo = currentOctave;
                    key[j].octaveS = String(currentOctave);
                  }
                }

                // Force display update
                encoderChanged = true;
              }
            }
            encoder6LastValue = stableValue;
          }
        } else if (i == 7) {  // Encoder 8 (0-based index) - Scale change
          // Initialize encoder if not done yet
          if (!encoderInitialized[i]) {
            encoder8LastValue = stableValue;
            encoderInitialized[i] = true;
            Serial.println("Physical Encoder 8 initialized");
          } else {

            // Simple delta calculation for debugging
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

                // Wrap around at boundaries (0-8)
                if (newScale > 8) {
                  newScale = 0;
                  Serial.println("Wrapping from 8 to 0");
                } else if (newScale < 0) {
                  newScale = 8;
                  Serial.println("Wrapping from 0 to 8");
                }

                // Update scale
                scaleNo = newScale;

                // Update scale manager
                scalemanager.setScale(scaleNo);
                scaleCacheNeedsUpdate = true;
                updateScaleCache();

                // Reset the inScale flag for all keys
                for (int j = 0; j < NUM_KEYS; j++) {
                  key[j].inScale = false;
                }

                // Set the inScale flag from scalemanager
                for (int j = 0; j < 7; j++) {
                  int scaleNote = scalemanager.getScaleNote(j);
                  if (scaleNote < 12) {
                    key[scaleNote].inScale = true;
                  } else {
                    key[scaleNote - 12].inScale = true;
                  }
                }

                // Calculate scale intervals
                setScaleIntervals();

                // Force display update
                encoderChanged = true;
              }
            }
            encoder8LastValue = stableValue;
          }
        }
      }
    }

     // Button state tracking - buttons are active LOW (0 when pressed, 1 when released)
    if (!buttonPressed && lastButtonState[i]) {


    // Safety: Clear any stuck states if button is actually released
    if (buttonPressed && (enc[i].shortPress || enc[i].isActive)) {
      Serial.print("SAFETY: Clearing stuck state for encoder ");
      Serial.println(i);
      enc[i].shortPress = false;
      enc[i].isActive = false;
      buttonHoldActivated[i] = false;
    }



      // Button just PRESSED (falling edge - 1 to 0)
      Serial.print("Button ");
      Serial.print(i);
      Serial.print(" PRESSED. enc[i].shortPress=");
      Serial.print(enc[i].shortPress);
      Serial.print(", buttonHoldActivated[i]=");
      Serial.println(buttonHoldActivated[i]);
      
      if (enc[i].isActive) {
        // Button pressed while already active - deactivate immediately
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" DEACTIVATED");
        enc[i].isActive = false;
        enc[i].shortPress = false;
        buttonHoldActivated[i] = false;
        try {
          sensor.setLEDColor(i, LED_COLOR_OFF);
        } catch (...) {
          // Ignore
        }
        encoderChanged = true;
      } else {
        // Button pressed while inactive - start press sequence
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" short press STARTED");
        enc[i].shortPress = true;
        buttonHoldActivated[i] = false;
        try {
          sensor.setLEDColor(i, LED_COLOR_ORANGE);
        } catch (...) {
          // Ignore
        }
        encoderChanged = true;
        buttonPressTime[i] = millis();  // Start timer on press
      }
    } 
    else if (buttonPressed && !lastButtonState[i]) {
      // Button just RELEASED (rising edge - 0 to 1)
      Serial.print("Button ");
      Serial.print(i);
      Serial.print(" RELEASED. enc[i].shortPress=");
      Serial.print(enc[i].shortPress);
      Serial.print(", buttonHoldActivated[i]=");
      Serial.println(buttonHoldActivated[i]);
      
      if (enc[i].shortPress && !buttonHoldActivated[i]) {
        // Short press - button pressed and released quickly
        Serial.print("Encoder ");
        Serial.print(i);
        Serial.println(" short press COMPLETED - turning OFF");
        enc[i].shortPress = false;
        try {
          sensor.setLEDColor(i, LED_COLOR_OFF);
        } catch (...) {
          // Ignore
        }
        encoderChanged = true;
      }
      // If hold was activated, do nothing on release (stay green)
    }
    else if (!buttonPressed && !lastButtonState[i]) {
      // Button STILL HELD down
      if (enc[i].shortPress && !buttonHoldActivated[i]) {
        unsigned long elapsed = millis() - buttonPressTime[i];
        if (elapsed > BUTTON_HOLD_TIME) {
          // Hold time reached - activate
          Serial.print("Encoder ");
          Serial.print(i);
          Serial.println(" HOLD ACTIVATED - turning GREEN");
          enc[i].isActive = true;
          enc[i].shortPress = false;
          buttonHoldActivated[i] = true;
          try {
            sensor.setLEDColor(i, LED_COLOR_GREEN);
          } catch (...) {
            // Ignore
          }
          encoderChanged = true;
        }
      }
    }

    lastButtonState[i] = buttonPressed;



  }





  // Update display if any encoder changed
  if (encoderChanged) {
    // Clear all buffer sprites first
    infoSpriteBuffer.fillSprite(0);
    keysSpriteBuffer.fillSprite(0);
    encsSpriteBuffer.fillSprite(0);

    // Redraw info area
    drawInfoArea();

    // Redraw all keys
    for (int i = 0; i < NUM_KEYS; i++) {
      key[i].onceOnlyKeyDraw = 0;  // Reset the once-only flag
      key[i].clearKeys(i);
      key[i].drawKeys(i);
    }

    // Redraw all encoders
    for (int i = 0; i < NUM_ENCODERS; i++) {
      enc[i].onceOnlyEncDraw = 0;  // Reset the once-only flag
      enc[i].clearEncs(i);
      enc[i].drawEncs(i);
    }

    // Draw switch indicator
    drawSwitchIndicator(currentSwitchState);

    // Copy buffer contents to main sprites
    infoSpriteBuffer.pushSprite(&infoSprite, 0, 0);
    keysSpriteBuffer.pushSprite(&keysSprite, 0, 0);
    encsSpriteBuffer.pushSprite(&encsSprite, 0, 0);

    // Push sprites to display
    infoSprite.pushSprite(0, 0);
    keysSprite.pushSprite(0, 50);
    encsSprite.pushSprite(0, 200);
  }
}

//------------------------------------------------------------------------
void setScaleIntervals() {
  // Debug output for scale notes
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

  // Calculate intervals between successive notes, starting from fundamental
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
      // Handle wrap around
      scaleIntervals[i] = (12 - currentNote) + nextNote;
      Serial.print(" (wrapped) = ");
    } else {
      scaleIntervals[i] = nextNote - currentNote;
      Serial.print(" = ");
    }
    Serial.println(scaleIntervals[i]);
  }

  // Clear all interval strings first
  for (int i = 0; i < NUM_KEYS; i++) {
    key[i].intervalS = "";
  }

  // Update the keys with intervals based on their position in the scale
  for (int i = 0; i < 7; i++) {
    int scaleIndex = (fundamentalScaleIndex + i) % 7;
    int noteIndex = scaleNoteNo[scaleIndex];
    if (noteIndex < 12) {  // Handle notes in current octave
      key[noteIndex].intervalS = String(scaleIntervals[i]);
      Serial.print("Key ");
      Serial.print(noteIndex);
      Serial.print(" (in scale) gets interval: ");
      Serial.println(key[noteIndex].intervalS);
    } else {  // Handle notes in next octave
      key[noteIndex - 12].intervalS = String(scaleIntervals[i]);
      Serial.print("Key ");
      Serial.print(noteIndex - 12);
      Serial.print(" (in scale) gets interval: ");
      Serial.println(key[noteIndex - 12].intervalS);
    }
  }
  Serial.println("--- End Scale Interval Debug ---\n");
}

// Add function to update scale cache
void updateScaleCache() {
  if (scaleCacheNeedsUpdate) {
    cachedScaleName = scalemanager.getScaleName();
    cachedFundamentalName = scalemanager.getFundamentalName();
    cachedRootName = cachedFundamentalName.substring(0, cachedFundamentalName.length() - 1);
    scaleCacheNeedsUpdate = false;

    // Debug output
    Serial.print("Scale cache updated - Root: ");
    Serial.print(cachedRootName);
    Serial.print(", Scale: ");
    Serial.println(cachedScaleName);
  }
}

// Add this function to update octave numbers
void updateOctaveNumbers() {
  for (int i = 0; i < NUM_KEYS; i++) {
    if (key[i].inScale) {
      // Convert octave number to string, handling negative numbers
      if (key[i].octaveNo < 0) {
        key[i].octaveS = String(key[i].octaveNo);  // Will show as "-1"
      } else {
        key[i].octaveS = String(key[i].octaveNo);
      }
      Serial.print("Key ");
      Serial.print(i);
      Serial.print(" octave set to: ");
      Serial.println(key[i].octaveS);
    } else {
      key[i].octaveS = "-";  // Use dash for notes not in scale
    }
  }
}
