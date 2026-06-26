#pragma once

#include <Arduino.h>
// Use flash memory as eeprom
#include <FlashAsEEPROM.h>

#define CALIBRATION_ADDR 0
#define PARAMS_ADDR 50

// Struct to hold params that are saved/loaded to/from EEPROM
struct LoadSaveParams {
    u_int8_t *atk1, *atk2, *dcy1, *dcy2, *sync1, *sync2, *sensitivity_ch1, *sensitivity_ch2, *oct1, *oct2;
};

const uint8_t WRITTEN_SIGNATURE = 0xDA;

void SaveCalibration(float calibrationValues[2][2]) {
    int addr = CALIBRATION_ADDR;
    EEPROM.write(addr, WRITTEN_SIGNATURE);
    addr += sizeof(WRITTEN_SIGNATURE);
    // Write the calibration values from data to the EEPROM
    const size_t arraySize = sizeof(float) * 2 * 2;
    byte data[arraySize];
    memcpy(data, calibrationValues, arraySize);

    for (size_t i = 0; i < arraySize; i++) {
        EEPROM.write(addr, data[i]);
        addr += sizeof(data[i]);
    }
    EEPROM.commit();
}

void LoadCalibration(float calibrationValues[2][2]) {
    int addr = CALIBRATION_ADDR;
    int signature = EEPROM.read(addr);
    if (signature == WRITTEN_SIGNATURE) {
        addr += sizeof(WRITTEN_SIGNATURE);
        // Read the calibration values from the EEPROM to data
        const size_t arraySize = sizeof(float) * 2 * 2;
        byte data[arraySize];
        for (size_t i = 0; i < arraySize; i++) {
            data[i] = EEPROM.read(addr);
            addr += sizeof(data[i]);
        }
        memcpy(calibrationValues, data, arraySize);
    } else {
        calibrationValues[0][0] = 0;
        calibrationValues[0][1] = 0;
        calibrationValues[1][0] = 0;
        calibrationValues[1][1] = 0;
    }
}

// Save data to flash memory
void Save(LoadSaveParams p, bool note1[], bool note2[]) {
    int addr = PARAMS_ADDR;
    EEPROM.write(addr, WRITTEN_SIGNATURE);
    addr += sizeof(WRITTEN_SIGNATURE);

    byte note1_str_pg1 = 0, note1_str_pg2 = 0;
    byte note2_str_pg1 = 0, note2_str_pg2 = 0;

    for (int j = 0; j <= 7; j++) { // Convert note setting to bits
        bitWrite(note1_str_pg1, j, note1[j]);
        bitWrite(note2_str_pg1, j, note2[j]);
    }
    for (int j = 0; j <= 3; j++) {
        bitWrite(note1_str_pg2, j, note1[j + 8]);
        bitWrite(note2_str_pg2, j, note2[j + 8]);
    }
    EEPROM.write(addr++, note1_str_pg1);
    EEPROM.write(addr++, note1_str_pg2);
    EEPROM.write(addr++, note2_str_pg1);
    EEPROM.write(addr++, note2_str_pg2);
    EEPROM.write(addr++, *p.atk1);
    EEPROM.write(addr++, *p.dcy1);
    EEPROM.write(addr++, *p.atk2);
    EEPROM.write(addr++, *p.dcy2);
    EEPROM.write(addr++, *p.sync1);
    EEPROM.write(addr++, *p.sync2);
    EEPROM.write(addr++, *p.oct1);
    EEPROM.write(addr++, *p.oct2);
    EEPROM.write(addr++, *p.sensitivity_ch1);
    EEPROM.write(addr++, *p.sensitivity_ch2);
    EEPROM.commit();
}

// Load data from flash memory
void Load(LoadSaveParams p, bool note1[], bool note2[]) {
    byte note1_str_pg1 = 0, note1_str_pg2 = 0;
    byte note2_str_pg1 = 0, note2_str_pg2 = 0;

    int addr = PARAMS_ADDR;
    int signature = EEPROM.read(addr);
    if (signature == WRITTEN_SIGNATURE) {
        addr += sizeof(WRITTEN_SIGNATURE);
        note1_str_pg1 = EEPROM.read(addr++);
        note1_str_pg2 = EEPROM.read(addr++);
        note2_str_pg1 = EEPROM.read(addr++);
        note2_str_pg2 = EEPROM.read(addr++);
        *p.atk1 = EEPROM.read(addr++);
        *p.dcy1 = EEPROM.read(addr++);
        *p.atk2 = EEPROM.read(addr++);
        *p.dcy2 = EEPROM.read(addr++);
        *p.sync1 = EEPROM.read(addr++);
        *p.sync2 = EEPROM.read(addr++);
        *p.oct1 = EEPROM.read(addr++);
        *p.oct2 = EEPROM.read(addr++);
        *p.sensitivity_ch1 = EEPROM.read(addr++);
        *p.sensitivity_ch2 = EEPROM.read(addr++);
    } else {                       // No eeprom data , setting defaults
        note1_str_pg1 = B11111111; // Initialize with chromatic scale
        note1_str_pg2 = B00001111;
        note2_str_pg1 = B10110101; // Iniitialize with C major scale
        note2_str_pg2 = B00001010;
        *p.atk1 = 1;
        *p.dcy1 = 4;
        *p.atk2 = 2;
        *p.dcy2 = 6;
        *p.sync1 = 1;
        *p.sync2 = 1;
        *p.oct1 = 3;
        *p.oct2 = 3;
        *p.sensitivity_ch1 = 4;
        *p.sensitivity_ch2 = 4;
    }
    // setting stored note data
    for (int j = 0; j <= 7; j++) {
        note1[j] = bitRead(note1_str_pg1, j);
        note2[j] = bitRead(note2_str_pg1, j);
    }
    for (int j = 0; j <= 3; j++) {
        note1[j + 8] = bitRead(note1_str_pg2, j);
        note2[j + 8] = bitRead(note2_str_pg2, j);
    }
}
