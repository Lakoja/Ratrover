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

// first pin must be the one for "forward"
const int MOTOR_R1 = 32;
const int MOTOR_R2 = 33;
const int MOTOR_L1 = 26;
const int MOTOR_L2 = 25;

class Motor
{
private:
  uint32_t systemStart;
  float motorRSpeed = 0;
  float motorLSpeed = 0;
  float deadZoneSpeed = 0.12; // motor doesn't move (too weak) below this
  uint16_t maxSpeedInt;
  uint32_t motorREndTime;
  uint32_t motorLEndTime;
  float motorRDesireSpeed = 0;
  float motorLDesireSpeed = 0;
  uint32_t lastDriveLoopTime = 0;

public:
  void setup()
  {
    uint8_t precision = 10;
    maxSpeedInt = pow(2, precision) - 1;
    
    outputPin(MOTOR_R1);
    outputPin(MOTOR_R2);
    outputPin(MOTOR_L1);
    outputPin(MOTOR_L2);

    // NOTE there are groups at work here: The setting for channel 1 also resets the one for 0.
    // Some documentation would have been fine here generally...

    // 168: sensible frequency for my motors (=rpm)
    ledcSetup(0, 168, precision); // precision bits 8: means we have 0..255 as steps, 4: 0..15
    ledcSetup(1, 168, precision);
    ledcSetup(2, 168, precision);
    ledcSetup(3, 168, precision);
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

  void drive()
  {
    uint32_t now = millis();

    if (lastDriveLoopTime > 0) {
      uint16_t passed = now - lastDriveLoopTime;
      float fromASecond = passed / 1000.0f;

      if (now >= motorREndTime) {
        motorRDesireSpeed = 0;
      }

      if (now >= motorLEndTime) {
        motorLDesireSpeed = 0;
      }

      // adapt the speed slowly (full range in one second)
      
      if (motorRDesireSpeed != motorRSpeed) {
        float sign = motorRDesireSpeed - motorRSpeed >= 0 ? +1 : -1;
        float diff = sign * _min(abs(motorRDesireSpeed - motorRSpeed), fromASecond);
        switchMotorR(motorRSpeed + diff);
      }

      if (motorLDesireSpeed != motorLSpeed) {
        float sign = motorLDesireSpeed - motorLSpeed >= 0 ? +1 : -1;
        float diff = sign * _min(abs(motorLDesireSpeed - motorLSpeed), fromASecond);
        switchMotorL(motorLSpeed + diff);
      }
    }

    lastDriveLoopTime = now;
  }

  void requestRightBurst(uint16_t durationMillis = 1000)
  {
    motorRDesireSpeed = 0.8f;
    motorREndTime = millis() + durationMillis;
  }

  void requestLeftBurst(uint16_t durationMillis = 1000)
  {
    motorLDesireSpeed = 0.8f;
    motorLEndTime = millis() + durationMillis;
  }

private:
  void outputPin(int num)
  {
    digitalWrite(num, LOW);
    pinMode(num, OUTPUT);
  }

  void switchMotorR(float speed)
  {
    if (speed != motorRSpeed) {
      switchMotor(speed, 0, 1);
      motorRSpeed = speed;
    }
  }

  void switchMotorL(float speed)
  {
    if (speed != motorLSpeed) {
      switchMotor(speed, 2, 3);
      motorLSpeed = speed;
    }
  }

  uint16_t outCounter = 0;
  bool showDebug = false;
  
  bool switchMotor(float speed, uint8_t channelForward, uint8_t channelReverse)
  {
    speed = getNonDeadSpeed(speed);

    if (showDebug) {
      Serial.print("M");
      Serial.print(speed, 2);
      Serial.print(",");
    }

    uint16_t speedInt = round(abs(speed) * maxSpeedInt);
    uint16_t chan1Speed = speed >= 0 ? speedInt : 0;
    uint16_t chan2Speed = speed < 0 ? speedInt : 0;

    if (showDebug) {
      //Serial.println("Make your speed "+String(chan1Speed)+" "+String(chan2Speed));
      Serial.print(chan1Speed);
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
      nonDeadSpeed = sign * deadZoneSpeed + (1 - deadZoneSpeed) * speed;
    }

    return nonDeadSpeed;
  }
};

#endif
