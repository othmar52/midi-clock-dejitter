#!/bin/env python3

# sudo pip install pygame

import os
import subprocess
from datetime import datetime
import pygame



previousMs=0

ppqn = 24

metronome = False

def main():
  print ('hello')
  process = subprocess.Popen(['aseqdump', '-p', '24:0'], stdout=subprocess.PIPE, cwd=None)
  lastSecond = None
  counter = 0
  tickCounter = 0;
  minBpm = None
  maxBpm = None
  
  pygame.mixer.init()
  pygame.mixer.music.load("tick1.wav")

  while True:
    line = process.stdout.readline()
    if not line:
      break
    counter += 1

  
    if str(line).find("Stop") > 0:
      tickCounter = 0
  
    if str(line).find("Start") > 0:
      pygame.mixer.music.load("tick2.wav")
      pygame.mixer.music.play()
      tickCounter = 0
      continue
  
    if str(line).find("Clock") == -1:
      print(line)
      continue


    tickCounter += 1
    now = datetime.now()
    currentSecond=now.timestamp()

    if lastSecond == None:
      lastSecond = currentSecond
      continue
  
    currentTickWidth = currentSecond - lastSecond
    currentBpm = tickWidth2Bpm(currentTickWidth)

    if minBpm == None:
      minBpm = currentBpm
    if maxBpm == None:
      maxBpm = currentBpm

    if minBpm > currentBpm:
      minBpm = currentBpm
    if maxBpm < currentBpm:
      maxBpm = currentBpm
    #the real code does filtering here
    print (f'aa {"{:10.2f}".format(currentBpm)} {"{:10.2f}".format(minBpm)} {"{:10.2f}".format(maxBpm)}')
    lastSecond = currentSecond

    if counter > 200:
      minBpm = None
      maxBpm = None
      counter = 4
    
    if metronome != True:
        continue

    if tickCounter % ppqn == 0:
        samplePath = "tick1.wav"
        if tickCounter % (ppqn*4) == 0:
            samplePath = "tick2.wav"

        pygame.mixer.music.load(samplePath)
        pygame.mixer.music.play()




def tickWidth2Bpm(tickWidth):
  return 60 / (tickWidth * ppqn)


if __name__ == "__main__":
    main()
