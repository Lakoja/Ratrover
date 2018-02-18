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
#include "Motor.h"

class ContinuousControl : public WiFiServer
{
private:
  WiFiClient client;
  uint32_t clientConnectTime;
  bool clientNowConnected = false;
  bool waitForRequest = false;
  Motor* motor;

public:
  ContinuousControl(Motor* m, int port) : WiFiServer(port)
  {
    motor = m;
  }

  void drive()
  {
    if (!client || !client.connected()) {
      if (clientNowConnected)
        Serial.println("Disconnected Co");
      
      clientNowConnected = false;
      
      client = accept();

      if (client)
        Serial.print("N");
    }

    if (!client || !client.connected())
      return;

    if (!clientNowConnected) {
      clientNowConnected = true;
      clientConnectTime = millis();
      
      waitForRequest = true;

      Serial.print("Client connected. IP address: ");
      Serial.println(client.remoteIP());
    }

    if (waitForRequest) {
      String requested = parseRequest();

      if (requested.length() > 0) {
        Serial.println("Requested "+requested);

        if (requested.startsWith("left ")
          || requested.startsWith("right ")
          || requested.startsWith("fore ")
          || requested.startsWith("back ")) {

          float v = -100;
          if (requested.startsWith("left ")) {
            v = parseValue(requested.substring(5));
            motor->requestRight(v);
            Serial.println("Left requested "+String(v));
          } else if (requested.startsWith("right ")) {
            v = parseValue(requested.substring(6));
            motor->requestLeft(v);
            Serial.println("Right requested "+String(v));
          } if (requested.startsWith("fore ")) {
            Serial.println("fore "+requested.substring(5));
            v = parseValue(requested.substring(5));
            motor->requestForward(v);
            Serial.println("Forward requested "+String(v));
          } else if (requested.startsWith("back ")) {
            v = parseValue(requested.substring(5));
            motor->requestReverse(v);
            Serial.println("Reverse requested "+String(v));
          }

          Serial.println("OKC for "+requested);
          client.println("OKC"+String(v));
        } else {
          
          client.println("HUH?");
        }
      }
    }
    
    // TODO use client.setTimeout? Or any transmission tracking?
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

    /* TODO require keep-alive?
    if (millis() - clientConnectTime > 2000) {
      waitForRequest = false;
      client.stop();
      Serial.println("Waited too long for a client request. Current line content: "+currentLine);
    }
    */
    
    uint32_t methodStartTime = micros();

    //Serial.print("P"+String(client.available() > 0));

    // TODO simplify time check - double while loop not necessary here?
    while (client.connected() && micros() - methodStartTime < 2000) {
      if (client.available() == 0)
        delayMicroseconds(200);

      while (client.available() > 0 && micros() - methodStartTime < 2000) {
        
        // TODO how not to wait too long here? Still read single bytes?
        String oneRequestLine = client.readStringUntil('\n');
        if (oneRequestLine.length() > 0) {
          return oneRequestLine;
        }
      }
    }

    return "";
  }
};

#endif

