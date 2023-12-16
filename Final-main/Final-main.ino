/*
CPE301 Final Project - main
Written by Griffin Covert, Fall 2023

Functionality Goals
 - Create a humidity and temperature controlled swamp cooler
 - Monintor water resevour levels
 - control fan
 - display temp and hum data
 - stepper motor controlled vent position
 - on/off button
 - record RTC time and date of events over serial
*/

/* Pins Used:
    LED : RGBY = D23, D25, D27, D29 (PA1,3,5,7)
    Fan : = D24 (PA2)
Display : const int RS = 11, EN = 12, D4 = 2, D5 = 3, D6 = 4, D7 = 5;
Buttons : A8 (PK0) (PCINT 16), A9 (PK1) (PCINT 17)
    Res : A0 
    RTC : D20 (SDA), D21 (SCL)
Vent adj: A7
Vent pin: D8
*/

// Standard Libraries
#include <LiquidCrystal.h>
#include <DHT.h>  //Hum/temp Lib
#include <DS3231.h> //RTC Lib

// My Classes and Globals
#include "State.h"

const uint8_t setTemp = 24;   // if temp is greater than 22, run cooler
const uint16_t resThresh = 430; // Low water threshold

// Serial UART Communication
#define TBE 0x20   //  B00100000  Transmit Buffer Empty
volatile unsigned char *myUCSR0A = (unsigned char *)0xC0;  // USART MSPIM Control and Status Register 
volatile unsigned char *myUCSR0B = (unsigned char *)0xC1;  // USART MSPIM Control and Status Register 
volatile unsigned char *myUCSR0C = (unsigned char *)0xC2;  // USART MSPIM Control and Status Register
volatile unsigned int  *myUBRR0  = (unsigned int *) 0xC4;  // Baud Rate Register
volatile unsigned char *myUDR0   = (unsigned char *)0xC6;  // USART Data Register

// Vent pins D9 (PH6) step, D8 (PH5) Direction, A7 - using OC2B timer to send step pulses
volatile unsigned char *myTCCR2A = (unsigned char *) 0xB0;
volatile unsigned char *myTCCR2B = (unsigned char *) 0xB1;
volatile unsigned int  *myTCNT2  = (unsigned  int *) 0xB2;
volatile unsigned char *myTIFR2 =  (unsigned char *) 0x37;
volatile unsigned char *port_h = (unsigned char*) 0x102;
volatile unsigned char *ddr_h = (unsigned char*) 0x101;
unsigned long pos_vent = 0; // 200 steps, 0-200, needs to be unsigned long for math

// buttons on pins A8 (PK0)(PCINT 16) and A9 (PK1)(PCINT 17)
volatile unsigned char *port_k = (unsigned char*) 0x108; 
volatile unsigned char *ddr_k = (unsigned char*) 0x107;
volatile unsigned char *pin_k = (unsigned char*) 0x106;

volatile bool rst_isr = 0;  // flags for button interrupt
volatile bool btn_isr = 0;

// LCD
const int RS = 11, EN = 12, D4 = 2, D5 = 3, D6 = 4, D7 = 5;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

// create objects
State state;
// Humidity and temp DHT11 sensor
DHT dht(10, DHT11);
// RTC
DS3231 myRTC;
byte month, day, hour, minute, second;
bool century = false;
bool h12h24;   // switch 12hr/24hr time
bool ampm;      // set flag for am/pm
bool refresh;   // update display if true
int last_refresh;
//________________________________________________________
//________________________________________________________
void setup() {
  U0init(9600);    // use this in place of Serial when ready
  Wire.begin();                 // init I2C for RTC

  lcd.begin(16, 2);             // init LCD
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  lcd.clear();
  state.init();     // initialize ADC, LED, Fan
  Buttons_init();
  dht.begin();
  vent_init();
  
  last_refresh = myRTC.getMinute() - 1; // start assuming last refresh was a minute ago

  U0print_line("Boot up complete");

}

void loop() {
  int temp = dht.readTemperature();
  int hum = dht.readHumidity();
  uint16_t resLevel = get_ResLevel();

  refresh = update_LCD(last_refresh);   // take last udate time
  // check refresh flag
  if (refresh){              // update the LCD once per minute
    lcd_hum(hum);
    lcd_temp(temp);
    lcd_ResLevel(resLevel);
    last_refresh = myRTC.getMinute();
    refresh = false;    // reset flag
  }

  if (rst_isr) {          // check if rst button was pressed
    U0print_line("isr flag for rst triggered");
    lcd.clear();
    state.newEvent('R');    // trigger reset event
    last_refresh = myRTC.getMinute() - 1; // call for lcd refresh
    rst_isr = false;    // reset isr flag
  }
  if (btn_isr) {
    U0print_line("isr flag for btn triggered");
    lcd.clear();
    state.newEvent('S');
    last_refresh = myRTC.getMinute() - 1;
    btn_isr = false;
  }
  
  if(state.error()){
    U0print_line("Error state triggered");
    // report to LCD one time
    lcd.clear();
    lcd.print("Error: Water Low");
    lcd.setCursor(0, 1);
    lcd.print("Time:");
    lcd.print(myRTC.getHour(h12h24, ampm));
    lcd.print(':');
    if (myRTC.getMinute() < 10) {lcd.print('0');} // add a leading zero to the minutes
    lcd.print(myRTC.getMinute());
    // whait for error to be cleared by button ISR
    while(state.error()){
      if (rst_isr) {          // check if rst button was pressed
        U0print_line("isr flag for rst triggered");
        lcd.clear();
        state.newEvent('R');    // trigger reset event
        last_refresh = myRTC.getMinute() - 1;
        rst_isr = false;    // reset isr flag
      }
      if (btn_isr) {
        U0print_line("isr flag for btn triggered");
        lcd.clear();
        state.newEvent('S');
        last_refresh = myRTC.getMinute() - 1;
        btn_isr = false;
      }
    }
  } else {
    set_Vent();      // update vent position
    
    if ((temp > setTemp) && state.idle()) {       // is hot
      state.newEvent('T');    // toggle to run
      U0print_line("is hot, started running");
    }
    if ((temp <= setTemp) && state.running()){    // is cold
      state.newEvent('T');    // toggle to idle
      U0print_line("is cold, stopped running");
    }
    if (resLevel < resThresh) {
      state.newEvent('L');    // report error
    }
  }
}

//________________________________________________________
//________________________________________________________
// UART Communications
void U0init(unsigned long U0baud){      // initialize uart
 unsigned long FCPU = 16000000;
 unsigned int tbaud;
 tbaud = (FCPU / 16 / U0baud - 1);    // set baud rate

 *myUCSR0A = 0x20;    //  B00100000 enables UDRE0 bit to indicate the transmit buffer is ready to receive new data.
 *myUCSR0B = 0x18;    //  B00011000 RXEN0 and TXEN0 enabling USART receive and transmit
 *myUCSR0C = 0x06;    //  B00000110 UDORD
 *myUBRR0  = tbaud;   //  Set the baud rate register
}

void U0putchar(unsigned char U0pdata){   // write serial
  while((*myUCSR0A & TBE) == 0);        // Wait till transmit buffer is ready
  *myUDR0 = U0pdata;                    // Transmit the char
}

// this function gives me nightmares.
void U0print_date(){                  // print the date and time when event occurs
  month = myRTC.getMonth(century);
  day = myRTC.getDate();
  hour = myRTC.getHour(h12h24, ampm);
  minute = myRTC.getMinute();
  second = myRTC.getSecond();

  char month_char[3];   // chars to hold 2 digit numbers as characters with null terminator
  char day_char[3];
  char hour_char[3];
  char minute_char[3];
  char second_char[3];
  
  itoa(month, month_char, 10);
  itoa(day, day_char, 10);
  itoa(hour, hour_char, 10);
  itoa(minute, minute_char, 10);
  itoa(second, second_char, 10);

  for (int i = 0; i<2; i++) {
    if (month_char[i] != '\0'){
      U0putchar(month_char[i]);
    } else {
      break;
    }
  }
  U0putchar('-');

  for (int i = 0; i<2; i++) {
    if (day_char[i] != '\0'){
      U0putchar(day_char[i]);
    } else {
      break;
    }
  }
  U0putchar(' ');

  for (int i = 0; i<2; i++) { 
    if (hour_char[i] != '\0'){
      U0putchar(hour_char[i]);
    } else {
      break;
    }
  }
  U0putchar(':');  
  for (int i = 0; i<2; i++) {
    if (minute_char[i] != '\0'){
      U0putchar(minute_char[i]);
    } else {
      break;
    }
  }
  U0putchar(':');  
  for (int i = 0; i<2; i++) {
    if (second_char[i] != '\0'){
      U0putchar(second_char[i]);
    } else {
      break;
    }
  }
  U0putchar('\n');
}

void U0print_line(char* msg){
  int i = 0;
  while(msg[i] != '\0'){           // until reaching null terminator
    U0putchar(msg[i]);
    i++;
  }
  U0putchar(' ');
  U0putchar('-');
  U0putchar(' ');
  U0print_date();
}

// Vent Control
void set_Vent() {             // position 0-1024
  long steps = 0;             // using longs to avoid overflows. Could save some mem here.
  unsigned long pot = 0;
  unsigned long last_pos = pos_vent;

  for(int i = 0; i < 64; i++) {   // sum over 64 samples
    pot += state.adc_read(7);
  }
  if(pot < 8100) {            // check boundaries to avoid overflow
    pos_vent = 0;             // min position
  } else if(pot > 61440) {    // max position
    pos_vent = 200;
  } else {
    pos_vent = ((pot - 8100) * 200) / 53340;
  }

  // calculate number of steps based on last position
  steps = last_pos - pos_vent;
  if(steps > 0){
    *port_h |= (1 << PH5); //Direction High
    move_vent(abs(steps));
  } else {
    *port_h &= ~(1 << PH5);    // Direction Low
    move_vent(abs(steps));
  }
}

void vent_init() {
  *ddr_h |= (1 << PH6); // set D9 to output
  *ddr_h |= (1 << PH5); // set D8 to output
  *port_h &= ~(1 << PH6);  // set output LOW
  *port_h &= ~(1 << PH5);  // set Direction LOW
}

void move_vent(unsigned int steps) {    // drives stepper at 250 Hz using timer counter 2B on (PH6)     // stop when desired number of steps made.
  unsigned int count = 0;
  while(count < 2*steps) {
    unsigned int ticks = 30;           
    *myTCCR2B &= 0xF8;            // stop the timer
    *myTCNT2 = (unsigned int) (255 - ticks);    // set the counts
    *myTCCR2A = 0x0;              // start the timer in normal mode
    *myTCCR2B |= 0b00000111;      // set timer prescaler from control register 1024
    while((*myTIFR2 & 0x01)==0);  // wait for overflow
    *myTCCR2B &= 0xF8;            // stop the timer
    *myTIFR2 |= 0x01;             // reset TOV    
    *port_h ^= (1 << PH6);        // Toggle PH6 do drive the motor
    count++;
  } 
 *port_h &= ~(1 << PH6);          // make sure it's set low when done
}

// Update and display temp and humidity
bool update_LCD(uint8_t t_last){    // take in the last updated time and check if it's time for a new update.
  if((myRTC.getMinute() != t_last)) {  //check for minute change
    return true;
  } else {
    return false;
  }
}

void lcd_hum(int hum){
  lcd.setCursor(0, 1);
  lcd.print("Hum:");
  lcd.print("   ");     // clear the space
  lcd.setCursor(4, 1);
  lcd.print(hum);
}

void lcd_temp(int temp) {
  lcd.setCursor(0, 0);
  lcd.print("Temp:");
  lcd.print("   ");     // clear the space
  lcd.setCursor(5, 0);
  lcd.print(temp);
}

unsigned int get_ResLevel() {
  unsigned int ResLevel = 1024 - state.adc_read(0);  // low num = low water
  return ResLevel;
}

void lcd_ResLevel(uint16_t ResLevel) {
  lcd.setCursor(8, 0);
  lcd.print("Res:");
  lcd.setCursor(12, 0);
  lcd.print(ResLevel);
}

// Buttons
void Buttons_init() {
  *ddr_k &= ~(1<<PK0);
  *ddr_k &= ~(1<<PK1); // set ports to input

  PCICR |= B00000100; // enable pin change interrupt for PCINT23:16
  PCMSK2 |= B00000011;    // enable PC interrupts on 17 and 16

}

ISR(PCINT2_vect) {    // button ISR to detect start/stop and reset
  if(!(*pin_k & (1 << PK0)))    // if start/stop (PK0) is pressed
  {
    rst_isr = true; // set reset isr flag
  }
  if(!(*pin_k & (1 << PK1)))   // if reset bttn (PK1) is pressed
  {
    btn_isr = true; // set button isr flag
  }
}