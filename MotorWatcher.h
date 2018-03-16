/*
 * Copyright (C) 2018 Lakoja on github.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MOTOR_WATCHER_H__
#define __MOTOR_WATCHER_H__

class MotorWatcher
{
public:
  static volatile uint32_t counterR;
  static volatile uint32_t counterL;

private:
  const uint8_t ENCODER_TICKS = 7; // per revolution; TODO this value is determined by observation (it doesn't corrspond to spec "12")
  
  uint32_t lastCheckTime = 0;
  uint32_t lastCounterRight = 0;
  uint32_t lastCounterLeft = 0;
  uint16_t motorReduction = 1;

  // contains summed media data of old values
  float lastTurnsRight = 0; 
  float lastTurnsLeft = 0;

public:
  void setup(uint8_t interruptRight, uint8_t interruptLeft, uint16_t reduction)
  {
    motorReduction = reduction;
    
    pinMode(interruptRight, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(interruptRight), countR, FALLING);
    pinMode(interruptLeft, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(interruptLeft), countL, FALLING);
  }

  void drive()
  {
    uint32_t now = millis();
    if (now - lastCheckTime >= 10) {
      uint32_t ri = counterR;
      uint32_t le = counterL;

      float partOfSecond = (now - lastCheckTime) / 1000.0f;
      float currentTurnsRight = 60 * (((ri - lastCounterRight) / (float)(ENCODER_TICKS * motorReduction)) / partOfSecond);
      float currentTurnsLeft = 60 * (((le - lastCounterLeft) / (float)(ENCODER_TICKS * motorReduction)) / partOfSecond);

      lastTurnsRight = (currentTurnsRight + lastTurnsRight) / 2.0f;
      lastTurnsLeft = (currentTurnsLeft + lastTurnsLeft) / 2.0f;

      lastCounterRight = ri;
      lastCounterLeft = le;
      
      lastCheckTime = now;
    }
  }

  float currentTurnsRight()
  {
    return lastTurnsRight;
  }

  uint16_t currentTurnsLeft()
  {
    return lastTurnsLeft;
  }

private:
  static void countR()
  {
    counterR++;
  }

  static void countL()
  {
    counterL++;
  }
};

#endif
