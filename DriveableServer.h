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

#ifndef __DRIVEABLE_SERVER_H__
#define __DRIVEABLE_SERVER_H__

#include <WiFiServer.h>

class DriveableServer : public WiFiServer
{
private:
  bool clientNowConnected = false;
  bool waitForRequest = false;
  String currentLine = "";
  
protected:
  WiFiClient client;
  uint32_t clientConnectTime;

public:
  DriveableServer(int port) : WiFiServer(port)
  {
    
  }

  void drive()
  {
    if (!client || !client.connected()) {
      clientNowConnected = false;
      
      client = accept();

      if (client)
        Serial.print("O");
    }

    if (!client || !client.connected())
      return;

    if (!clientNowConnected) {
      clientNowConnected = true;
      clientConnectTime = millis();
      currentLine = "";
      
      waitForRequest = true;

      Serial.print("Client connected. IP address: ");
      Serial.println(client.remoteIP());
    }

    if (waitForRequest) {
      String requested = parseRequest();

      if (requested.length() > 0) {
        waitForRequest = false;
        
        if (shouldAccept(requested)) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: "+contentType(requested));
          client.println();

          Serial.println("Handling "+requested);

          startHandling(requested);
        } else {
          Serial.println("Ignoring request "+requested);
          
          client.println("HTTP/1.1 404 Not Found");
          client.println();
          client.stop();
        }
      }
    }
    
    // TODO use client.setTimeout?
  }

protected:
  virtual bool shouldAccept(String requested)
  {
    return false;
  }

  virtual String contentType(String requested)
  {
    return "";
  }

  virtual void startHandling(String requested)
  {
    
  }

private:
  String parseRequest()
  {
    if (!waitForRequest)
      return "";

    if (millis() - clientConnectTime > 2000) {
      waitForRequest = false;
      client.stop();
      Serial.println("Waited too long for a client request. Current line content: "+currentLine);
    }
    
    uint32_t methodStartTime = micros();

    //Serial.print("P"+String(client.available() > 0));

    // TODO simplify time check
    while (client.connected() && micros() - methodStartTime < 2000) {
      if (client.available() == 0)
        delayMicroseconds(200);

      while (client.available() > 0 && micros() - methodStartTime < 2000) {
        /* This does not read past the first line and results in a "broken connection" in Firefox
        String oneRequestLine = client.readStringUntil('\n');
        Serial.println(oneRequestLine);
        if (currentLine.length() == 0) {
          break;
        }*/
        
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            Serial.print("B");
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

    return "";
  }
};

#endif
