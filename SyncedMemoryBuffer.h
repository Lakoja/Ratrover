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

#ifndef __SYNCED_MEMORY_BUFFER_H__
#define __SYNCED_MEMORY_BUFFER_H__

const uint32_t BUFFER_SIZE = 50000;

class SyncedMemoryBuffer
{
private:
  byte* buffer;
  uint32_t maxBufferSize = 0;
  uint32_t currentTimestamp = 0;
  uint32_t currentContentSize = 0;
  SemaphoreHandle_t semaphore;
  String currentOwner = "";
  
public:
  SyncedMemoryBuffer()
  {
  }
  
  void setup()
  {
    buffer = (byte *)malloc(BUFFER_SIZE);
    maxBufferSize = BUFFER_SIZE;
    memset(buffer, 0, BUFFER_SIZE);
    buffer[0] = 0xff;
    
    semaphore = xSemaphoreCreateMutex();
  }

  uint32_t maxSize()
  {
    return maxBufferSize;
  }

  bool take(String taker)
  {
    bool ok = xSemaphoreTake(semaphore, 0) == pdTRUE;
    if (ok) {
      currentOwner = taker;
    }
    return ok;
  }

  void release(uint32_t dataLength = 0, uint32_t timestamp = 0)
  {
    if (dataLength > 0) {
      currentContentSize = dataLength;

      if (timestamp == 0) {
        currentTimestamp = millis();
      } else {
        currentTimestamp = timestamp;
      }
    }

    currentOwner = "";
    
    xSemaphoreGive(semaphore);
  }

  void copyTo(SyncedMemoryBuffer *other)
  {
    memcpy(other->buffer, buffer, currentContentSize);
    other->currentContentSize = currentContentSize;
    other->currentTimestamp = currentTimestamp;
  }

  String getTaker()
  {
    return currentOwner;
  }

  byte* content()
  {
    // TODO check with semaphore??
    return buffer;
  }

  uint32_t timestamp()
  {
    return currentTimestamp;
  }

  uint32_t contentSize()
  {
    return currentContentSize;
  }

  bool hasContent()
  {
    return currentContentSize > 0;
  }
};

#endif
