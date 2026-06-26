#include <Arduino.h>
#include <Wire.h>
#include <Encoder.h>
// Use flash memory as eeprom
#include <FlashAsEEPROM.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// rotary encoder setting
#define ENCODER_OPTIMIZE_INTERRUPTS // counter measure of noise

#define CLK_IN_PIN 7     // Clock input pin
#define CV_1_IN_PIN 8    // channel 1 analog in
#define CV_2_IN_PIN 9    // channel 2 analog in
#define ENC_PIN_1 3      // rotary encoder left pin
#define ENC_PIN_2 6      // rotary encoder right pin
#define ENC_CLICK_PIN 10 // pin for encoder switch
#define GATE_OUT_PIN_1 1
#define GATE_OUT_PIN_2 2
#define DAC_INTERNAL_PIN A0 // DAC output pin (internal)
// Second DAC output goes to MCP4725 via I2C

// Declare function prototypes
void OLED_display();
void intDAC(int);
void MCP(int);
void PWM1(int);
void PWM2(int);
void lottery();
void load();
void save();

////////////////////////////////////////////
// ADC calibration. Change these according to your resistor values to make readings more accurate
float AD_CH1_calb = 0.98; // reduce resistance error
float AD_CH2_calb = 0.98; // reduce resistance error
/////////////////////////////////////////

// OLED display initialization
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Rotary encoder initialization
Encoder myEnc(ENC_PIN_1, ENC_PIN_2); // rotary encoder library setting
float oldPosition = -999;            // rotary encoder library setting
float newPosition = -999;            // rotary encoder library setting

// Amount of menu items
int menuItems = 5;
// i is the current position of the encoder
int menu_index = 1;

bool SW = 0;
bool old_SW = 0;
bool CLK_in = 0;
bool old_CLK_in = 0;
byte mode = 0; // 0=looping, 1=length, 2=width, 3=refrain

float AD_CH1, old_AD_CH1, AD_CH2, old_AD_CH2;

int CV_in1, CV_in2;
float CV_out1, CV_out2, old_CV_out1, old_CV_out2;
long gate_timer1, gate_timer2;

// display
bool disp_refresh = 1; // 0=not refresh display , 1= refresh display , countermeasure of display refresh busy

byte stgAgate[2][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

byte stgBgate[2][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

int stgAcv[2][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

int stgBcv[2][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

byte i = 0;
byte j = 0;
byte k = 0;

byte gate_set = 1;
byte gate_count = 0;
byte old_gate_count = 0;

byte chance_set = 3;    // The reading value of the main knob. Pass a number to chance.
byte chance = 1;        // Determined based on chance_set and length. Determines the number of variations in the lottery.
byte lottery_stepA = 2; // Lottery for stgA.
byte lottery_stepB = 2; // Lottery for stgB.
byte lottery_done = 2;  // Limits the lottery to only once.

const int LDAC = 9; // Latch operation output pin.

int sub_knob = 0;       // サブツマミの読み取りAD値
int old_sub_knob = 0;   // SW切り替え直後、切り替え前のAD値を反映させないため、古いADを記憶
byte refrain_set = 1;   // リフレインの設定回数。AD値から導出。
byte refrain_count = 0; // リフレインした回数をカウント。指定値に達すると0に戻る。
byte length_set = 4;    // レングスの数値
int width_max = 1023;
int width_min = 0;

int loopingValue = 0; // メインツマミの読み取りAD値
int lengthValue = 0;
int widthValue = 0;
int refrainValue = 0;

byte repeat_set = 0;   // リピートの設定回数。AD値から導出。
byte repeat_count = 0; // リピートした回数をカウント。指定値に達すると0に戻る

byte mode_set = 0; // 0=length,1=width,2=refrain

void setup()
{
  analogWriteResolution(10);
  analogReadResolution(12);
  pinMode(CLK_IN_PIN, INPUT);           // CLK in
  pinMode(CV_1_IN_PIN, INPUT);          // CV IN1
  pinMode(CV_2_IN_PIN, INPUT);          // CV IN2
  pinMode(GATE_OUT_PIN_1, OUTPUT);      // CH1 Gate out
  pinMode(GATE_OUT_PIN_2, OUTPUT);      // CH2 Gate out
  pinMode(ENC_CLICK_PIN, INPUT_PULLUP); // push sw
  // OLED initialize
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // I2C connect
  Wire.begin();

  // Load the EEPROM data
  load();

  // ADC settings. These increase ADC reading stability but at the cost of cycle time. Takes around 0.7ms for one reading with these
  REG_ADC_AVGCTRL |= ADC_AVGCTRL_SAMPLENUM_1;
  ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_128 | ADC_AVGCTRL_ADJRES(4);

  for (i = 0; i < 2; i = i + 1)
  {
    for (j = 0; j < 16; j = j + 1)
    {
      stgAgate[i][j] = random(2);
      stgBgate[i][j] = random(2);
      stgAcv[i][j] = random(4096);
      stgBcv[i][j] = random(4096);
    }
  }
}

void loop()
{

  //-------------Reading the state of external input-----------------
  old_SW = SW;
  old_CLK_in = CLK_in;
  old_CV_out1 = CV_out1;
  old_CV_out2 = CV_out2;
  old_AD_CH1 = AD_CH1;
  old_AD_CH2 = AD_CH2;

  gate_set = digitalRead(CLK_IN_PIN); // Read the state of gate_input

  newPosition = myEnc.read();
  // If the encoder is decremented
  if ((newPosition - 3) / 4 > oldPosition / 4)
  { // 4 is resolution of encoder
    oldPosition = newPosition;
    disp_refresh = 1;
    switch (mode)
    {
    case 0: // Option select
      menu_index = menu_index - 1;
      if (menu_index < 0)
      {
        menu_index = menuItems;
      }
      break;
    case 1: // Looping
      loopingValue = loopingValue - pow(2, log2(loopingValue) - 1);
      break;
    case 2: // Length
      lengthValue = lengthValue - pow(2, log2(lengthValue) - 1);
      break;
    case 3: // Width
      widthValue = widthValue - pow(2, log2(widthValue) - 1);
      break;
    case 4: // Refrain
      refrainValue = refrainValue - pow(2, log2(refrainValue) - 1);
      break;
    }
  }
  // If the encoder is incremented
  else if ((newPosition + 3) / 4 < oldPosition / 4)
  { // 4 is resolution of encoder
    oldPosition = newPosition;
    disp_refresh = 1;
    switch (mode)
    {
    case 0:
      menu_index = menu_index + 1;
      if (menuItems < menu_index)
      {
        menu_index = 0;
      }
      break;
    case 1: // Looping
      loopingValue = loopingValue + pow(2, log2(loopingValue) - 1);
      break;
    case 2: // Length
      lengthValue = lengthValue + pow(2, log2(lengthValue) - 1);
      break;
    case 3: // Width
      widthValue = widthValue + pow(2, log2(widthValue) - 1);
      break;
    case 4: // Refrain
      refrainValue = refrainValue + pow(2, log2(refrainValue) - 1);
      break;
    }
  }
  menu_index = constrain(menu_index, 0, menuItems);
  loopingValue = constrain(loopingValue, 1, 1024);
  lengthValue = constrain(lengthValue, 1, 1024);
  widthValue = constrain(widthValue, 1, 1024);
  refrainValue = constrain(refrainValue, 1, 1024);

  //-----------------PUSH SW------------------------------------
  SW = digitalRead(ENC_CLICK_PIN);
  if (SW == 1 && old_SW != 1)
  {
    disp_refresh = 1;
    if (menu_index == 1 && mode == 0)
    {
      mode = 1; // Switch to looping selection mode
    }
    else if (menu_index == 2 && mode == 0)
    {
      mode = 2; // Switch to length selection mode
    }
    else if (menu_index == 3 && mode == 0)
    {
      mode = 3; // Switch to width selection mode
    }
    else if (menu_index == 4 && mode == 0)
    {
      mode = 4; // Switch to refrain selection mode
    }
    // Revert from modes to menu selection
    else if (menu_index == 1 && mode != 1)
    {
      mode = 0; // Switch to option select mode
    }
    else if (menu_index == 2 && mode == 2)
    {
      mode = 0; // Switch to option select mode
    }
    else if (menu_index == 3 && mode == 3)
    {
      mode = 0; // Switch to option select mode
    }
    else if (menu_index == 4 && mode == 4)
    {
      mode = 0; // Switch to option select mode
    }
    else if (menu_index == 5 && mode == 0)
    {
      save(); // Save the current settings to EEPROM
    }
  }
  //-------------------------------Analog read and qnt setting--------------------------
  // Still not used but could control internal parameters
  AD_CH1 = analogRead(CV_1_IN_PIN) / AD_CH1_calb;
  AD_CH2 = analogRead(CV_2_IN_PIN) / AD_CH2_calb;

  //-------------refrainの設定----------------------

  if (mode_set == 2)
  {
    if (refrainValue < 25)
    {
      refrain_set = 1; // do not repeat
    }
    else if (refrainValue < 313 && refrainValue >= 26)
    {
      refrain_set = 2; // Repeat 1 time
    }

    else if (refrainValue < 624 && refrainValue >= 314)
    {
      refrain_set = 3; // Repeat 2 times
    }

    else if (refrainValue < 873 && refrainValue >= 625)
    {
      refrain_set = 4; // Repeat 3 times
    }

    else if (refrainValue >= 874)
    {
      refrain_set = 8; // Repeat 7 times
    }
  }

  //----------------length設定---------------------

  if (mode_set == 0)
  {
    if (lengthValue < 25)
    {
      length_set = 4;
    }
    else if (lengthValue < 313 && lengthValue >= 26)
    {
      length_set = 6;
    }

    else if (lengthValue < 624 && lengthValue >= 314)
    {
      length_set = 8;
    }

    else if (lengthValue < 873 && lengthValue >= 625)
    {
      length_set = 12;
    }

    else if (lengthValue >= 874)
    {
      length_set = 16;
    }
  }

  //-------------width setting----------------------

  if (mode_set == 1)
  {
    width_max = 612 + widthValue * 4 / 10;
    width_min = 412 - widthValue * 4 / 10;
  }

  //-------------repeat setting----------------------
  if (loopingValue < 5)
  {
    repeat_set = 0; // do not repeat
    chance = 0;
  }
  else if (loopingValue < 111 && loopingValue >= 6)
  {
    repeat_set = 0; // do not repeat
    chance = 1;
  }
  else if (loopingValue < 214 && loopingValue >= 112)
  {
    repeat_set = 0; // do not repeat
    chance = 2;
  }
  else if (loopingValue < 376 && loopingValue >= 215)
  {
    repeat_set = 0; // do not repeat
    if (length_set == 4 || length_set == 6)
    {
      chance = 3;
    }
    else
    {
      chance = 4;
    }
  }

  else if (loopingValue < 555 && loopingValue >= 377)
  {
    repeat_set = 0; // do not repeat
    chance = length_set;
  }
  else if (loopingValue < 700 && loopingValue >= 556)
  {
    repeat_set = 1; // repeat
    if (length_set == 4 || length_set == 6)
    {
      chance = 3;
    }
    else
    {
      chance = 4;
    }
  }
  else if (loopingValue < 861 && loopingValue >= 701)
  {
    repeat_set = 1; // repeat
    chance = 2;
  }
  else if (loopingValue < 970 && loopingValue >= 862)
  {
    repeat_set = 1; // repeat
    chance = 1;
  }
  else if (loopingValue < 1024 && loopingValue >= 971)
  {
    repeat_set = 1; // repeat
    chance = 0;
  }
  //-----------------Start of lottery process-----------------

  if (refrain_count == 0 && gate_count == 1 && lottery_done == 0 && repeat_count == 0)
  {
    lottery(); // Start lottery process
    lottery_done = 1;
  }

  //-----------------stg sequence output-----------------

  switch (repeat_count)
  {

  case 0:
    switch (gate_set)
    {

    case 0:                              // When gate_input is LOW
      digitalWrite(GATE_OUT_PIN_1, LOW); // Set gate_output to LOW
      old_gate_count = gate_count;
      break;

    case 1: // When gate_input is HIGH
      if (old_gate_count == gate_count)
      {
        gate_count++;

        if (gate_count > length_set)
        {
          gate_count = 1;
          refrain_count++;

          if (refrain_count >= refrain_set)
          { // Transition from stgA to stgB
            repeat_count++;
            refrain_count = 0;
            lottery_done = 0;
          }
        }
        // WriteRegister(map(stgAcv[0][gate_count - 1], 0, 1023, width_min, width_max));
        intDAC(map(stgAcv[0][gate_count - 1], 0, 4095, width_min, width_max));

        digitalWrite(GATE_OUT_PIN_1, stgAgate[0][gate_count - 1]); // Output CV before gate
        // analogWrite(6, stgAcv[0][gate_count] / 4); // Replace this LED output with something on screen
        break;
      }
      break;
    }

  case 1:
    if (repeat_set == 0)
    {
      repeat_count = 0;
    }
    else
    {
      switch (gate_set)
      {

      case 0:                              // When gate_input is LOW
        digitalWrite(GATE_OUT_PIN_1, LOW); // Set gate_output to LOW
        old_gate_count = gate_count;
        break;

      case 1: // When gate_input is HIGH
        if (old_gate_count == gate_count)
        {
          gate_count++;

          if (gate_count > length_set)
          {
            gate_count = 1;
            refrain_count++;

            if (refrain_count >= refrain_set)
            { // Transition from stgB to stgA
              repeat_count++;
              refrain_count = 0;
              if (repeat_count >= 2)
              {
                repeat_count = 0;
                lottery_done = 0;
              }
            }
          }
          // WriteRegister(map(stgBcv[0][gate_count - 1], 0, 1023, width_min, width_max));

          intDAC(map(stgBcv[0][gate_count - 1], 0, 1023, width_min, width_max));

          digitalWrite(GATE_OUT_PIN_1, stgBgate[0][gate_count - 1]); // Output CV before gate
          // analogWrite(6, stgBcv[0][gate_count] / 4); // Replace this LED output with something on screen
          break;
        }
        break;
      }
    }
  }

  // display out
  if (disp_refresh == 1)
  {
    OLED_display(); // refresh display
    disp_refresh = 0;
  }
}

void OLED_display()
{
  int maxBarLength = 70;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Draw menu options
  // Looping selection (mode 1)
  display.setCursor(10, 0);
  display.print("LOOP:");
  // Draw a rectangle to show the current value of the main knob (loopingValue)
  int mappedValue = map(loopingValue, 0, 1024, 0, maxBarLength);
  display.fillRect(50, 0, mappedValue, 6, WHITE);
  // Draw the difference to the maximum value as an empty rectangle
  display.drawRect(50 + mappedValue, 0, mappedValue, 6, WHITE);
  // Length selection (mode 2)
  display.setCursor(10, 10);
  display.print("LENGTH:");
  mappedValue = map(lengthValue, 0, 1024, 0, maxBarLength);
  display.fillRect(50, 10, mappedValue, 6, WHITE);
  display.drawRect(50 + mappedValue, 10, mappedValue, 6, WHITE);
  // Width selection (mode 3)
  display.setCursor(10, 20);
  display.print("WIDTH:");
  mappedValue = map(widthValue, 0, 1024, 0, maxBarLength);
  display.fillRect(50, 20, mappedValue, 6, WHITE);
  display.drawRect(50 + mappedValue, 20, mappedValue, 6, WHITE);
  // Refrain selection (mode 4)
  display.setCursor(10, 30);
  display.print("REFRAIN:");
  mappedValue = map(refrainValue, 0, 1024, 0, maxBarLength);
  display.fillRect(50, 30, mappedValue, 6, WHITE);
  display.drawRect(50 + mappedValue, 30, mappedValue, 6, WHITE);
  // Save settings
  display.setCursor(10, 40);
  display.print("SAVE");

  // Draw the current selection triangles
  if (mode == 0)
  { // On menu selection
    if (menu_index == 0)
    {
      display.drawTriangle(0, 0, 5, 3, 0, 6, WHITE);
    }
    else if (menu_index == 1)
    {
      display.drawTriangle(0, 10, 5, 13, 0, 16, WHITE);
    }
    else if (menu_index == 2)
    {
      display.drawTriangle(0, 20, 5, 23, 0, 26, WHITE);
    }
    else if (menu_index == 3)
    {
      display.drawTriangle(0, 30, 5, 33, 0, 36, WHITE);
    }
    else if (menu_index == 4)
    {
      display.drawTriangle(0, 40, 5, 43, 0, 46, WHITE);
    }

    display.drawTriangle(0, 0, 5, 3, 0, 6, WHITE);
  }
  else if (mode == 1)
  {
    display.fillTriangle(0, 10, 5, 13, 0, 16, WHITE);
  }
  else if (mode == 2)
  {
    display.fillTriangle(0, 20, 5, 23, 0, 26, WHITE);
  }
  else if (mode == 3)
  {
    display.fillTriangle(0, 30, 5, 33, 0, 36, WHITE);
  }
  else if (mode == 4)
  {
    display.fillTriangle(0, 40, 5, 43, 0, 46, WHITE);
  }

  display.display();
}

void lottery()
{

  if (chance != 0)
  {
    for (k = 0; k <= chance; k = k + 1)
    {
      lottery_stepA = random(length_set);
      stgAgate[0][lottery_stepA] = 1 - stgAgate[0][lottery_stepA];
      stgAcv[0][lottery_stepA] = random(4096);

      lottery_stepB = random(length_set);
      stgBgate[0][lottery_stepB] = 1 - stgBgate[0][lottery_stepB];
      stgBcv[0][lottery_stepB] = random(4096);
    }
  }
}

void intDAC(int intDAC_OUT)
{
  analogWrite(DAC_INTERNAL_PIN, intDAC_OUT / 4); // "/4" -> 12bit to 10bit
}

void MCP(int MCP_OUT)
{
  Wire.beginTransmission(0x60);
  Wire.write((MCP_OUT >> 8) & 0x0F);
  Wire.write(MCP_OUT);
  Wire.endTransmission();
}

void PWM1(int duty1)
{
  pwm(GATE_OUT_PIN_1, 46000, duty1);
}
void PWM2(int duty2)
{
  pwm(GATE_OUT_PIN_2, 46000, duty2);
}

void save()
{
  EEPROM.write(0, length_set);  // Save data for next session
  EEPROM.write(1, refrain_set); // Save data for next session
  EEPROM.write(2, width_max);   // Save data for next session
  EEPROM.write(3, width_min);   // Save data for next session
}

void load()
{
  if (EEPROM.isValid())
  {
    // Load the EEPROM data
    length_set = EEPROM.read(0);  // Read data from previous session
    refrain_set = EEPROM.read(1); // Read data from previous session
    width_max = EEPROM.read(2);   // Read data from previous session
    width_min = EEPROM.read(3);   // Read data from previous session
  }
}
