 

// thanks to https://github.com/RobTillaart/RunningMedian
// this is very comfortable for debouncing the incoming tempo
#include "RunningMedian.h"

// instead of using RX, TX pins of hardware serial use different pins
// so we are able to use the debug monitor
//#define USE_SOFTWARE_SERIAL_PIN_2_3

// thanks to https://github.com/arduino-libraries/LiquidCrystal
// in this sketch a LCD1602 (16 chars x 2 lines) is used
#define USE_LCD

// thanks to https://github.com/FortySevenEffects/arduino_midi_library
// but as long as we recieve or send only three different bytes (start, stop, tick) there is no need to use such an overhead
//#define USE_MIDI_LIBRARY

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
#ifdef USE_MIDI_LIBRARY
MIDI_CREATE_INSTANCE(SoftwareSerial, softSerial, MIDI);
#endif
#else
#ifdef USE_MIDI_LIBRARY
MIDI_CREATE_DEFAULT_INSTANCE();
#endif
#endif

uint32_t currentMicros = 0;
const int8_t ppqn = 24;
bool weHaveADebouncedTempo = false;

// incoming (jittered) clock
uint8_t inClockQuarterBarTickCounter = 0;  // [tickNum] loops from 0 to 24 (ppqn)
uint32_t inClockLastQuarterBarStart = 0;   // [microseconds]
uint32_t inClockTickCounter = 0;           // [tickNum] incremental counter since last clock start
uint8_t inClockFullBarTickCounter = 0;     // loops from 0 to 96 (4*ppqn)

// outgoing (dejittered) clock
uint8_t outClockFullBarTickCounter = 0;    // [tickNum] loops from 0 to 96 (4*ppqn)
uint32_t outClockLastFullBarStart = 0;     // [microseconds]
uint32_t outClockTickCounter = 0;          // [tickNum] incremental counter since last clock start
uint32_t outClockLastSentTick = 0;         // [microseconds]

uint32_t scheduledNextTickMicroSecond = 0; // [microseconds]

const uint8_t tickDriftTreshold = 2;       // [tickNum]
int32_t inOutTickDrift = 0;                // [tickNum]
int32_t driftingTickWidth = 0;             // [microseconds]

float currentTempo = .0;                  // [BPM]

// incoming serial data
int incomingByte = 0;


const uint16_t minBpmTickWidth = 65000; // [microseconds] =~ 38 BPM
const uint16_t maxBpmTickWidth = 8000;  // [microseconds] =~ 312 BPM


// helper variables for debouncing incoming tempo
int32_t debouncedTickWidth = 0;
RunningMedian recentQuarterBarDurations = RunningMedian(10);
RunningMedian recentDebouncedTickWidths = RunningMedian(10);
RunningMedian recentDebouncedBpm = RunningMedian(10);


// positive = send later than recieve
// negative = send before recieve
const int16_t clockDelayMilliseconds = 0; // [milliseconds]  // 2051 =~ 1 bar @ 117 BPM

int32_t forcedTickDeltaOfClockDelay = 0;

#if !defined(USE_SOFTWARE_SERIAL_PIN_2_3) && !defined(USE_MIDI_LIBRARY)
// rename "Serial" to "MIDI" to be able to use different sketch configurations without renaming variables
HardwareSerial & MIDI = Serial;
#endif


void setup() {
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.begin(115200);
  Serial.println("starting serial with debug monitor MIDI RX/TX pins are 2/3");
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

#ifdef USE_LCD
  lcd.begin(16, 2);
  lcd.print("hi");
#endif
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
      handleMidiEventStop();
    }
  }
#endif

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
  recentDebouncedBpm.clear();

  debouncedTickWidth = 0;

  currentTempo = .0;
  forcedTickDeltaOfClockDelay = 0;
}

void handleMidiEventTick() {
  handleMidiEventClock();
}


/**
 * we got a clock tick from incoming clock
 */
void handleMidiEventClock() {
  inClockTickCounter+=1;
  inClockQuarterBarTickCounter+=1;
  inClockFullBarTickCounter+=1;
  if (inClockFullBarTickCounter == ppqn * 4) {
    inClockFullBarTickCounter = 0;
  }
  if (inClockQuarterBarTickCounter == ppqn) {
    //debug("----------------- quarter note clock IN ----------------");
    recentQuarterBarDurations.add(currentMicros - inClockLastQuarterBarStart);
    if(recentDebouncedBpm.getHighest() - recentDebouncedBpm.getLowest() > 5) {
      // obviously there had been a huge tempo change
      recentDebouncedTickWidths.clear();
    }
    recentDebouncedTickWidths.add(recentQuarterBarDurations.getAverage()/ppqn);
    debouncedTickWidth = recentDebouncedTickWidths.getAverage();
    weHaveADebouncedTempo = true;
    currentTempo = tickWidthToBpm(debouncedTickWidth);
    recentDebouncedBpm.add(currentTempo);
    if (clockDelayMilliseconds != 0) {
      // difference between incoming and outgoing ticks as is maybe on purpose caused by configured clockDelayMilliseconds
      forcedTickDeltaOfClockDelay = (int32_t)(((float)clockDelayMilliseconds*1000)/(float)debouncedTickWidth);
    }
    inClockQuarterBarTickCounter = 0;
    inClockLastQuarterBarStart = currentMicros;

#ifdef USE_LCD
    if (inClockFullBarTickCounter % (2*ppqn) == 0) {
      // send some debug informations to attached LCD with a refresh rate of 0.5 bars (48 ticks)
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(String(inClockTickCounter) + " i " + String(currentTempo) + "bpm");
      lcd.setCursor(0,1);
      int32_t diff = outClockTickCounter - inClockTickCounter;
      lcd.print(String(outClockTickCounter) + " o "+ String(diff)  + " " + String(forcedTickDeltaOfClockDelay));
    }
#endif

  }

  if (weHaveADebouncedTempo == false) {
    // pass thru the jittering ticks until we know the tempo
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
  
  if (currentMicros - outClockLastSentTick < maxBpmTickWidth) {
    // never send a tempo faster than 312 BPM
    return;
  }

  sendClockTick();
}

/**
 * really send the tick and calculate the time when the next tick has to be sent
 */
void sendClockTick() {

  if (forcedTickDeltaOfClockDelay < 0 && inClockTickCounter < abs(forcedTickDeltaOfClockDelay)) {
    // do not send out ticks if we have a configured negative clock offset (send later than recieve)
    return;
  }

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
#ifdef USE_MIDI_LIBRARY
  MIDI.sendClock();
#else USE_MIDI_LIBRARY
  MIDI.write(0xF8);
#endif

  // without any drift next tick has to be sent in debouncedTickWidth microseconds
  scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*debouncedTickWidth);
  applyClockDelay();

  if (inClockTickCounter > (outClockTickCounter + tickDriftTreshold + forcedTickDeltaOfClockDelay)) {
    // we are behind. lets remove a little time of scheduled next tick by increasing the tempo by <driftAmount> BPM
    inOutTickDrift = inClockTickCounter - outClockTickCounter + forcedTickDeltaOfClockDelay;

    driftingTickWidth = bpmToTickWidth((float)(currentTempo + inOutTickDrift));
    debug("driftingTickWidth [behind] " + String(driftingTickWidth) + " " + String(debouncedTickWidth));

    scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*driftingTickWidth);
    applyClockDelay();

    // pseudo debugging to see the drift in the output of aseqdump (./monitor-midi-clock-jitter.py)
    /*
    MIDI.write(0x90); // MIDI Note-on; channel 1
    MIDI.write(60); // MIDI note pitch 60
    MIDI.write(inOutTickDrift); // MIDI note velocity inOutTickDrift
    */
    
    return;
  }

  if (outClockTickCounter > (inClockTickCounter + tickDriftTreshold + forcedTickDeltaOfClockDelay)) {
    // our in out tick drift is tooo large (we are ahead)
    // add a little time to the schedule by decreasing the tempo by <driftAmount> BPM
    
    inOutTickDrift = outClockTickCounter - inClockTickCounter + forcedTickDeltaOfClockDelay;

    driftingTickWidth = (inOutTickDrift > currentTempo)
      ? minBpmTickWidth
      : bpmToTickWidth((float)(currentTempo - inOutTickDrift));

    debug("driftingTickWidth [ahead] " + String(driftingTickWidth) + " " + String(debouncedTickWidth));

    scheduledNextTickMicroSecond = outClockLastFullBarStart + ((outClockFullBarTickCounter+1)*driftingTickWidth);
    applyClockDelay();
    
    //MIDI.write(0xFE);  // active sensing  (just for debuggung over serial visible in aseqdump)

    // pseudo debugging to see the drift in the output of aseqdump (./monitor-midi-clock-jitter.py)
    /*
    MIDI.write(0x90); // MIDI Note-on; channel 1
    MIDI.write(20);   // MIDI note pitch 20
    MIDI.write(inOutTickDrift); // MIDI note velocity inOutTickDrift
    */
  }
}

void applyClockDelay() {
  if (clockDelayMilliseconds == 0) {
    // no need to apply any offset for the next tick
    return;
  }
  if (clockDelayMilliseconds < 0) {
    if(currentMicros < clockDelayMilliseconds * -1000) {
      // we cant apply a negative offset until we have reached this time
      return;
    }
  }
  scheduledNextTickMicroSecond += (clockDelayMilliseconds*1000);
}

void handleMidiEventStart() {
  resetJitterHelperVariables();
#ifdef USE_MIDI_LIBRARY
  MIDI.sendStart();
#else
  MIDI.write(0xFA);
#endif
  inClockLastQuarterBarStart = currentMicros;
#ifdef USE_LCD
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
  // calculate tempo [BPM] based on tick width [microseconds]
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

void debug(String Msg)
{
#ifdef USE_SOFTWARE_SERIAL_PIN_2_3
  Serial.println(Msg);
#endif
}
