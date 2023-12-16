#include "State.h"

// LED and Fan
volatile unsigned char* port_a = (unsigned char*) 0x22;
volatile unsigned char* ddr_a  = (unsigned char*) 0x21; 


// LCD
volatile unsigned char* port_b = (unsigned char*) 0x25; 
volatile unsigned char* ddr_b  = (unsigned char*) 0x24; 

// ADC and Res sensor
volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

void State::init() {
  currentStatus = status[2];
  idle();
  LED_init();
  fan_init();
  adc_init();
}

unsigned char State::newEvent(char event){
  switch (event){
    case 'R':           // reset event
      if(currentStatus == status[0]){      // if in the error state returns true
        currentStatus = status[2];         // reset to idle
        idle();
      }
      break;
    case 'S':           // start/stop button event
      if(currentStatus == status[3]){ // if disabled, go to ilde.
        currentStatus = status[2];
        idle();
        break;
      } else {
        currentStatus = status[3];
        disabled();
      }
      break;
    case 'L':                   // Low reservoir
      currentStatus = status[0];        // set error status
      error();
      break;
    case 'T':
      if (currentStatus == status[2]) {   // if idle, run
        currentStatus = status[1];        // switch to running
        running();
        break;
      } else if (currentStatus == status[1]) { // if running, idle
        currentStatus = status[2];        // switch to idle
        idle();
      }
      break;
    default:
    break;
  }
  return currentStatus;
  // returns status as a char.
}

// Private state functions
bool State::error(){  // status 0
  if (currentStatus == status[0]) {
    red();      // set LED to red
    fan(false);
    return 1;
  } else {
    return 0;
  }
}

bool State::running(){  // status 1
  if (currentStatus == status[1]) {
    blue();
    fan(true);
    return 1;
  } else {
    return 0;
  }
}

bool State::idle(){   // status 2
  if (currentStatus == status[2]) {
    green();
    fan(false);
    return 1;
  } else {
    return 0;
  }
}

bool State::disabled(){   // status 3
  if (currentStatus == status[3]) {
    yellow();
    fan(false);
    return 1;
  } else {
    return 0;
  }
}

// Fan control ______________________________
void State::fan(bool fanstate) {
  if (fanstate){
    *port_a |= (1<<pinFan);     // set the pin high
  } else {
    *port_a &= ~(1<<pinFan);    // set the pin low
  }
}

void State::fan_init(){
  *ddr_a |= (1<<pinFan);    // set pin as output
  fan(false);                   // start with fan off
}

// LED control ______________________________
void State::LED_off() {
  *port_a = (1<<pinRed)|(1<<pinGreen)|(1<<pinBlue);
  *port_a &= ~(1<<pinYellow);
}

void State::red() {
  LED_off();
  *port_a &= ~(1<<pinRed);
}

void State::green() {
  LED_off();
  *port_a &= ~(1<<pinGreen);
}

void State::blue() {
  LED_off();
  *port_a &= ~(1<<pinBlue);
}

void State::yellow() {
  LED_off();
  *port_a |= (1<<pinYellow);
}

void State::LED_init() {
  // use port commands to set these all to outputs;
  *ddr_a = (1<<pinRed)|(1<<pinGreen)|(1<<pinBlue)|(1<<pinYellow);
  // Test LED - PLACEHOLDER
  red();
  delay(500); // NOT ALLOWED
  green();
  delay(500);
  blue();
  delay(500);
  yellow();
  delay(500);
  // start with LED off
  LED_off();
}

// initialize ADC
void State::adc_init() {
  // setup the A register
  *my_ADCSRA |= 0b10000000; // set bit   7 to 1 to enable the ADC
  *my_ADCSRA &= 0b11011111; // clear bit 6 to 0 to disable the ADC trigger mode
  *my_ADCSRA &= 0b11110111; // clear bit 5 to 0 to disable the ADC interrupt
  *my_ADCSRA &= 0b11111000; // clear bit 0-2 to 0 to set prescaler selection to slow reading
  // setup the B register
  *my_ADCSRB &= 0b11110111; // clear bit 3 to 0 to reset the channel and gain bits
  *my_ADCSRB &= 0b11111000; // clear bit 2-0 to 0 to set free running mode
  // setup the MUX Register
  *my_ADMUX  &= 0b01111111; // clear bit 7 to 0 for AVCC analog reference
  *my_ADMUX  |= 0b01000000; // set bit   6 to 1 for AVCC analog reference
  *my_ADMUX  &= 0b11011111; // clear bit 5 to 0 for right adjust result
  *my_ADMUX  &= 0b11100000; // clear bit 4-0 to 0 to reset the channel and gain bits
}

// Read ADC channel
unsigned int State::adc_read(unsigned char adc_channel_num)
{
  // clear the channel selection bits (MUX 4:0)
  *my_ADMUX  &= 0b11100000;
  // clear the channel selection bits (MUX 5)
  *my_ADCSRB &= 0b11110111;
  // set the channel number
  if(adc_channel_num > 7)
  {
    // set the channel selection bits, but remove the most significant bit (bit 3)
    adc_channel_num -= 8;
    // set MUX bit 5
    *my_ADCSRB |= 0b00001000;
  }
  // set the channel selection bits
  *my_ADMUX  += adc_channel_num;
  // set bit 6 of ADCSRA to 1 to start a conversion
  *my_ADCSRA |= 0x40;
  // wait for the conversion to complete
  while((*my_ADCSRA & 0x40) != 0);
  // return the result in the ADC data register
  return *my_ADC_DATA;
}