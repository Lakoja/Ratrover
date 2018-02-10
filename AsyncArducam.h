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

const int VCS = 5;
const int VSCK = 18;
const int VMISO = 19;
const int VMOSI = 23;

const int16_t OLDER_IS_TOO_OLD = 700; // only effective when idle

class AsyncArducam : public ArduCAM
{
private:
  bool cameraReady = false;
  uint32_t lastCaptureStart = 0;
  uint32_t lastCopyStart = 0;
  bool captureStarted = false;
  bool copyActive = false;
  bool hasCopySemaphore = false;
  uint32_t currentDataInCamera = 0;
  uint32_t currentlyCopied = 0;
  uint8_t ffsOnLine = 0;
  uint32_t semaphoreWaitStartTime = 0;
  
public:
  AsyncArducam(byte model) : ArduCAM(model, VCS)
  {
  }
  
  bool begin(uint8_t size)
  {
    Wire.begin();
  
    pinMode(VCS, OUTPUT);
  
    SPI.begin(VSCK, VMISO, VMOSI, VCS);
    SPI.setFrequency(8000000);
  
    //Check if the ArduCAM SPI bus is OK
    write_reg(ARDUCHIP_TEST1, 0x55);
    uint8_t temp = read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55){
      Serial.println("SPI1 interface Error!");
      return false;
    }
  
    //Check if the camera module type is OV2640; TODO? model is a parameter here
    uint8_t vid, pid;
    wrSensorReg8_8(0xff, 0x01);
    rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))) {
      Serial.println("Can't find OV2640 module!");
      return false;
    } else {
      Serial.println("OV2640 detected.");
    }
  
    set_format(JPEG);
    InitCAM();
    OV2640_set_JPEG_size(size);
    clear_fifo_flag();

    cameraReady = true;

    return true;
  }
  
  // Call repeatedly (from loop()). Will initiate capturing if last image too old. Will copy. Will not block
  void drive(SyncedMemoryBuffer* buffer, bool clientConnected)
  {
      if (!cameraReady)
        return;

      if (captureStarted && !isCaptureActive()) {
        int total_time = millis() - lastCaptureStart;
        Serial.print("C" + String(total_time) + " ");
        
        ffsOnLine++;
    
        if (++ffsOnLine % 20 == 0)
          Serial.println();
      
        initiateCopy();
      }
      
      if (copyActive)
        copyData(buffer);
      else if (!captureStarted && (clientConnected || lastCaptureStart == 0 || millis() - lastCaptureStart > OLDER_IS_TOO_OLD)) {
        initiateCapture();
      }
  }
  
private:
  void initiateCapture() 
  {
    if (captureStarted)
      return;
    
    captureStarted = true;
    lastCaptureStart = millis();
    
    clear_fifo_flag();
    start_capture();
  }

  void initiateCopy()
  {
    if (copyActive)
      return;
      
    captureStarted = false;
    copyActive = true;

    currentDataInCamera = 0;
    currentlyCopied = 0;

    lastCopyStart = millis();
  }

  bool isCaptureActive()
  {
    return !get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK);
  }

  void copyData(SyncedMemoryBuffer* buffer)
  {
    if (semaphoreWaitStartTime == 0)
      semaphoreWaitStartTime = millis();
      
    if (!hasCopySemaphore)
      hasCopySemaphore = buffer->take();

    if (!hasCopySemaphore)
      return;

    if (millis() - semaphoreWaitStartTime > 10)
      Serial.print("X"+String(millis() - semaphoreWaitStartTime));
    semaphoreWaitStartTime = 0;

    uint32_t methodStartTime = micros();

    if (currentDataInCamera == 0) {
      currentDataInCamera = read_fifo_length();

      if (currentDataInCamera >= 0x07ffff){
        currentDataInCamera = 0;
        Serial.println("Camera data length over size.");
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

      // NOTE this has the right result (time taken) even for an overflow of micros() ("negative" result)
      uint16_t passed = micros() - methodStartTime;
      if (passed >= 1000) {
        if (passed > 3000)
          Serial.print("!"+String(passed));

        return; // maintain system responsiveness
      }
    }
    
    //Serial.print('F');
    //Serial.print("F"+String(millis() - lastCopyStart));
    // This takes roughly 30ms for 30kb data (spi 8Mhz)
      
    CS_HIGH();
    currentDataInCamera = 0;
    buffer->release(maximumToCopy);
    hasCopySemaphore = false;
    copyActive = false;
  }
};

#endif
