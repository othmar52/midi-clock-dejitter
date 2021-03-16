#!/bin/env python3

# sudo pip install pygame

import os
import subprocess
from datetime import datetime
import pygame



previousMs=0

ppqn = 24

metronome = True
samplePathBeat = 'tick1.wav'
samplePathBar = 'tick2.wav'

def playSound(samplePath):
  if metronome != True:
    return
  pygame.mixer.music.load(samplePath)
  pygame.mixer.music.play()


def main():
  print ('hello')
  process = subprocess.Popen(['aseqdump', '-p', '24:0'], stdout=subprocess.PIPE, cwd=None)
  lastSecond = None
  counter = 0
  tickCounter = 0;
  counterBars = 1;
  counterBeats = 0;
  minBpm = None
  maxBpm = None
  
  pygame.mixer.init()

  while True:
    line = process.stdout.readline()
    if not line:
      break
    counter += 1
  
    if str(line).find('Stop') > 0:
      tickCounter = 0
      counterBeats = 0
      counterBars = 1
      print(line)
      print ('001.1.01')
      continue
  
    if str(line).find('Start') > 0:
      playSound(samplePathBar)
      tickCounter = 0
      counterBeats = 0
      counterBars = 1
      print(line)
      print ('001.1.01')
      continue
  
  
    if str(line).find('Active Sensing') > 0:
      continue

    if str(line).find('Clock') == -1:
      print(line)
      continue


    tickCounter += 1
    now = datetime.now()
    currentSecond = now.timestamp()

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
      

    if tickCounter % ppqn == 0:
      samplePath = samplePathBeat
      counterBeats += 1
      if tickCounter % (ppqn*4) == 0:
        samplePath = samplePathBar
        counterBars += 1
      playSound(samplePath)

    print (f'{counterBars:03}.{counterBeats % 4 + 1}.{(tickCounter % ppqn + 1):02} {"{:10.2f}".format(currentBpm)} {"{:10.2f}".format(minBpm)} {"{:10.2f}".format(maxBpm)}')
    lastSecond = currentSecond

    if counter > 200:
      minBpm = None
      maxBpm = None
      counter = 4


def tickWidth2Bpm(tickWidth):
  return 60 / (tickWidth * ppqn)


if __name__ == '__main__':
  main()
