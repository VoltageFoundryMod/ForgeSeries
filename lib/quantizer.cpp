#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include "Arduino.h"
#endif

// Build the quantizer buffer
// Inputs:
//   note: array of 12 booleans, one for each note in an octave
// Outputs:
//   buff: array of 62 integers, the quantizer buffer
void BuildQuantBuffer(bool note[], int buff[]) {
    int k = 0;
    for (byte j = 0; j < 62; j++) {
        if (note[j % 12] == 1) {
            buff[k] = 17 * j - 8;
            k++;
        }
    }
};

// Identify the closest note index to the input CV
// Note indexes are 0 to 11, corresponding to C to B
void GetNote(float CV_OUT, int *note_index) {
    int note = CV_OUT / 68.25;
    *note_index = note % 12;
}

// Quantize the input CV to the closest note
void QuantizeCV(float AD_CH, float AD_CH_old, int cv_qnt_thr_buf[], int sensitivity_ch, int oct, float *CV_out) {
    int cmp1 = 0, cmp2 = 0; // Detect closest note
    byte search_qnt = 0;
    AD_CH = AD_CH * (16 + sensitivity_ch) / 20; // sens setting
    if (AD_CH > 4095) {
        AD_CH = 4095;
    }
    if (abs(AD_CH_old - AD_CH) > 10) // counter measure for AD error , ignore small changes
    {
        for (search_qnt = 0; search_qnt <= 61; search_qnt++) { // quantize
            if (AD_CH >= cv_qnt_thr_buf[search_qnt] * 4 && AD_CH < cv_qnt_thr_buf[search_qnt + 1] * 4) {
                cmp1 = AD_CH - cv_qnt_thr_buf[search_qnt] * 4;     // Detect closest note
                cmp2 = cv_qnt_thr_buf[search_qnt + 1] * 4 - AD_CH; // Detect closest note
                break;
            }
        }
        if (cmp1 >= cmp2) { // Detect closest note
            *CV_out = (cv_qnt_thr_buf[search_qnt + 1] + 8) / 17 * 68.25 + (oct - 3) * 12 * 68.25;
            *CV_out = constrain(*CV_out, 0, 4095);
        } else if (cmp2 > cmp1) { // Detect closest note
            *CV_out = (cv_qnt_thr_buf[search_qnt] + 8) / 17 * 68.25 + (oct - 3) * 12 * 68.25;
            *CV_out = constrain(*CV_out, 0, 4095);
        }
    }
}
