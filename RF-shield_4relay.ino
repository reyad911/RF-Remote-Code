#include <Wire.h>
#include <RCSwitch.h>

// External EEPROM I2C address
#define EEPROM_ADDR 0x50 // Default I2C address for 24LC256 EEPROM

RCSwitch mySwitch = RCSwitch();

// Relay Configuration
const int relayPins[] = {3, 4, 5, 6}; // Relay pins
bool relayStates[] = {false, false, false, false}; // Relay states

// RF Code Configuration
unsigned long buttonCodes[4]; // Array to store codes (loaded from EEPROM)
#define EEPROM_SIZE 20 // 4 codes * 4 bytes each + 4 relay states * 1 byte each

// Learning Mode Configuration
const int learnButtonPin = 7; // Button to enter learning mode
bool learningMode = false;
int learnStep = 0; // Tracks which relay we're pairing

// Debounce Configuration
unsigned long lastReceivedTime = 0;
const unsigned long debounceTime = 1000;
unsigned long lastReceivedCode = 0;

// Button Debounce
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Learning Mode Timeout
const unsigned long learningModeTimeout = 300000; // 10 seconds
unsigned long learningModeStartTime = 0;

void setup() {
  Serial.begin(9600);
  pinMode(learnButtonPin, INPUT_PULLUP); // Learning mode button

  // Initialize relays
  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Initialize I2C
  Wire.begin();

  // Load saved codes and relay states from EEPROM
  for (int i = 0; i < 4; i++) {
    buttonCodes[i] = readEEPROM(i * sizeof(unsigned long));
    relayStates[i] = readEEPROMByte(16 + i); // Relay states start at address 16
  }

  // Restore relay states
  for (int i = 0; i < 4; i++) {
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }

  Serial.println("Setup complete. Current codes and relay states:");
  for (int i = 0; i < 4; i++) {
    Serial.print("Relay ");
    Serial.print(i + 1);
    Serial.print(": Code = ");
    Serial.print(buttonCodes[i]);
    Serial.print(", State = ");
    Serial.println(relayStates[i] ? "ON" : "OFF");
  }

  mySwitch.enableReceive(0); // RF receiver on pin 2
}

void loop() {
  // Check if learning mode button is pressed (with debounce)
  if (digitalRead(learnButtonPin) == LOW && millis() - lastDebounceTime > debounceDelay) {
    enterLearningMode();
    lastDebounceTime = millis();
  }

  // Normal operation (if not in learning mode)
  if (!learningMode) {
    handleRFInput();
  }
}

// Handles normal RF control
void handleRFInput() {
  if (mySwitch.available()) {
    unsigned long value = mySwitch.getReceivedValue();

    if (value == 0) {
      Serial.println("Unknown encoding");
    } else if (value != lastReceivedCode || millis() - lastReceivedTime > debounceTime) {
      Serial.print("Received: ");
      Serial.println(value);

      for (int i = 0; i < 4; i++) {
        if (value == buttonCodes[i]) {
          relayStates[i] = !relayStates[i];
          digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
          Serial.print("Relay ");
          Serial.print(i + 1);
          Serial.println(relayStates[i] ? " ON" : " OFF");

          // Save the updated relay state to EEPROM
          writeEEPROM(16 + i, relayStates[i]);
          break;
        }
      }

      lastReceivedCode = value;
      lastReceivedTime = millis();
    }

    mySwitch.resetAvailable();
  }
}

// Enters learning mode to pair new codes
void enterLearningMode() {
  learningMode = true;
  learnStep = 0;
  learningModeStartTime = millis();
  Serial.println("LEARNING MODE: Press the remote button for Relay 1");

  while (learningMode) {
    if (mySwitch.available()) {
      unsigned long value = mySwitch.getReceivedValue();

      if (value != 0 && value != lastReceivedCode) {
        buttonCodes[learnStep] = value; // Save code to memory
        writeEEPROM(learnStep * sizeof(unsigned long), value); // Save to EEPROM

        Serial.print("Saved code for Relay ");
        Serial.print(learnStep + 1);
        Serial.print(": ");
        Serial.println(value);

        learnStep++;
        if (learnStep >= 4) {
          learningMode = false;
          Serial.println("Learning mode complete!");
        } else {
          delay(2000); // Wait 2 seconds before listening for the next signal
          Serial.print("Now press the button for Relay ");
          Serial.println(learnStep + 1);
        }

        lastReceivedCode = value;
        mySwitch.resetAvailable(); // Reset receiver state
      }
    }

    // Exit learning mode if no signal is received for 10 seconds
    if (millis() - learningModeStartTime > learningModeTimeout) {
      learningMode = false;
      Serial.println("Learning mode timeout. Exiting...");
    }
  }
}

// Write data to external EEPROM (unsigned long)
void writeEEPROM(int address, unsigned long value) {
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((address >> 8) & 0xFF);  // High byte of address
  Wire.write(address & 0xFF);         // Low byte of address
  Wire.write((value >> 24) & 0xFF);  // MSB
  Wire.write((value >> 16) & 0xFF);
  Wire.write((value >> 8) & 0xFF);
  Wire.write(value & 0xFF);          // LSB
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C write failed!");
    return;
  }
  delay(20); // Wait for the write to complete (increase if necessary)
}

// Write a single byte (bool) to external EEPROM
void writeEEPROM(int address, bool value) {
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((address >> 8) & 0xFF);  // High byte of address
  Wire.write(address & 0xFF);         // Low byte of address
  Wire.write(value ? 1 : 0);         // Write 1 for true, 0 for false
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C write failed!");
    return;
  }
  delay(20); // Wait for the write to complete (increase if necessary)
}

// Read data from external EEPROM (unsigned long)
unsigned long readEEPROM(int address) {
  unsigned long value = 0;
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((address >> 8) & 0xFF);  // High byte of address
  Wire.write(address & 0xFF);         // Low byte of address
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C read failed!");
    return 0;
  }
  Wire.requestFrom(EEPROM_ADDR, 4);   // Request 4 bytes

  if (Wire.available() == 4) {
    value |= (unsigned long)Wire.read() << 24; // MSB
    value |= (unsigned long)Wire.read() << 16;
    value |= (unsigned long)Wire.read() << 8;
    value |= (unsigned long)Wire.read();       // LSB
  }

  return value;
}

// Read a single byte from external EEPROM
byte readEEPROMByte(int address) {
  byte value = 0;
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((address >> 8) & 0xFF);  // High byte of address
  Wire.write(address & 0xFF);         // Low byte of address
  if (Wire.endTransmission() != 0) {
    Serial.println("I2C read failed!");
    return 0;
  }
  Wire.requestFrom(EEPROM_ADDR, 1);   // Request 1 byte

  if (Wire.available() == 1) {
    value = Wire.read();
  }

  return value;
}0
