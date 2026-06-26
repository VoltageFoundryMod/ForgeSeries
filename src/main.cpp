#include <Arduino.h>

// Rotary encoder setting
#define ENCODER_OPTIMIZE_INTERRUPTS // counter measure of noise
#include <Encoder.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Load local libraries
#include "boardIO.cpp"
#include "loadsave.cpp"
#include "pinouts.hpp"
#include "quantizer.cpp"
#include "scales.cpp"
#include "splash.hpp"
#include "version.hpp"

// ADC Calibration settings
const int ADC_THRESHOLD = 5; // Threshold for ADC stability
// float ADCCal[2] = {1.026, 1.026}; // ADC readings for the channels
// int ADCOffset[2] = {25, 25};      // ADC offset for the channels
float ADCCal[2] = {1.0180, 1.0180}; // ADC readings for the channels
int ADCOffset[2] = {23, 23};        // ADC offset for the channels
////////////////////////////////////////////

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// OLED display initialization
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Rotary encoder initialization
Encoder encoder(ENC_PIN_1, ENC_PIN_2); // rotary encoder library setting
float oldPosition = -999;              // rotary encoder library setting
float newPosition = -999;              // rotary encoder library setting

int menuItems = 38; // Amount of menu items
int menuItem = 1;   // Current position of the encoder

bool switchState = 1;    // Encoder switch state
bool oldSwitchState = 1; // Encoder switch state on last cycle
bool clockInput = 0;     // Clock input state
bool oldClockInput = 0;  // Clock input state on last cycle
int menuMode = 0;        // 0=select,1=atk[0],2=dcy[0],3=atk[1],4=dcy[1], 5=scale, 6=note

// ADC input variables
float channelADC[2], oldChannelADC[2];
int quantizedNoteIdx[2], oldQuantizedNoteIdx[2] = {0, 0};

float CVOutput[2], oldCVOutput[2] = {0, 0}; // CV output
long gateTimer[2] = {0, 0};                 // EG curve progress speed

// envelope curve setting
int ADEnvelopeTable[200] = { // envelope table
    0, 15, 30, 44, 59, 73, 87, 101, 116, 130, 143, 157, 170, 183, 195, 208, 220, 233, 245, 257, 267, 279, 290, 302, 313, 324, 335, 346, 355, 366, 376, 386, 397, 405, 415, 425, 434, 443, 452, 462, 470, 479, 488, 495, 504, 513, 520, 528, 536, 544, 552, 559, 567, 573, 581, 589, 595, 602, 609, 616, 622, 629, 635, 642, 648, 654, 660, 666, 672, 677, 683, 689, 695, 700, 706, 711, 717, 722, 726, 732, 736, 741, 746, 751, 756, 760, 765, 770, 774, 778, 783, 787, 791, 796, 799, 803, 808, 811, 815, 818, 823, 826, 830, 834, 837, 840, 845, 848, 851, 854, 858, 861, 864, 866, 869, 873, 876, 879, 881, 885, 887, 890, 893, 896, 898, 901, 903, 906, 909, 911, 913, 916, 918, 920, 923, 925, 927, 929, 931, 933, 936, 938, 940, 942, 944, 946, 948, 950, 952, 954, 955, 957, 960, 961, 963, 965, 966, 968, 969, 971, 973, 975, 976, 977, 979, 980, 981, 983, 984, 986, 988, 989, 990, 991, 993, 994, 995, 996, 997, 999, 1000, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1012, 1013, 1014, 1014, 1015, 1016, 1017, 1018, 1019, 1020};

int adcValues[2] = {0, 0}; // PWM DUTY reference
bool ADTrigger[2] = {0, 0};
u_int8_t attackEnvelope[2], decayEnvelope[2]; // attack time,decay time
u_int8_t syncSignal[2];                       // 0=sync with trig , 1=sync with note change
u_int8_t octaveShift[2];                      // oct=octave shift
u_int8_t channelSensitivity[2];               // sens = AD input attn,amp

// CV setting
int quantizerThresholdBuff[2][62]; // input quantize

// Scale and Note loading indexes
int scaleIndex = 1;
int noteIndex = 0;

// Note Storage
bool activeNotes[2][12] = {{false}}; // 1=note valid,0=note invalid

// display
bool displayRefresh = 1;     // 0=not refresh display , 1= refresh display , countermeasure of display refresh busy
bool unsavedChanges = false; // Unsaved changes flag

// Function prototypes
void HandleEncoderPosition();
void HandleEncoderClick();
void HandleInputs();
void HandleOutputs();
void HandleIO();

// Handle encoder button click
void HandleEncoderClick() {
    oldSwitchState = switchState;
    switchState = digitalRead(ENCODER_SW);
    if (switchState == 1 && oldSwitchState != 1) {
        Serial.println("SW pushed at index: " + String(menuItem));
        displayRefresh = 1;
        if (menuItem <= 11 && menuItem >= 0 && menuMode == 0) {
            activeNotes[0][menuItem] = !activeNotes[0][menuItem];
        } else if (menuItem >= 14 && menuItem <= 25 && menuMode == 0) {
            activeNotes[1][menuItem - 14] = !activeNotes[1][menuItem - 14];
        } else if (menuItem == 12 && menuMode == 0) { // CH1 atk setting
            menuMode = 1;                             // atk[0] setting
        } else if (menuItem == 12 && menuMode == 1) { // CH1 atk setting
            menuMode = 0;
        } else if (menuItem == 13 && menuMode == 0) { // CH1 dcy setting
            menuMode = 2;                             // dcy[0] setting
        } else if (menuItem == 13 && menuMode == 2) { // CH1 dcy setting
            menuMode = 0;
        } else if (menuItem == 26 && menuMode == 0) { // CH2 atk setting
            menuMode = 3;                             // atk[1] setting
        } else if (menuItem == 26 && menuMode == 3) { // CH2 atk setting
            menuMode = 0;
        } else if (menuItem == 27 && menuMode == 0) { // CH2 dcy setting
            menuMode = 4;                             // dcy[1] setting
        } else if (menuItem == 27 && menuMode == 4) { // CH2 dcy setting
            menuMode = 0;
        } else if (menuItem == 28) { // CH1 sync setting
            syncSignal[0] = !syncSignal[0];
            unsavedChanges = true;
        } else if (menuItem == 29) { // CH2 sync setting
            syncSignal[1] = !syncSignal[1];
            unsavedChanges = true;
        } else if (menuItem == 30 && menuMode == 0) { // CH1 oct setting
            menuMode = 5;
        } else if (menuItem == 30 && menuMode == 5) {
            menuMode = 0;
        } else if (menuItem == 31 && menuMode == 0) { // CH2 oct setting
            menuMode = 6;
        } else if (menuItem == 31 && menuMode == 6) {
            menuMode = 0;
        } else if (menuItem == 32 && menuMode == 0) { // CH1 sens setting
            menuMode = 7;
        } else if (menuItem == 32 && menuMode == 7) {
            menuMode = 0;
        } else if (menuItem == 33 && menuMode == 0) { // CH2 sens setting
            menuMode = 8;
        } else if (menuItem == 33 && menuMode == 8) {
            menuMode = 0;
        } else if (menuItem == 34 && menuMode == 0) { // Scale setting
            menuMode = 9;
        } else if (menuItem == 34 && menuMode == 9) {
            menuMode = 0;
        } else if (menuItem == 35 && menuMode == 0) { // Note setting
            menuMode = 10;
        } else if (menuItem == 35 && menuMode == 10) {
            menuMode = 0;
        } else if (menuItem == 36) { // Load Scale into quantizer 1
            Serial.println("Loading scale " + String(scaleIndex) + "  for note " + String(noteIndex) + " into quantizer 1");
            BuildScale(scaleIndex, noteIndex, activeNotes[0]);
            BuildQuantBuffer(activeNotes[0], quantizerThresholdBuff[0]);
            display.clearDisplay(); // clear display
            display.setTextSize(2);
            display.setTextColor(BLACK, WHITE);
            display.setCursor(1, 40);
            display.print("LOADED");
            display.display();
            unsigned long saveMessageStartTime = millis();
            while (millis() - saveMessageStartTime < 1000) {
                HandleIO();
            }
            unsavedChanges = true;
        } else if (menuItem == 37) { // Load Scale into quantizer 2
            Serial.println("Loading scale " + String(scaleIndex) + "  for note " + String(noteIndex) + " into quantizer 2");
            BuildScale(scaleIndex, noteIndex, activeNotes[1]);
            BuildQuantBuffer(activeNotes[1], quantizerThresholdBuff[1]);
            display.clearDisplay(); // clear display
            display.setTextSize(2);
            display.setTextColor(BLACK, WHITE);
            display.setCursor(1, 40);
            display.print("LOADED");
            display.display();
            unsigned long saveMessageStartTime = millis();
            while (millis() - saveMessageStartTime < 1000) {
                HandleIO();
            }
            unsavedChanges = true;
        } else if (menuItem == 38) { // Save settings
            LoadSaveParams p = {
                &attackEnvelope[0],
                &attackEnvelope[1],
                &decayEnvelope[0],
                &decayEnvelope[1],
                &syncSignal[0],
                &syncSignal[1],
                &channelSensitivity[0],
                &channelSensitivity[1],
                &octaveShift[0],
                &octaveShift[1]};
            Save(p, activeNotes[0], activeNotes[1]);
            unsavedChanges = false;
            display.clearDisplay(); // clear display
            display.setTextSize(2);
            display.setTextColor(BLACK, WHITE);
            display.setCursor(10, 40);
            display.print("SAVED");
            display.display();
            unsigned long saveMessageStartTime = millis();
            while (millis() - saveMessageStartTime < 1000) {
                HandleIO();
            }
        }
    }
}

void HandleEncoderPosition() {
    newPosition = encoder.read();
    if ((newPosition - 3) / 4 > oldPosition / 4) { // Decrease, turned counter-clockwise
        oldPosition = newPosition;
        displayRefresh = 1;
        switch (menuMode) {
        case 0:
            menuItem = (menuItem - 1 < 0) ? menuItems : menuItem - 1;
            break;
        case 1:
            attackEnvelope[0] = constrain(attackEnvelope[0] - 1, 1, 26);
            unsavedChanges = true;
            break;
        case 2:
            decayEnvelope[0] = constrain(decayEnvelope[0] - 1, 1, 26);
            unsavedChanges = true;
            break;
        case 3:
            attackEnvelope[1] = constrain(attackEnvelope[1] - 1, 1, 26);
            unsavedChanges = true;
            break;
        case 4:
            decayEnvelope[1] = constrain(decayEnvelope[1] - 1, 1, 26);
            unsavedChanges = true;
            break;
        case 5:
            octaveShift[0] = (octaveShift[0] - 1 + 7) % 7;
            unsavedChanges = true;
            break;
        case 6:
            octaveShift[1] = (octaveShift[1] - 1 + 7) % 7;
            unsavedChanges = true;
            break;
        case 7:
            channelSensitivity[0] = constrain(channelSensitivity[0] - 1, 0, 8);
            unsavedChanges = true;
            break;
        case 8:
            channelSensitivity[1] = constrain(channelSensitivity[1] - 1, 0, 8);
            unsavedChanges = true;
            break;
        case 9:
            scaleIndex = (scaleIndex - 1 + numScales) % numScales;
            unsavedChanges = true;
            break;
        case 10:
            noteIndex = (noteIndex - 1 + 12) % 12;
            unsavedChanges = true;
            break;
        }
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // Increase, turned clockwise
        oldPosition = newPosition;
        displayRefresh = 1;
        switch (menuMode) {
        case 0:
            menuItem = (menuItem + 1 > menuItems) ? 0 : menuItem + 1;
            break;
        case 1:
            attackEnvelope[0] = constrain(attackEnvelope[0] + 1, 1, 26);
            unsavedChanges = true;
            break;
        case 2:
            decayEnvelope[0] = constrain(decayEnvelope[0] + 1, 1, 26);
            unsavedChanges = true;
            break;
        case 3:
            attackEnvelope[1] = constrain(attackEnvelope[1] + 1, 1, 26);
            unsavedChanges = true;
            break;
        case 4:
            decayEnvelope[1] = constrain(decayEnvelope[1] + 1, 1, 26);
            unsavedChanges = true;
            break;
        case 5:
            octaveShift[0] = (octaveShift[0] + 1) % 7;
            unsavedChanges = true;
            break;
        case 6:
            octaveShift[1] = (octaveShift[1] + 1) % 7;
            unsavedChanges = true;
            break;
        case 7:
            channelSensitivity[0] = constrain(channelSensitivity[0] + 1, 0, 8);
            unsavedChanges = true;
            break;
        case 8:
            channelSensitivity[1] = constrain(channelSensitivity[1] + 1, 0, 8);
            unsavedChanges = true;
            break;
        case 9:
            scaleIndex = (scaleIndex + 1) % numScales;
            unsavedChanges = true;
            break;
        case 10:
            noteIndex = (noteIndex + 1) % 12;
            unsavedChanges = true;
            break;
        }
    }
}

void NoteDisplay(int x0, int y0, boolean on, boolean playing) {
    int width = 11;
    int height = 13;
    int radius = 2;
    int color = WHITE;
    if (on) {
        if (playing) {
            display.drawRoundRect(x0, y0, width, height, radius, color);
            display.fillRoundRect(x0 + 3, y0 + 3, width - 6, height - 6, radius, color);
        } else {
            display.fillRoundRect(x0, y0, width, height, radius, color);
        }
    } else {
        display.drawRoundRect(x0, y0, width, height, radius, color);
    }
}

// Redraw the display and show unsaved changes indicator
void RedrawDisplay() {
    // If there are unsaved changes, display an asterisk at the top right corner
    if (unsavedChanges) {
        display.setTextSize(1);
        display.setCursor(120, 0);
        display.print("*");
    }
    display.display();
    displayRefresh = 0;
}

void HandleOLED() {
    if (displayRefresh == 1) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);

        if (menuItem <= 27) {
            // Draw the keyboard scale 1
            NoteDisplay(7, 0, activeNotes[0][1] == 1, quantizedNoteIdx[0] == 1);             // C#
            NoteDisplay(7 + 14 * 1, 0, activeNotes[0][3] == 1, quantizedNoteIdx[0] == 3);    // D#
            NoteDisplay(8 + 14 * 3, 0, activeNotes[0][6] == 1, quantizedNoteIdx[0] == 6);    // F#
            NoteDisplay(8 + 14 * 4, 0, activeNotes[0][8] == 1, quantizedNoteIdx[0] == 8);    // G#
            NoteDisplay(8 + 14 * 5, 0, activeNotes[0][10] == 1, quantizedNoteIdx[0] == 10);  // A#
            NoteDisplay(0, 15, activeNotes[0][0] == 1, quantizedNoteIdx[0] == 0);            // C
            NoteDisplay(0 + 14 * 1, 15, activeNotes[0][2] == 1, quantizedNoteIdx[0] == 2);   // D
            NoteDisplay(0 + 14 * 2, 15, activeNotes[0][4] == 1, quantizedNoteIdx[0] == 4);   // E
            NoteDisplay(0 + 14 * 3, 15, activeNotes[0][5] == 1, quantizedNoteIdx[0] == 5);   // F
            NoteDisplay(0 + 14 * 4, 15, activeNotes[0][7] == 1, quantizedNoteIdx[0] == 7);   // G
            NoteDisplay(0 + 14 * 5, 15, activeNotes[0][9] == 1, quantizedNoteIdx[0] == 9);   // A
            NoteDisplay(0 + 14 * 6, 15, activeNotes[0][11] == 1, quantizedNoteIdx[0] == 11); // B

            // Draw the keyboard scale 2
            NoteDisplay(7, 0 + 34, activeNotes[1][1] == 1, quantizedNoteIdx[1] == 1);             // C#
            NoteDisplay(7 + 14 * 1, 0 + 34, activeNotes[1][3] == 1, quantizedNoteIdx[1] == 3);    // D#
            NoteDisplay(8 + 14 * 3, 0 + 34, activeNotes[1][6] == 1, quantizedNoteIdx[1] == 6);    // F#
            NoteDisplay(8 + 14 * 4, 0 + 34, activeNotes[1][8] == 1, quantizedNoteIdx[1] == 8);    // G#
            NoteDisplay(8 + 14 * 5, 0 + 34, activeNotes[1][10] == 1, quantizedNoteIdx[1] == 10);  // A#
            NoteDisplay(0, 15 + 34, activeNotes[1][0] == 1, quantizedNoteIdx[1] == 0);            // C
            NoteDisplay(0 + 14 * 1, 15 + 34, activeNotes[1][2] == 1, quantizedNoteIdx[1] == 2);   // D
            NoteDisplay(0 + 14 * 2, 15 + 34, activeNotes[1][4] == 1, quantizedNoteIdx[1] == 4);   // E
            NoteDisplay(0 + 14 * 3, 15 + 34, activeNotes[1][5] == 1, quantizedNoteIdx[1] == 5);   // F
            NoteDisplay(0 + 14 * 4, 15 + 34, activeNotes[1][7] == 1, quantizedNoteIdx[1] == 7);   // G
            NoteDisplay(0 + 14 * 5, 15 + 34, activeNotes[1][9] == 1, quantizedNoteIdx[1] == 9);   // A
            NoteDisplay(0 + 14 * 6, 15 + 34, activeNotes[1][11] == 1, quantizedNoteIdx[1] == 11); // B

            // Debug print
            Serial.print("Note 1: " + String(noteNames[quantizedNoteIdx[0]]) + " index: " + String(quantizedNoteIdx[0]) + "| Input CV: " + String(channelADC[0]) + " Quantized CV: " + String(CVOutput[0]) + "\n");
            Serial.print("Note 2: " + String(noteNames[quantizedNoteIdx[1]]) + " index: " + String(quantizedNoteIdx[1]) + "| Input CV: " + String(channelADC[1]) + " Quantized CV: " + String(CVOutput[1]) + "\n");
            // Draw the selection triangle
            if (menuItem <= 4) {
                display.fillTriangle(5 + menuItem * 7, 28, 2 + menuItem * 7, 33, 8 + menuItem * 7, 33, WHITE);
            } else if (menuItem >= 5 && menuItem <= 11) {
                display.fillTriangle(12 + menuItem * 7, 28, 9 + menuItem * 7, 33, 15 + menuItem * 7, 33, WHITE);
            } else if (menuItem == 12) {
                if (menuMode == 0) {
                    display.drawTriangle(127, 0, 127, 6, 121, 3, WHITE);
                } else if (menuMode == 1) {
                    display.fillTriangle(127, 0, 127, 6, 121, 3, WHITE);
                }
            } else if (menuItem == 13) {
                if (menuMode == 0) {
                    display.drawTriangle(127, 16, 127, 22, 121, 19, WHITE);
                } else if (menuMode == 2) {
                    display.fillTriangle(127, 16, 127, 22, 121, 19, WHITE);
                }
            } else if (menuItem >= 14 && menuItem <= 18) {
                display.fillTriangle(12 + (menuItem - 15) * 7, 33, 9 + (menuItem - 15) * 7, 28, 15 + (menuItem - 15) * 7, 28, WHITE);
            } else if (menuItem >= 19 && menuItem <= 25) {
                display.fillTriangle(12 + (menuItem - 14) * 7, 33, 9 + (menuItem - 14) * 7, 28, 15 + (menuItem - 14) * 7, 28, WHITE);
            } else if (menuItem == 26) {
                if (menuMode == 0) {
                    display.drawTriangle(127, 32, 127, 38, 121, 35, WHITE);
                } else if (menuMode == 3) {
                    display.fillTriangle(127, 32, 127, 38, 121, 35, WHITE);
                }
            } else if (menuItem == 27) {
                if (menuMode == 0) {
                    display.drawTriangle(127, 48, 127, 54, 121, 51, WHITE);
                } else if (menuMode == 4) {
                    display.fillTriangle(127, 48, 127, 54, 121, 51, WHITE);
                }
            }

            // Draw envelope param
            display.setTextSize(1);
            display.setCursor(100, 0); // effect param3
            display.print("ATK");
            display.setCursor(100, 16); // effect param3
            display.print("DCY");
            display.setCursor(100, 32); // effect param3
            display.print("ATK");
            display.setCursor(100, 48); // effect param3
            display.print("DCY");
            display.fillRoundRect(100, 9, attackEnvelope[0] + 1, 4, 1, WHITE);
            display.fillRoundRect(100, 25, decayEnvelope[0] + 1, 4, 1, WHITE);
            display.fillRoundRect(100, 41, attackEnvelope[1] + 1, 4, 1, WHITE);
            display.fillRoundRect(100, 57, decayEnvelope[1] + 1, 4, 1, WHITE);
        }

        // Draw config settings
        if (menuItem >= 28 && menuItem <= 33) { // draw sync mode setting
            display.setTextSize(1);
            display.setCursor(10, 0);
            display.print("SYNC CH1:");
            display.setCursor(72, 0);
            if (syncSignal[0] == 0) {
                display.print("TRIG");
            } else if (syncSignal[0] == 1) {
                display.print("NOTE");
            }
            display.setCursor(10, 9);
            display.print("     CH2:");
            display.setCursor(72, 9);
            if (syncSignal[1] == 0) {
                display.print("TRIG");
            } else if (syncSignal[1] == 1) {
                display.print("NOTE");
            }
            // draw octave shift
            display.setCursor(10, 18);
            display.print("OCT  CH1:");
            display.setCursor(10, 27);
            display.print("     CH2:");
            display.setCursor(72, 18);
            display.print(octaveShift[0] - 3);
            display.setCursor(72, 27);
            display.print(octaveShift[1] - 3);

            // draw sensitivity
            display.setCursor(10, 36);
            display.print("SENS CH1:");
            display.setCursor(10, 45);
            display.print("     CH2:");
            display.setCursor(72, 36);
            display.print(channelSensitivity[0] - 4);
            display.setCursor(72, 45);
            display.print(channelSensitivity[1] - 4);

            if (menuItem >= 28 && menuItem <= 29) {
                display.fillTriangle(1, (menuItem - 28) * 9, 1, (menuItem - 28) * 9 + 8, 5, (menuItem - 28) * 9 + 4, WHITE);
            }
            if (menuItem >= 30 && menuItem <= 33) {
                int menuModeOffset = menuItem - 30 + 5;
                if (menuMode == 0) {
                    display.drawTriangle(1, (menuItem - 30 + 2) * 9, 1, (menuItem - 30 + 2) * 9 + 8, 5, (menuItem - 30 + 2) * 9 + 4, WHITE);
                } else if (menuMode == menuModeOffset) {
                    display.fillTriangle(1, (menuItem - 30 + 2) * 9, 1, (menuItem - 30 + 2) * 9 + 8, 5, (menuItem - 30 + 2) * 9 + 4, WHITE);
                }
            }
        }

        // draw scale load setting
        if (menuItem >= 34 && menuItem <= 38) {
            display.setTextSize(1);
            display.setCursor(10, 0);
            display.print("SCALE:");
            display.setCursor(72, 0);
            display.print(scaleNames[scaleIndex]);
            display.setCursor(10, 9);
            display.print("ROOT:");
            display.setCursor(72, 9);
            display.print(noteNames[noteIndex]);
            display.setCursor(10, 18);
            display.print("LOAD IN CH1");
            display.setCursor(10, 27);
            display.print("LOAD IN CH2");
            display.setCursor(10, 45);
            display.print("SAVE");
            // Draw triangles
            if (menuItem == 34) {
                if (menuMode == 0) {
                    display.drawTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
                } else if (menuMode == 9) {
                    display.fillTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
                }
            } else if (menuItem == 35) {
                if (menuMode == 0) {
                    display.drawTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
                } else if (menuMode == 10) {
                    display.fillTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
                }
            } else if (menuItem == 36) {
                display.fillTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
            } else if (menuItem == 37) {
                display.fillTriangle(1, (menuItem - 34) * 9, 1, (menuItem - 34) * 9 + 8, 5, (menuItem - 34) * 9 + 4, WHITE);
            } else if (menuItem == 38) {
                display.fillTriangle(1, (menuItem - 34) * 9 + 9, 1, (menuItem - 34) * 9 + 9 + 8, 5, (menuItem - 34) * 9 + 4 + 9, WHITE);
            }
        }

        RedrawDisplay();
    }
}

void AdjustADCReadings(int CV_IN_PIN, int ch) {
    // Apply calibration
    float calibratedReading = max((analogRead(CV_IN_PIN) - ADCOffset[ch]) * ADCCal[ch], 0.0f);

    // Wait for the ADC to stabilize for a few readings
    for (int count = 0; count < 10; count++) {
        float newReading = max((analogRead(CV_IN_PIN) - ADCOffset[ch]) * ADCCal[ch], 0.0f);
        if (abs(calibratedReading - newReading) <= ADC_THRESHOLD) {
            break;
        }
        calibratedReading = newReading;
    }

    channelADC[ch] = calibratedReading;
}

void HandleInputs() {
    oldClockInput = clockInput;
    oldCVOutput[0] = CVOutput[0];
    oldCVOutput[1] = CVOutput[1];
    oldChannelADC[0] = channelADC[0];
    oldChannelADC[1] = channelADC[1];
    oldQuantizedNoteIdx[0] = quantizedNoteIdx[0];
    oldQuantizedNoteIdx[1] = quantizedNoteIdx[1];

    //-------------------------------Analog read and qnt setting--------------------------
    AdjustADCReadings(CV_1_IN_PIN, 0);
    AdjustADCReadings(CV_2_IN_PIN, 1);

    QuantizeCV(channelADC[0], oldChannelADC[0], quantizerThresholdBuff[0], channelSensitivity[0], octaveShift[0], &CVOutput[0]);
    QuantizeCV(channelADC[1], oldChannelADC[1], quantizerThresholdBuff[1], channelSensitivity[1], octaveShift[1], &CVOutput[1]);

    clockInput = digitalRead(CLK_IN_PIN);

    // Trig sync trigger detect
    if (clockInput == 1 && oldClockInput == 0) {
        for (int ch = 0; ch < 2; ch++) {
            // If the sync mode is set to trigger
            if (syncSignal[ch] == 0) {
                adcValues[ch] = 0;
                ADTrigger[ch] = 1;
                gateTimer[ch] = micros();
                if (attackEnvelope[ch] == 1) {
                    adcValues[ch] = 200; // no attack time
                }
            }
        }
    }

    // Note sync trigger detect
    for (int ch = 0; ch < 2; ch++) {
        if (syncSignal[ch] == 1 && oldCVOutput[ch] != CVOutput[ch]) {
            adcValues[ch] = 0;
            ADTrigger[ch] = 1;
            gateTimer[ch] = micros();
            if (attackEnvelope[ch] == 1) {
                adcValues[ch] = 200; // no attack time
            }
        }
    }

    // Envelope ch out
    for (int ch = 0; ch < 2; ch++) {
        if (gateTimer[ch] + (attackEnvelope[ch] - 1) * 200 <= static_cast<long>(micros()) && ADTrigger[ch] == 1 && adcValues[ch] <= 199) {
            adcValues[ch]++;
            gateTimer[ch] = micros();
        } else if (gateTimer[ch] + (decayEnvelope[ch] - 1) * 600 <= static_cast<long>(micros()) && ADTrigger[ch] == 1 && adcValues[ch] > 199) {
            adcValues[ch]++;
            gateTimer[ch] = micros();
        }

        if (adcValues[ch] <= 199) {
            PWMWrite(ch + 1, 1021 - ADEnvelopeTable[adcValues[ch]]);
        } else if (adcValues[ch] > 199 && adcValues[ch] < 399) {
            PWMWrite(ch + 1, ADEnvelopeTable[adcValues[ch] - 200]);
        } else if (adcValues[ch] >= 399) {
            PWMWrite(ch + 1, 1023);
            ADTrigger[ch] = 0;
        }
    }

    for (int ch = 0; ch < 2; ch++) {
        if (oldCVOutput[ch] != CVOutput[ch]) {
            DACWrite(ch + 1, CVOutput[ch]);
        }
        // Get the quantized note from the CV output
        GetNote(CVOutput[ch], &quantizedNoteIdx[ch]);
    }

    // Trigger display refresh if the note has changed
    if ((oldQuantizedNoteIdx[0] != quantizedNoteIdx[0]) || (oldQuantizedNoteIdx[1] != quantizedNoteIdx[1])) {
        displayRefresh = 1;
    }
}

// Handle IO without the display
void HandleIO() {
    HandleEncoderClick();

    HandleEncoderPosition();

    HandleInputs();
}

void loop() {
    HandleIO();

    HandleOLED();
}

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    InitIO(); // Initialize IO pins

    // OLED initialize
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, false, true);
    display.clearDisplay();

    display.clearDisplay();
    display.drawBitmap(30, 0, VFM_Splash, 68, 64, 1);
    display.display();
    delay(2000);
    // Print module name in the middle of the screen
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(10, 20);
    display.print("NoteForge");
    display.setTextSize(1);
    display.setCursor(80, 54);
    display.print("V" VERSION);
    display.display();
    delay(1500);

    // Load scale and note settings from flash memory
    LoadSaveParams p = {
        &attackEnvelope[0],
        &attackEnvelope[1],
        &decayEnvelope[0],
        &decayEnvelope[1],
        &syncSignal[0],
        &syncSignal[1],
        &channelSensitivity[0],
        &channelSensitivity[1],
        &octaveShift[0],
        &octaveShift[1],
    };
    Load(p, activeNotes[0], activeNotes[1]);
    BuildQuantBuffer(activeNotes[0], quantizerThresholdBuff[0]);
    BuildQuantBuffer(activeNotes[1], quantizerThresholdBuff[1]);
}
