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
 
#include <ArduCAM.h>
#include <Wire.h>
#include <SPI.h>
#include "memorysaver.h"
#include <math.h>

const int VCS = 5;
const int VSCK = 18;
const int VMISO = 19;
const int VMOSI = 23;

const int16_t OLDER_IS_TOO_OLD = 700;
const int32_t BUFFER_SIZE = 50000;

ArduCAM arduCam(OV2640, VCS);

class AsyncArducam
{
private:
  byte* buffer;
  uint32_t lastCaptureStart = 0;
  bool captureStarted = false;
  bool copyingActive = false;
  int32_t bufferContent = 0;
  
  void doCapture() 
  {
    //Serial.print("D");
    if (captureStarted)
      return;
    //Serial.print("E");
    
    captureStarted = true;
    lastCaptureStart = millis();
    
    arduCam.clear_fifo_flag();
    arduCam.start_capture();
  }

  bool isCaptureActive()
  {
    return !arduCam.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK) || copyingActive;
  }

  void copyData()
  {
    size_t dataLength = arduCam.read_fifo_length();
    if (dataLength >= 0x07ffff){
      Serial.println("Over size.");
      return;
    } else if (dataLength == 0 ){
      Serial.println("Size is 0.");
      return;
    }

    copyingActive = true;
    
    arduCam.CS_LOW();
    arduCam.set_fifo_burst();

    #if !(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM))
    // this is true for my OV2640_CAM; discards the one surplus 0-byte at the beginning
    SPI.transfer(0xFF);
    #endif

    bufferContent = 0;
    buffer[0] = 0xff;
    size_t dataToCopy = dataLength;
    
    while (dataToCopy > 0 && BUFFER_SIZE - bufferContent > 0) {
      size_t copyNow = _min(4096, _min(dataToCopy, BUFFER_SIZE - bufferContent));
      SPI.transferBytes(&buffer[bufferContent], &buffer[bufferContent], copyNow);
      
      dataToCopy -= copyNow;
      bufferContent += copyNow;
    }

    arduCam.CS_HIGH();
    copyingActive = false;
  }
  
public:
  AsyncArducam()
  {
    buffer = (byte *)malloc(BUFFER_SIZE);
    memset(buffer, 0, BUFFER_SIZE);
    buffer[0] = 0xff;
  }

  // Call repeatedly (from loop()). Will initiate capturing if last one too old.
  void drive()
  {
      if (!captureStarted && (lastCaptureStart == 0 || millis() - lastCaptureStart > OLDER_IS_TOO_OLD)) {
        doCapture();
      }

      if (captureStarted && !isCaptureActive()) {
        int total_time = millis() - lastCaptureStart;
        //Serial.print("C" + String(total_time) + " ");

        int time1 = millis();
        copyData();
        int time2 = millis();

        //Serial.print("M" + String(time2-time1) + " ");
        
        captureStarted = false;
      }
  }

  bool begin(uint8_t size)
  {
    Wire.begin();
  
    pinMode(VCS, OUTPUT);
  
    SPI.begin(VSCK, VMISO, VMOSI, VCS);
    SPI.setFrequency(8000000);
  
    //Check if the ArduCAM SPI bus is OK
    arduCam.write_reg(ARDUCHIP_TEST1, 0x55);
    uint8_t temp = arduCam.read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55){
      Serial.println("SPI1 interface Error!");
      return false;
    }
  
    //Check if the camera module type is OV2640
    uint8_t vid, pid;
    arduCam.wrSensorReg8_8(0xff, 0x01);
    arduCam.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    arduCam.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))) {
      Serial.println("Can't find OV2640 module!");
      return false;
    } else {
      Serial.println("OV2640 detected.");
    }
  
    arduCam.set_format(JPEG);
    arduCam.InitCAM();
    arduCam.OV2640_set_JPEG_size(size);
    arduCam.clear_fifo_flag();

    return true;
  }

  void transferCapture(WiFiClient client)
  {
    int time1 = millis();
    while (captureStarted) {
      delay(1);
      drive();
    }
    doCapture();
    int time2 = millis();

    if (time2 - time1 > 10) {
      Serial.print("W" + String(time2-time1) + " ");
    }

    if (bufferContent == 0) {
      Serial.println();
      Serial.println("BUFFER empty!!!");
      delay(5000);
    }
  
    if (!client.connected()) return;

    int time3 = millis();
    client.println("Content-Type: image/jpeg");
    client.println("Content-Length: " + String(bufferContent));
    //client.println("Refresh: 5");
    //client.println("Connection: close");
    client.println();

    uint32_t written = 0;
    while (written < bufferContent) {
      if (written > 0)
        Serial.print("+");
      written += client.write(&buffer[0], bufferContent);
    }
  
    client.println();

    int time4 = millis();
    Serial.print("Z"+String(time4-time3)+" ");
  }
};
