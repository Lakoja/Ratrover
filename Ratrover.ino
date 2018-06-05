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
#include "ImageServer.h"
#include "UdpImageServer.h"
#include "ContinuousControl.h"
//#include "Motor.h"
#include "StepperMotors.h"
#include "SyncedMemoryBuffer.h"

const int LED2 = 16;

const int CHANNEL = 1;

// first pin must be the one for "forward"
const uint8_t MOTOR_R1 = 33;
const uint8_t MOTOR_R2 = 32;
const uint8_t MOTOR_I_R = 35;
const uint8_t MOTOR_L1 = 25;
const uint8_t MOTOR_L2 = 26;
const uint8_t MOTOR_I_L = 27;
const uint16_t MOTOR_UMIN_MAX = 56;
const uint16_t MOTOR_REDUCTION = 298;


const uint8_t MOTOR_L_STEP = 32;
const uint8_t MOTOR_L_DIR = 33;
const uint8_t MOTOR_L_SLEEP = 14;

const uint8_t MOTOR_R_STEP = 26;
const uint8_t MOTOR_R_DIR = 27;
const uint8_t MOTOR_R_SLEEP = 25;

const uint16_t MOTOR_MAX_RPM = 100; // for steppers this can be higher (and weaker)
const uint16_t MOTOR_RESOLUTION = 800; // steps per rotation; this assumes a sub-step sampling (drv8834) of 4

//SyncedMemoryBuffer cameraBuffer;
SyncedMemoryBuffer serverBufferOne;
SyncedMemoryBuffer serverBufferOther;
//volatile uint32_t MotorWatcher::counterR = 0;
//volatile uint32_t MotorWatcher::counterL = 0;
StepperMotors motor;
ContinuousControl control(&motor);
//ImageServer imageServer(80, &control);
UdpImageServer imageServer(1510, &control);
AsyncArducam camera;
bool cameraValid = true;
uint8_t lastWifiClientCount = 0;

uint32_t lastSuccessfulImageCopy = 0;
bool llWarning = false;
uint32_t lastShowAlive = 0;
uint32_t lastShowLow = 0;

void setup() 
{
  Serial.begin(115200);
  Serial.println("Rover start!");

  //outputPin(IRLED2); // TODO use an analog output? (not so big a resistor/power loss needed)

  motor.setup(MOTOR_R_STEP, MOTOR_R_DIR, MOTOR_R_SLEEP, MOTOR_L_STEP, MOTOR_L_DIR, MOTOR_L_SLEEP, MOTOR_MAX_RPM, MOTOR_RESOLUTION);

  // NOTE for 3 buffers:
  // NOTE 50.000 bytes per buffer are too much for poor WiFi: no connections anymore
  // NOTE 40.000 bytes per buffer are too much for poor Udp: crashes on parsePacket()
  //cameraBuffer.setup();
  serverBufferOne.setup();
  serverBufferOther.setup();

  // NOTE this breaks voltage metering on pin 27 (=ADC2)...
  if (!setupWifi()) {
    while(1);
  }
  
  if (!camera.setup(OV2640_800x600, &serverBufferOne, &serverBufferOther)) {  // OV2640_320x240, OV2640_1600x1200, 
    cameraValid = false;
  }

  if (cameraValid) {
    outputPin(LED2);
    digitalWrite(LED2, HIGH);
    camera.start("cam", 4, 4000);
  }
  imageServer.begin();

  motor.start("motor", 5);

  //motor.requestMovement(0.02, 0, 500);
  //motor.hold();
  
  Serial.println("Waiting for connections... free "+String(ESP.getFreeHeap()));
}

void outputPin(int num)
{
  digitalWrite(num, LOW);
  pinMode(num, OUTPUT);
  digitalWrite(num, LOW);
}

bool setupWifi()
{
  WiFi.mode(WIFI_AP);
  
  bool b1 = WiFi.softAP("Roversnail", NULL, CHANNEL, 0, 2); // TODO scan continuum for proper channel?
  delay(100);
  bool b2 = WiFi.softAPConfig(IPAddress(192,168,151,1), IPAddress(192,168,151,254), IPAddress(255,255,255,0));


  if (b1 && b2) {
    Serial.print("WiFi AP started ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Could not start AP. config: "+String(b1)+" start:"+String(b2));
    return false;
  }

  return true;
}

bool showDebug = false;

void loop()
{
  uint32_t now = millis();
  
  uint32_t freex = ESP.getFreeHeap();
  if (freex < 6000 && now - lastShowLow > 500) {
    Serial.print("L"+String(freex)+","+String(ESP.getFreeHeap())+" ");
    lastShowLow = now;
  }
  
  uint8_t wifiClientCount = WiFi.softAPgetStationNum();

  if (wifiClientCount != lastWifiClientCount) {
    Serial.println((wifiClientCount > lastWifiClientCount ? "Connect" : "Disconnect")+String(" now ")+String(wifiClientCount));
    lastWifiClientCount = wifiClientCount;
  }
  
  //prepareImageFromCamera();
  
  imageServer.drive(&serverBufferOne, &serverBufferOther);

  // TODO emergency release if buffer is blocked somehow?

  if (showDebug) {
    if (now - lastShowAlive > 5000) {
      Serial.print("IST "+imageServer.getState()+" ");
      lastShowAlive = now;
    }
  }

  uint16_t passed = millis() - now;

  if (passed < 5) {
    delay(5 - passed);
  } else {
    yield();
  }
}

/*
void prepareImageFromCamera()
{
  if (!cameraValid) {
    return;
  }

  if (!cameraBuffer.take("main", 15 / portTICK_PERIOD_MS)) {
    return;
  }
  
  if (cameraBuffer.timestamp() > serverBufferOne.timestamp() && cameraBuffer.timestamp() > serverBufferOther.timestamp()) {
    if (serverBufferOne.timestamp() < serverBufferOther.timestamp()) {
      cameraBuffer.copyTo(&serverBufferOne);
    } else {
      cameraBuffer.copyTo(&serverBufferOther);
    }
    
    lastSuccessfulImageCopy = millis();
  }

  cameraBuffer.release();
}*/
