 
#include "RunningMedian.h"

// instead of using RX, TX pins of hardware serial use different pins
// so we are able to use the debug monitor
// #define USE_SOFTWARE_SERIAL_PIN_2_3

#define USE_LCD

#define USE_MIDI_LIBRARY

#ifdef USE_MIDI_LIBRARY
#include <MIDI.h>
#endif


#ifdef USE_LCD
#include <LiquidCrystal.h>
const int rs = 12, en = 11, d4 = 7, d5 = 6, d6 = 5, d7 = 4;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
#endif


#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
#include <SoftwareSerial.h>
SoftwareSerial MIDI(2, 3); // RX, TX
#endif

uint32_t currentMicros = 0;
const int8_t ppqn = 24;
bool weHaveADebouncedTempo = false;

// incoming (jittered) clock
int8_t inClockQuarterBarTickCounter = 0; // loops from 0 to 24 (ppqn)
uint32_t inClockLastQuarterBarStart = 0;
uint32_t inClockTickCounter = 0;
int8_t inClockFullBarTickCounter = 0;    // loops from 0 to 96 (4*ppqn)

// outgoing (dejittered) clock
int32_t outClockFullBarTickCounter = 0;   // loops from 0 to 96 (4*ppqn)
uint32_t outClockLastFullBarStart = 0;
uint32_t outClockTickCounter = 0;

uint32_t outClockLastSentTick = 0;

uint32_t scheduledNextTickMicroSecond = 0;

const uint8_t tickDriftTreshold = 2;
int32_t inOutDickDrift = 0;
int32_t driftingTickWidth = 0;

float currentTempo = 0.0;

// incoming serial data
int incomingByte = 0;


const uint16_t minBpmTickWidth = 65000; // [microseconds] =~ 38 BPM
const uint16_t maxBpmTickWidth = 8000;  // [microseconds] =~ 312 BPM


// helper variables for debouncing incoming tempo
int32_t debouncedTickWidth = 0;
RunningMedian recentQuarterBarDurations = RunningMedian(10);
RunningMedian recentDebouncedTickWidths = RunningMedian(10);


// positive = send later than recieve
// negative = send before recieve
const int16_t clockDelayMilliseconds = -30;

const int8_t softTickNumTreshold = 5;
const int16_t softCorrectionMicros = 300;

bool weAreToSlow = false;
bool weAreToFast = false;
#ifndef USE_SOFTWARE_SERIAL_PIN_2_3
// rename "Serial" to "MIDI "to can use different configurations without renaming variables
HardwareSerial & MIDI = Serial;
#endif


void setup() {
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.begin(115200);
  Serial.println("starting serial with debug monitor....");
#endif
  MIDI.begin(31250);
 
#ifdef USE_LCD
  lcd.begin(16, 2);
  lcd.print("hi");
#endif

}

void loop() {
  currentMicros = micros();
  if (MIDI.available()) {
    int incomingByte = MIDI.read();
    if (incomingByte == 0xF8) {
      handleMidiEventClock();
    }
    if (incomingByte == 0xFA) {
      handleMidiEventStart();
    }
    if (incomingByte == 0xFC) {
      handleMidiEventStop();
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
  
  outClockFullBarTickCounter = 0;
  outClockLastFullBarStart = 0;
  outClockTickCounter = 0;
  outClockLastSentTick = 0;
  
  weHaveADebouncedTempo = false;
  
  recentQuarterBarDurations.clear();
  recentDebouncedTickWidths.clear();

  debouncedTickWidth = 0;

  currentTempo = 0.0;

}

void handleMidiEventTick() {
  handleMidiEventClock();
}


/**
 * we got a clock tick from incoming clock
 */
void handleMidiEventClock() {
  //MIDI.sendClock();
  //softSerial.write(0xF8);
  //return;
  inClockTickCounter+=1;
  inClockQuarterBarTickCounter+=1;
  inClockFullBarTickCounter+=1;
  if (inClockFullBarTickCounter == ppqn * 4) {
    inClockFullBarTickCounter = 0;
    weHaveADebouncedTempo = true;
  }
  if (inClockQuarterBarTickCounter == ppqn) {
    //debug("----------------- quarter note clock IN ----------------");
    recentQuarterBarDurations.add(currentMicros - inClockLastQuarterBarStart);
    if(recentDebouncedTickWidths.getHighest() - recentDebouncedTickWidths.getLowest() > 2000) {
      // obviously we had a drastic tempo change
      recentDebouncedTickWidths.clear();
    }
    recentDebouncedTickWidths.add(recentQuarterBarDurations.getAverage()/ppqn);
    debouncedTickWidth = recentDebouncedTickWidths.getAverage();
    currentTempo = tickWidthToBpm(debouncedTickWidth);
    inClockQuarterBarTickCounter = 0;
    inClockLastQuarterBarStart = currentMicros;

#ifdef USE_LCD
    if (inClockFullBarTickCounter % (2*ppqn) == 0) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(String(inClockTickCounter) + " i " + String(currentTempo) + "bpm");
      lcd.setCursor(0,1);
      int32_t diff = outClockTickCounter - inClockTickCounter;
      lcd.print(String(outClockTickCounter) + " o "+ String(diff));
    }
#endif

  }

  if (weHaveADebouncedTempo == false) {
    sendClockTick();
  }
}


/**
 * check if there is need to send out a single clock tick
 */
void checkSendOutClockTick() {

  if (weHaveADebouncedTempo == false) {
    // after start we have to collect some timings for beeing able to dejitter
    // during this phase the jittered ticks will be directly sent out in handleMidiEventClock()
    return;
  }

  if (currentMicros <= scheduledNextTickMicroSecond) {
    // we still have to wait before sending the tick
    return;
  }
  
  if( currentMicros - outClockLastSentTick < maxBpmTickWidth) {
    // never send a tempo faster than 312 BPM
    //MIDI.write(0xFF);  // sestem reset - for debugging over serial...
    return;
  }
  
  //if( currentMicros - outClockLastSentTick > minBpmTickWidth) {
  //  // never send a tempo slower than 38 BPM
  //  return;
  //}
  
  sendClockTick();
 
}

int32_t correctionDelta = 0;

/**
 * really send the tick and calculate the time when the next tick has to be sent
 */
void sendClockTick() {
  if (outClockTickCounter == 0) {
    outClockLastFullBarStart = currentMicros;
  }
  outClockTickCounter+=1;
  outClockFullBarTickCounter+=1;
  outClockLastSentTick = currentMicros;
  if (outClockFullBarTickCounter == ppqn*4) {
    outClockLastFullBarStart = currentMicros;
    outClockFullBarTickCounter = 0;
  }
  //MIDI.sendClock();
  MIDI.write(0xF8);
  //scheduleNextTick();
  // without any drift next tick has to be sent in debouncedTickWidth microseconds
  scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*debouncedTickWidth);

  if (inClockTickCounter > (outClockTickCounter + tickDriftTreshold)) {
    // we are behind. lets remove a little time of scheduled next tick by increasing the tempo
    inOutDickDrift = inClockTickCounter - outClockTickCounter;

    driftingTickWidth = bpmToTickWidth((float)(currentTempo + inOutDickDrift));

    scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*driftingTickWidth);

    //scheduledNextTickMicroSecond = 10;
    //scheduledNextTickMicroSecond -= (inOutDickDrift * (debouncedTickWidth/inOutDickDrift));
    MIDI.write(0xFB);  // continue  (just for debuggung over serial visible in aseqdump)
    return;
  }

  if (outClockTickCounter > (inClockTickCounter + tickDriftTreshold)) {
    // our in out tick drift is tooo large (we are to fast)
    // add a little time to te schedule
    
    inOutDickDrift = outClockTickCounter - inClockTickCounter;

    driftingTickWidth = (inOutDickDrift > currentTempo)
      ? minBpmTickWidth
      : bpmToTickWidth((float)(currentTempo - inOutDickDrift));

    scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*driftingTickWidth);
    
    //scheduledNextTickMicroSecond += (inOutDickDrift * (debouncedTickWidth/inOutDickDrift));
    MIDI.write(0xFE);  // active sensing  (just for debuggung over serial visible in aseqdump)
  }
  
}

/**
 * this function checks if we have an I/O drift [tickAmount]
 * and adds or removes a few microseconds to the next tick scheduli8ng in case we need correction
 */
 /*
void scheduleNextTick () {
  // without any drift next tick has to be sent in debouncedTickWidth microseconds
  scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*debouncedTickWidth);

  if (inClockTickCounter > (outClockTickCounter + tickDriftTreshold)) {
    // we are to slow. lets remove a little time of scheduled next tick
    scheduledNextTickMicroSecond = 10;
    //scheduledNextTickMicroSecond = scheduledNextTickMicroSecond - debouncedTickWidth + (debouncedTickWidth/(inClockTickCounter - outClockTickCounter));
    MIDI.write(0xFB);  // continue  (just for debuggung over serial visible in aseqdump)
    return;
  }

  if (outClockTickCounter > (inClockTickCounter + tickDriftTreshold)) {
    // our in out tick drift is tooo large (we are to fast)
    // add a little time to te schedule
    scheduledNextTickMicroSecond += ((outClockTickCounter - inClockTickCounter) * (debouncedTickWidth/(outClockTickCounter - inClockTickCounter)));
    MIDI.write(0xFE);  // active sensing  (just for debuggung over serial visible in aseqdump)
  }
}
*/

void handleMidiEventStart() {
  resetJitterHelperVariables();
  //MIDI.sendStart();
  MIDI.write(0xFA);
  inClockLastQuarterBarStart = currentMicros;
#ifdef USE_LCD
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("start");
#endif
}

void handleMidiEventStop() {
  //MIDI.sendStop();
  MIDI.write(0xFC);
  
  resetJitterHelperVariables();
  // most clocks also send ticks during stop
  inClockLastQuarterBarStart = currentMicros;

#ifdef USE_LCD
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("stop");
#endif
}

float tickWidthToBpm(int32_t tickWidth)
{
  // calculate interval of clock tick[microseconds] (24 ppqn)
  if (tickWidth < 1) {
    return 0;
  }
  return 60 / (tickWidth * 0.000001 * ppqn);
}

uint32_t bpmToTickWidth(float bpm)
{
  // calculate interval of clock tick[microseconds] (24 ppqn)
  if (bpm < 1) {
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
