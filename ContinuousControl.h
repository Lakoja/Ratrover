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

#ifndef __CONTINUOUS_CONTROL_H__
#define __CONTINUOUS_CONTROL_H__

#include <WiFiServer.h>
#include "Task.h"
#include "Motor.h"

class ContinuousControl : public WiFiServer, public Task
{
private:
  const int VOLTAGE = 34;

  WiFiClient client;
  uint32_t clientConnectTime;
  bool clientNowConnected = false;
  bool waitForRequest = false;
  Motor* motor;
  uint16_t lastVoltageRaw = 0; // TODO remove
  uint32_t lastVoltageOut = 0;

public:
  ContinuousControl(Motor* m, int port) : WiFiServer(port)
  {
    motor = m;

    // NOTE using anything other than 10 bit and 0 db leads to radically worse values
    analogReadResolution(10); // now range is 0..1023
    analogSetPinAttenuation(VOLTAGE, ADC_0db); // metering range 1.1 volts
  }

  void inform(bool wifiClientPresent)
  {
    if (!wifiClientPresent && client.connected()) {
      client.stop();
      Serial.println("Ending control connection. Wifi client lost.");
    }
  }

  virtual void run()
  {
    while (true) {
      uint32_t loopStart = millis();
      if (!client.connected()) {
        if (clientNowConnected)
          Serial.println("Disconnected Co");
      
        clientNowConnected = false;
      
        client = accept();

        if (client)
          Serial.print("N");
      }

      if (client.connected()) {
        if (!clientNowConnected) {
          clientNowConnected = true;
          clientConnectTime = millis();
      
          waitForRequest = true;
        }

        if (waitForRequest) {
          String requested = parseRequest();
        
          if (requested.length() > 0) {
            /*
            if (!requested.equals("status")) {
              Serial.println("Requested "+requested);
            }*/

            if (requested.startsWith("move ")) {
              
              // TODO synchronize with motor?

              String numberPart = requested.substring(5);
              int idx = numberPart.indexOf(' ');
              if (idx > 0 && idx < numberPart.length() - 1) {
                String numberOne = numberPart.substring(0, idx);
                String numberTwo = numberPart.substring(idx+1);

                // Sends 0..1000 for the range of -1 .. 1
                float f = (parseValue(numberOne) - 0.5f) * 2;
                float r = (parseValue(numberTwo) - 0.5f) * 2;

                if (f > 1 || f < -1 || r > 1 || r < -1) {
                  Serial.println("\nIgnoring bogus movement value(s) "+String(f)+","+String(r));
                  client.println("HUH?");
                } else {

                  // NOTE client only (should) sends values on a circle so it will never be
                  //    both forward AND right = 1

                  // TODO support "climbing": one wheel holds
  
                  motor->requestMovement(f, r);

                  client.println("OKC "+String(f)+","+String(r));
                }
              } else {
                Serial.println("\nIgnoring bogus movement value(s) "+numberPart);
                client.println("HUH?");
              }
            } else if (requested.startsWith("left ")
              || requested.startsWith("right ")
              || requested.startsWith("fore ")
              || requested.startsWith("back ")) {

              // TODO remove? Also in Motor.h
              
              float v = -100;
              if (requested.startsWith("left ")) {
                v = parseValue(requested.substring(5));
                motor->requestRight(v);
                motor->requestLeft(0);
                Serial.println("Left requested "+String(v));
              } else if (requested.startsWith("right ")) {
                v = parseValue(requested.substring(6));
                motor->requestLeft(v);
                motor->requestRight(0);
                Serial.println("Right requested "+String(v));
              } if (requested.startsWith("fore ")) {
                Serial.println("fore "+requested.substring(5));
                v = parseValue(requested.substring(5));
                motor->requestForward(v);
              } else if (requested.startsWith("back ")) {
                v = parseValue(requested.substring(5));
                motor->requestReverse(v);
                Serial.println("Reverse requested "+String(v));
              }
        
              client.println("OKC"+String(v));
            } else if (requested.startsWith("status")) {
              float voltage = readVoltage();
              client.println("VOLT "+String(voltage,2)+" from "+String(lastVoltageRaw));
            } else {
              
              client.println("HUH?");
            }
          }
        }
        
        // TODO use client.setTimeout? Or any transmission tracking?
      }

      float voltage = readVoltage();
      if (loopStart - lastVoltageOut > 2000) {
    
        // TODO remove
        
        //Serial.println("VOLT "+String(voltage,2)+" from raw "+String(lastVoltageRaw));
        lastVoltageOut = loopStart;
      }

      sleepAfterLoop(5, loopStart);
    }
  }
  
private:

  float parseValue(String requestValueString)
  {
      long v = requestValueString.toInt();
      return v / 1000.0f;
  }
  
  String parseRequest()
  {
    if (!waitForRequest)
      return "";

    if (client.available() == 0) {
      return "";
    }

    /* TODO require keep-alive?
    if (millis() - clientConnectTime > 2000) {
      waitForRequest = false;
      client.stop();
      Serial.println("Waited too long for a client request. Current line content: "+currentLine);
    }
    */
    
    uint32_t methodStartTime = micros();

    //Serial.print("P"+String(client.available() > 0));

    while (client.connected() && micros() - methodStartTime < 2000) {
      if (client.available() == 0)
        delayMicroseconds(200);

      if (client.available() > 0) {
        
        // TODO how not to wait too long here? Still read single bytes?
        String oneRequestLine = client.readStringUntil('\n');
        if (oneRequestLine.length() > 0) {
          return oneRequestLine;
        }
      }
    }

    return "";
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
  
      return voltage;
  }
};

#endif

