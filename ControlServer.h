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
#include "Motor.h"

class ControlServer : public DriveableServer
{
private:
  bool isWritingControlPage;
  Motor* motor;
  int imagePort;
  
public:
  ControlServer(Motor* m, int port) : DriveableServer(port)
  {
    motor = m;
  }

  void drive()
  {
    uint32_t methodStartTime = micros();
    
    DriveableServer::drive();

    if (micros() - methodStartTime > 1000)
      return;

    if (isWritingControlPage) {
      writeControlPage();
      isWritingControlPage = false;
    }
  }
  
protected:
  virtual bool shouldAccept(String requested)
  {
    return requested.startsWith("/ ") 
      || requested.startsWith("/left")
      || requested.startsWith("/right")
      || requested.startsWith("/fore")
      || requested.startsWith("/back")
      || requested.startsWith("/status ");
  }

  virtual String contentType(String requested)
  {
    return "text/html";
  }

  virtual void startHandling(String requested)
  {
    if (requested.startsWith("/left")) {
      float v = parseValue(requested.substring(5));
      motor->requestRight(v);
      Serial.println("Left requested "+String(v));
      writeOkAndClose();
    } else if (requested.startsWith("/right")) {
      float v = parseValue(requested.substring(6));
      motor->requestLeft(v);
      Serial.println("Right requested "+String(v));
      writeOkAndClose();
    }if (requested.startsWith("/fore")) {
      float v = parseValue(requested.substring(5));
      motor->requestForward(v);
      Serial.println("Forward requested "+String(v));
      writeOkAndClose();
    } else if (requested.startsWith("/back")) {
      float v = parseValue(requested.substring(5));
      motor->requestReverse(v);
      Serial.println("Reverse requested "+String(v));
      writeOkAndClose();
    } else if (requested.startsWith("/status ")) {
      Serial.println("Status requested");
      writeOkAndClose();
    } else {
      isWritingControlPage = true;
    }
  }

private:
  float parseValue(String requestValueString)
  {
      long v = requestValueString.toInt();
      return v / 1000.0f;
  }

  void writeOkAndClose()
  {
    client.println("<body>Ok</body>");
    client.println();
    client.flush();
    client.stop();
  }
  
  void writeControlPage()
  {
    Serial.println("Writing control page");
    
    client.println("<html><head><meta charset=\"utf-8\"/></head><body>");
    client.println(
      "<div style='display: flex;'>\n"
      "  <div style='display: flex; flex-direction: column; flex-grow: 1'>\n"
      "    <div data-control='left' style='flex-grow: 1; min-width: 150px; background: lightcyan; justify-content: center'>L</div>\n"
      "    <div data-control='back' style='flex-grow: 1; min-width: 150px; background: lightcoral'>B</div>\n"
      "  </div>\n"
      "  <iframe src='' style='width: 800px; height: 600px; border: none'></iframe>\n"
      "  <div style='display: flex; flex-direction: column; flex-grow: 1'>\n"
      "    <div data-control='fore' style='flex-grow: 1; min-width: 150px; background: lightgreen'>F</div>\n"
      "    <div data-control='right' style='flex-grow: 1; min-width: 150px; background: lightblue'>R</div>\n"
      "  </div>\n"
      "</div>\n"
    );
    client.println("<script>");
    client.println(
      "var onePlusPort = (location.port == 0 ? 80 : location.port) + 1;\n"
      "document.querySelectorAll('iframe')[0].setAttribute('src', 'http://'+window.location.hostname+':'+onePlusPort);\n"
      "var requestActive = false;\n"
      "var controlClickFunc = function(event) {\n"
      "  if (!requestActive) {\n"
      "    requestActive = true;\n"
      "    var controlWord = event.target.getAttribute('data-control');\n"
      "    console.log('Clicked for '+controlWord);\n"
      "    var xhttp = new XMLHttpRequest();\n"
      "    xhttp.onreadystatechange = function() {\n"
      "        if (this.readyState === 4) {\n"
      "            console.log('Result got '+this.status);\n"
      "            requestActive = false;\n"
      "        }\n"
      "    };\n"
      "    xhttp.open('GET', '/'+controlWord, true);\n"
      "    xhttp.send();\n"
      "  } else {\n"
      "    console.log('Not sent request');\n"
      "  }\n"
      "}\n"
      "var controls = document.querySelectorAll('[data-control]');\n"
      "controls.forEach(function(element) {\n"
      "    element.addEventListener('click', controlClickFunc);\n"
      "});\n"
    );
    client.println("</script>");
    client.println("</body></html>");
    client.println();

    client.flush();
    client.stop();
  }
};

#endif
