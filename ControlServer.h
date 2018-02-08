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

#ifndef __CONTROL_SERVER_H__
#define __CONTROL_SERVER_H__
 
#include "DriveableServer.h"

class ControlServer : public DriveableServer
{
private:
  bool isHandling;
  
public:
  ControlServer(int port) : DriveableServer(port)
  {
    
  }

  void drive()
  {
    uint32_t methodStartTime = micros();
    
    DriveableServer::drive();

    if (micros() - methodStartTime > 1000)
      return;

    if (isHandling) {
      writeControlPage();
      isHandling = false;
    }
  }
  
protected:
  virtual bool shouldAccept(String requested)
  {
    return requested.startsWith("/ ") 
      || requested.startsWith("/left ")
      || requested.startsWith("/right ");
  }

  virtual String contentType(String requested)
  {
    return "text/html";
  }

  virtual void startHandling(String requested)
  {
    isHandling = true;
  }

private:
  void writeControlPage()
  {
    Serial.println("Writing control page");
    
    client.println("<html><body>");
    client.println("<div style=\"display: flex;\">");
    client.println("<div style=\"width: 10%; background: lightcyan\">L</div>");
    client.println("<iframe src=\"http://192.168.151.1:81\" style=\"width: 80%; height: 600px; border: none\"></iframe>");
    client.println("<div style=\"width: 10%; background: lightblue\">R</div>");
    client.println("</div>");
    client.println("</body></html>");
    client.println();

    client.flush();
    client.stop();
  }
};

#endif
