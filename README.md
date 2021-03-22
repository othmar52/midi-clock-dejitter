# midi-clock-dejitter
**Tryout** for an Arduino based midi clock stabilizer  
It seems to work pretty well on an Arduino UNO with MIDI in+out (DIN-MIDI or USB-MIDI)

## recieving MIDI clock via bluetooth is not that stable

In my test setup i send the clock via [CME WIDI Master](https://www.cme-pro.com/widi-master/) to my [Waldorf Blofeld](https://waldorfmusic.com/en/blofeld-overview) which results in crackling sounds.  
After doing some research you have 2 choices to solve this problem:  

- Change the Blofeld settings to use internal clock (hold shift + global and use the top left encoder to scroll to the clock settings). But effects, LFO's and arpeggios will not be in sync to the incoming clock anymore.
- Make sure the incoming clock is stable without jitter which seems to be impossible when you have a bluetooth connection within your incoming MIDI chain.

## Alternatives

The iOS+macOS app [MidiPace by Audeonic](https://audeonic.com/midipace/) seems to be a rock solid alternative
