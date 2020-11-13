/* MIT Open-Source LICENSE:
*
* Copyright 2018 Charles Grassin
* 
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the 
* "Software"), to deal in the Software without restriction, including 
* without limitation the rights to use, copy, modify, merge, publish, 
* distribute, sublicense, and/or sell copies of the Software, and to 
* permit persons to whom the Software is furnished to do so, subject 
* to the following conditions:
* 
* The above copyright notice and this permission notice shall be 
* included in all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <SPI.h>

// Pins
#define PIN_ADF4351_LE 2
#define PIN_ADF4351_MUX 3

// ADF4351 settings
#define ADF4351_DEFAULT_FREQ 1000.0f
#define ADF4351_PFDRF 25.0f
#define ADF4351_MIN_FREQ 35.0f
#define ADF4351_MAX_FREQ 4400.0f


// Other setting
#define SERIAL_BUFFER_LENGTH 40
#define MAIN_LOOP_DELAY 20

// ADF control variables
float currentFrequency = ADF4351_DEFAULT_FREQ;
char currentPower = 0,currentOutputEnable = 0;
char *powerSettings[] = { "-4dBm", "-2dBm",  "+1dBm", "+5dBm"};
char *outputSettings[] = { "Off", "On"};

// Serial
char serialBuffer[SERIAL_BUFFER_LENGTH];
uint8_t currentIndex=0;
char *serialFeedback[] = { "Ok", "NOk"};


// ----------------------------------------
// ----------------ARDUINO-----------------
// ----------------------------------------

void setup()   {                
  Serial.begin(9600);
  Serial.println("Command: <0/1 (on/off)> <Freq in MHz> <0-3 (output power)>");
  // Configure pins
  pinMode(PIN_ADF4351_MUX, INPUT);
  pinMode(PIN_ADF4351_LE, OUTPUT);
  digitalWrite(PIN_ADF4351_LE, HIGH);

  // Configure SPI
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  // Init. ADF4351
  ADF4351_set(currentOutputEnable,currentPower,currentFrequency,1);
  //serialWriteStatus();
}


void loop() {
  // Handle serial events
  if (serialReadCommand()) {
    ADF4351_set(currentOutputEnable,currentPower,currentFrequency,.01);
    serialWriteStatus();
  }

  delay(MAIN_LOOP_DELAY);
}

// ----------------------------------------
// ----------------Serial-----------------
// ----------------------------------------

uint8_t serialReadCommand(){
  while (Serial.available() > 0) {
    char recieved = Serial.read();

    serialBuffer[currentIndex] = recieved;
    currentIndex ++;
    if (currentIndex >= SERIAL_BUFFER_LENGTH) {
      // Invalid command: command is too long
      Serial.println(serialFeedback[1]);
      currentIndex = 0;
    }

    if (recieved == '\n' || recieved == '\r') {
    // Close string
      serialBuffer[currentIndex-1] = '\0';
    // Clear buffer for next serial transaction
      currentIndex = 0; 

    // Split the string
      char* commaIndex = strchr(serialBuffer, ' ');
      if (commaIndex==NULL) {
        // Invalid command: command is malformed
        Serial.println(serialFeedback[1]);
        return 0;
      }
      commaIndex[0] = '\0';
      char* secondCommaIndex = strchr(commaIndex+1, ' ');
      if (secondCommaIndex==NULL) {
        // Invalid command: command is malformed
        Serial.println(serialFeedback[1]);
        return 0;
      }
      secondCommaIndex[0] = '\0';

    // Check values validity
      if (!(strcmp(serialBuffer,"0") == 0 || strcmp(serialBuffer,"1") == 0)) {
        // Invalid command: invalid enable
        Serial.println(serialFeedback[1]);
        return 0;
      }
      if (!(strcmp(secondCommaIndex+1,"0") == 0 || strcmp(secondCommaIndex+1,"1") == 0 || strcmp(secondCommaIndex+1,"2") == 0 || strcmp(secondCommaIndex+1,"3") == 0)) {
        // Invalid command: invalid power
        Serial.println(serialFeedback[1]);
        return 0;
      }
      float serialFrequency = atof(commaIndex+1);
      if (serialFrequency < ADF4351_MIN_FREQ || serialFrequency > ADF4351_MAX_FREQ) {
        // Invalid command: invalid frequency
        Serial.println(serialFeedback[1]);
        return 0;
      }

    // Apply values
      currentOutputEnable = (strcmp(serialBuffer,"0")!=0);
      currentFrequency = serialFrequency;
      currentPower = atoi(secondCommaIndex+1);

      Serial.println(serialFeedback[0]);
      return 1;
    }
  }
  return 0;
}

void serialWriteStatus(){
  Serial.print("Status: ");
  Serial.print(currentFrequency,1);
  Serial.print("MHz ");
  Serial.print(powerSettings[currentPower]);
  Serial.print(" ");
  Serial.println(outputSettings[currentOutputEnable]);
}

// ----------------------------------------
// ----------------ADF4351-----------------
// ----------------------------------------

// Interface 
uint16_t gcd (uint16_t a, uint16_t b) {
  uint16_t c;
  while ( a != 0 ) {
    c = a; a = b%a;  b = c;
  }
  return b;
}

/*
* Check if PLL is locked. 
* Returns 1 if locked, 0 otherwise.
*/
uint8_t ADF4351_lockdetect(){
  return digitalRead(PIN_ADF4351_MUX);
}

/*
* Set the ADF4351 registers.
* frequency: desired output frequency, in MHz
* power: enum->(0=-4dBm,1=-1dBm,2=2dBm,3=5dBm)
* outputEnable: boolean to enable output
* frequencyResolution: ???
* Return 1 if action was performed, 0 otherwise.
*/
uint8_t ADF4351_set(const uint8_t outputEnable, const uint8_t power, const float frequency, const float frequencyResolution) {
  uint32_t regADF4351[6];

  // Check arguments validity
  if (power < 0 || power > 3)
    return 0;
  if (frequency < ADF4351_MIN_FREQ || frequency > ADF4351_MAX_FREQ)
    return 0;
  if (frequencyResolution < 0.01 || frequencyResolution > 10)
    return 0;

  // Compute registers value
  uint8_t rfDivider,rfDividerValue;
  if (frequency < 68.75) {
    rfDividerValue = 64;
    rfDivider = 6;
  } else if (frequency < 137.5) {
    rfDividerValue = 32;
    rfDivider = 5;
  } else if (frequency < 275) {
    rfDividerValue = 16;
    rfDivider = 4;
  } else if (frequency < 550) {
    rfDividerValue = 8;
    rfDivider = 3;
  } else if (frequency < 1100) {
    rfDividerValue = 4;
    rfDivider = 2;
  } else if (frequency < 2200) {
    rfDividerValue = 2;
    rfDivider = 1;
  } else {
    rfDividerValue = 1;
    rfDivider = 0;
  }
  // FIXME WIERD BEHAVIOR WITH NON DECIMAL RESOLUTION
  float N = ((frequency * (float)rfDividerValue) / ADF4351_PFDRF);
  uint16_t INTA = floor(N);
  uint16_t MOD = round(ADF4351_PFDRF / frequencyResolution);
  uint16_t FRAC = round((N-(float)INTA)*(float)MOD);
  uint16_t div = gcd(MOD,FRAC);
  MOD = (uint16_t)(MOD / div);
  FRAC = (uint16_t)(FRAC / div);

  if (MOD == 1) MOD = 2;

  // Set register array
  regADF4351[0]=__ADF4351Register0(FRAC,INTA);
  regADF4351[1]=__ADF4351Register1(MOD);
  regADF4351[2]=__ADF4351Register2();
  regADF4351[3]=__ADF4351Register3();
  regADF4351[4]=__ADF4351Register4(outputEnable,power,0,rfDivider);
  regADF4351[5]=__ADF4351Register5();

  // Write register array (R5 to R0)
  for (int i = 5; i >= 0; i--){
    __WriteRegister32(regADF4351[i]);
    // Serial.print(regADF4351[i],HEX);
    // Serial.print(" "); //debug
  }
  // Serial.println("");
  return 1;
}

// Private functions

/* Write to a 32 bits register */
void __WriteRegister32(const uint32_t value)   
{
  digitalWrite(PIN_ADF4351_LE, LOW);
  for (int i = 3; i >= 0; i--){
    SPI.transfer((value >> 8 * i) & 0xFF); 
  }   
  digitalWrite(PIN_ADF4351_LE, HIGH);
}
uint32_t __ADF4351Register0(const uint16_t FRAC, const uint16_t INTA){
  return ((uint32_t)FRAC << 3 | (uint32_t)INTA << 15);
}
uint32_t __ADF4351Register1(const uint16_t MOD){
  return ( 0UL << 28 // Disable phase adj. (1 bit)
    | 1UL << 27 // Set prescaler to 8/9 (1 bit)
    | 1UL << 15 // Phase, recommended value (12 bits)
    | (uint32_t)MOD << 3 // Modulus value (12 bits)
    | 0x1UL); // Register 1 (3 control bits)
}
uint32_t __ADF4351Register2(){
  return ( 0UL << 29 // Low noise mode (2 bits)
    | 0UL << 26 // Mux output = three state (3 bits)
    | 0UL << 25 // Reference doubler disable (1 bit)
    | 0UL << 24 // Reference/2 disable (1 bit)
    | 1UL << 14 // Reference counter (10 bits)
    | 0UL << 13 // Disable double buffer (1 bit)
    | 7UL << 9  // Charge current pump = 2.5mA (4 bits)
    | 0UL << 8  // LDF = FRAC-N (1 bit)
    | 0UL << 7  // LDP = 10ns (1 bit)
    | 1UL << 6  // Set PD pol to positive (1 bit)
    | 0UL << 5  // Disable power-down (PD) (1 bit)
    | 0UL << 4  // Disable CP three state (1 bit)
    | 0UL << 3  // Disable counter reset (1 bit)
    | 0x2UL);   // Register 2 (3 control bits)
}
uint32_t __ADF4351Register3(){
  return ( 0UL << 23 // Band select clock mode = low (1 bit)
    | 0UL << 22  // Antibacklash as FRAC-N (1 bit)
    | 0UL << 21  // Disable charge cancelation (1 bit)
    | 0UL << 18  // Disable cycle slip reduction (1 bit)
    | 0UL << 15  // Clock divider OFF (2 bits)
    | 150UL << 3 // Clock divider value (10 bits)
    | 0x3UL);    // Register 3 (3 control bits)
}

// outputPower : (0=-4dBm,1=-1dBm,2=2dBm,3=5dBm) 
// outputEnable : (1=enable rf output,0=disable rf output)
// muteTillLockDetect : 1=disable rf output until Locked (default=0)
// rfDivider: see datasheet
uint32_t __ADF4351Register4(const uint8_t outputEnable, const uint8_t outputPower, const uint8_t muteTillLockDetect, const uint8_t rfDivider){
  return (1UL << 23 // Feedback select - Fundamental
    | (uint32_t)rfDivider << 20
    | 200UL << 12 // Band select frequency fixed to 200
    | 0UL << 11 // Power the VCO up (1 bit)
    | (uint32_t)((muteTillLockDetect==1)?1:0) << 10 // Mute RF until locked (1 bit)
    | 0UL << 9 // Aux. output select (1 bit)
    | 0UL << 8 // Aux. enable output (1 bit)
    | 0UL << 6 // Aux. output power (2 bits)
    | (uint32_t)((outputEnable==1)?1:0) << 5 // Enable output (1 bit)
    | (uint32_t)outputPower << 3 // Output power (2 bits)
    | 0x4UL); // Register 4 (3 control bits)
}
uint32_t __ADF4351Register5(){
  return ((1UL << 22) // Sets lock detect pin operation to DigitalLockDetect (2 bits enum)
    | (3UL << 19) // Reserved, set to 1 (2 bits)
    | 0x5UL); // Register 5 (3 control bits)
  // return 0x580005;
}
