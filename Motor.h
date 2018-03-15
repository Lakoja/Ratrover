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

#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <math.h>
#include "Task.h"

class MotorWatcher
{
public:
  static volatile uint32_t counterR;
  static volatile uint32_t counterL;

private:
  uint32_t lastCheckTime = 0;
  uint32_t lastCounterRight = 0;
  uint32_t lastCounterLeft = 0;
  uint32_t lastCounterOutTime = 0;
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
      float currentTurnsRight = ((ri - lastCounterRight) / (6.0f * motorReduction)) / partOfSecond;
      float currentTurnsLeft = ((le - lastCounterLeft) / (6.0f * motorReduction)) / partOfSecond;

      lastTurnsRight = (currentTurnsRight + lastTurnsRight) / 2.0f;
      lastTurnsLeft = (currentTurnsLeft + lastTurnsLeft) / 2.0f;

      if (now - lastCounterOutTime > 2000) {
        Serial.println("Turns R "+String(currentTurnsRight) + ">" +String(lastTurnsRight) + " L "+String(currentTurnsLeft)+ ">" +String(lastTurnsLeft));
        
        lastCounterOutTime = now;
      }

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

class Motor: public Task
{
private:
  const float DEAD_ZONE_SPEED_HIGH = 0.90; // do not go above this normally: leave room for a slightly slower motor of a pair to keep up
  const float DEAD_ZONE_SPEED_LOW = 0.10; // motor doesn't move (too weak) below this

  uint8_t MOTOR_R1;
  uint8_t MOTOR_R2;
  uint8_t MOTOR_L1;
  uint8_t MOTOR_L2;

  MotorWatcher watcher;
  
  uint32_t systemStart;
  // the externally requested value
  float motorRSpeedDesired = 0;
  float motorLSpeedDesired = 0;
  // the currently internally requested value (without PID correction)
  float motorRSpeedRequested = 0;
  float motorLSpeedRequested = 0;
  // the currently internally PID corrected value
  float motorRSpeedCorrected = 0;
  float motorLSpeedCorrected = 0;
  uint16_t maxSpeedInt;
  uint32_t motorREndTime;
  uint32_t motorLEndTime;
  uint32_t lastDriveLoopTime = 0;
  uint16_t motorMaxTurns = 0;

public:
  void setup(
    uint8_t forePinRight,
    uint8_t backPinRight,
    uint8_t interruptRight,
    uint8_t forePinLeft,
    uint8_t backPinLeft,
    uint8_t interruptLeft,
    uint16_t umin,
    uint16_t reduction)
  {
    MOTOR_R1 = forePinRight;
    MOTOR_R2 = backPinRight;
    MOTOR_L1 = forePinLeft;
    MOTOR_L2 = backPinLeft;
    
    uint8_t precision = 10;
    maxSpeedInt = pow(2, precision) - 1;
    
    outputPin(MOTOR_R1);
    outputPin(MOTOR_R2);
    outputPin(MOTOR_L1);
    outputPin(MOTOR_L2);

    watcher.setup(interruptRight, interruptLeft, reduction);

    // a sensible frequency for my motors
    uint16_t maxRpm = umin * 2;
    motorMaxTurns = umin;

    // NOTE there are groups at work here: The setting for channel 1 also resets the one for 0.
    // Some documentation would have been fine here generally...

    ledcSetup(0, maxRpm, precision); // precision bits 8: means we have 0..255 as steps, 4: 0..15
    ledcSetup(1, maxRpm, precision);
    ledcSetup(2, maxRpm, precision);
    ledcSetup(3, maxRpm, precision);
    ledcAttachPin(MOTOR_R1, 0);
    ledcAttachPin(MOTOR_R2, 1);
    ledcAttachPin(MOTOR_L1, 2);
    ledcAttachPin(MOTOR_L2, 3);

    ledcWrite(0, 0);
    ledcWrite(1, 0);
    ledcWrite(2, 0);
    ledcWrite(3, 0);

    systemStart = millis();
  }

  void run()
  {
    while (true) {
      uint32_t now = millis();

      watcher.drive();
  
      if (lastDriveLoopTime > 0) {
        uint16_t passed = now - lastDriveLoopTime;
        float maxSpeedChange = passed / 250.0f; // slow down/speed up at most from 0 to 1 in that timespace (denominator; ie 500ms)
  
        if (now >= motorREndTime) {
          motorRSpeedDesired = 0;
        }
  
        if (now >= motorLEndTime) {
          motorLSpeedDesired = 0;
        }
  
        // adapt the speed slowly (full range in one second)
        
        if (motorRSpeedDesired != motorRSpeedRequested) {
          float distance = motorRSpeedDesired - motorRSpeedRequested;
          float sign = distance >= 0 ? +1 : -1;
          float diff = sign * _min(abs(distance), maxSpeedChange);
          switchMotorR(motorRSpeedRequested + diff);
        }
  
        if (motorLSpeedDesired != motorLSpeedRequested) {
          float distance = motorLSpeedDesired - motorLSpeedRequested;
          float sign = distance >= 0 ? +1 : -1;
          float diff = sign * _min(abs(distance), maxSpeedChange);
          switchMotorL(motorLSpeedRequested + diff);
        }
      }
  
      lastDriveLoopTime = now;

      sleepAfterLoop(4, now);
    }
  }

  void requestMovement(float forward, float right, uint16_t durationMillis = 1000) {
    // This is the only one with value range -1 .. 1

    float rightSpeed = forward;
    float leftSpeed = forward;
    rightSpeed -= right / 2;
    leftSpeed += right / 2;
    
    requestRight(rightSpeed, durationMillis);
    requestLeft(leftSpeed, durationMillis);
  }

  void requestRight(float value, uint16_t durationMillis = 1000)
  {
    // TODO check for value range?
    
    uint32_t now = millis();
    motorRSpeedDesired = value;
    motorREndTime = now + durationMillis;
  }
  
  void requestLeft(float value, uint16_t durationMillis = 1000)
  {
    uint32_t now = millis();
    motorLSpeedDesired = value;
    motorLEndTime = now + durationMillis;
  }  
  
  void requestForward(float value, uint16_t durationMillis = 1000)
  {
    uint32_t desiredEndTime = millis() + durationMillis;
    motorRSpeedDesired = value;
    motorREndTime = desiredEndTime;
    motorLSpeedDesired = value;
    motorLEndTime = desiredEndTime;
  }
  
  void requestReverse(float value, uint16_t durationMillis = 1000)
  {
    uint32_t desiredEndTime = millis() + durationMillis;
    motorRSpeedDesired = -value;
    motorREndTime = desiredEndTime;
    motorLSpeedDesired = -value;
    motorLEndTime = desiredEndTime;
  }

private:
  void outputPin(int num)
  {
    digitalWrite(num, LOW);
    pinMode(num, OUTPUT);
  }

  void switchMotorR(float speed)
  {
    float correctedSpeed = speed;
    /*
    if (correctedSpeed < DEAD_ZONE_SPEED_LOW) {
      correctedSpeed = 0;
    } else if (correctedSpeed > DEAD_ZONE_SPEED_HIGH) {
      correctedSpeed = DEAD_ZONE_SPEED_HIGH;
    }*/
    if (correctedSpeed != motorRSpeedCorrected) {
      switchMotor(correctedSpeed, 0, 1);
      motorRSpeedCorrected = correctedSpeed;
    }

    motorRSpeedRequested = speed;
  }

  void switchMotorL(float speed)
  {
    float correctedSpeed = speed;
    /*
    if (correctedSpeed < DEAD_ZONE_SPEED_LOW) {
      correctedSpeed = 0;
    } else if (correctedSpeed > DEAD_ZONE_SPEED_HIGH) {
      correctedSpeed = DEAD_ZONE_SPEED_HIGH;
    }*/
    if (correctedSpeed != motorLSpeedCorrected) {
      switchMotor(correctedSpeed, 2, 3);
      motorLSpeedCorrected = correctedSpeed;
    }

    motorLSpeedRequested = speed;
  }

  uint16_t outCounter = 0;
  bool showDebug = true;
  
  bool switchMotor(float speed, uint8_t channelForward, uint8_t channelReverse)
  {
    speed = getNonDeadSpeed(speed);

    if (showDebug) {
      Serial.print("M");
      Serial.print(speed, 2);
      Serial.print(", ");
    }

    uint16_t speedInt = round(abs(speed) * maxSpeedInt);
    uint16_t chan1Speed = _min(maxSpeedInt, speed >= 0 ? speedInt : 0);
    uint16_t chan2Speed = _min(maxSpeedInt, speed < 0 ? speedInt : 0);

    if (showDebug) {
      Serial.print(chan1Speed);
      Serial.print(",");
      Serial.print(chan2Speed);
      Serial.print(" ");
  
      if (++outCounter % 20 == 0)
        Serial.println();
    }

    ledcWrite(channelForward, chan1Speed);
    ledcWrite(channelReverse, chan2Speed);
  }

  float getNonDeadSpeed(float speed)
  {
    float nonDeadSpeed = 0;
    if (speed != 0) {
      float sign = speed < 0 ? -1 : +1;
      nonDeadSpeed = sign * DEAD_ZONE_SPEED_LOW + (1 - DEAD_ZONE_SPEED_LOW) * speed;
    }

    return nonDeadSpeed;
  }

  float getCurrentlyDesiredTurns(float desiredSpeed)
  {
    return desiredSpeed * motorMaxTurns;
  }
};

#endif
