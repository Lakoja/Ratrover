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
#include "SyncedMemoryBuffer.h"
#include "AsyncArducam.h"

class ControlRequestHandler
{
private:
  WiFiServer webServer; // TODO why does this work; but not with (80) ...?
  WiFiClient client;
  uint32_t clientConnectTime;
  uint16_t imageCounter;
  bool waitForRequest = false;
  bool transferActive = false;
  uint32_t currentlyTransferred = 0;
  uint32_t currentlyInBuffer = 0;
  bool hasBufferSemaphore = false;
  String currentLine = "";
  uint32_t lastTransferredTimestamp = 0;

public:
  void begin()
  {
    webServer.begin(); 
  }

  void drive(SyncedMemoryBuffer* buffer)
  {
    if (!client || !client.connected()) {
      client = webServer.accept();

      if (client)
        Serial.print("O");
    }

    if (!client || !client.connected())
      return;

    if (!transferActive && !waitForRequest) {
      clientConnectTime = millis();
      imageCounter = 0;
      currentLine = "";
      
      waitForRequest = true;

      Serial.print("Client connected. IP address: ");
      Serial.println(client.remoteIP());
    }

    if (transferActive) {
      transferBuffer(buffer);
    } else if (waitForRequest) {
      String requested = parseRequest();

      if (requested.length() > 0) {
         if (requested.startsWith("/ ")) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
          client.println();

          transferActive = true;
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

    Serial.print("P"+String(client.connected())+String(client.available())+" ");
    
    uint32_t methodStartTime = micros();

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
              waitForRequest = false;
              
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

  void transferBuffer(SyncedMemoryBuffer* buffer)
  {
    if (!transferActive)
      return;

    if (lastTransferredTimestamp == buffer->timestamp())
      return;

    if (!hasBufferSemaphore)
      hasBufferSemaphore = buffer->take();

    if (!hasBufferSemaphore)
      return;

    uint32_t methodStartTime = micros();

    if (currentlyInBuffer == 0) {
      currentlyInBuffer = buffer->contentSize();

      if (currentlyInBuffer == 0) {
        // This shouldn't happen?
        lastTransferredTimestamp = buffer->timestamp();
        buffer->release();
        hasBufferSemaphore = false;
        Serial.println("Handler found no content in buffer!!");
        return;
      }

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.println("Content-Length: " + String(currentlyInBuffer));
      client.println();
    }

    while (currentlyTransferred < currentlyInBuffer && micros() - methodStartTime < 2000) {
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint16_t copyNow = _min(1460, currentlyInBuffer - currentlyTransferred);

      currentlyTransferred += client.write(bufferPointer, copyNow);
    }
    
    if (currentlyTransferred == currentlyInBuffer) {
      client.println();
      imageCounter++;
      lastTransferredTimestamp = buffer->timestamp();
      currentlyInBuffer = 0;
      currentlyTransferred = 0;

      buffer->release();
      hasBufferSemaphore = false;
  
      if (imageCounter++ > 59) {
        transferActive = false;
        delay(1); // TODO why and if is this enough for "wait for all data being sent"?
        // TODO this is not enough; at least not for the last image
        client.stop();
  
        Serial.println("Stopped after "+String(imageCounter)+" images");
        Serial.println("Total server side time: "+String(millis() - clientConnectTime)+"ms");
      }
    }
  }
};

#endif
