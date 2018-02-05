#include <ArduCAM.h>
#include <Wire.h>
#include <SPI.h>
#include "memorysaver.h"

const int VCS = 5;
const int VSCK = 18;
const int VMISO = 19;
const int VMOSI = 23;

// This (extension?) does not work: There will be an SPI transfer/connection problem then.
class AsyncArducam : public ArduCAM
{
public:
  AsyncArducam(byte model)
  {
    ArduCAM(model, VCS);
  }
  
  bool begin(uint8_t size)
  {
    Wire.begin();
  
    pinMode(VCS, OUTPUT);
  
    SPI.begin(VSCK, VMISO, VMOSI, VCS);
    SPI.setFrequency(4000000);
  
    //Check if the ArduCAM SPI bus is OK
    write_reg(ARDUCHIP_TEST1, 0x55);
    uint8_t temp = read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55){
      Serial.println("SPI1 interface Error!");
      return false;
    }
  
    //Check if the camera module type is OV2640
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

    return true;
  }

  void doCapture() 
  {
    clear_fifo_flag();
    start_capture();
  
    int total_time = millis();
    
    while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
    
    total_time = millis() - total_time;
    Serial.println("Capture total_time used: " + String(total_time) + "ms");
  }

  void transferCapture(WiFiClient client)
  {
    size_t dataLength = read_fifo_length();
    if (dataLength >= 0x07ffff){
      Serial.println("Over size.");
      return;
    } else if (dataLength == 0 ){
      Serial.println("Size is 0.");
      return;
    }
    
    CS_LOW();
    set_fifo_burst();
  
    if (!client.connected()) return;
  
    static const size_t bufferSize = 4096;
    static uint8_t buffer[bufferSize] = {0xFF};
  
    int total_time = millis();
  
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    String lengthLine = "Content-Length: " + String(dataLength);
    //client.println(lengthLine); // TODO not correct see below
    //client.println("Refresh: 5");
    client.println("Connection: close");
    client.println();
  
    bool first = true;
    bool corrected = false;
    size_t dataToCopy = dataLength;
    while (dataToCopy) {
      size_t copyNow = (dataToCopy < bufferSize) ? dataToCopy : bufferSize;
      SPI.transferBytes(&buffer[0], &buffer[0], copyNow);
      if (!client.connected()) break;
  
      if (first && copyNow > 0 && buffer[0] == 0) {
        client.write(&buffer[1], copyNow-1); // TODO first byte being 0 is an illegal image
        corrected = true;
      } else {
        client.write(&buffer[0], copyNow);
      }
      dataToCopy -= copyNow;
  
      first = false;
    }
  
    client.println();
    
    total_time = millis() - total_time;
  
    CS_HIGH();
  
    Serial.println("Transfer finished "+String(total_time)+"ms. For "+String(dataLength)+" bytes.");
  }
};

