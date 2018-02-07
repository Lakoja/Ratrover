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
  
public:
  SyncedMemoryBuffer()
  {
  }
  
  void setup()
  {
    // TODO this could be a double buffer (or two buffers) to parallelize transfers
    
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

  bool take()
  {
    return xSemaphoreTake(semaphore, 0) == pdTRUE;
  }

  void release(uint32_t dataLength = 0)
  {
    if (dataLength > 0) {
      currentContentSize = dataLength;
      currentTimestamp = millis();
    }
    
    xSemaphoreGive(semaphore);
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
};

#endif
