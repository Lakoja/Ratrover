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

#ifndef __IMAGE_SERVER_H__
#define __IMAGE_SERVER_H__
 
#include <WiFiServer.h>
#include "SyncedMemoryBuffer.h"
#include "ContinuousControl.h"

bool SERVE_MULTI_IMAGES = false;

class ImageServer : public WiFiServer
{
private:
  WiFiClient client;
  bool waitForRequest = false;
  bool waitForFirstRequest = true;
  bool transferActive = false;
  uint32_t waitForRequestStartTime = 0;
  String currentLine = "";
  uint32_t clientConnectTime;
  bool clientNowConnected = false;
  uint32_t lastTransferredTimestamp = 0;
  bool wifiClientPresent = false;
  uint32_t transferredImageCounter = 0;

  ContinuousControl *control = NULL;
  
  uint32_t transferredThisSecond = 0;
  uint32_t lastTransferOutMillis = 0;

public:
  ImageServer(int port, ContinuousControl *cont) : WiFiServer(port)
  {
    setTimeout(2);
    control = cont;
  }

  void drive(SyncedMemoryBuffer* imageData)
  {
    if (!client.connected()) {
      if (clientNowConnected) {
        Serial.println("Disconnect");
        clientNowConnected = false;
      }
      
      client = accept();
  
      if (client.connected()) {
        Serial.println("Client connect");
        uint32_t now = millis();
        clientNowConnected = true;
        clientConnectTime = now;
        waitForRequest = true;
        waitForRequestStartTime = now;
        transferActive = false;
        currentLine = "";
      }
    }
  
    if (client.connected()) {
      if (waitForRequest) {
        uint32_t now = millis();
        if (waitForRequestStartTime == 0) {
          waitForRequestStartTime = now;
        } else if (now - waitForRequestStartTime > 5000) {
          Serial.println("Waiting for request...");
          waitForRequestStartTime = 0;
        }
        String requested = parseRequest();
  
        if (requested.length() > 0) {
          waitForFirstRequest = false;
  
          //Serial.println("Requested "+requested);
          
          if (requested.startsWith("/ ")) {
            String responseHeader = "HTTP/1.1 200 OK\n";
            if (SERVE_MULTI_IMAGES) {
              responseHeader += "Content-Type: multipart/x-mixed-replace; boundary=frame\n";
              responseHeader += "\n";
            }
            client.print(responseHeader);
  
            waitForRequest = false;
            transferActive = true;
          } else if (requested.startsWith("/image_s")) {
            client.println(getState());
          } else if (control->supports(requested.substring(1))) {
            String returnValue = control->handle(requested.substring(1));

            if (returnValue.length() > 0) {
              client.println(returnValue);
              Serial.println("Showing "+returnValue);
            } else {
              client.println("HUH?");
            }
          } else {
            Serial.println("Ignoring request "+requested);
            
            client.println("HTTP/1.1 404 Not Found");
            client.println();
            client.stop();
          }
        }
      }
  
      if (transferActive && imageData->contentSize() > 0) {
        bool imageValid = lastTransferredTimestamp == 0 || imageData->timestamp() != lastTransferredTimestamp;

        if (!imageValid) {
          client.println("NOIY");

          // TODO centralize/refactor - see below
          transferActive = false;
          waitForRequest = true;
          waitForRequestStartTime = millis();

          return;
        }
        
        String imageHeader = "";
        if (SERVE_MULTI_IMAGES) {
          imageHeader += "--frame\n";
        }
        imageHeader += "Content-Type: image/jpeg\n";
        imageHeader += "Content-Length: ";
        imageHeader += String(imageData->contentSize());
        imageHeader += "\n\n";
        client.print(imageHeader); // Print as one block - will also work ok with setNoDelay(true)
  
        uint32_t currentlyTransferred = 0;
        uint32_t blockStart = millis();
  
        while(currentlyTransferred < imageData->contentSize()) {
          byte* bufferPointer = &((imageData->content())[currentlyTransferred]);
          uint32_t transferredNow = client.write(bufferPointer, imageData->contentSize() - currentlyTransferred);
          transferredThisSecond += transferredNow;
          currentlyTransferred += transferredNow;
        }
        client.println();
        //client.flush(); // This will eventually destroy an incoming request; the server is then dead (??)

        transferredImageCounter++;
        lastTransferredTimestamp = imageData->timestamp();
        
        uint32_t now = millis();
  
        if (SERVE_MULTI_IMAGES) {
          if (now - lastTransferOutMillis >= 1000) {
            double transferKbps = (transferredThisSecond / ((now - lastTransferOutMillis) / 1000.0)) / 1024.0;
            Serial.println(String(transferredImageCounter)+" "+String(transferKbps)+" "+String(now - blockStart));
          
            transferredThisSecond = 0;
            lastTransferOutMillis = now;
          }
        } else {
          double transferKbps = (currentlyTransferred / ((now - blockStart) / 1000.0)) / 1024.0;
          if (transferredImageCounter % 10 == 0 || transferKbps < 50) {
            Serial.println(String(transferredImageCounter)+" "+String(transferKbps)+" "+String(now - blockStart));
          }
        }
        
        if (SERVE_MULTI_IMAGES && now - clientConnectTime > 120000L) {
          client.stop();
          Serial.println("Test stop");
          transferActive = false;
        }
  
        if (!SERVE_MULTI_IMAGES) {
          transferActive = false;
          waitForRequest = true;
          waitForRequestStartTime = now;
        }
      }
    }
  }

  String getState()
  {
    return String(client.connected()) + " " + String(waitForRequest) + " " + String(transferActive);
  }
  
private:
  String parseRequest()
  {
    if (!waitForRequest)
      return "";

    if (waitForFirstRequest && millis() - clientConnectTime > 2000) {
      waitForRequest = false;
      client.stop();
      Serial.println("Waited too long for a client request. Current line content: "+currentLine);
    }
    
    uint64_t methodStartTime = esp_timer_get_time();

    while (client.available() > 0 && esp_timer_get_time() - methodStartTime < 2000) {
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
            String currentRequest = currentLine.substring(4);
            currentLine = "";
            return currentRequest;
          }
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }

    return "";
  }
};

#endif
