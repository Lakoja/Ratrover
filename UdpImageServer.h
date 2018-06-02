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

#ifndef __UDP_IMAGE_SERVER_H__
#define __UDP_IMAGE_SERVER_H__
 
#include <WiFiUdp.h>
#include "SyncedMemoryBuffer.h"
#include "ContinuousControl.h"
#include <math.h>

#define DATA_SIZE 1200 // NOTE does not work for smaller sizes (ie 500 bytes: 6x as long transfer time...)

class UdpImageServer : public WiFiUDP
{
private:
  uint16_t udpPort;
  uint32_t lastSentTimestamp = 0;
  uint8_t receiveBuffer[101];
  uint32_t sentPackets = 0;
  uint32_t errorPackets = 0;
  uint32_t lastSentPacketsOut = 0;
  ContinuousControl *control = NULL;
  
public:
  UdpImageServer(uint16_t port, ContinuousControl *cont) : WiFiUDP()
  {
    udpPort = port;
    control = cont;
  }

  void begin() 
  {
    WiFiUDP::begin(WiFi.localIP(), udpPort);

    // TODO does not work; remove in WiFiUdp.cpp?
    //int size = getSendBufferSize();
    //Serial.println("UDP Send buffer size "+String(size)+" "+String(size < 0 ? errno : 0));
  }

  void drive(SyncedMemoryBuffer* imageDataOne, SyncedMemoryBuffer* imageDataOther)
  {
    if (WiFi.softAPgetStationNum() == 0) {
      return;
    }

    int len = parsePacket();

    bool packetSentAlready = false;
    if (len > 0) {
      if (len < sizeof(receiveBuffer) - 1 && len > 2) {
        memset(receiveBuffer, 0, sizeof(receiveBuffer)); // esp null-terminates data...
        read(receiveBuffer, len);
        
        if (receiveBuffer[0] == 'M' && receiveBuffer[1] == 'N' && (len == 8 || len == 10 || len == 12)) {
          bool hasSecondPacket = len == 10;
          bool hasThirdPacket = len == 12;
        
          Serial.print("Got rerequest ");
  
          uint32_t missingTimestamp = (receiveBuffer[2] << 24) & 0xff000000 | (receiveBuffer[3] << 16) & 0xff0000 | (receiveBuffer[4] << 8) & 0xff00 | receiveBuffer[5] & 0xff;
          uint16_t missing1 = (receiveBuffer[6] << 8) & 0xff00 | receiveBuffer[7] & 0xff;
          uint16_t missing2 = 0;
          uint16_t missing3 = 0;
          if (hasSecondPacket) {
            missing2 = (receiveBuffer[8] << 8) & 0xff00 | receiveBuffer[9] & 0xff;
          }
          if (hasThirdPacket) {
            missing3 = (receiveBuffer[10] << 8) & 0xff00 | receiveBuffer[11] & 0xff;
          }
  
          SyncedMemoryBuffer* imageData = NULL;
          
          if (imageDataOne->hasContent() && imageDataOne->timestamp() == missingTimestamp) {
            imageData = imageDataOne;
          } else if (imageDataOther->hasContent() && imageDataOther->timestamp() == missingTimestamp) {
            imageData = imageDataOther;
          }
  
          if (imageData != NULL) {
            packetSentAlready = true;
            writePacket(missing1, imageData);
            // TODO
            delay(2);
            Serial.print("Sent again "+String(missingTimestamp)+": "+String(missing1));
            if (hasSecondPacket) {
              writePacket(missing2, imageData);
              delay(2);
              Serial.print(" "+String(missingTimestamp)+": "+String(missing2));
            }
            if (hasThirdPacket) {
              writePacket(missing3, imageData);
              delay(2);
              Serial.print(" "+String(missingTimestamp)+": "+String(missing3));
            }
          } else {
            Serial.print("No repair data buffer found "+String(missingTimestamp)+" ex "+String(imageDataOne->timestamp())+" or "+(imageDataOther->timestamp()));
          }
  
          Serial.println();
        } else if (receiveBuffer[0] == 'C' && receiveBuffer[1] == 'T') {
          String requested = String((char *)&(receiveBuffer[2]));  
        
          Serial.println("Got control request "+requested);

          if (control->supports(requested)) {
            String returnValue = control->handle(requested);

          } else {
            Serial.println("!!!! Did no understand control command");
          }
        }
      } else {
        Serial.println("!!!!! Received packet with wrong length "+String(len));
        flush();
      }
    } else {
      if (errno != 0 && errno != EAGAIN) {
        Serial.println("!!!! Error on receive "+String(errno));
      }
    }

    if (!packetSentAlready) {
      if (!imageDataOne->hasContent() && !imageDataOther->hasContent()) {
        return;
      }

      bool oneIsNewer = imageDataOne->hasContent() && imageDataOne->timestamp() >= imageDataOther->timestamp();
      
      SyncedMemoryBuffer* imageData = oneIsNewer ? imageDataOne : imageDataOther;

      uint32_t t1 = millis();
      if (imageData->timestamp() > lastSentTimestamp) {
        uint16_t packetCountTotal = ceil(imageData->contentSize() / (float) DATA_SIZE);

        //Serial.print("+ of"+String(imageData->contentSize())+"c"+String(packetCountTotal)+" ");

  
        for (uint16_t num = 0; num < packetCountTotal; num++) {
          writePacket(num, imageData);
          // TODO return after some time?
          // TODO use a delay/yield here?
        }
        
        //Serial.println("e");

        lastSentTimestamp = imageData->timestamp();
      }
      
      uint32_t t2 = millis();

      if (sentPackets - lastSentPacketsOut > 400) {
        float kbps = (imageData->contentSize() / 1024.0f) / ((t2-t1) / 1000.0f);
        Serial.print("S"+String(t2-t1)+" ");//+"ms "+String(kbps,1)+"kbps ");
        Serial.println(" Sent "+String(sentPackets)+"e"+String(errorPackets)+" free "+ESP.getFreeHeap());
        lastSentPacketsOut = sentPackets;
      }
    }
  }

  String getState()
  {
    return String(sentPackets);
  }
private:
  void writePacket(uint16_t packetNumber, SyncedMemoryBuffer* imageData) 
  {
    uint16_t packetCountTotal = ceil(imageData->contentSize() / (float) DATA_SIZE);
    if (packetNumber >= packetCountTotal) {
      Serial.println("!!!! Trying to write illegal packet of image data size "+String(packetNumber)+" vs "+String(imageData->contentSize()));
      return;
    }
    
    // TODO use non-broadcast address?
    
    beginPacket("192.168.151.255", udpPort);
    print("RI");

    uint32_t timestamp = imageData->timestamp();
    
    write((byte)(timestamp >> 24));
    write((byte)(timestamp >> 16));
    write((byte)(timestamp >> 8));
    write((byte)(timestamp));
    write((byte)(packetNumber >> 8));
    write((byte)(packetNumber));
    write((byte)(packetCountTotal >> 8));
    write((byte)(packetCountTotal));

    uint32_t byteStart = packetNumber * DATA_SIZE;
    uint16_t byteCount = _min(imageData->contentSize() - byteStart + 1, DATA_SIZE);
    byte* bufferPointer = &((imageData->content())[byteStart]);
    write(bufferPointer, byteCount);

    int retryCounter = 0;
    int sendSuccess = 0;
    do {
      if (retryCounter > 0) {
        delayMicroseconds(500);
      }
      sendSuccess = endPacket();
    } while (sendSuccess == 0 && errno == ENOMEM && ++retryCounter <= 30);

    if (sendSuccess == 0) {
      //Serial.println("Error sending packet "+String(errno));

      errorPackets++;
    }
  
    sentPackets++;
  }
};

#endif
