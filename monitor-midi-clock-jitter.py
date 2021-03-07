#!/bin/env python3
import os
import subprocess
from datetime import datetime

previousMs=0

maxTickWidth=0
minTickWidth=1000000

def main():
  print ('hello')
  process = subprocess.Popen(['aseqdump', '-p', '24:0'], stdout=subprocess.PIPE, cwd=None)
  lastSecond = 0
  counter = 0
  minTickWidth = 1000000
  maxTickWidth = 0
  minBpm = 500
  maxBpm = 0

  while True:
    line = process.stdout.readline()
    if not line:
      break
    counter += 1
    if counter < 3:
      continue
    #print(line)
    now = datetime.now()
    currentSecond=now.timestamp()
    
    if counter < 4:
      lastSecond = currentSecond
      continue
  
    if str(line).find("Clock") == -1:
      print(line)
      continue
    
    currentTickWidth = currentSecond - lastSecond
    currentBpm = tickWidth2Bpm(currentTickWidth)

    if minBpm > currentBpm:
      minBpm = currentBpm
    if maxBpm < currentBpm:
      maxBpm = currentBpm
    #the real code does filtering here
    print (f'aa {"{:10.2f}".format(currentBpm)} {"{:10.2f}".format(minBpm)} {"{:10.2f}".format(maxBpm)}')
    lastSecond = currentSecond

    if counter > 200:
      minBpm = 500
      maxBpm = 0
      counter = 4



def tickWidth2Bpm(tickWidth):
  return 60 / (tickWidth * 24)


if __name__ == "__main__":
    main()
