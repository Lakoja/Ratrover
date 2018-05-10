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
#include <lwip/sockets.h>

// TODO remove
const int DUMMY_BUFFER_SIZE = 1460;
byte dummyData[DUMMY_BUFFER_SIZE];

// TODO join with DriveableServer?
class ImageServer : public WiFiServer, public Task
{
private:
  bool clientNowConnected = false;
  bool waitForRequest = false;
  String currentLine = "";
  WiFiClient client;
  uint32_t clientConnectTime;

  SyncedMemoryBuffer *buffer;
  
  uint16_t imageCounter = 0;
  bool transferActive = false;
  uint32_t currentlyTransferred = 0;
  uint32_t currentlyInBuffer = 0;
  bool hasBufferSemaphore = false;
  uint32_t lastTransferredTimestamp = 0;
  uint32_t imageStartTime = 0;
  uint32_t semaphoreWaitStartTime = 0;
  uint16_t outCount = 0;
  uint32_t imageWaitStartTimestamp = 0;
  bool ignoreImageAge = false;
  bool wifiClientPresent = false;
  SemaphoreHandle_t activitySemaphore;
  uint32_t lastKbpsOutput = 0;

public:
  ImageServer(int port) : WiFiServer(port)
  {
    setTimeout(2);
  }

  void setup(SyncedMemoryBuffer* mb, bool ignoreAge, SemaphoreHandle_t semaphore)
  {
    buffer = mb;
    ignoreImageAge = ignoreAge;
    activitySemaphore = semaphore;
    begin();
  }

  void inform(bool clientPresent)
  {
    wifiClientPresent = clientPresent;
  }

  String getState()
  {
    return String(clientNowConnected) + " " + String(waitForRequest) + " " + String(transferActive) + " " + String(hasBufferSemaphore);
  }
  
  bool clientConnected()
  {
    return client.connected();
  }

  virtual void run()
  {
    while (true) {
      uint32_t loopStart = millis();
      
      drive();

      sleepAfterLoop(6, loopStart);
    }
  }
  
private:
  void drive()
  {
    if (!wifiClientPresent && client.connected()) {
      client.stop();
      Serial.println("Ending image connection. Wifi client lost.");
    }
    
    if (!client.connected()) {
      if (clientNowConnected) {
        if (hasBufferSemaphore) {
          hasBufferSemaphore = false;
          Serial.println("Stopping connection while having lock!");
          buffer->release();
        }
        
        Serial.println("Disconnected Dr");
        clientNowConnected = false;
      }

      client = accept();

      if (client.connected()) {
        // TODO does this have any influence at all?
        //bool timeoutSuccess = setTimeoutMillis(300);
        client.setTimeout(2); // TODO "blocking" time for a single write is still 10 seconds
        //client.setNoDelay(true);
        //client.flush(); // Will erase the request
    
        Serial.print("I ");//String(timeoutSuccess)+" ");
      }
    }

    if (!client.connected())
      return;

    if (!clientNowConnected) {
      clientNowConnected = true;
      clientConnectTime = millis();
      currentLine = "";

      transferActive = false;
      waitForRequest = true;

      Serial.print("I-Client connected. IP address: ");
      Serial.println(client.remoteIP());
    }

    if (waitForRequest) {
      String requested = parseRequest();

      if (requested.length() > 0) {
        waitForRequest = false;
        
        if (requested.startsWith("/ ")) {
          String responseHeader = "HTTP/1.1 200 OK\n";
          responseHeader += "Content-Type: multipart/x-mixed-replace; boundary=frame\n";
          responseHeader += "\n";
          client.print(responseHeader);

          Serial.println("Serving new client the HTTP response header");

          imageCounter = 0;
          currentlyInBuffer = 0;
          currentlyTransferred = 0;
          transferActive = true;
        } else {
          Serial.println("Ignoring request "+requested);
          
          client.println("HTTP/1.1 404 Not Found");
          client.println();
          client.stop();
        }
      }
    }

    if (!client.connected())
      return;

    if (transferActive) {
      transferBuffer(client);
    } 
    
    // TODO use client.setTimeout?
  }

  void transferBuffer(WiFiClient transferClient)
  {
    if (!transferActive)
      return;

    if (!buffer->hasContent())
      return;

    uint32_t now = millis();
    if (!ignoreImageAge && lastTransferredTimestamp == buffer->timestamp()) {
      if (imageWaitStartTimestamp == 0) {
        imageWaitStartTimestamp = now;
      }
      return;
    }

    if (!ignoreImageAge && imageWaitStartTimestamp > 0 && now - imageWaitStartTimestamp > 3000) {
      Serial.println("\n!\nWaited long for new image "+String(now - imageWaitStartTimestamp)+" last transferred ts "+String(lastTransferredTimestamp));
    }

    if (!transferClient.connected()) {
      return;
    }

    imageWaitStartTimestamp = 0;

    if (semaphoreWaitStartTime == 0)
      semaphoreWaitStartTime = millis();

    if (!hasBufferSemaphore)
      hasBufferSemaphore = buffer->take("iserve");

    if (!hasBufferSemaphore)
      return;

    if (activitySemaphore != NULL) {
      uint32_t t1 = millis();
      if (xSemaphoreTake(activitySemaphore, 500 / portTICK_PERIOD_MS) != pdTRUE) {
        uint32_t t2 = millis();
        Serial.println("LWI "+String(t2-t1)+" ");
        return;
      }
    }

    if (millis() - semaphoreWaitStartTime > 500)
      Serial.print("W"+String(millis() - semaphoreWaitStartTime)+" ");
    semaphoreWaitStartTime = 0;

    if (!transferClient.connected()) {
      // TODO improve this semaphore handing/tracking!
      if (activitySemaphore != NULL) {
        xSemaphoreGive(activitySemaphore);
      }
      
      return;
    }

    //Serial.print("t");
    
    if (currentlyInBuffer == 0) {
      currentlyInBuffer = buffer->contentSize();

      if (currentlyInBuffer == 0) {
        // This shouldn't happen?
        lastTransferredTimestamp = buffer->timestamp();
        buffer->release();
        hasBufferSemaphore = false;
        Serial.println("Handler found no content in buffer!!");
        xSemaphoreGive(activitySemaphore);
        if (activitySemaphore != NULL) {
          xSemaphoreGive(activitySemaphore);
        }
        return;
      }

      transferClient.flush();

      //Serial.print("H ");

      String imageHeader = "--frame\n";
      imageHeader += "Content-Type: image/jpeg\n";
      imageHeader += "Content-Length: ";
      imageHeader += String(currentlyInBuffer);
      imageHeader += "\n\n";
      transferClient.print(imageHeader); // Print as one block - will also work ok with setNoDelay(true)

      //Serial.println("Transferring "+String(currentlyInBuffer));

      imageStartTime = millis();
    }
    
    while (transferClient.connected() && currentlyTransferred < currentlyInBuffer) {
      uint32_t blockStart = millis();
      
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint32_t copyNow = _min(1460, currentlyInBuffer - currentlyTransferred);

      uint32_t transferredNow = transferClient.write(bufferPointer, copyNow);
      //uint32_t transferredNow = transferClient.write(dummyData, copyNow);
      currentlyTransferred += transferredNow;
      
      uint32_t blockEnd = millis();

      uint32_t blockWriteMillis = blockEnd - blockStart;
      
      if (blockWriteMillis > 200 || transferredNow <= 0) {
        Serial.print("b!"+String(blockWriteMillis)+"("+String(transferredNow)+") ");

        outCount++;

        if (outCount % 15 == 0) {
          Serial.println();
        }

        if (blockWriteMillis > 5000) {
          // Consider this a connection break
          transferClient.flush();
          transferClient.stop();
          // TODO do more; and on client-side?
        }
        
        yield();
      } else {
        //Serial.print("b"+String(blockWriteMillis));
      }
    }
    
    if (currentlyTransferred == currentlyInBuffer) {
      uint16_t waitTime = 0;
      if (transferClient.connected()) {
        transferClient.println();
        
        //waitTime = waitForAcknowledge();
        transferClient.flush();
      }
      
      imageCounter++;
      uint32_t now = millis();
      uint32_t totalImageTime = now - imageStartTime;
      float kbs = (currentlyTransferred / (totalImageTime / 1000.0f)) / 1000.0f;
      if (totalImageTime > 400) {
        Serial.print("T!!"+String(totalImageTime)+"/");
        Serial.print(kbs, 1);
        Serial.print(" ");
        outCount++;
      
        if (outCount % 15 == 0) {
          Serial.println();
        }
        lastKbpsOutput = now;
      } else if (now - lastKbpsOutput > 1000) {
        /*
        Serial.print("T");
        Serial.print(kbs, 1);
        Serial.print(" ");
        outCount++;
      
        if (outCount % 15 == 0) {
          Serial.println();
        }*/
        lastKbpsOutput = now;
      }

      if (random(5) == 4) {
        //Serial.print("S "+String(now - buffer->timestamp())+" ");
      }
        
      imageStartTime = 0;
      lastTransferredTimestamp = buffer->timestamp();
      currentlyInBuffer = 0;
      currentlyTransferred = 0;

      buffer->release();
      hasBufferSemaphore = false;

      // TODO check for connection? keep-alive?
      if (!hasClient()) {
        //Serial.println("Server has no client."); // TODO this always the case?
      }

      if (imageCounter++ > 5000 || millis() - clientConnectTime > 5L*60*1000) {
        transferActive = false;
        if (transferClient.connected()) {
          transferClient.flush();
          transferClient.stop();
        }
  
        Serial.println("Stopped after "+String(imageCounter)+" images");
        Serial.println("Total server side time: "+String(now - clientConnectTime)+"ms");
      } else {
        // TODO do more elegantly

        delay(20); // give other communication a chance
      }
    } else if (!transferClient.connected()) {
      buffer->release();
      hasBufferSemaphore = false;
    }

    if (activitySemaphore != NULL) {
      xSemaphoreGive(activitySemaphore);
    }
  }

  /*
  uint16_t waitForAcknowledge()
  {
    uint32_t waitForReplyStart = millis();

    bool alreadyPrinted = false;
    while (client.available() < 2) { // NOTE checking for "3" will never return; \n does not count?
      delayMicroseconds(200);
      uint16_t passed = millis() - waitForReplyStart;
      if (passed > 1000) {
        Serial.println("Waited too long for acknowledgement. "+String(passed)+" "+String(client.connected()));
        alreadyPrinted = true;
        break;
      }
    }
    
    int data1 = client.read();
    int data2 = client.read();
    int data3 = client.read();

    uint16_t passed = millis() - waitForReplyStart;

    if (passed > 300 && !alreadyPrinted) {
      Serial.println("Acknowledging took long "+String(passed));
    }
    
    if (data1 == 'o' && data2 == 'k' && data3 == '\n') {
      //Serial.println("Client acknowledged "+String(passed));
    } else {
      Serial.print("NAK ");

      outCount++;

      if (outCount % 15 == 0) {
        Serial.println();
      }
    }

    return millis() - waitForReplyStart;
  }*/

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
  
  bool setTimeoutMillis(uint16_t timoutms)
  {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = (uint32_t)timoutms * 1000L;
    int16_t error = client.setSocketOption(SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
    int16_t error2 = client.setSocketOption(SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));

    if (error < 0 || error2 < 0) {
      return false;
    }

    return true;
  }
};

#endif
