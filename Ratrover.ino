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
 
#include <WiFi.h>

#include "AsyncArducam.h"
#include "ControlRequestHandler.h"
#include "Motor.h"
#include "SyncedMemoryBuffer.h"

const int LED1 = 2;
const int LED2 = 4;
const int IRLED2 = 13;

SyncedMemoryBuffer buffer;
ControlRequestHandler control;
AsyncArducam camera(OV2640);
Motor motor;

bool setupWifi()
{
  WiFi.mode(WIFI_AP);
  bool b1 = WiFi.softAPConfig(IPAddress(192,168,151,1), IPAddress(192,168,151,254), IPAddress(255,255,255,0));
  bool b2 = WiFi.softAP("Roversnail", NULL, 8); // TODO scan continuum?
  delay(100);

  if (b1 && b2) {
    Serial.println("WiFi AP started");
  } else {
    Serial.println("Could not start AP. config: "+String(b1)+" start:"+String(b2));
    return false;
  }

  return true;
}

void outputPin(int num)
{
  digitalWrite(num, LOW);
  pinMode(num, OUTPUT);
}

void setup() 
{
  motor.setup();

  Serial.begin(115200);
  Serial.println("ArduCAM Start!");

  outputPin(LED1);
  outputPin(LED2);
  outputPin(IRLED2); // TODO use an analog output? (not so big a resistor/power loss needed)

  if (!camera.begin(OV2640_800x600)) {  // OV2640_320x240, OV2640_1600x1200, 
    while(1);
  }

  digitalWrite(LED1, HIGH);
  
  if (!setupWifi()) {
    while(1);
  }

  control.begin();

  buffer.setup();

  digitalWrite(LED2, HIGH);
  digitalWrite(IRLED2, HIGH);
  
  Serial.println("Waiting for connection to our webserver...");
}

void loop() 
{
  uint32_t loopStartTime = micros();
  
  motor.drive();
  camera.drive(&buffer);
  control.drive(&buffer);

  int32_t sleepNow = 1000 - (micros() - loopStartTime);
  
  if (sleepNow >= 0)
    delayMicroseconds(sleepNow);
}
