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

#ifndef __RTOS_TASK_WRAPPER_H__
#define __RTOS_TASK_WRAPPER_H__

class Task
{

private:
  xTaskHandle taskHandle;

public:
  void start()
  {
    // TODO what is a reasonable stack size?
    ::xTaskCreate(&runTask, "Me", 1000, this, 1, &taskHandle);
    // TODO could also use ..PinnedToCore(... tskNO_AFFINITY);
  }

  virtual void run()
  {
  }
  
protected:
  void delay(uint16_t ms) {
    ::vTaskDelay(ms / portTICK_PERIOD_MS);
  }

private:
  static void runTask(void *pTaskInstance)
  {
    Task* pTask = (Task*)pTaskInstance;
    pTask->run();
    pTask->cleanup();
  }

  void cleanup()
  {
    ::vTaskDelete(taskHandle);
  }
};

#endif

