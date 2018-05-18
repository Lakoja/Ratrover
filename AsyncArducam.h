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

#ifndef __ASYNC_ARDUCAM_H__
#define __ASYNC_ARDUCAM_H__

#include <ArduCAM.h>
#include <Wire.h>
#include <SPI.h>
#include "memorysaver.h"
#include <math.h>

#include "SyncedMemoryBuffer.h"
#include "Task.h"

const int VCS = 5;
const int VSCK = 18;
const int VMISO = 19;
const int VMOSI = 23;

const byte model = OV2640;

class AsyncArducam : public ArduCAM, public Task
{
private:
  bool cameraReady = false;
  uint32_t lastCaptureStart = 0;
  uint16_t lastCaptureDuration = 200;
  uint32_t lastCopyStart = 0;
  bool captureStarted = false;
  bool copyActive = false;
  bool hasCopySemaphore = false;
  uint32_t currentDataInCamera = 0;
  uint32_t currentlyCopied = 0;
  uint8_t ffsOnLine = 0;
  uint32_t semaphoreWaitStartTime = 0;
  bool writtenSemaphoreError = false;
  SyncedMemoryBuffer *buffer;
  
public:
  AsyncArducam() : ArduCAM(model, VCS)
  {
  }
  
  bool setup(uint8_t size, SyncedMemoryBuffer* mb)
  {
    buffer = mb;
    
    Wire.begin();
  
    pinMode(VCS, OUTPUT);
  
    SPI.begin(VSCK, VMISO, VMOSI, VCS);
    SPI.setFrequency(8000000); // TODO 16mhz produces illegal bytes?

    //Check if the ArduCAM SPI bus is OK
    write_reg(ARDUCHIP_TEST1, 0x55);
    uint8_t temp = read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55){
      Serial.println("SPI1 interface Error!");
      return false;
    }

    bool checkSuccess = checkCamera();

    if (!checkSuccess) {
      return false;
    }

    set_format(JPEG);
    InitCAM();
    OV2640_set_JPEG_size(size);
    clear_fifo_flag();

    cameraReady = true;

    return true;
  }

  virtual void run()
  {
    if (!cameraReady)
      return;

    while (true) {
      uint32_t loopStart = millis();
      
      if (captureStarted && !isCaptureActive()) {
        lastCaptureDuration = millis() - lastCaptureStart;
        if (lastCaptureDuration > 300) {
          Serial.print("C" + String(lastCaptureDuration) + " ");
          
          if (++ffsOnLine % 20 == 0)
            Serial.println();
        }
        //else
        //  Serial.print("C ");
      
        captureStarted = false;
        
        initiateCopy();
      }
      
      if (copyActive) {
        copyDataToBuffer();
      } else if (!captureStarted) {
        initiateCapture();
      }
      
      sleepAfterLoop(10, loopStart);
    }
  }

  bool isReady()
  {
    return cameraReady;
  }

  bool isIdle()
  {
    return !captureStarted && !copyActive;
  }
  
private:
  bool checkCamera()
  {
    //Check if the camera module type is OV2640
    uint8_t vid, pid;
    wrSensorReg8_8(0xff, 0x01);
    rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))) {
      Serial.println("Can't find OV2640 module!");
      return false;
    } else {
      return true;
    }
  }

  void initiateCapture() 
  {
    if (captureStarted)
      return;
    
    captureStarted = true;
    uint32_t now = millis();
    //Serial.print("D"+String(now-lastCaptureStart)+" ");
    lastCaptureStart = now;
    
    clear_fifo_flag();
    start_capture();
  }

  void initiateCopy()
  {
    if (copyActive)
      return;
    
    copyActive = true;

    currentDataInCamera = 0;
    currentlyCopied = 0;

    lastCopyStart = millis();
  }

  bool isCaptureActive()
  {
    return !get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK);
  }

  void copyDataToBuffer()
  {
    if (semaphoreWaitStartTime == 0) {
      semaphoreWaitStartTime = millis();
    }
      
    if (!hasCopySemaphore) {
      hasCopySemaphore = buffer->take("cam");
    }

    if (!hasCopySemaphore) {
      if (millis() - semaphoreWaitStartTime > 3000 && !writtenSemaphoreError) {
        Serial.println("Long wait for semaphore for image copy; owner "+buffer->getTaker());
        writtenSemaphoreError = true;
      }
      return;
    }

    if (millis() - semaphoreWaitStartTime > 500) {
      Serial.print("X"+String(millis() - semaphoreWaitStartTime)+" ");
    }
    
    semaphoreWaitStartTime = 0;
    writtenSemaphoreError = false;

    if (currentDataInCamera == 0) {
      currentDataInCamera = read_fifo_length();

      if (currentDataInCamera >= 0x07ffff){
        currentDataInCamera = 0;
        Serial.println("Camera data length over size." + currentDataInCamera);
        return;
      } else if (currentDataInCamera == 0 ){
        Serial.println("Camera data length is 0.");
        return;
      }

      if (currentDataInCamera > buffer->maxSize())
        Serial.println("Image too big: "+String(currentDataInCamera));
      
      CS_LOW();
      set_fifo_burst();
  
      #if !(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM))
      // this is true for my OV2640_CAM; discards the one surplus 0-byte at the beginning
      SPI.transfer(0xFF);
      #endif
    }
    
    uint32_t maximumToCopy = _min(buffer->maxSize(), currentDataInCamera);
    while (currentlyCopied < maximumToCopy) {
      byte* bufferPointer = &((buffer->content())[currentlyCopied]);
      uint32_t bytesToCopyLeft = maximumToCopy - currentlyCopied;
      uint16_t copyNow = _min(2048, bytesToCopyLeft);

      SPI.transferBytes(bufferPointer, bufferPointer, copyNow);
      currentlyCopied += copyNow;

      // Don't copy in one go (would block for ie 30ms for 30kb - SPI 8Mhz)
      yield();
    }
      
    CS_HIGH();
    currentDataInCamera = 0;
    buffer->release(maximumToCopy, lastCaptureStart);
    hasCopySemaphore = false;
    copyActive = false;
  }
};

#endif
