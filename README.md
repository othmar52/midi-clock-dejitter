# midi-clock-dejitter
**Tryout** for an Arduino based midi clock stabilizer  
**Unfortunately this does not work as good as expected :/**  
Around 20% of the time the outgoing clock is jittering as well. In case you know the reason feel free to create an issue and tell me the reason.

## recieving MIDI clock via bluetooth is not that stable

In my test setup i send the clock via [CME WIDI Master](https://www.cme-pro.com/widi-master/) to my [Waldorf Blofeld](https://waldorfmusic.com/en/blofeld-overview) which results in crackling sounds.  
After doing some research you have 2 choices to solve this problem:  

- Change the Blofeld settings to use internal clock (hold shift + global and use the top left encoder to scroll to the clock settings). But effects, LFO's and arpeggios will not be in sync to the incoming clock anymore.
- Make sure the incoming clock is stable without jitter which seems to be impossible when you have a bluetooth connection within your incoming MIDI chain.

## Alternatives

The iOS+macOS app [MidiPace by Audeonic](https://audeonic.com/midipace/) seems to be a rock solid alternative