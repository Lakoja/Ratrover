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

#ifndef __CONTROL_REQUEST_HANDLER_H__
#define __CONTROL_REQUEST_HANDLER_H__
 
#include <WiFiServer.h>
#include "AsyncArducam.h"

class ControlRequestHandler
{
private:
  WiFiServer webServer; // TODO why does this work; but not with (80) ...?
  WiFiClient client;

public:
  void setup()
  {
    webServer.begin(); 
  }

  void drive(AsyncArducam* aCam)
  {
    client = webServer.accept();

    if (client && client.connected()) {
      Serial.print("Client connected. IP address: ");
      Serial.println(client.remoteIP());

      int total_time = millis();

      String requested = parseRequest();
      Serial.println("Requested "+requested);

      // TODO check for requested (fail on favicon for example)

      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
      client.println();
  
      uint16_t imageCounter = 0;
      while(client.connected()) {
        
        client.println("--frame");
  
        //client.setNoDelay(true);
  
        int time1 = millis();
        aCam->transferCapture(client);
        int time2 = millis();
  
        Serial.print("T"+String(time2-time1)+" ");
  
        delay(1); // why is this enough for "wait for all data being sent"?
        // TODO vs setNoDelay()?
  
        if (imageCounter++ > 59) {
          client.stop();
          break;
        }
        
        aCam->drive();
      }
  
      Serial.println("Stopped after "+String(imageCounter)+" images");
      Serial.println("Total server side time: "+String(millis() - total_time)+"ms");
    }
  }

private:
  String parseRequest()
  {
    // TODO wait for something to be present?
    if (client.available()) {
      String currentLine = "";
      
      while (client.connected()) {
        if (client.available()) {
          /* This does not read past the first line and results in a "broken connection" in Firefox
          String oneRequestLine = client.readStringUntil('\n');
          Serial.println(oneRequestLine);
          if (currentLine.length() == 0) {
            break;
          }*/
          
          char c = client.read();
          if (c == '\n') {
            if (currentLine.length() == 0) {
              break;
            } else {
              if (currentLine.startsWith("GET ")) {
                return currentLine.substring(4);
              }
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }
        }
      }
    }

    return "";
  }
};

#endif
