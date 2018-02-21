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
 
#include "DriveableServer.h"
#include "SyncedMemoryBuffer.h"
#include <lwip/sockets.h>

class ImageServer : public DriveableServer
{
private:
  uint16_t imageCounter = 0;
  bool transferActive = false;
  uint32_t currentlyTransferred = 0;
  uint32_t currentlyInBuffer = 0;
  bool hasBufferSemaphore = false;
  uint32_t lastTransferredTimestamp = 0;
  uint32_t imageStartTime = 0;
  uint32_t semaphoreWaitStartTime = 0;
  uint16_t outCount = 0;

public:
  ImageServer(int port) : DriveableServer(port)
  {
    
  }

  void drive(SyncedMemoryBuffer* buffer, bool ignoreImageAge)
  {
    DriveableServer::drive();
    
    if (!client.connected())
      return;

    if (transferActive) {
      //Serial.print("t");
      transferBuffer(buffer, ignoreImageAge);
    } 
  }

protected:
  virtual bool shouldAccept(String requested)
  {
    return requested.startsWith("/ ");
  }

  virtual String contentType(String requested)
  {
    return "multipart/x-mixed-replace; boundary=frame";
  }

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
  void transferBuffer(SyncedMemoryBuffer* buffer, bool ignoreImageAge)
  {
    if (!transferActive)
      return;

    if (!ignoreImageAge && lastTransferredTimestamp == buffer->timestamp())
      return;
    // TODO print wait time for picture?

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
      client.print(imageHeader); // Print as one block - would also work ok with setNoDelay(true)

      imageStartTime = millis();
    }

    bool firstWrite = true;
    while (client.connected() && currentlyTransferred < currentlyInBuffer && micros() - methodStartTime < 10000) {
      uint32_t blockStart = micros();
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint32_t copyNow = _min(2000, currentlyInBuffer - currentlyTransferred);

      currentlyTransferred += client.write(bufferPointer, copyNow);
      uint32_t blockEnd = micros();

      uint16_t blockWriteMillis = (blockEnd - blockStart) / 1000;
      if (blockWriteMillis > 200) {
        Serial.print("b!"+String(blockWriteMillis)+"");

        outCount++;

        if (outCount % 15 == 0) {
          Serial.println();
        }
        
        delay(100);
      } else {
        //Serial.print("b"+String(blockWriteMillis));
      }
      

      //if (!firstWrite)
      //  Serial.print("w");

      firstWrite = false;
    }
    
    if (currentlyTransferred == currentlyInBuffer) {
      uint16_t waitTime = 0;
      if (client.connected()) {
        client.println();
        
        //waitTime = waitForAcknowledge();
        client.flush();
      }
      
      imageCounter++;
      //Serial.print("T"+String(currentlyTransferred)+","+String(millis() - imageStartTime));
      uint32_t totalImageTime = millis() - imageStartTime;
      if (totalImageTime > 500) {
        Serial.print("T"+String(totalImageTime)+" ");//(i"+String(waitTime)+") ");

        outCount++;

        if (outCount % 15 == 0) {
          Serial.println();
        }
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
      }
    } else if (!client.connected()) {
      Serial.println("Emergency buffer release");
      buffer->release();
      hasBufferSemaphore = false;
    }
  }

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
        /*
        Serial.print("Client did not acknowledged "+String(passed)+" ");
        Serial.print(data1);
        Serial.print(data2);
        Serial.print(data3);
        Serial.println();*/
      }

      return millis() - waitForReplyStart;
  }
};

#endif
