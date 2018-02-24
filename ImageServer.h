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
class ImageServer : public WiFiServer
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

public:
  ImageServer(int port) : WiFiServer(port)
  {
    
  }

  void setup(SyncedMemoryBuffer* mb)
  {
    buffer = mb;
    
    begin();
  }

  bool clientConnected()
  {
    return client.connected();
  }
  
  void drive(bool ignoreImageAge)
  {
    if (!client.connected()) {
      if (clientNowConnected) {
        stopHandling();
        Serial.println("Disconnected Dr");
      }
        
      clientNowConnected = false;
      
      client = accept();

      if (client.connected()) {
        // TODO does this have any influence at all?
        //bool timeoutSuccess = setTimeoutMillis(300);
        client.setNoDelay(true);
    
        Serial.print("I ");//String(timeoutSuccess)+" ");
      }
    }

    if (!client.connected())
      return;

    if (!clientNowConnected) {
      clientNowConnected = true;
      clientConnectTime = millis();
      currentLine = "";
      
      waitForRequest = true;

      Serial.print("Client connected. IP address: ");
      Serial.println(client.remoteIP());
      //client.setTimeout(5);
      //client.setNoDelay(true); // imperative for at least _some_ throughput with smaller packets (1460)
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

          startHandling(requested);
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
      transferBuffer(ignoreImageAge);
    } 
    
    // TODO use client.setTimeout?
  }

private:
  virtual void startHandling(String requested)
  {
    Serial.println("Starting handling");
    imageCounter = 0;
    currentlyInBuffer = 0;
    currentlyTransferred = 0;
    transferActive = true;
  }

  virtual void stopHandling()
  {
    if (hasBufferSemaphore) {
      hasBufferSemaphore = false;
      Serial.println("Stopping connection while having lock!");
      //buffer->release();
    }
  }

private:
  void transferBuffer(bool ignoreImageAge)
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
      Serial.println("\n\n!\nWaited long for new image "+String(now - imageWaitStartTimestamp)+" last transferred ts "+String(lastTransferredTimestamp));
    }

    imageWaitStartTimestamp = 0;

    if (semaphoreWaitStartTime == 0)
      semaphoreWaitStartTime = millis();

    if (!hasBufferSemaphore)
      hasBufferSemaphore = buffer->take("iserve");

    if (!hasBufferSemaphore)
      return;

    if (millis() - semaphoreWaitStartTime > 500)
      Serial.print("W"+String(millis() - semaphoreWaitStartTime)+" ");
    semaphoreWaitStartTime = 0;

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

      String imageHeader = "--frame\n";
      imageHeader += "Content-Type: image/jpeg\n";
      imageHeader += "Content-Length: ";
      imageHeader += String(currentlyInBuffer);
      imageHeader += "\n\n";
      client.print(imageHeader); // Print as one block - will also work ok with setNoDelay(true)

      imageStartTime = millis();
    }

    uint16_t blockCounter = 0;
    
    while (client.connected() && currentlyTransferred < currentlyInBuffer) {
      uint32_t blockStart = millis();
      
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint32_t copyNow = _min(1460, currentlyInBuffer - currentlyTransferred);
      blockCounter++;

      uint32_t transferredNow = client.write(bufferPointer, copyNow);
      //uint32_t transferredNow = client.write(dummyData, copyNow);
      currentlyTransferred += transferredNow;
      
      uint32_t blockEnd = millis();

      uint32_t blockWriteMillis = blockEnd - blockStart;
      
      if (blockWriteMillis > 200 || transferredNow <= 0) {
        Serial.print("b!"+String(blockWriteMillis)+" ");

        outCount++;

        if (outCount % 15 == 0) {
          Serial.println();
        }
        
        yield();
      } else {
        //Serial.print("b"+String(blockWriteMillis));
      }
    }
    
    if (currentlyTransferred == currentlyInBuffer) {
      uint16_t waitTime = 0;
      if (client.connected()) {
        client.println();
        
        //waitTime = waitForAcknowledge();
        client.flush();
      }
      
      imageCounter++;
      uint32_t totalImageTime = millis() - imageStartTime;
      float kbs = (currentlyTransferred / (totalImageTime / 1000.0f)) / 1000.0f;
      if (totalImageTime > 400) {
        Serial.print("T!!"+String(totalImageTime)+"/");
        Serial.print(kbs, 1);
        Serial.print(" ");
        outCount++;
      } else if (kbs < 120) {
        Serial.print("T!");
        Serial.print(kbs, 1);
        Serial.print(" ");
        outCount++;
      } else {
        Serial.print("T ");
        outCount++;
      }

      Serial.print("S "+String(millis() - buffer->timestamp())+" ");
      
      if (outCount % 15 == 0) {
        Serial.println();
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

      if (imageCounter++ > 1000) {
        transferActive = false;
        if (client.connected()) {
          client.flush();
          client.stop();
        }
  
        Serial.println("Stopped after "+String(imageCounter)+" images");
        Serial.println("Total server side time: "+String(millis() - clientConnectTime)+"ms");
      } else {
        // TODO do more elegantly

        delay(20); // give other communication a chance
      }
    } else if (!client.connected()) {
      Serial.println("Emergency buffer release");
      buffer->release();
      hasBufferSemaphore = false;
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
