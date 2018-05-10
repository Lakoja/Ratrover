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
 
#include <WiFi.h>
#include <WiFiServer.h>

#include "AsyncArducam.h"
//#include "ImageServer.h"
#include "ContinuousControl.h"
//#include "Motor.h"
#include "StepperMotors.h"
//#include "MotorWatcher.h"
#include "SyncedMemoryBuffer.h"

const int LED2 = 16;

const int CHANNEL = 1;

// first pin must be the one for "forward"
const uint8_t MOTOR_R1 = 33;
const uint8_t MOTOR_R2 = 32;
const uint8_t MOTOR_I_R = 35;
const uint8_t MOTOR_L1 = 25;
const uint8_t MOTOR_L2 = 26;
const uint8_t MOTOR_I_L = 27;
const uint16_t MOTOR_UMIN_MAX = 56;
const uint16_t MOTOR_REDUCTION = 298;


const uint8_t MOTOR_L_STEP = 32;
const uint8_t MOTOR_L_DIR = 33;
const uint8_t MOTOR_L_SLEEP = 14;

const uint8_t MOTOR_R_STEP = 26;
const uint8_t MOTOR_R_DIR = 27;
const uint8_t MOTOR_R_SLEEP = 25;
/*
const uint8_t MOTOR_R_STEP = 25;
const uint8_t MOTOR_R_DIR = 26;
const uint8_t MOTOR_R_SLEEP = 27;
*/
const uint16_t MOTOR_MAX_RPM = 100; // for steppers this can be higher (and weaker)
const uint16_t MOTOR_RESOLUTION = 800; // steps per rotation; this assumes a sub-step sampling (drv8834) of 4


SyncedMemoryBuffer cameraBuffer;
SyncedMemoryBuffer serverBuffer;
//volatile uint32_t MotorWatcher::counterR = 0;
//volatile uint32_t MotorWatcher::counterL = 0;
StepperMotors motor;
//ImageServer imageServer(81);
WiFiServer server(80);
WiFiClient client;
ContinuousControl controlServer(&motor, NULL, 81);
AsyncArducam camera;
bool cameraValid = true;
bool waitForRequest = false;
bool transferActive = false;
uint32_t transferredThisSecond = 0;
uint32_t lastTransferOutMillis = 0;
uint32_t clientConnectTime = 0;
String currentLine = "";
uint32_t lastSuccessfulImageCopy = 0;
bool llWarning = false;

void setup() 
{
  Serial.begin(115200);
  Serial.println("Rover start!");

  //outputPin(IRLED2); // TODO use an analog output? (not so big a resistor/power loss needed)

  //motor.setup(MOTOR_R1, MOTOR_R2, MOTOR_I_R, MOTOR_L1, MOTOR_L2, MOTOR_I_L, MOTOR_UMIN_MAX, MOTOR_REDUCTION);

  motor.setup(MOTOR_R_STEP, MOTOR_R_DIR, MOTOR_R_SLEEP, MOTOR_L_STEP, MOTOR_L_DIR, MOTOR_L_SLEEP, MOTOR_MAX_RPM, MOTOR_RESOLUTION);
  
  cameraBuffer.setup();
  serverBuffer.setup();

  // NOTE this breaks voltage metering on pin 27 (=ADC2)...
  if (!setupWifi()) {
    while(1);
  }

  server.begin();
  
  if (!camera.setup(OV2640_800x600, &cameraBuffer)) {  // OV2640_320x240, OV2640_1600x1200, 
    cameraValid = false;
  }

  controlServer.begin();

  if (cameraValid) {
    outputPin(LED2);
    digitalWrite(LED2, HIGH);
    camera.start("cam", 2, 4000);
  } else {
    // empty test data
    serverBuffer.take("test");
    serverBuffer.release(27000);
  }
  //imageServer.setup(&serverBuffer, !cameraValid, NULL);

  motor.start("motor", 5);
  controlServer.start("control", 4);
  //imageServer.start("image", 3);

  //motor.requestForward(0.16, 30000);
  //motor.requestMovement(0, 1, 10000);
  
  Serial.println("Waiting for connection to our webserver...");
}

void outputPin(int num)
{
  digitalWrite(num, LOW);
  pinMode(num, OUTPUT);
  digitalWrite(num, LOW);
}

bool setupWifi()
{
  WiFi.mode(WIFI_AP);
  
  bool b1 = WiFi.softAP("Roversnail", NULL, CHANNEL, 0, 1); // TODO scan continuum for proper channel?
  delay(100);
  bool b2 = WiFi.softAPConfig(IPAddress(192,168,151,1), IPAddress(192,168,151,254), IPAddress(255,255,255,0));


  if (b1 && b2) {
    Serial.print("WiFi AP started ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Could not start AP. config: "+String(b1)+" start:"+String(b2));
    return false;
  }

  return true;
}

void loop()
{
  //camera.drive(&cameraBuffer);
  
  if (!client.connected()) {
    client = server.accept();

    if (client.connected()) {
      Serial.println("Client connect");
      clientConnectTime = millis();
      waitForRequest = true;
      transferActive = false;
      currentLine = "";
    }
  }

  if (client.connected()) {
    if (waitForRequest) {
      String requested = parseRequest();

      if (requested.length() > 0) {
        waitForRequest = false;
        
        if (requested.startsWith("/ ")) {
          String responseHeader = "HTTP/1.1 200 OK\n";
          //responseHeader += "Content-Type: image/jpeg\n";
          responseHeader += "Content-Type: multipart/x-mixed-replace; boundary=frame\n";
          responseHeader += "\n";
          client.print(responseHeader);

          Serial.println("Serving new client the HTTP response header for"+requested);

          transferActive = true;
        } else {
          Serial.println("Ignoring request "+requested);
          
          client.println("HTTP/1.1 404 Not Found");
          client.println();
          client.stop();
        }
      }
    }

    copyImage();

    if (transferActive && serverBuffer.contentSize() > 0) {
      String imageHeader = "--frame\n";
      imageHeader += "Content-Type: image/jpeg\n";
      imageHeader += "Content-Length: ";
      imageHeader += String(serverBuffer.contentSize());
      imageHeader += "\n\n";
      client.print(imageHeader); // Print as one block - will also work ok with setNoDelay(true)

      uint32_t currentlyTransferred = 0;
      uint32_t blockStart = millis();

      while(currentlyTransferred < serverBuffer.contentSize()) {
        byte* bufferPointer = &((serverBuffer.content())[currentlyTransferred]);
        uint32_t transferredNow = client.write(bufferPointer, serverBuffer.contentSize() - currentlyTransferred);
        transferredThisSecond += transferredNow;
        currentlyTransferred += transferredNow;
      }
      client.println();
      client.flush();
      
      uint32_t now = millis();

      if (now - lastTransferOutMillis >= 1000) {
        double transferKbps = (transferredThisSecond / ((now - lastTransferOutMillis) / 1000.0)) / 1024.0;
        Serial.println(String(transferKbps)+" "+String(now - blockStart));
      
        transferredThisSecond = 0;
        lastTransferOutMillis = now;
      }

      if (now - clientConnectTime > 120000L) {
        client.stop();
        Serial.println("Test stop");
        transferActive = false;
      }
    }
  }
}

String parseRequest()
{
  if (!waitForRequest)
    return "";

  if (millis() - clientConnectTime > 2000) {
    waitForRequest = false;
    client.stop();
    Serial.println("Waited too long for a client request. Current line content: "+currentLine);
  }
  
  uint32_t methodStartTime = esp_timer_get_time();

  // TODO simplify time check
  while (client.connected() && esp_timer_get_time() - methodStartTime < 2000) {
    if (client.available() == 0)
      delayMicroseconds(200);

    while (client.available() > 0 && esp_timer_get_time() - methodStartTime < 2000) {
      /* This does not read past the first line and results in a "broken connection" in Firefox
      String oneRequestLine = client.readStringUntil('\n');
      Serial.println(oneRequestLine);
      if (currentLine.length() == 0) {
        break;
      }*/
      
      char c = client.read();
      if (c == '\n') {
        if (currentLine.length() == 0) {
          Serial.print("B");
          break;
        } else {
          if (currentLine.startsWith("GET ")) {
            return currentLine.substring(4);
          }
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }

  return "";
}

void copyImage()
{
  if (!cameraValid) {
    return;
  }

  if (!cameraBuffer.take("main", 15 / portTICK_PERIOD_MS)) {
    return;
  }
  
  if (cameraBuffer.timestamp() > serverBuffer.timestamp()) {
    cameraBuffer.copyTo(&serverBuffer);
    
    lastSuccessfulImageCopy = millis();
  }

  cameraBuffer.release();
}


void copyImageComplex()
{
  if (!cameraValid) {
    return;
  }
  
  if (cameraBuffer.timestamp() > serverBuffer.timestamp()) {
    bool cameraLock = cameraBuffer.take("main");
    bool serverLock = serverBuffer.take("main");

    if (!cameraLock || !serverLock) {
      uint32_t now = millis();
      if (!llWarning && now - lastSuccessfulImageCopy > 3000) {
        llWarning = true;
        Serial.print("LL cannot "+String(cameraLock)+String(serverLock)+" ");
      }
    }
    
    if (cameraLock && serverLock) {
      cameraBuffer.copyTo(&serverBuffer);
      //Serial.print("O ");
      
      uint32_t now = millis();
      if (now - lastSuccessfulImageCopy > 3000) {
        Serial.print("LL"+String(now - lastSuccessfulImageCopy)+" ");
        llWarning = false;
      }
      
      lastSuccessfulImageCopy = now;
    }

    if (cameraLock) {
      cameraBuffer.release();
    }
    
    if (serverLock) {
      serverBuffer.release();
    }
  } else {
    uint32_t now = millis();
    if (!llWarning && now - lastSuccessfulImageCopy > 3000) {
      llWarning = true;
      Serial.print("LL nothing "+String(cameraBuffer.timestamp())+"vs"+String(serverBuffer.timestamp())+" ");
    }
  }
}

void loopComplex() 
{
  uint32_t loopStart = millis();

  bool wifiHasClient = WiFi.softAPgetStationNum() > 0;

  if (cameraValid && camera.isReady()) {
    //camera.inform(imageServer.clientConnected());
  }

  copyImage();

  //controlServer.inform(wifiHasClient);
  //imageServer.inform(wifiHasClient);

  int32_t sleepNow = 2 - (millis() - loopStart);
  if (sleepNow >= 0)
    delay(sleepNow);
  else
    yield();
}
