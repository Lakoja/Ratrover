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

#include <esp_err.h>
#include <esp_wifi.h>

const int VOLTAGE = 27;

#include "AsyncArducam.h"
#include "ImageServer.h"
#include "ContinuousControl.h"
#include "Motor.h"
#include "SyncedMemoryBuffer.h"

const int LED1 = 2;
const int LED2 = 4;
const int IRLED2 = 13;

const int CHANNEL = 8;

// first pin must be the one for "forward"
const uint8_t MOTOR_R1 = 33;
const uint8_t MOTOR_R2 = 32;
const uint8_t MOTOR_L1 = 25;
const uint8_t MOTOR_L2 = 26;
const uint16_t MOTOR_UMIN_MAX = 96;

SyncedMemoryBuffer cameraBuffer;
SyncedMemoryBuffer serverBuffer;
Motor motor;
ImageServer imageServer(81);
ContinuousControl controlServer(&motor, 80);
AsyncArducam camera(OV2640);
bool cameraValid = true;
uint32_t lastSuccessfulImageCopy = 0;
uint32_t lastVoltageOut = 0;
bool llWarning = false;
uint16_t lastVoltageRaw = 0;

bool setupWifi()
{
  WiFi.mode(WIFI_AP);

  /* Probably doesn't really save power...
  esp_err_t error = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
  if (error != 0) {
    Serial.println("Set protocol error "+String(error));
  }*/

  /* This worsens transfer abyss times
  esp_err_t error = esp_wifi_set_max_tx_power(52); // =level 5 low; or 8, 52, 68 or 127 see esp_wifi.h
  if (error != 0) {
    Serial.println("Set max tx error "+String(error));
  }*/
  
  bool b1 = WiFi.softAPConfig(IPAddress(192,168,151,1), IPAddress(192,168,151,254), IPAddress(255,255,255,0));
  bool b2 = WiFi.softAP("Roversnail", NULL, CHANNEL, 0, 1); // TODO scan continuum?
  delay(100);

  if (b1 && b2) {
    Serial.print("WiFi AP started ");
    Serial.println(WiFi.softAPIP());
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
  Serial.begin(115200);
  Serial.println("Rover start!");

  // NOTE using anything other than 10 bit and 0 db leads to radically worse values
  analogReadResolution(10); // now range is 0..1023
  analogSetPinAttenuation(VOLTAGE, ADC_0db); // metering range 1.1 volts

  //outputPin(LED1);
  //outputPin(LED2);
  outputPin(IRLED2); // TODO use an analog output? (not so big a resistor/power loss needed)

  motor.setup(MOTOR_R1, MOTOR_R2, MOTOR_L1, MOTOR_L2, MOTOR_UMIN_MAX);
  cameraBuffer.setup();
  serverBuffer.setup();
  
  if (!camera.setup(OV2640_800x600, &cameraBuffer)) {  // OV2640_320x240, OV2640_1600x1200, 
    cameraValid = false;
  }

  //digitalWrite(LED1, HIGH);
  
  if (!setupWifi()) {
    while(1);
  }

  controlServer.begin();

  if (cameraValid) {
    imageServer.setup(&serverBuffer);
    //digitalWrite(LED2, HIGH);
    camera.start("cam", 2, 4000);
  } else {
    serverBuffer.take("test");
    serverBuffer.release(27000);
    imageServer.setup(&serverBuffer);
  }
  digitalWrite(IRLED2, HIGH);

  controlServer.start("control", 4);
  
  Serial.println("Waiting for connection to our webserver...");
}

void copyImage()
{
  if (!cameraValid) {
    return;
  }
  
  if (cameraBuffer.timestamp() > serverBuffer.timestamp()) {
    bool cameraLock = cameraBuffer.take("main");
    bool serverLock = serverBuffer.take("main");

    if (!cameraLock || !serverLock) {
      uint32_t now = millis();
      if (!llWarning && now - lastSuccessfulImageCopy > 3000) {
        llWarning = true;
        Serial.print("LL cannot "+String(cameraLock)+String(serverLock)+" ");
      }
    }
    
    if (cameraLock && serverLock) {
      cameraBuffer.copyTo(&serverBuffer);
      Serial.print("O ");
      
      uint32_t now = millis();
      if (now - lastSuccessfulImageCopy > 3000) {
        Serial.print("LL"+String(now - lastSuccessfulImageCopy)+" ");
        llWarning = false;
      }
      
      lastSuccessfulImageCopy = now;
    }

    if (cameraLock) {
      cameraBuffer.release();
    }
    
    if (serverLock) {
      serverBuffer.release();
    }
  } else {
    uint32_t now = millis();
    if (!llWarning && now - lastSuccessfulImageCopy > 3000) {
      llWarning = true;
      Serial.print("LL nothing "+String(cameraBuffer.timestamp())+"vs"+String(serverBuffer.timestamp())+" ");
    }
  }
}

float readVoltage()
{
    lastVoltageRaw = analogRead(VOLTAGE);

    float bridgeFactor = (266.0f + 80) / 80;
    float refVoltage = 1.1f;
    float maxValue = 1023.0f;
    float measureVoltage = lastVoltageRaw / maxValue * refVoltage;
    // 4.2 volts then corresponds to 0.97 volts measured
    
    float voltage = measureVoltage * bridgeFactor;

    // Makes no sense with usb connected
    //Serial.println("VOLT RAW "+String(volt1)+" "+String(volt2)+" => "+String(measureVoltage, 3)+","+String(voltage, 3));

    return voltage;
}

void loop() 
{
  uint32_t loopStart = millis();

  bool wifiHasClient = WiFi.softAPgetStationNum() > 0;

  motor.drive();

  if (cameraValid && camera.isReady()) {
    camera.inform(imageServer.clientConnected());
  }

  copyImage();
  float voltage = readVoltage();

  controlServer.inform(voltage, lastVoltageRaw, wifiHasClient);

  //if (camera.isIdle()) {
    uint32_t imgStart = millis();
    imageServer.drive(!camera.isReady(), wifiHasClient);
    uint32_t imgEnd = millis();
  
    if (imgEnd - imgStart > 20000) {
      Serial.println("!! Image drive took long "+String(imgEnd-imgStart));
    }
  //}

  int32_t sleepNow = 2 - (millis() - loopStart);
  if (sleepNow >= 0)
    delay(sleepNow);
  else
    yield();
}
