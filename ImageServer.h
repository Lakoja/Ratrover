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

public:
  ImageServer(int port) : DriveableServer(port)
  {
    
  }

  void drive(SyncedMemoryBuffer* buffer)
  {
    DriveableServer::drive();
    
    if (!client.connected())
      return;

    if (transferActive) {
      //Serial.print("t");
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
    
    while (client.connected() && currentlyTransferred < currentlyInBuffer && micros() - methodStartTime < 4000) {
      uint32_t blockStart = micros();
      byte* bufferPointer = &((buffer->content())[currentlyTransferred]);
      uint16_t copyNow = _min(1460, currentlyInBuffer - currentlyTransferred);

      currentlyTransferred += client.write(bufferPointer, copyNow);
      uint32_t blockEnd = micros();

      if (blockEnd - blockStart > 10000L) {
        //Serial.print("b"+String((blockEnd - blockStart) / 1000.0f));
      }
    }
    
    if (client.connected() && currentlyTransferred == currentlyInBuffer) {
      client.println();
      client.flush();
      imageCounter++;
      /*
      Serial.print("First 5 bytes ");
      Serial.print((buffer->content())[0], 16);
      Serial.print((buffer->content())[1], 16);
      Serial.print((buffer->content())[2], 16);
      Serial.print((buffer->content())[3], 16);
      Serial.print((buffer->content())[4], 16);
      Serial.println();
      Serial.print("Last 5 bytes ");
      Serial.print((buffer->content())[currentlyInBuffer-5], 16);
      Serial.print((buffer->content())[currentlyInBuffer-4], 16);
      Serial.print((buffer->content())[currentlyInBuffer-3], 16);
      Serial.print((buffer->content())[currentlyInBuffer-2], 16);
      Serial.print((buffer->content())[currentlyInBuffer-1], 16);
      Serial.println();
      */
      //Serial.print("T"+String(currentlyTransferred)+","+String(millis() - imageStartTime));
      imageStartTime = 0;
      lastTransferredTimestamp = buffer->timestamp();
      currentlyInBuffer = 0;
      currentlyTransferred = 0;

      buffer->release();
      hasBufferSemaphore = false;

/*
      // check if connection is really alive
      if (client.connected()) {
        Serial.print("Hl");
        uint8_t dummy;
        int res = recv(client.fd(), &dummy, 0, MSG_DONTWAIT);
        if (res <= 0) {
            switch (errno) {
                case ENOTCONN:
                case EPIPE:
                case ECONNRESET:
                case ECONNREFUSED:
                case ECONNABORTED:
                    Serial.print("-");
                    break;
                default:
                    Serial.print("+"+String(errno));
                    break;
            }
        }
        else {
            // Should never happen since requested 0 bytes
            Serial.print("x");
        }
      }
*/
      
  
      if (imageCounter++ > 1000) {
        transferActive = false;
        client.flush();
        client.stop();
  
        Serial.println("Stopped after "+String(imageCounter)+" images");
        Serial.println("Total server side time: "+String(millis() - clientConnectTime)+"ms");
      }
    } else if (!client.connected()) {
      Serial.println("Emergency buffer release");
      buffer->release();
      hasBufferSemaphore = false;
    }
  }
};

#endif
