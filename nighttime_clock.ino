/*
   Night Time Clock v1.2
   Copyright (c) 2021-2022 Johan Oscarsson
   Released under the MIT licence

   Documentation and project comments can be found on Github:
   https://www.github.com/johan-m-o/NightTimeClock
*/

#include <Adafruit_VEML7700.h>      // Adafruit VEML 7700 light sensor library https://github.com/adafruit/Adafruit_VEML7700 (Tested and working on v1.1.1)
#include <FastLED.h>                // FastLED LED control library https://github.com/FastLED/FastLED (Tested and working on v3.5.0)
#include <FlashStorage.h>           // Save data to flash memory (the Nano 33 lacks an EEPROM) https://github.com/cmaglie/FlashStorage (Tested and working on v1.0.0)
#include <RTCZero.h>                // Arduino RTC library https://www.arduino.cc/en/Reference/RTC (Tested and working on v1.6.0)
#include <TimeLib.h>                // Arduino Time Library https://www.github.com/PaulStoffregen/Time (Tested and working on v1.6.1)
#include <WiFiNINA.h>               // Arduino WiFi Library https://github.com/arduino-libraries/wifinina (Tested and working on v1.8.13)

#include "arduino_secrets.h"

RTCZero rtc;
Adafruit_VEML7700 veml = Adafruit_VEML7700();

/*****************
 *   Variables   *
 *****************/

/* LEDs */
#define CLOCK_STRIPS 4
#define CLOCK_SEGMENTS 7
#define SEPARATOR_LEDS 2
CRGB numbers[CLOCK_STRIPS][CLOCK_SEGMENTS];
CRGB separator[SEPARATOR_LEDS];
CRGB dColour;
CRGB sepColour;
byte hueG = 96, hueR = 0, hueB = 160, ledBright, prevBright, minBright = 1, maxBright = 200;
bool blinkOn, lightOn = true, prevLightOn = lightOn, partyTime = false, luxMeter = false;

/* WiFi */
const char WSSID[] = WIFI_SSID, WPASS[] = WIFI_PASSWORD;
int status = WL_IDLE_STATUS;

/* Time */
unsigned long prevBlinkMS, blinkIntervalMS = 500, partyTimer, pTimerMS, luxTimerMS;
byte prevM = 0, nightH, nightM, morningH, morningM, prevNH, prevNM, prevMH, prevMM, changeState = 0;
bool night, changeNight = false;

/* Button */
const byte bPin = 13;
byte clicksCheck = 0;
bool buttonPress = false, buttonNightPress = false, buttonLongPress = false, buttonCancelPress = false, cancelNight = false, turnOnNight = false;
unsigned long buttonTimer = 0, buttonEventMS = 0, buttonPressDurationMS = 0;

/* Potentiometer */
const byte pPin = A0;
byte pVal, prevPVal;
bool pChange = false;

/* Persistent settings */
FlashStorage(nightH_storage, byte);
FlashStorage(nightM_storage, byte);
FlashStorage(morningH_storage, byte);
FlashStorage(morningM_storage, byte);

/*****************
 *   Functions   *
 *****************/
 
// Calcualte if 1 or 2 hours should be added to the time, depending on if it's daylight savings time or not (current code for GMT+1)
int dstCheck(int y, byte mth, byte d, byte w) {
  int item;
  int h = 3600;

  if (mth == 3 && d >= 25) { // Daylight savings starts on the last Sunday of March
    item = h * 2;
    for (byte i = 0; i < 6; i++) {
      if (d == 25 + i && w > 0 + i) {
        item = h;
        break;
      }
    }
  } else if (mth == 10 && d >= 25) { // Daylight savings stops on the last Sunday of October
    item = h;
    for (byte i = 0; i < 6; i++) {
      if (d == 25 + i && w > 0 + i) {
        item = h * 2;
        break;
      }
    }
  } else if (mth >= 4 && mth <= 9 || mth == 10 && d < 25) { // April to September and before 25th of October it's for sure daylight savings.
    item = h * 2;
  } else {
    item = h;
  }

  return item;
}

// Set RTC time from NTP server
void rtcSet() {
  unsigned long t = WiFi.getTime(); // Poll the WiFi-module for NTP time
  int dstComp = dstCheck(year(t), month(t), day(t), weekday(t)-1); // Compensate weekday for the 1-7 implementation, starting on Sunday (we need 0-6)
  rtc.setTime(hour(t + dstComp), minute(t), second(t));
  rtc.setDate(day(t), month(t), year(t));
}

// Check if night mode needs to be enabled
void nightTime() {
  // Need to add persistent storage of varibles.
  if ((!cancelNight && (rtc.getHours() == nightH && rtc.getMinutes() >= nightM || rtc.getHours() > nightH && rtc.getHours() < 24 || rtc.getHours() >= 0 && rtc.getHours() < morningH || rtc.getHours() == morningH && rtc.getMinutes() < morningM)) || turnOnNight) {
    dColour = CRGB::Red;
    night = true;
  } else {
    dColour = CRGB::Green;
    if (!(rtc.getHours() == nightH && rtc.getMinutes() >= nightM || rtc.getHours() > nightH && rtc.getHours() < 24 || rtc.getHours() >= 0 && rtc.getHours() < morningH || rtc.getHours() == morningH && rtc.getMinutes() <= morningM)) {
      cancelNight = false;
    }
    night = false;
  }
}

// Change the digits on the clock
void clockSegment(byte digit, byte num) {
  // All on (99) or off (100)
  if (num == 99 || num == 0 || num == 2 || num == 3 || num == 4 || num == 5 || num == 6 || num == 8 || num == 9) {
    for (byte i = 0; i < CLOCK_SEGMENTS; i++) {
      numbers[digit][i] = dColour;
    }
  } else if (num == 100 || num == 1 || num == 7) {
    for (byte i = 0; i < CLOCK_SEGMENTS; i++) {
      numbers[digit][i] = CRGB::Black;
    }
  }

  if (num == 0) {
    numbers[digit][0] = CRGB::Black;
    
  } else if (num == 1) {
    numbers[digit][3] = dColour;
    numbers[digit][4] = dColour;
    
  } else if (num == 2) {
    numbers[digit][1] = CRGB::Black;
    numbers[digit][4] = CRGB::Black;
    
  } else if (num == 3) {
    numbers[digit][1] = CRGB::Black;
    numbers[digit][6] = CRGB::Black;
    
  } else if (num == 4) {
    numbers[digit][2] = CRGB::Black;
    numbers[digit][5] = CRGB::Black;
    numbers[digit][6] = CRGB::Black;
    
  } else if (num == 5) {
    numbers[digit][3] = CRGB::Black;
    numbers[digit][6] = CRGB::Black;
    
  } else if (num == 6) {
    numbers[digit][3] = CRGB::Black;
    
  } else if (num == 7) {
    numbers[digit][2] = dColour;
    numbers[digit][3] = dColour;
    numbers[digit][4] = dColour;
    
  } else if (num == 9) {
    numbers[digit][5] = CRGB::Black;
    numbers[digit][6] = CRGB::Black;
  }

  FastLED.setBrightness(ledBright);
  FastLED.show();
}

// Display the current time
void dispTime() {
  byte h, m;
  
  if (changeNight) { // If Night Time interval is being set
    if (changeState < 2) {
      h = nightH;
      m = nightM;
    } else if (changeState > 1) {
      h = morningH;
      m = morningM;
    }
  } else { // Get time from the RTC
    rtcSet(); // Set the RTC every minute to compensate for the Nano 33's horrendous time accuracy (it doesn't have a proper crystal). Could of course use a different board, but this means a larger form factor or higher price (maybe there's a TeensLED or something that would work, but I haven't done the reasearch for that yet). Compensating in software is more than good enough. Hm... This comment is becoming rather long. Maybe I should stop.
    
    h = rtc.getHours();
    m = rtc.getMinutes();
    prevM = m;
    
    nightTime();
  }

  // Light up the digits
  if (h < 10) {
    clockSegment(0,100); // No leading zero
  } else {
    clockSegment(0,(h / 10) % 10);
  }
  clockSegment(1,h % 10);
  if (m < 10) {
    clockSegment(2,0);
  } else {
    clockSegment(2,(m / 10) % 10);
  }
  clockSegment(3,m % 10);

  // Turn off the separator LEDs when setting Night Time interval
  if (!changeNight) {
    separator[0] = dColour;
    separator[1] = dColour;
  }

  FastLED.show();
}

void ledBrightness() {
  ledBright = map(veml.readLux(), 0, 1500, minBright, maxBright); // Read the light sensors calculated lux value and map it to fit into 8 bits.
  if (ledBright != prevBright) {
    prevBright = ledBright;
    FastLED.setBrightness(ledBright);
  }
}

/*************
 *   Setup   *
 *************/

void setup() {
  /* LEDs */
  FastLED.addLeds<WS2812, 3, GRB>(numbers[0], CLOCK_SEGMENTS);
  FastLED.addLeds<WS2812, 5, GRB>(numbers[1], CLOCK_SEGMENTS);
  FastLED.addLeds<WS2812, 7, GRB>(separator, SEPARATOR_LEDS);
  FastLED.addLeds<WS2812, 9, GRB>(numbers[2], CLOCK_SEGMENTS);
  FastLED.addLeds<WS2812, 11, GRB>(numbers[3], CLOCK_SEGMENTS);

  /* WiFi */
  while (status != WL_CONNECTED) {
    separator[0] = CHSV(hueR,255,255);
    separator[1] = CHSV(hueR,255,255);
    FastLED.show();
    
    status = WiFi.begin(WSSID, WPASS);
    unsigned long wifiDelayMS = millis();
    
    byte i = 0;
    bool sepUp;
    while (millis() - wifiDelayMS < 5000) { // Fade separator LEDs in and out while connecting (in a different colour from the regular clock)
      separator[0] = CHSV(hueB,255,i);
      separator[1] = CHSV(hueB,255,i);
      
      FastLED.show();

      // Check if fade should increase or decrease
      if (i == 255) {
        sepUp = false;
      } else if (i == 0) {
        sepUp = true;
      }
      
      if (sepUp) {
        i++;
      } else {
        i--;
      }
    }
  }

  /* Button */
  pinMode(bPin, INPUT_PULLUP);

  /* Persistent settings */
  // Load Night Time interval
  if (nightH_storage.read() == 0 && nightM_storage.read() == 0 && morningH_storage.read() == 0 && morningM_storage.read() == 0) { // If there are no settings stored in flash memory, load default values
    nightH = 18;
    nightM = 30;
    morningH = 6;
    morningM = 45;
  } else { // Load settings from flash memory
    nightH = nightH_storage.read();
    nightM = nightM_storage.read();
    morningH = morningH_storage.read();
    morningM = morningM_storage.read();
  }
  prevNH = nightH;
  prevNM = nightM;
  prevMH = morningH;
  prevMM = morningM;
  
  /* Light sensor */
  veml.begin();
  veml.setGain(VEML7700_GAIN_1_4);
  veml.setIntegrationTime(VEML7700_IT_100MS);
  veml.interruptEnable(false);

  /* Time */
  rtc.begin();
  dispTime();
  prevBlinkMS = millis();
}

/************
 *   Loop   *
 ************/

void loop() {
  // Button
  if (digitalRead(bPin) == LOW) { // Button pressed
    if (!buttonPress) {
      buttonPress = true;
      buttonEventMS = millis();
      if (buttonTimer == 0) {
        buttonTimer = buttonEventMS;
      }
    }

    // Save the duration the button is pressed
    buttonPressDurationMS = millis() - buttonEventMS;

    if (buttonPressDurationMS >= 6000 || (changeNight && buttonPressDurationMS >= 4000)) { // If pressed long enough, long press detection resets
      buttonNightPress = false;
      buttonLongPress = false;
      buttonCancelPress = true;
      buttonTimer = 0;
      buttonPress = false;
      buttonPressDurationMS = 0;
    } else if ((buttonPressDurationMS >= 4000 && buttonPressDurationMS < 6000 && !buttonLongPress && !changeNight && !night && lightOn) || (changeNight && buttonPressDurationMS >= 2000 && buttonPressDurationMS < 4000)) { // Long press detected
      buttonNightPress = false;
      buttonLongPress = true;
    } else if (buttonPressDurationMS >= 2000 && buttonPressDurationMS < 4000 && !buttonNightPress && !changeNight && !night && !partyTime) { // Night mode press detected, or if the LEDs are off it's Party Time!
      if (lightOn) {
        buttonNightPress = true;
      } else {
        buttonTimer = 0;
        buttonPress = false;
        buttonPressDurationMS = 0;
        partyTime = true;
        partyTimer = millis();
      }
    }
  } else { // Button released (or never pressed)
    if (buttonPress) {
      if (buttonLongPress) { // Long press actions
        changeNight = !changeNight;
          if (!changeNight) { // Setting the Night Time interval needs a faster blink
            changeState = 0;
            blinkIntervalMS = 500;
            dispTime();
          } else {
            blinkIntervalMS = 250;
          }
        buttonTimer = 0;
        buttonLongPress = false;
        
      } else if (buttonNightPress) { // Night mode press actions
        turnOnNight = true;
        buttonTimer = 0;
        buttonNightPress = false;
        dispTime();
        
      } else {
        if (buttonPressDurationMS > 50 && !buttonCancelPress) { // Regular press actions (debounce duration check)
          if (changeNight && changeState < 4) { // Change Night Time set state
            changeState++;
            pChange = false;
            
          } else { // Regular click
            clicksCheck++;
          }
        }
        buttonCancelPress = false;
      }
      
      buttonPress = false;
      buttonPressDurationMS = 0;
    }
    
    // Double click check
    if (clicksCheck >= 2 && millis() - buttonTimer < 500) {
      if (!lightOn) {
        luxMeter = !luxMeter;
        luxTimerMS = millis();
        if (!luxMeter) {
          prevLightOn = !lightOn;
        }
        
      } else if (changeNight && changeState == 4) {
        // Save new values for the Night Time interval (only if they've changed, we want to spare that flash memory from excessive writing)
            if (nightH != prevNH) {
              nightH_storage.write(nightH);
            }
            if (nightM != prevNM) {
              nightM_storage.write(nightM);
            }
            if (morningH != prevMH) {
              morningH_storage.write(morningH);
            }
            if (morningM != prevMM) {
              morningM_storage.write(morningM);
            }
            prevNH = nightH;
            prevNM = nightM;
            prevMH = morningH;
            prevMM = morningM;
            
            // Reset parameters and display time
            changeNight = false;
            changeState = 0;
            pChange = false;
            blinkIntervalMS = 500;
            dispTime();
            
      } else if (night) {
        if (turnOnNight) {
          turnOnNight = false;
        } else {
          cancelNight = !cancelNight;
        }
      }
      
      clicksCheck = 0;
      buttonTimer = 0;
      if (lightOn) {
        dispTime();
      }
      
    } else if (!partyTime && clicksCheck > 0 && millis() - buttonTimer >= 500) {
      if (!luxMeter) {
        lightOn = !lightOn;
      }
      clicksCheck = 0;
      buttonTimer = 0;
    }
  }
  
  // Party time!
  if (partyTime) {
    CRGB newColour;
    randomSeed(analogRead(A1) * (millis() % 10 + ((millis() / 10) % 10) * 10 + ((millis() / 100) % 10) * 100 + ((millis() / 1000) % 10) * 1000)); // Randomise the seed using unused A1 and the seconds part of millis()
    byte i, j, k, l;
    byte brightness = 200, rainbowStrip = 0;
    byte randP = random(3), randColour, fireWorksDelay = 10;
    byte scatterArray[9], scatterBrightArray[5] = {0,0,0,0}, scatterColourArray[5];
    unsigned long scatterStart = 0, ledDelayMS = millis();

    // Turn off all LEDs and set brightness
    FastLED.clear();
    FastLED.setBrightness(brightness);
    
    while (partyTime && millis() - partyTimer < 30000) { // Run the party pattern for 30 seconds or until the button is double clicked
      // Button press to cancel Party Time
      if (digitalRead(bPin) == LOW) { // Button pressed
        if (!buttonPress) {
          buttonPress = true;
          buttonEventMS = millis();
          if (buttonTimer == 0) {
            buttonTimer = buttonEventMS;
          }
        }

        // Save the duration the button is pressed
        buttonPressDurationMS = millis() - buttonEventMS;

      } else { // Button released (or never pressed)
        if (buttonPress) {
          if (buttonPressDurationMS > 50) { // Regular press actions (debounce duration check)
            clicksCheck++;
            buttonPress = false;
            buttonPressDurationMS = 0;
          }
        }
        
        // Double click check
        if (clicksCheck >= 2 && millis() - buttonTimer < 500) {
          partyTime = false;
          clicksCheck = 0;
          buttonTimer = 0;
          
        } else if (millis() - buttonTimer >= 500) {
          clicksCheck = 0;
          buttonTimer = 0;
        }
      }
      
      if (randP == 0) { // Random rainbow blinking, all LEDs
        if (millis() - ledDelayMS >= 200) { // Delay between LED changes
          // Randomise the LED strip order
          byte ledStripsArray[] = {0,1,2,3};
          size_t nStrips = sizeof(ledStripsArray) / sizeof(ledStripsArray[0]);
          for (size_t kS = 0; kS < nStrips - 1; kS++) {
            size_t lS = random(0, nStrips - kS);
            byte tmpVal = ledStripsArray[kS];
            ledStripsArray[kS] = ledStripsArray[lS];
            ledStripsArray[lS] = tmpVal;
          }
          
          // Randomise the LED order per strip
          byte ledsArray[] = {0,1,2,3,4,5,6};
          size_t nLEDs = sizeof(ledsArray) / sizeof(ledsArray[0]);
          for (size_t kL = 0; kL < nLEDs - 1; kL++) {
            size_t lL = random(0, nLEDs - kL);
            byte tmpVal = ledsArray[kL];
            ledsArray[kL] = ledsArray[lL];
            ledsArray[lL] = tmpVal;
          }

          // Pick random colour
          randColour = random(256);

          // Light up the digits
          for (i = 0; i < CLOCK_STRIPS; i++) {
              for (j = 0; j < CLOCK_SEGMENTS; j++) {
                numbers[ledStripsArray[i]][ledsArray[j]] = CHSV(randColour + random(256), 255, brightness);
              }
          }
          // Light up the separator LEDs
          separator[0] = CHSV(k + random(256), 255, brightness);
          separator[1] = CHSV(k + random(256), 255, brightness);
          
          FastLED.show();
          ledDelayMS = millis();
        }

      } else if (randP == 1) { // Rainbow colours travel across the clock face, over and over
        if (millis() - ledDelayMS >= 10) { // Delay between LED changes
          rainbowStrip = 0;
          
          // Cycle through the hues
          i++;
          
          for (j = 0; j < 13; j++) {
            // Change LED strip
            if (j < 6 && j != 0 && j % 3 == 0 || j > 6 && j % 3 == 1) {
              rainbowStrip++;
            }
            
            // Cycle through the colours and increase the hue, one column of LEDs at a time
            if (j == 6) { // Light up the separator LEDs
              separator[0] = CHSV(i + (j * 10), 255, brightness);
              separator[1] = CHSV(i + (j * 10), 255, brightness);
            } else {
              if (j < 6 && j % 3 == 0 || j > 6 && j % 3 == 1) { // Light up the digits
                numbers[rainbowStrip][1] = CHSV(i + (j * 10), 255, brightness);
                numbers[rainbowStrip][6] = CHSV(i + (j * 10), 255, brightness);
              } else if (j < 6 && j % 3 == 1 || j > 6 && j % 3 == 2) {
                numbers[rainbowStrip][0] = CHSV(i + (j * 10), 255, brightness);
                numbers[rainbowStrip][2] = CHSV(i + (j * 10), 255, brightness);
                numbers[rainbowStrip][5] = CHSV(i + (j * 10), 255, brightness);
              } else if (j < 6 && j % 3 == 2 || j > 6 && j % 3 == 0) {
                numbers[rainbowStrip][3] = CHSV(i + (j * 10), 255, brightness);
                numbers[rainbowStrip][4] = CHSV(i + (j * 10), 255, brightness);
              }
            }
          }
          
          FastLED.show();
          ledDelayMS = millis();
        }
        
      } else if (randP == 2) { // Cycle through the rainbow on all LEDs at the same time.
        if (millis() - ledDelayMS >= 30) { // Delay between LED changes
          // Cycle through the hues
          if (i < 256) {
            i++;
          } else {
            i = 0;
          }
          
          // Ligth up the digits
          for (j = 0; j < CLOCK_STRIPS; j++) {
            fill_solid(numbers[j], CLOCK_SEGMENTS, CHSV(i, 255, brightness));
          }
          // Light up the separator LEDs
          separator[0] = CHSV(i, 255, brightness);
          separator[1] = CHSV(i, 255, brightness);
          
          FastLED.show();
          ledDelayMS = millis();
        } 
      }
    }
    // End Party Time
    partyTime = false;
    lightOn = true;
    dispTime();
  }

  // Change Night Light time
  if (!partyTime && changeNight) {
    if (changeState == 0 || changeState == 2) { // Change the hour component
      pVal = map(analogRead(pPin), 0, 1023, 0, 23);
      if (changeState == 0) {
        if (pVal == nightH) { // Only start changing if the potentiometer position matches the current value
          pChange = true;
        }
        if (pChange) {
          nightH = pVal;
        }
      } else {
        if (pVal == morningH) {
          pChange = true;
        }
        if (pChange) {
          morningH = pVal;
        }
      }
      
    } else { // Change the minute compontent (in 5 minute increments)
      pVal = map(analogRead(pPin), 0, 1023, 0, 11);
      if (changeState == 1) {
        if (5 * pVal == nightM) { // Only start changing if the potentiometer position matches the current value
          pChange = true;
        }
        if (pChange) {
          nightM = 5 * pVal;
        }
      } else {
        if (5 * pVal == morningM) {
          pChange = true;
        }
        if (pChange) {
          morningM = 5 * pVal;
        }
      }
    }
  }

  // Turn the LEDs off or on
  if (!partyTime && !changeNight && prevLightOn != lightOn) {
    if (lightOn) {
      dispTime();
    } else {
      FastLED.clear();
      FastLED.show();
    }
    prevLightOn = lightOn;
  }

  // Measure light level and display the lux value on the clock face
  if (!partyTime && !changeNight && luxMeter && !lightOn) {
    if (millis() - luxTimerMS > 1000) { // Only update once every second
      separator[0] = CRGB::Black;
      separator[1] = CRGB::Black;
      
      float luxValue = veml.readLux(); // Read light sensor

      if (luxValue >= 10000) { // Can't display anything if the recorded value is higher than 9999
        for (byte i = 0; i < CLOCK_STRIPS; i++) {
          numbers[i][0] = dColour;
        }
      } else if (luxValue < 100) { // Use the lower separator LED as a decimal point if the recorded lux value is less than 100
        separator[0] = dColour;
        if (luxValue < 10) {
          clockSegment(0,100); // No leading zero
        } else {
          clockSegment(0,int(luxValue / 10) % 10);
        }
        clockSegment(1,int(luxValue) % 10);
        clockSegment(2,int(luxValue * 10) % 10);
        clockSegment(3,int(luxValue * 100) % 10);
        
      } else {
        if (luxValue < 1000) {
          clockSegment(0,100); // No leading zero
        } else {
          clockSegment(0,int(luxValue / 1000) % 10);
        }
        clockSegment(1,int(luxValue / 100) % 10);
        clockSegment(2,int(luxValue / 10) % 10);
        clockSegment(3,int(luxValue) % 10);
      }
      
      ledBrightness();
      FastLED.show();
      
      luxTimerMS = millis();
    }
  }

  // Check light levels and update the time
  if (!partyTime && !changeNight && lightOn) {
    // Update the clock every new minute
    if (rtc.getMinutes() != prevM) {
      dispTime();
    }
    
    ledBrightness();
    FastLED.show();
  }

  // Blink the separator LEDs every second, but only during the day, or blink active number LEDs if set Night Time is active
  if (!partyTime && changeNight || !night && lightOn) {
    if (millis() - prevBlinkMS >= blinkIntervalMS) { // Turn LEDS on or off at a set interval (depends on the current state). Not gonna give an exact 1 second indication when the normal clock is active, but close enough for our purposes. No need to make it complicated...
      if (blinkOn) { // Blink digits or separator LEDs depending on what state is active
        if (changeNight) {
          dispTime();
        }
        // Change the LED colour when a long press is detected
        if (buttonLongPress) {
          sepColour = CRGB::Blue;
          
        } else if (buttonNightPress) {
          if (!night) {
            sepColour = CRGB::Red;
          }
          
        } else {
          sepColour = dColour;
        }
        
      } else { // Turn off digits or separator LEDs depending on what state is active
        if (changeNight) {
          if (changeState == 0 || changeState == 2) {
            clockSegment(0,100);
            clockSegment(1,100);
          } else {
            clockSegment(2,100);
            clockSegment(3,100);
          }
        } else {
          sepColour = CRGB::Black;
        }
      }
      separator[0] = sepColour;
      separator[1] = sepColour;
      
      prevBlinkMS = millis();
      blinkOn = !blinkOn;
      FastLED.show();
    }
  }
}
