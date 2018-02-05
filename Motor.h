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

#include <math.h>

const int MOTOR_R1 = 32;
const int MOTOR_R2 = 33;
const int MOTOR_L1 = 25;
const int MOTOR_L2 = 26;

class Motor
{
private:
  uint32_t systemStart;
  bool motorROn = false;
  bool motorLOn = false;
  uint8_t precision;
  uint8_t maxSpeedInt;

  void outputPin(int num)
  {
    digitalWrite(num, LOW);
    pinMode(num, OUTPUT);
  }

  void switchMotorR(float speed)
  {
    ledcWrite(0, round(speed * maxSpeedInt));
    ledcWrite(1, 0);

    if (speed != 0) {
      motorROn = true;
    } else {
      motorROn = false;
    }
  }

  void switchMotorL(float speed)
  {
    ledcWrite(2, round(speed * maxSpeedInt));
    ledcWrite(3, 0);

    if (speed != 0) {
      motorLOn = true;
    } else {
      motorLOn = false;
    }
  }

public:
  void setup()
  {
    precision = 5;
    maxSpeedInt = pow(2, precision) - 1;
    
    outputPin(MOTOR_R1);
    outputPin(MOTOR_R2);
    outputPin(MOTOR_L1);
    outputPin(MOTOR_L2);
  
    ledcSetup(0, 16000, precision); // precision bits 8: means we have 0..255 as steps, 4: 0..15
    ledcSetup(1, 16000, precision);
    ledcSetup(2, 16000, precision);
    ledcSetup(3, 16000, precision);
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
    uint32_t passed = millis() - systemStart;
    if (passed >= 1000 && passed < 4000) {
      if (!motorLOn) {
        Serial.println("Motors on");
        switchMotorR(0.6);
        switchMotorL(1);
      }
    } else {
      if (motorLOn) {
        Serial.println("Motors off");
        switchMotorR(0);
        switchMotorL(0);
      }
    }
  }
};

