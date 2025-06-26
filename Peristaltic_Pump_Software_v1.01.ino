// include the library code:
#include <LiquidCrystal.h> //https://www.arduino.cc/en/Reference/LiquidCrystal -> LCD control
#include <ClickEncoder.h> //https://github.com/0xPIT/encoder/blob/master/README.md -> Encoder processing (timer based)
#include <TimerOne.h> //required for ClickEncoder.h 
#include <EEPROM.h> //write and read EEPROM (to save and load settings)


//LCD -----------------------------------------------------------------------------------
#define LCD_PIN_D4 8
#define LCD_PIN_D5 9
#define LCD_PIN_D6 10
#define LCD_PIN_D7 11
#define LCD_PIN_RS 13
#define LCD_PIN_EN 12
#define LCD_COLUMNS 16
#define LCD_ROWS 2
// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7);

//ENCODER --------------------------------------------------------------------------------
#define ENCODER_PIN_BUTTON 2
#define ENCODER_PIN_A 3
#define ENCODER_PIN_B 4
ClickEncoder *encoder;
int16_t last, value;

//STEP MOTOR -----------------------------------------------------------------------------
#define MOTOR_STEP_PIN 7
#define MOTOR_DIR_PIN 6
#define STEP_MODE 4 // (1: Full Step, 2: Half Step, 4: Quarter Step, ...)
#define STEPS_PER_FULL_ROT 200 // @ full steps
long delay_us;
long steps;
long step_counter = 0;

//CALIBRATION -----------------------------------------------------------------------------
#define CALIBR_ROTATIONS 30
#define CALIBR_DURATION 30 // seconds
#define CALIBR_DECIMALS 3
const int CALIBR_DECIMAL_CORR = pow(10,CALIBR_DECIMALS); // Calculated at compile/load time

//SERIAL COMMUNICATION ---------------------------------------------------------------------
#define BAUD 9600
#define SERIAL_INPUT_BUFFER_SIZE 200
String inputString = "";         // a String to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean usb_start=0; // Global flag for active USB-controlled motor action

//TIMER AND DELAYS
#define ENCODER_TIMER_INTERVAL_US 1000
#define SAVE_CONFIRM_DELAY_MS 700
#define MAX_MOTOR_STEP_DELAY_US 2000000L


//STATE ------------------------------------------------------------------------------------
boolean in_menu=0;
volatile boolean in_action=0;
boolean menu_entered=0;
boolean menu_left=0;

//GENERAL -----------------------------------------------------------------------------------
#define MICROSEC_PER_SEC 1000000

const unsigned int CALIBR_STEPS = CALIBR_ROTATIONS * STEPS_PER_FULL_ROT * STEP_MODE;
const unsigned int CALIBR_DELAY_US = (CALIBR_DURATION * MICROSEC_PER_SEC)/(CALIBR_STEPS*2);


//MENU ---------------------------------------------------------------------------------------
#define MAX_NUM_OF_OPTIONS 4
#define NUM_OF_MENU_ITEMS 10
#define VALUE_MAX_DIGITS 7 // Increased to accommodate larger numbers like "20.000"
int menu_number_1=0;
int menu_number_2=1;
boolean val_change =0;
double value_dbl;
char value_str[VALUE_MAX_DIGITS+1];

unsigned long lastLcdUpdateTime = 0;
const unsigned long lcdUpdateInterval = 100; // milliseconds

enum menu_type {
  VALUE,
  OPTION,
  ACTION
};

typedef struct 
{
  const char* name_;
  menu_type type; //0: value type, 1:option type, 2:action type
  int value;
  int decimals;
  int lim;
  const char* options[4];
  const char* suffix;
}menu_item;
int menu_items_limit = 10-1;
menu_item menu[10];


//███ SETUP ████████████████████████████████████████████████████████████████████████████████████████████████████
void setup(){
  
 pinMode(MOTOR_STEP_PIN,OUTPUT); 
 pinMode(MOTOR_DIR_PIN,OUTPUT);
 digitalWrite(MOTOR_DIR_PIN,LOW);
 digitalWrite(MOTOR_STEP_PIN,LOW);

 menu[0].name_ = "Start";
 menu[0].type = ACTION;
 menu[0].value = 0;
 menu[0].lim = 0;
 menu[0].suffix = "RUNNING!";

 menu[1].name_ = "Volume";
 menu[1].type = VALUE;
 menu[1].value = 0;
 menu[1].decimals = 1;
 menu[1].lim = 9999;
 menu[1].suffix="mL";

 menu[2].name_ = "V.Unit:";
 menu[2].type = OPTION;
 menu[2].value = 0;
 menu[2].lim = 3-1;
 menu[2].options[0] = "mL";
 menu[2].options[1] = "uL";
 menu[2].options[2] = "rot";

 menu[3].name_ = "Speed";
 menu[3].type = VALUE;
 menu[3].value = 0;
 menu[3].decimals = 1;
 menu[3].lim = 999;
 menu[3].suffix="mL/min";

 menu[4].name_ = "S.Unit:";
 menu[4].type = OPTION;
 menu[4].value = 0;
 menu[4].lim = 3-1;
 menu[4].options[0] = "mL/min";
 menu[4].options[1] = "uL/min";
 menu[4].options[2] = "rpm";

 menu[5].name_ = "Direction:";
 menu[5].type = OPTION;
 menu[5].value = 0;
 menu[5].lim = 2-1;
 menu[5].options[0] = "CW";
 menu[5].options[1] = "CCW";

 menu[6].name_ = "Mode:";
 menu[6].type = OPTION;
 menu[6].value = 0;
 menu[6].lim = 3-1;
 menu[6].options[0] = "Dose";
 menu[6].options[1] = "Pump";
 menu[6].options[2] = "Cal.";

 menu[7].name_ = "Cal.";
 menu[7].type = VALUE;
 menu[7].value = 0;
 menu[7].decimals = CALIBR_DECIMALS;
 menu[7].lim = 20000;
 menu[7].suffix="mL";

 menu[8].name_ = "Save Sett.";
 menu[8].type = ACTION;
 menu[8].value = 0;
 menu[8].lim = 0;
 menu[8].suffix = "OK!";

 menu[9].name_ = "USB Ctrl";
 menu[9].type = ACTION;
 menu[9].value = 0;
 menu[9].lim = 0;
 menu[9].suffix = "ON!";

  for (int i=0; i <= menu_items_limit; i++){
      menu[i].value = eepromReadInt(i*2);
  }
  
  encoder = new ClickEncoder(ENCODER_PIN_B, ENCODER_PIN_A, ENCODER_PIN_BUTTON, 4); //(Encoder A, Encoder B, PushButton)
  encoder->setAccelerationEnabled(false);
  Timer1.initialize(ENCODER_TIMER_INTERVAL_US);
  Timer1.attachInterrupt(timerIsr);
  last = 0;
  
  Serial.begin(BAUD);
  inputString.reserve(SERIAL_INPUT_BUFFER_SIZE);
  // set up the LCD's number of columns and rows:
  lcd.begin(LCD_COLUMNS, LCD_ROWS);
  // Print a message to the LCD.
  menu[1].suffix = menu[2].options[menu[2].value];
  if (strcmp(menu[1].suffix, "uL") == 0){
    menu[1].decimals = 0;
  } else {
    menu[1].decimals = 1;
  }
  menu[3].suffix = menu[4].options[menu[4].value]; // Moved this line up to ensure suffix is set before comparison
  if (strcmp(menu[3].suffix, "uL/min") == 0){
    menu[3].decimals = 0;
  } else {
    menu[3].decimals = 1;
  }
  update_lcd();
  steps = steps_calc(menu[1].value, menu[2].value, menu[7].value, menu[1].decimals);
  delay_us = delay_us_calc(menu[3].value, menu[4].value, menu[7].value, menu[3].decimals);
  
}



//███ LOOP ████████████████████████████████████████████████████████████████████████████████████████████████████

void loop() {
  
// BUTTON HANDLING ////////////////////////////////////////////////////////////////////////////////

ClickEncoder::Button b = encoder->getButton();
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Clicked:
        if(menu[menu_number_1].type == VALUE ||menu[menu_number_1].type == OPTION){ // if value or option type
          in_menu =!in_menu;
        }
        if(menu[menu_number_1].type == ACTION){ // if action type
          in_action=!in_action;
          step_counter= 0;
        }
        
        if (in_action == true ||in_menu ==true){ //menu entered
          menu_entered = true;
        }
        
        if (in_action == false && in_menu ==false){ //menu left
          menu_left = true;
        }
        break;
        
      case ClickEncoder::DoubleClicked:
          if (menu[menu_number_1].type == VALUE){
          menu[menu_number_1].value = menu[menu_number_1].value + menu[menu_number_1].lim/10;
          val_change=true;
          }
        break;
      case ClickEncoder::Held:
          if (menu[menu_number_1].type == VALUE){
          menu[menu_number_1].value = 0;
          val_change=true;
          }
        break;
      case ClickEncoder::Released:
        break;
        }
  }


/// SETUP ////////////////////////////////////////////////////////////////////////////////

if (menu_entered){
  lcd.blink();
  if (menu[menu_number_1].type == ACTION){
    lcd.setCursor((LCD_COLUMNS - strlen(menu[menu_number_1].suffix)), 0);
    lcd.print(menu[menu_number_1].suffix);
    lcd.setCursor(15, 0);
  }
  if (menu[menu_number_1].type == VALUE){
    encoder->setAccelerationEnabled(true);
  }
  menu_entered = false;
}



/// ACTIONS ////////////////////////////////////////////////////////////////////////////////

if (in_action){
  switch (menu_number_1){
  case 0: //Start
  if (menu[6].value == 0){ //Dose
    if (dose(steps, delay_us, step_counter)){
      exit_action_menu();
    }
  } else if (menu[6].value == 1){ //Pump
    pump(delay_us);
  } else if (menu[6].value == 2){ //Cal.
    if (dose(CALIBR_STEPS, CALIBR_DELAY_US, step_counter)){
      exit_action_menu();
    }
  }
  break;

  case 8:
   for (int i=0; i <= menu_items_limit; i++){
      eepromWriteInt(i*2,menu[i].value);
   }
   delay(SAVE_CONFIRM_DELAY_MS);
   menu_left = true;
  break;
  
  case 9: // USB Control
    { // New scope for local variables
      static char currentUsbCommand = 0; // Persists across loop iterations for this case
      static long usb_vol_uL = 0;
      static long usb_rate_uL_min = 0;
      static int usb_cal_value = 0;
      static long usb_steps = 0;
      static long usb_delay_us = 0;

      char receivedChar; // Temporary for reading serial

      while (Serial.available()) {
        receivedChar = (char)Serial.read();
        step_counter = 0; // Reset step counter on any new command char
        usb_start = true; // Assume a command will start, can be overridden by 'x' or 'w'

        if (receivedChar == 'p') {
          currentUsbCommand = 'p';
          usb_rate_uL_min = Serial.parseInt();
          usb_cal_value = Serial.parseInt();
          if (usb_cal_value == 0) {
            usb_cal_value = menu[7].value;
          }
          usb_delay_us = delay_us_calc(usb_rate_uL_min, 1 /*uL/min mode*/, usb_cal_value, 0 /*decimals*/);
        } else if (receivedChar == 'd') {
          currentUsbCommand = 'd';
          usb_vol_uL = Serial.parseInt();
          usb_rate_uL_min = Serial.parseInt();
          usb_cal_value = Serial.parseInt();
          if (usb_cal_value == 0) {
            usb_cal_value = menu[7].value;
          }
          usb_steps = steps_calc(usb_vol_uL, 1 /*uL mode*/, usb_cal_value, 0 /*decimals*/);
          usb_delay_us = delay_us_calc(usb_rate_uL_min, 1 /*uL/min mode*/, usb_cal_value, 0 /*decimals*/);
        } else if (receivedChar == 'c') {
          currentUsbCommand = 'c';
          // Uses CALIBR_STEPS, CALIBR_DELAY_US directly, no params needed from serial
        } else if (receivedChar == 'w') {
          currentUsbCommand = 0; // Not an action command
          usb_start = false;
          int temp_cal = Serial.parseInt();
          menu[7].value = temp_cal;
          for (int i = 0; i <= menu_items_limit; i++) {
            eepromWriteInt(i * 2, menu[i].value);
          }
        } else if (receivedChar == 'x') {
          currentUsbCommand = 0;
          usb_start = false;
        } else {
          // Potentially an unknown character or just a delimiter (like newline)
          // If it's not a command character, we might not want to set usb_start = true
          // For now, the logic implies any serial char could start some processing if not 'w' or 'x'
          // This might need refinement if delimiters cause issues.
           if (currentUsbCommand == 0) usb_start = false; // If no active command, don't start on random char
        }
      }

      if (usb_start && currentUsbCommand != 0) {
        if (currentUsbCommand == 'p') {
          pump(usb_delay_us);
        } else if (currentUsbCommand == 'd') {
          if (dose(usb_steps, usb_delay_us, step_counter)) {
            usb_start = false;
            currentUsbCommand = 0;
          }
        } else if (currentUsbCommand == 'c') {
          if (dose(CALIBR_STEPS, CALIBR_DELAY_US, step_counter)) {
            usb_start = false;
            currentUsbCommand = 0;
          }
        }
      } else if (!usb_start) { // Ensure command is cleared if usb_start became false
          currentUsbCommand = 0;
      }
    } // End scope for USB local variables
    break;
}

/// MENU (no action) ////////////////////////////////////////////////////////////////////////////////

} else if (!in_action){

if (val_change == true) {
  unsigned long currentTime = millis();
  if (currentTime - lastLcdUpdateTime > lcdUpdateInterval) {
    update_lcd();
    lastLcdUpdateTime = currentTime;
  }
  val_change = false; // Reset flag regardless of whether LCD updated
}
  
value += encoder->getValue(); // encoder update

if (!in_menu){ // no menu selected
  val_change = encoder_selection(menu_number_1, menu_number_2, menu_items_limit); //process value change

}else if(in_menu){ // menu selected
  if(menu[menu_number_1].type == 0){
    val_change = encoder_value_selection(menu[menu_number_1].value, menu[menu_number_1].lim);
  } else {
    val_change = encoder_selection(menu[menu_number_1].value, menu[menu_number_1].lim);
  }
}

}

/// CLOSE ////////////////////////////////////////////////////////////////////////////////

if (menu_left){
  lcd.noBlink();
  if (menu[menu_number_1].type == ACTION){
    exit_action_menu();
  }
  if (menu[menu_number_1].type == VALUE){
    encoder->setAccelerationEnabled(false);
  }
  
  if (menu_number_1 == 2){ // V.Unit changed
    menu[1].suffix = menu[2].options[menu[2].value]; // Update Volume suffix
      if (strcmp(menu[1].suffix, "uL") == 0){
        menu[1].decimals = 0;
      } else {
        menu[1].decimals = 1;
       }
  }

  if (menu_number_1 == 4){ // S.Unit changed
    menu[3].suffix = menu[4].options[menu[4].value]; // Update Speed suffix
    if (strcmp(menu[3].suffix, "uL/min") == 0){
      menu[3].decimals = 0;
    } else {
      menu[3].decimals = 1;
    }
  }
  if (menu_number_1 == 5){ //Change Direction
    if (menu[5].value == 0){
      digitalWrite(MOTOR_DIR_PIN,LOW); 
    }else{
      digitalWrite(MOTOR_DIR_PIN,HIGH);
    }
  }
  steps = steps_calc(menu[1].value, menu[2].value, menu[7].value, menu[1].decimals);
  delay_us = delay_us_calc(menu[3].value, menu[4].value, menu[7].value, menu[3].decimals);
  
  menu_left = false;
}

} 

//███ FUNCTION DECLARATION █████████████████████████████████████████████████████████████████████████████████████████████████

//_____________________________________________________________________________________________

void timerIsr() {
  encoder->service();
}

//_____________________________________________________________________________________________

boolean dose(long _steps, int _delay_us, long & inc) {
      if(inc < _steps){
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        delayMicroseconds(_delay_us);
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        delayMicroseconds(_delay_us);
        inc++;
        return false;
      } else {
        inc=0;
        return true;
      }
}
//_____________________________________________________________________________________________
/*
void dose_slow(long _steps, int _delay_us, long & inc) {
      if(inc < _steps){
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        delay(_delay_us/1000); 
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        delay(_delay_us/1000);
        inc++;
      } else {
        inc=0;
        exit_action_menu();
      }
}*/
//_____________________________________________________________________________________________

void pump(int _delay_us) {
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        delayMicroseconds(_delay_us); 
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        delayMicroseconds(_delay_us);
}
//_____________________________________________________________________________________________

void exit_action_menu(){
   in_action = false;
   // Clear the suffix previously printed for the action
   const char* suffix_to_clear = menu[menu_number_1].suffix;
   int suffix_len = strlen(suffix_to_clear);
   if (suffix_len > 0 && suffix_len < LCD_COLUMNS) { // Basic sanity check for length
       lcd.setCursor((LCD_COLUMNS - suffix_len), 0);
       for (int i = 0; i < suffix_len; ++i) {
           lcd.print(" ");
       }
   }
   lcd.noBlink();
}
//_____________________________________________________________________________________________

long steps_calc(long volume_val, int unit_mode, int calibr_val, int vol_decimals) {
  long _steps;
  double actual_volume = (double)volume_val / powerOf10(vol_decimals);

  // cal_mL_per_rot: Calibration factor in mL per one full rotation of the motor.
  // calibr_val is the EEPROM stored value, e.g., 15000 for 15.000 mL per CALIBR_ROTATIONS.
  // CALIBR_DECIMAL_CORR is pow(10, CALIBR_DECIMALS), e.g., 1000.
  double cal_mL_per_rot = (double)calibr_val / CALIBR_DECIMAL_CORR / CALIBR_ROTATIONS;

  if (unit_mode == 2) { // Unit is 'rotations'
    // actual_volume is interpreted directly as number of rotations.
    _steps = (long)(STEPS_PER_FULL_ROT * STEP_MODE * actual_volume);
    return _steps;
  }

  // For units mL or uL
  double volume_in_mL;
  if (unit_mode == 1) { // uL
    volume_in_mL = actual_volume / 1000.0;
  } else { // mL (unit_mode == 0)
    volume_in_mL = actual_volume;
  }

  if (cal_mL_per_rot == 0) { // Avoid division by zero if calibration is invalid
    return 0;
  }

  double rotations_needed = volume_in_mL / cal_mL_per_rot;
  _steps = (long)(STEPS_PER_FULL_ROT * STEP_MODE * rotations_needed);
  return _steps;
}
//_____________________________________________________________________________________________

long delay_us_calc(long speed_val, int speed_unit_mode, int calibr_val, int speed_decimals) {
  double actual_speed = (double)speed_val / powerOf10(speed_decimals); // e.g., mL/min, uL/min, or rpm

  // cal_mL_per_rot: Same as in steps_calc
  double cal_mL_per_rot = (double)calibr_val / CALIBR_DECIMAL_CORR / CALIBR_ROTATIONS;

  double rotations_per_minute;
  if (speed_unit_mode == 2) { // Unit is 'rpm' (rotations per minute)
    rotations_per_minute = actual_speed;
  } else { // Unit is volume/min (mL/min or uL/min)
    double volume_mL_per_minute;
    if (speed_unit_mode == 1) { // uL/min
      volume_mL_per_minute = actual_speed / 1000.0;
    } else { // mL/min (speed_unit_mode == 0)
      volume_mL_per_minute = actual_speed;
    }

    if (cal_mL_per_rot == 0) { // Invalid calibration
      return MAX_MOTOR_STEP_DELAY_US; // Return a very large delay (effectively zero speed)
    }
    rotations_per_minute = volume_mL_per_minute / cal_mL_per_rot;
  }

  if (rotations_per_minute <= 0) { // Speed is zero or negative (invalid)
    return MAX_MOTOR_STEP_DELAY_US; // Very large delay
  }

  double steps_per_minute = STEPS_PER_FULL_ROT * STEP_MODE * rotations_per_minute;
  if (steps_per_minute <= 0) { // Should not happen if rotations_per_minute > 0
      return MAX_MOTOR_STEP_DELAY_US;
  }

  double steps_per_second = steps_per_minute / 60.0;
  
  // Each step has two phases (HIGH pulse, LOW pulse). Delay is for one phase.
  // Total time per step = 2 * delay_us.
  // Steps per second = 1 / (2 * delay_us_in_seconds)
  // delay_us_in_seconds = 1 / (2 * steps_per_second)
  // delay_us = (1.0 / (2.0 * steps_per_second)) * MICROSEC_PER_SEC;
  double d_delay_us = MICROSEC_PER_SEC / (2.0 * steps_per_second);

  long _delay_us = (long)d_delay_us;

  if (_delay_us < 1) { // Practical minimum delay
    _delay_us = 1;
  }
  // Max delay could also be capped if motor can't go arbitrarily slow due to holding torque etc.
  // but 2,000,000 us (2 seconds per phase) is already very slow.

  return _delay_us;
}
//_____________________________________________________________________________________________

void update_lcd(){
    lcd.clear();
//first line LCD ------------------------------------
  lcd.setCursor(0, 0);
  lcd.print(menu_number_1);
  lcd.print("|");
  lcd.print(menu[menu_number_1].name_);
  if (menu[menu_number_1].type == 0){         //if value type
    value_dbl = menu[menu_number_1].value;
    value_dbl = value_dbl/powerOf10(menu[menu_number_1].decimals);
    dtostrf(value_dbl, VALUE_MAX_DIGITS, menu[menu_number_1].decimals, value_str );
    lcd.print(" ");
    lcd.print(value_str); //print value
    lcd.print(menu[menu_number_1].suffix);
  }  else if(menu[menu_number_1].type == 1){  //if option type
    lcd.print(" ");
    lcd.print(menu[menu_number_1].options[menu[menu_number_1].value]); //print menu[x].option[] of menu[x].value
  } else if(menu[menu_number_1].type == 2){   //if action type

  }
  
//second line LCD ------------------------------------
  lcd.setCursor(0, 1);
  lcd.print(menu_number_2);
  lcd.print("|");
  lcd.print(menu[menu_number_2].name_);
  if (menu[menu_number_2].type == 0){         //if value type
    value_dbl = menu[menu_number_2].value;
    value_dbl = value_dbl/powerOf10(menu[menu_number_2].decimals);
    dtostrf(value_dbl, VALUE_MAX_DIGITS, menu[menu_number_2].decimals, value_str );
    lcd.print(" ");
    lcd.print(value_str); //print value
    lcd.print(menu[menu_number_2].suffix);
  }  else if(menu[menu_number_2].type == 1){  //if option type
    lcd.print(" ");
    lcd.print(menu[menu_number_2].options[menu[menu_number_2].value]); //print menu[x].option[] of menu[x].value
  } else if(menu[menu_number_2].type == 2){   //if action type

  }
  
  lcd.setCursor(1, 0);
}
//_____________________________________________________________________________________________

boolean encoder_selection(int & x, int lim){ //sub menu
  if (value > last) {
    x++;
    if(x>lim){
      x=0;
    }
    last = value;
    return true;
  }else if(value < last){
    x--;
    if(x<0){
      x=lim;
    }
    last = value;
    return true;
  } else {
    return false;
  }
}
//_____________________________________________________________________________________________

boolean encoder_value_selection(int & x, int lim){ //sub menu
  if (value != last) {
    x = x + value - last;
    if(x>lim){
      x=0;
    }
    if(x<0){
      x=lim;
    }
    last = value;
    return true;
  } else {
    return false;
  }
}
//_____________________________________________________________________________________________

boolean encoder_selection(int & x, int & y, int lim){ //main menu
  if (value > last) {
    x++;
    y++;
    if(x>lim)
    {
      x=0;
    }
    if(y>lim)
    {
      y=0;
    }
    last = value;
    return true;
  }else if(value < last){
    y = menu_number_1;
    x--;
    if(x<0)
    {
      x=lim;
    }
    last = value;
    return true;
  }else {
    return false;
  }

}
//_____________________________________________________________________________________________

void eepromWriteInt(int adr, int wert) { 
//http://shelvin.de/eine-integer-zahl-in-das-arduiono-eeprom-schreiben/
byte low, high;

  low=wert&0xFF;
  high=(wert>>8)&0xFF;
  EEPROM.write(adr, low); // dauert 3,3ms 
  EEPROM.write(adr+1, high);
  return;
} //eepromWriteInt
//_____________________________________________________________________________________________

int eepromReadInt(int adr) { 
//http://shelvin.de/eine-integer-zahl-in-das-arduiono-eeprom-schreiben/
byte low, high;

  low=EEPROM.read(adr);
  high=EEPROM.read(adr+1);
  return low + ((high << 8)&0xFF00);
} //eepromReadInt

//_____________________________________________________________________________________________
// Helper function to calculate powers of 10 for small integer exponents.
// More efficient than float pow() for this specific case if used frequently.
double powerOf10(int exp) {
  double res = 1.0;
  if (exp == 0) return 1.0; // Common case, quick return

  boolean is_negative = exp < 0;
  if (is_negative) exp = -exp; // Work with positive exponent

  for (int i = 0; i < exp; ++i) {
    res *= 10.0;
  }

  return is_negative ? (1.0 / res) : res;
}
