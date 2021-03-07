 
#include "RunningMedian.h"

// instead of using RX, TX pins of hardware serial use different pins
// so we are able to use the debug monitor
//#define USE_SOFTWARE_SERIAL_PIN_2_3

#define USE_LCD

#ifdef USE_LCD
#include <LiquidCrystal.h>
const int rs = 12, en = 11, d4 = 7, d5 = 6, d6 = 5, d7 = 4;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
#endif


#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
#include <SoftwareSerial.h>
SoftwareSerial MIDI(2, 3); // RX, TX
#endif

uint32_t currentMicros = 0;                // should be uint32_t

const int ppqn = 24;                       // should be uint8_t
int64_t inClockQuarterBarTickCounter = 0;  // should be uint8_t     as it loops from 0 to 24 (ppqn)
uint32_t inClockLastQuarterBarStart = 0;   // should be uint32_t
uint32_t inClockTickCounter = 0;           // should be uint32_t

int64_t inClockFullBarTickCounter = 0;     // should be uint8_t as it loops from 0 to 96 (4*ppqn)
uint32_t inClockLastFullBarStart = 0;      // should be uint32_t

int64_t outClockFullBarTickCounter = 0;    // should be uint8_t as it loops from 0 to 96 (4*ppqn)
uint32_t outClockLastFullBarStart = 0;     // should be uint32_t
uint32_t outClockTickCounter = 0;          // should be uint32_t

uint32_t tempLastStartMicros = 0;          // should be uint32_t

uint32_t tempScheduledNextTick = 0;        // should be uint32_t

bool waitForTicks = true;

RunningMedian recentQuarterBarDurations = RunningMedian(10);
// add another debounce level
RunningMedian recentQuarterBarDurations2 = RunningMedian(10);


int32_t debouncedTickWidth = 0;          // should be uint32_t
int32_t debouncedTickWidthWithDelta = 0; // should be uint32_t


// positive = send later than recieve
// negative = send before recieve
const int16_t clockDelayMilliseconds = -30;

// int16_t softTickWidthDelta = 0;
const int8_t softTickNumTreshold = 5;
const int16_t softCorrectionMicros = 300;

bool weAreToSlow = false;
bool weAreToFast = false;
#ifndef USE_SOFTWARE_SERIAL_PIN_2_3
HardwareSerial & MIDI = Serial;
#endif
void setup() {
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.begin(115200);
  Serial.println("starting serial with debug monitor....");
  MIDI.begin(31250);
#endif
#ifndef USE_SOFTWARE_SERIAL_PIN_2_3
  MIDI.begin(31250); // MIDI baud rate
#endif

#ifdef USE_LCD
  lcd.begin(16, 2);
  lcd.print("hi");
#endif

  tempLastStartMicros = micros();
}
int incomingByte = 0;
void loop() {
  currentMicros = micros();
  if (MIDI.available()) {
    int incomingByte = MIDI.read();
    if (incomingByte == 0xF8) {
      handleMidiEventClock();
    }
    if (incomingByte == 0xFA) {
      resetJitterHelperVariables();
      handleMidiEventStart();
#ifdef USE_LCD
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("start");
#endif
    }
    if (incomingByte == 0xFC) {
      handleMidiEventStop();
      resetJitterHelperVariables();
#ifdef USE_LCD
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("stop");
#endif
    }
  }
  
  //MIDI.read();
  checkSendOutClockTick();
}

void resetJitterHelperVariables() {
  
  inClockQuarterBarTickCounter = 0;
  inClockLastQuarterBarStart = 0;
  inClockTickCounter = 0;
  inClockFullBarTickCounter = 0;
  inClockLastFullBarStart = 0;
  
  outClockFullBarTickCounter = 0;
  outClockLastFullBarStart = 0;
  outClockTickCounter = 0;
  
  waitForTicks = true;
  
  recentQuarterBarDurations.clear();
  recentQuarterBarDurations2.clear();

  debouncedTickWidth = 0;
  debouncedTickWidthWithDelta = 0;
  
  //softTickWidthDelta = 0;

  weAreToSlow = false;
  weAreToFast = false;
}

void handleMidiEventTick() {
  handleMidiEventClock();
}


void handleMidiEventClock() {
  //MIDI.sendClock();
  //softSerial.write(0xF8);
  //return;
  inClockTickCounter+=1;
  inClockQuarterBarTickCounter+=1;
  inClockFullBarTickCounter+=1;
  if (inClockFullBarTickCounter == ppqn * 4) {
    inClockFullBarTickCounter = 0;
    inClockLastFullBarStart = currentMicros;
    waitForTicks = false;
  }
  if (inClockQuarterBarTickCounter == ppqn) {
    //debug("----------------- quarter note clock IN ----------------");
    recentQuarterBarDurations.add(currentMicros - inClockLastQuarterBarStart);
    recentQuarterBarDurations2.add(recentQuarterBarDurations.getAverage()/ppqn);
    debouncedTickWidth = recentQuarterBarDurations2.getAverage();
    /*
    if(weAreToFast == true) {
      debug("we are to fast");
    }
    if(weAreToSlow == true) {
      debug("we are to slow");
    }
    */
    inClockQuarterBarTickCounter = 0;
    inClockLastQuarterBarStart = currentMicros;
#ifdef USE_LCD
    if (inClockFullBarTickCounter % (2*ppqn) == 0) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(String(inClockTickCounter) + " i " + String(tickWidthToBpm(debouncedTickWidth)) + "bpm");
      lcd.setCursor(0,1);
      int32_t diff = outClockTickCounter - inClockTickCounter;
      lcd.print(String(outClockTickCounter) + " o "+ String(diff));
    }
#endif
  }

  if (waitForTicks == true) {
    sendClockTick();
  }
}

void checkSendOutClockTick() {


  if (waitForTicks == true) {
    return;
  }
  
  if (currentMicros < tempScheduledNextTick) {
    // debug("db = 0");
    return;
  }
  sendClockTick();
 
}

int tickDiff = 0;

int32_t correctionDelta = 0;

void sendClockTick() {
  outClockTickCounter+=1;
  outClockFullBarTickCounter+=1;
  if (outClockFullBarTickCounter == ppqn*4) {
    outClockLastFullBarStart = currentMicros;
    outClockFullBarTickCounter = 0;
  }
  //MIDI.sendClock();
  MIDI.write(0xF8);
  correctionDelta = 0;
  if(outClockTickCounter > (inClockTickCounter + 2)) {
    // we are to fast. lets add a little time for scheduled next tick
    
    correctionDelta = (outClockTickCounter - inClockTickCounter) * 10000;
    
  }
  if(inClockTickCounter > (outClockTickCounter + 2)) {
    // we are to slow. lets remove a little time for scheduled next tick
    
    //correctionDelta = (outClockTickCounter - inClockTickCounter) * 10000;
    // correctionDelta = debouncedTickWidth/((outClockTickCounter - inClockTickCounter) * -10000); 
    correctionDelta = debouncedTickWidth * -1; 
    
  }
  tempScheduledNextTick = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*debouncedTickWidth) + correctionDelta;
}


void handleMidiEventStart() {
  resetJitterHelperVariables();
  tempLastStartMicros = currentMicros;
  //MIDI.sendStart();
  MIDI.write(0xFA);
  
  inClockLastQuarterBarStart = currentMicros;
  inClockLastFullBarStart = currentMicros;
}
void handleMidiEventStop() {
  //MIDI.sendStop();
  MIDI.write(0xFC);
  
  resetJitterHelperVariables();
}

float tickWidthToBpm(int32_t tickWidth)
{
  // calculate interval of clock tick[microseconds] (24 ppqn)
  if (tickWidth < 1)
  {
    return 0;
  }
  return 60 / (tickWidth * 0.000001 * ppqn);
}

uint32_t bpmToTickWidth(float bpm)
{
  // calculate interval of clock tick[microseconds] (24 ppqn)
  if (bpm < 1)
  {
    return 0;
  }
  return 60 / (bpm * 0.000001 * ppqn);
}


int32_t add1BpmToDebouncedTickWidth()
{
  return bpmToTickWidth(tickWidthToBpm(debouncedTickWidth) + 3);
}

int32_t remove1BpmToDebouncedTickWidth()
{
  return bpmToTickWidth(tickWidthToBpm(debouncedTickWidth) - 3);
}


void debug(String Msg)
{
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.println(Msg);
#endif
}
