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

public:
  ImageServer(int port) : DriveableServer(port)
  {
    
  }

  void drive(SyncedMemoryBuffer* buffer)
  {
    DriveableServer::drive();
    
    if (!clientConnected())
      return;

    if (transferActive) {
      transferBuffer(buffer);
    } 
    // TODO use client.setTimeout?
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
    imageCounter = 0;
    transferActive = true;
  }

private:
  void transferBuffer(SyncedMemoryBuffer* buffer)
  {
    if (!transferActive)
      return;

    if (lastTransferredTimestamp == buffer->timestamp())
      return;

    if (semaphoreWaitStartTime == 0)
      semaphoreWaitStartTime = millis();

    if (!hasBufferSemaphore)
      hasBufferSemaphore = buffer->take();

    if (!hasBufferSemaphore)
      return;

    if (millis() - semaphoreWaitStartTime > 0)
      Serial.print("W"+String(millis() - semaphoreWaitStartTime));
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

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.println("Content-Length: " + String(currentlyInBuffer));
      client.println();
      imageStartTime = millis();
    }

    // TODO check for client connected?
    while (currentlyTransferred < currentlyInBuffer && micros() - methodStartTime < 2000) {
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint16_t copyNow = _min(1460, currentlyInBuffer - currentlyTransferred);

      currentlyTransferred += client.write(bufferPointer, copyNow);
    }
    
    if (currentlyTransferred == currentlyInBuffer) {
      client.println();
      client.flush();
      imageCounter++;
      Serial.print("T"+String(millis() - imageStartTime));
      imageStartTime = 0;
      lastTransferredTimestamp = buffer->timestamp();
      currentlyInBuffer = 0;
      currentlyTransferred = 0;

      buffer->release();
      hasBufferSemaphore = false;
  
      if (imageCounter++ > 1000) {
        transferActive = false;
        client.flush();
        client.stop();
  
        Serial.println("Stopped after "+String(imageCounter)+" images");
        Serial.println("Total server side time: "+String(millis() - clientConnectTime)+"ms");
      }
    }
  }
};

#endif
