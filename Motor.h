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
#include <PID_v1.h>
#include "Task.h"
#include "MotorWatcher.h"

class Motor: public Task
{
private:
  const float DEAD_ZONE_SPEED_HIGH = 0.92; // do not go above this normally: leave room for a slightly slower motor of a pair to keep up
  const float DEAD_ZONE_SPEED_LOW = 0.20; // motor doesn't move properly (too weak) below this
  const float LIVE_ZONE_RANGE = DEAD_ZONE_SPEED_HIGH - DEAD_ZONE_SPEED_LOW;

  uint8_t MOTOR_R1;
  uint8_t MOTOR_R2;
  uint8_t MOTOR_L1;
  uint8_t MOTOR_L2;

  MotorWatcher watcher;

  // set points are rpm
  double pidInputRight = 0, pidOutputRight = 0, pidSetpointRight = 0, pidInputLeft = 0, pidOutputLeft = 0, pidSetpointLeft = 0;

  double p = 18, i = 70, d = 0;
  PID *pidRight = NULL;
  PID *pidLeft = NULL;
  
  uint32_t systemStart;
  // the externally (or automatically) requested values
  float motorRSpeedDesired = 0;
  float motorLSpeedDesired = 0;
  double currentPwmRight = 0;
  double currentPwmLeft = 0;
  uint32_t motorREndTime;
  uint32_t motorLEndTime;
  uint16_t maxSpeedInt;
  uint32_t lastDriveLoopTime = 0;
  uint16_t motorMaxTurns = 0;
  uint32_t lastCounterOutTime = 0;

public:
  Motor()
  {
    pidRight = new PID(&pidInputRight, &pidOutputRight, &pidSetpointRight, p, i, d, DIRECT);
    pidLeft = new PID(&pidInputLeft, &pidOutputLeft, &pidSetpointLeft, p, i, d, DIRECT);
  }

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

    pidRight->SetOutputLimits(0, maxSpeedInt);
    pidLeft->SetOutputLimits(0, maxSpeedInt);

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

    pidRight->SetMode(AUTOMATIC);
    pidLeft->SetMode(AUTOMATIC);
    systemStart = millis();
  }

  void run()
  {
    while (true) {
      uint32_t now = millis();

      watcher.drive();
  
      if (lastDriveLoopTime > 0) {
        uint16_t passed = now - lastDriveLoopTime;
        
        if (now >= motorREndTime) {
          motorRSpeedDesired = 0;
        }
  
        if (now >= motorLEndTime) {
          motorLSpeedDesired = 0;
        }
      }

      // TODO SetControllerDirection

      setPidSetpoints(motorRSpeedDesired, motorLSpeedDesired);
      pidInputRight = watcher.currentTurnsRight();
      pidInputLeft = watcher.currentTurnsLeft();

      bool rightValueNew = pidRight->Compute();
      bool leftValueNew = pidLeft->Compute();

      if (rightValueNew) {
        switchMotorR(pidOutputRight);
      }

      if (leftValueNew) {
        switchMotorL(pidOutputLeft);
      }

      if (now - lastCounterOutTime > 1200) {
        float dtL = getCurrentlyDesiredTurns(motorLSpeedDesired);
        float dtR = getCurrentlyDesiredTurns(motorRSpeedDesired);
        Serial.print("Turns R c"+String(watcher.currentTurnsRight()) + "r" +String(dtL) + " p"+String( pidOutputRight));
        Serial.println(" L c"+String(watcher.currentTurnsLeft())+ "r" +String(dtR)  + " p"+String( pidOutputLeft));
        
        lastCounterOutTime = now;
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

  void setPidSetpoints(float desireR, float desireL)
  {
      pidSetpointRight = getNonDeadSpeed(desireR) * motorMaxTurns;
      pidSetpointLeft = getNonDeadSpeed(desireL) * motorMaxTurns;

/*
      Serial.print("SP "+String(pidSetpointRight)+" ");
      if (++outCounter % 20 == 0)
          Serial.println();*/
  }

  void switchMotorR(double pwmValue)
  {
    if (pwmValue != currentPwmRight) {
      /*
      if (showDebug) {
        Serial.print("RRa"+String(pwmValue)+" ");
  
        if (++outCounter % 20 == 0)
          Serial.println();
      }*/
        
      uint16_t speedInt = round(pwmValue);
      switchMotor(speedInt, 0, 1);

      currentPwmRight = pwmValue;
    }
  }

  void switchMotorL(double pwmValue)
  {
    if (pwmValue != currentPwmLeft) {
      if (showDebug && random(5) == 4) {
        Serial.print("LRa"+String(pwmValue)+" ");
  
        if (++outCounter % 20 == 0)
          Serial.println();
      }
  
      uint16_t speedInt = round(pwmValue);
      switchMotor(speedInt, 2, 3);
      
      currentPwmLeft = pwmValue;
    }
  }

  uint16_t outCounter = 0;
  bool showDebug = true;
  
  bool switchMotor(uint16_t speedInt, uint8_t channelForward, uint8_t channelReverse)
  {
    uint16_t chan1Speed = _min(maxSpeedInt, speedInt >= 0 ? speedInt : 0);
    uint16_t chan2Speed = _min(maxSpeedInt, speedInt < 0 ? -speedInt : 0);

/*
    if (showDebug) {
      Serial.print("SWM "+String(chan1Speed)+","+String(chan2Speed)+" ");
  
      if (++outCounter % 20 == 0)
        Serial.println();
    }*/

    ledcWrite(channelForward, chan1Speed);
    ledcWrite(channelReverse, chan2Speed);
  }

  double getNonDeadSpeed(float speed)
  {
    double nonDeadSpeed = 0;
    if (speed != 0) {
      double sign = speed < 0 ? -1 : +1;
      nonDeadSpeed = sign * DEAD_ZONE_SPEED_LOW + LIVE_ZONE_RANGE * speed;
    }

    return nonDeadSpeed;
  }

  float getCurrentlyDesiredTurns(float desiredSpeed)
  {
    return desiredSpeed * motorMaxTurns;
  }
};

#endif
