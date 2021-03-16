 
// midi-clock-dejitter / clock stabilizer for Arduino
//
// Original Created by audeonic
// ported to Arduino by othmar52
// @see https://github.com/othmar52/midi-clock-dejitter


// alternative wiring for debugging
//
// instead of using RX, TX pins of hardware serial use different pins
// so we are able to use the debug monitor of Arduino IDE
//#define USE_SOFTWARE_SERIAL_PIN_2_3

// alternative debugging via LCD display 16 x 2
//
// thanks to https://github.com/arduino-libraries/LiquidCrystal
// in this sketch a LCD1602 (16 chars x 2 lines) is used
//#define USE_LCD1602


// alternative usage of arduino midi library
//
// in case you want to implement some more midi magic to your sketch this
// library may be useful. but as long as we recieve or send only three
// different bytes (start, stop, tick) there is no need to use such an overhead
// thanks to https://github.com/FortySevenEffects/arduino_midi_library
//#define USE_MIDI_LIBRARY

#ifdef USE_MIDI_LIBRARY
#include <MIDI.h>
#endif

#if defined(USE_SOFTWARE_SERIAL_PIN_2_3) || defined(USE_LCD1602)
// thanks to https://github.com/yoursunny/PriUint64
#include "PriUint64.h"
#endif

#ifdef USE_LCD1602
#include <LiquidCrystal.h>
const int rs = 12, en = 11, d4 = 7, d5 = 6, d6 = 5, d7 = 4;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
#endif


#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
#include <SoftwareSerial.h>
#ifdef USE_MIDI_LIBRARY
SoftwareSerial softSerial(2, 3); // RX, TX
MIDI_CREATE_INSTANCE(SoftwareSerial, softSerial, MIDI);
#else
SoftwareSerial MIDI(2, 3); // RX, TX
#endif
#else
#ifdef USE_MIDI_LIBRARY
MIDI_CREATE_DEFAULT_INSTANCE();
#endif
#endif

#define PPQN 24

uint64_t currentMicros = 0;

uint64_t inClockTotalTickCount = 0;  // [tick] incremental counter incoming ticks after start
uint64_t inClockLastStartMicros = 0; // timestamp very first in tick
uint64_t inClockLastTickMicros = 0;  // microsecond of last incoming tick

uint64_t outClockTickIntervalMicros = 0;    // [microsecond]
static uint64_t outClockTotalTickCount = 0; // [tick] incremental counter sended ticks after start
uint64_t outClockLastTickMicros = 0;        // [microsecond]
float outClockTempoRoundingFactor = .0;     // for displaying the out clock tempo

float tempoDeviationBpm;            // [BPM] deviation of IN/OUT tempo

uint16_t inClockDebouncerAverageCount = 0;          // [tick]
uint64_t inClockDebouncerAverageIntervalMicros = 0; // [microsecond]
uint16_t inClockDebouncerAverageCountMax = 0;       // [tick]
uint8_t inClockDebouncerCycleTicks = 0;             // [tick]
int32_t inClockDebouncerCycleMod = 0;               // [tick]

float debouncerTolerance = .0;

uint64_t schedulerReferenceTickNum = 0;      // [tick]
uint64_t schedulerReferenceTickMicros = 0;   // [microsecond]
uint64_t schedulerReferenceTickInterval = 0; // [microsecond]


// positive = send later than recieve  !!!DANGER!!! positive value not supported yet
// negative = send before recieve
const int32_t clockDelayMillis = 0; // [milliseconds]  // 2051 =~ 1 bar @ 117 BPM


double sensitivity = 2;

/*
 *   taken from https://audeonic.com/midipace/
 *   ---------------------------------------------------------------------
 *   sensitivity   deviation        BPM rounding          Rounding factor
 *   ---------------------------------------------------------------------
 *   0.25          +/- 0.01 bpm     nearest 0.01 bpm         100
 *   0.5           +/- 0.025 bpm    nearest 0.05 bpm          20
 *   1             +/- 0.05 bpm     nearest 0.1 bpm           10
 *   2             +/- 0.25 bpm     nearest 0.5 bpm            2
 *   4             +/- 0.5 bpm      nearest 1.0 bpm            1
 *   8             +/- 2.5 bpm      nearest 5.0 bpm            0.2
 *   16            +/- 5.0 bpm      nearest 10.0 bpm           0.1
 */
void setSensitivity() {
  // set the defaults of sensitivity = 2
  outClockTempoRoundingFactor = 2.0;
  debouncerTolerance = .25;
  
  // TODO switch..case possible with double??
  if (sensitivity == 16) {  outClockTempoRoundingFactor =    .2; debouncerTolerance = 2.5;   }
  if (sensitivity == 8) {   outClockTempoRoundingFactor =    .1; debouncerTolerance = 5.0;   }
  if (sensitivity == 4) {   outClockTempoRoundingFactor =   1;   debouncerTolerance =  .5;   }
  if (sensitivity == 2) {   outClockTempoRoundingFactor =   2.0; debouncerTolerance =  .25;  }
  if (sensitivity == 1) {   outClockTempoRoundingFactor =  10.0; debouncerTolerance =  .05;  }
  if (sensitivity == .5) {  outClockTempoRoundingFactor =  20.0; debouncerTolerance =  .025; }
  if (sensitivity == .25) { outClockTempoRoundingFactor = 100.0; debouncerTolerance =  .01;  }

  inClockDebouncerCycleTicks = sensitivity * PPQN;

  // cycle mod is 2/3 of a cycle or 24 ticks if distributing
  inClockDebouncerCycleMod = (inClockDebouncerCycleTicks ? inClockDebouncerCycleTicks / 3 * 2 - 1 : PPQN);
  inClockDebouncerAverageCountMax = (inClockDebouncerCycleTicks ? inClockDebouncerCycleTicks : PPQN);
}



#if !defined(USE_SOFTWARE_SERIAL_PIN_2_3) && !defined(USE_MIDI_LIBRARY)
// rename "Serial" to "MIDI" to be able to use different sketch configurations without renaming variables
HardwareSerial & MIDI = Serial;
#endif


void setup() {
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.begin(115200);
  Serial.println("starting serial with debug monitor");
  Serial.println("MIDI RX/TX pins are 2/3");
#endif

#ifndef USE_MIDI_LIBRARY
  MIDI.begin(31250);
#else
  MIDI.begin(MIDI_CHANNEL_OMNI); // Launch MIDI, by default listening to all channels
  MIDI.turnThruOff(); // we have to avoid clock ticks beeing sent thru
  MIDI.setHandleClock(handleMidiEventClock);
  MIDI.setHandleTick(handleMidiEventClock);
  MIDI.setHandleStop(handleMidiEventStop);
  MIDI.setHandleStart(handleMidiEventStart);
#endif

#ifdef USE_LCD1602
  lcd.begin(16, 2);
  lcd.print("hi");
#endif
  setSensitivity();
}

void loop() {
  currentMicros = micros();

#ifdef USE_MIDI_LIBRARY
  // midi library uses callbacks defined in setup()
  MIDI.read();
#else
  // manually read incoming bytes from serial
  if (MIDI.available()) {
    int incomingByte = MIDI.read();
    if (incomingByte == 0xF8) {
      handleMidiEventClock();
    }
    if (incomingByte == 0xFA) {
      handleMidiEventStart();
    }
    if (incomingByte == 0xFC) {
      // debug("incoming byte is stop");
      handleMidiEventStop();
    }
  }
#endif

  checkSendOutClockTick();
}

void resetJitterHelperVariables() {
  inClockTotalTickCount = 0;
  inClockLastStartMicros = 0;
  inClockLastTickMicros = 0;
    
  outClockTickIntervalMicros = 0;
  outClockTotalTickCount = 0;
  outClockLastTickMicros = 0;
  outClockTempoRoundingFactor = .0;
  
  tempoDeviationBpm = 0;

  inClockDebouncerAverageCount = 0;
  inClockDebouncerAverageIntervalMicros = 0;
  inClockDebouncerAverageCountMax = 0;
  inClockDebouncerCycleTicks = 0;
  inClockDebouncerCycleMod = 0;
  debouncerTolerance = .0;

  schedulerReferenceTickNum = 0;
  schedulerReferenceTickMicros = 0;
  schedulerReferenceTickInterval = 0;
  
  setSensitivity();
}

void handleMidiEventTick() {
  handleMidiEventClock(); 
}


/**
 * we got a clock tick from incoming clock
 */
void handleMidiEventClock() {
  uint16_t mod = inClockTotalTickCount % inClockDebouncerCycleTicks;
   
  // set anchor if tick 0
  if (inClockTotalTickCount == 0)
  {
     inClockLastStartMicros = currentMicros;
  }



  // update incoming average
  if (inClockTotalTickCount)
  {
     // total them up
     uint64_t sum = inClockDebouncerAverageCount * inClockDebouncerAverageIntervalMicros;
     
     // remove extra ticks+1 that we are about to add from sum
     // (also cater for distribution case)
     if (inClockDebouncerAverageCount >= inClockDebouncerAverageCountMax)
     {
        uint16_t remove = inClockDebouncerAverageCount - inClockDebouncerAverageCountMax + 1;
        inClockDebouncerAverageCount -= remove;
        sum -= (inClockDebouncerAverageIntervalMicros * remove);
     }
     
     // add the new interval
     uint64_t inClockLastInterval = currentMicros - inClockLastTickMicros;
     sum += inClockLastInterval;
     inClockDebouncerAverageCount++;
     
     // update the average
     inClockDebouncerAverageIntervalMicros = sum / inClockDebouncerAverageCount;
    
#ifdef USE_LCD1602
     if (inClockTotalTickCount % (inClockDebouncerCycleTicks*4) == 0) {
       lcd.clear();
       lcd.setCursor(0,0);
       lcd.print(PriUint64<DEC>(inClockTotalTickCount));
       lcd.setCursor(8,0);
       lcd.print(String(tickWidthToBpm(inClockDebouncerAverageIntervalMicros)));
       lcd.setCursor(0,1);
       lcd.print(PriUint64<DEC>(outClockTotalTickCount));
       lcd.setCursor(8,1);
       lcd.print(String(tickWidthToBpm(outClockTickIntervalMicros)));
     }
#endif
  }



  // if tick_count < cycle_ticks then pass these - first tracking cycle
  // or if we are distributing only
  if (inClockTotalTickCount < inClockDebouncerCycleTicks || inClockDebouncerCycleTicks == 0)
  {
     sendClockTick();
     
     if (inClockDebouncerCycleTicks == 0)
     {
        // only distributing, so need to manually update in bpm every 24 ticks
        if (inClockTotalTickCount && (inClockTotalTickCount % inClockDebouncerCycleMod) == 0)
        {
           // calculate deviation for distribution
           float inClockTempo = tickWidthToBpm(inClockDebouncerAverageIntervalMicros);
           float outClockTempo = (outClockTickIntervalMicros ? tickWidthToBpm(outClockTickIntervalMicros) : inClockTempo);
           
           // use out_tick_interval to retain our previous bpm
           outClockTickIntervalMicros = inClockDebouncerAverageIntervalMicros;
           
           // determine deviation in bpm
           tempoDeviationBpm = fabs(outClockTempo - inClockTempo);
        }
        
        //goto incoming_tick;
        
        inClockTotalTickCount++;
        inClockLastTickMicros = currentMicros;
        return;
     }
  }


  // schedule batch of our own cycle_ticks when tick_count % cycle_ticks == 2/3 of a cycle
  if (mod == inClockDebouncerCycleMod)
  {         
     // determine current in/out tempo
     float inClockTempo = tickWidthToBpm(inClockDebouncerAverageIntervalMicros);
     float outClockTempo = (outClockTickIntervalMicros ? tickWidthToBpm(outClockTickIntervalMicros) : inClockTempo);
     
     // determine deviation in bpm
     tempoDeviationBpm = fabs(outClockTempo - inClockTempo);

    /*
    debug("--------- new scheduling cycle begin ------------------");
    debug("in bpm " + String(inClockTempo));
    debug("out bpm " + String(outClockTempo));
    debug("tempoDeviationBpm " + String(tempoDeviationBpm));
    debug("--------- new scheduling cycle end ------------------");
    */
        
     // if deviation is above tolerance or no out_tick_interval yet, then tempo change
     if (tempoDeviationBpm > debouncerTolerance || !outClockTickIntervalMicros)
     {
        // round outClockTempo according to sensitivity
        if (outClockTempoRoundingFactor > 0.0)
           outClockTempo = roundf(inClockTempo * outClockTempoRoundingFactor) / outClockTempoRoundingFactor;
        
        /*
        debug("---------tempo change begin ------------------");
        debug("in bpm " + String(inClockTempo));
        debug("out bpm " + String(outClockTempo));
        debug("tempoDeviationBpm " + String(tempoDeviationBpm));
        debug("debouncerTolerance " + String(debouncerTolerance));
        debug("---------tempo change end ------------------");
        */
        outClockTickIntervalMicros = bpmToTickWidth(outClockTempo);
        // debug("outClockTickIntervalMicros", outClockTickIntervalMicros);
     }

     // schedule next batch of ticks
     // set last_out_tick to start of first window if not yet set
     if (!outClockLastTickMicros)
        outClockLastTickMicros = inClockLastStartMicros +
        (outClockTickIntervalMicros * (inClockDebouncerCycleTicks-1));
     
     // determine starting timestamp of tick batch
     schedulerReferenceTickNum = outClockTotalTickCount;
     schedulerReferenceTickMicros = outClockLastTickMicros;
     schedulerReferenceTickInterval = outClockTickIntervalMicros;

     // if our drift (received IN ticks vs. sent OUT ticks) is too large -> hard cut
     // TODO: consider to implement smooth corretion by modify schedulerReferenceTickInterval for the next batch
     if (outClockTotalTickCount > inClockTotalTickCount && outClockTotalTickCount - inClockTotalTickCount > 3) {
      schedulerReferenceTickNum = inClockTotalTickCount;
     }
     if (outClockTotalTickCount < inClockTotalTickCount && inClockTotalTickCount - outClockTotalTickCount > 3) {
      schedulerReferenceTickNum = inClockTotalTickCount;
     }

  }

  /*
  incoming_tick:
  {
  
  }
  */

  // update tick stats/counters
  inClockTotalTickCount++;
  inClockLastTickMicros = currentMicros;
}

/**
 * check if there is need to send out a single clock tick
 */
void checkSendOutClockTick() {

  if (inClockTotalTickCount < inClockDebouncerCycleTicks || inClockDebouncerCycleTicks == 0)
  {
    return;
  }
  // apply clock delay
  // positive = send later than recieve
  // negative = send before recieve
  if (clockDelayMillis < 0 && currentMicros < abs(clockDelayMillis*1000)) {
    return;
  }


  // wait at least 5 millisecond between ticks (gets applied when we have a negative value of clockDelayMicros)
  if (currentMicros - outClockLastTickMicros < 5000) {
    return;
  }

  // check if already sent out tick num < needed ticknum
  if (outClockTotalTickCount < (currentMicros - (clockDelayMillis * 1000) - schedulerReferenceTickMicros) / schedulerReferenceTickInterval + schedulerReferenceTickNum) {
    sendClockTick();
  }
}

/**
 * really send the tick and calculate the time when the next tick has to be sent
 */
void sendClockTick() {
  outClockLastTickMicros = currentMicros;
  outClockTotalTickCount++;
  
#ifdef USE_MIDI_LIBRARY
  MIDI.sendClock();
#else USE_MIDI_LIBRARY
  MIDI.write(0xF8);
#endif
}


void handleMidiEventStart() {
  resetJitterHelperVariables();
#ifdef USE_MIDI_LIBRARY
  MIDI.sendStart();
#else
  MIDI.write(0xFA);
#endif


#ifdef USE_LCD1602
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("start");
#endif
}

void handleMidiEventStop() {
#ifdef USE_MIDI_LIBRARY
  MIDI.sendStop();
#else
  MIDI.write(0xFC);
#endif
  resetJitterHelperVariables();

#ifdef USE_LCD1602
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("stop");
#endif
}

float tickWidthToBpm(uint64_t tickWidth)
{
  // calculate tempo [BPM] based on tick width [microseconds]
  if (tickWidth < 1) {
    return 0;
  }
  return 60 / (tickWidth * 0.000001 * PPQN);
}

uint64_t bpmToTickWidth(float bpm)
{
  // calculate interval of clock tick[microseconds] (24 inClockDebouncerCycleTicks)
  if (bpm < 1) {
    return 0;
  }
  return 60 / (bpm * 0.000001 * PPQN);
}

void debug(String Msg)
{
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.println(Msg);
#endif
}

void debug(String Msg, uint64_t value)
{
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.print(Msg + ": "); Serial.println(PriUint64<DEC>(value));
#endif
}
