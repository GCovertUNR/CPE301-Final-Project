/* Dependancies:

*/

#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

class State
{
private:
  // LED
  const uint8_t pinRed = PA1;   // = pin 23
  const uint8_t pinGreen = PA3; // = pin 25
  const uint8_t pinBlue = PA5;  // = pin 27
  const uint8_t pinYellow = PA7; // = pin 29
  void LED_init();
  // Fan
  const uint8_t pinFan = PA2;   // = pin 24
  void fan_init();

  const unsigned char status[4] = {'E','R','I','D'};
  const unsigned char events[4] = {'R','S','L','T'};  // Reset, Start/Stop, Low Res, Toggle run/idle
  unsigned char currentStatus;
  void adc_init();

  // determine the state of the system based on:
    // temp, humidity, on/off button, water level
    // use interupts to update the State
public:
  State() {} //Default constructor
  void init();
  unsigned char newEvent(char event); // Take an event, update and return status
  void LED_off();
  void red();
  void green();
  void blue();
  void yellow();
  void fan(bool fanState);
  unsigned int adc_read(unsigned char adc_channel_num);
 
      // State Events
  bool error(); // Report Error when reservior is low
  bool running();   // not sure what the bool will be used for.
  bool idle();
  bool disabled();
};

#endif