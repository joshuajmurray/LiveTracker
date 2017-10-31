#include "Adafruit_FONA.h"

#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4

char replybuffer[255];// this is a large buffer for replies

#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

uint8_t type;

void setup() {
  while (!Serial);

  Serial.begin(115200);
  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }

  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));
  switch (type) {
    case FONA808_V2:
      Serial.println(F("FONA 808 (v2)")); break;
    default: 
      Serial.println(F("???")); break;
  }
  
  char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!  // Print module IMEI number.
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0) {
    Serial.print("Module IMEI: "); Serial.println(imei);
  }

  Serial.println(F("-------------------------------------"));

  // read the battery voltage and percentage
  uint16_t vbat;
  if (! fona.getBattVoltage(&vbat)) {
    Serial.println(F("Failed to read Batt"));
  } else {
    Serial.print(F("VBat = ")); Serial.print(vbat); Serial.println(F(" mV"));
  }


  if (! fona.getBattPercent(&vbat)) {
    Serial.println(F("Failed to read Batt"));
  } else {
    Serial.print(F("VPct = ")); Serial.print(vbat); Serial.println(F("%"));
  }
//  delay(10000);
  while(!fona.enableGPRS(true));//enables post ability and cell based location services
  while(!fona.enableGPS(true));//enable gps module
}

void loop() {
  int8_t stat;
  stat = fona.GPSstatus();
  if (stat < 0)
    Serial.println(F("Failed to query"));
  if (stat == 0) Serial.println(F("GPS off"));
  if (stat == 1) Serial.println(F("No fix"));
  if (stat == 2) Serial.println(F("2D fix"));
  if (stat == 3) Serial.println(F("3D fix"));

  uint16_t vbat;
  if (! fona.getBattPercent(&vbat)) {
    Serial.println(F("Failed to read Batt"));
  } else {
    Serial.print(F("VPct = ")); Serial.print(vbat); Serial.println(F("%"));
  }
  delay(10000);

// Post data to website http://wilsonja.pythonanywhere.com/
  uint16_t statuscode;
  int16_t length;
  char url[160] = "http://wilsonja.pythonanywhere.com/?ID=123456789&lat=999&lon=777";
  char data[80];
  flushSerial();
  //readline(url, 79);
  //readline(data, 79);
  
  fona.HTTP_POST_start(url, F("text/plain"), (uint8_t *) data, strlen(data), &statuscode, (uint16_t *)&length);
  while (length > 0) {
    while (fona.available()) {
      char c = fona.read();
  
      #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
      loop_until_bit_is_set(UCSR0A, UDRE0); // Wait until data register empty.
      UDR0 = c;
      #else
        Serial.write(c);
      #endif
  
      length--;
      if (! length) break;
    }
  }
  fona.HTTP_POST_end();
  delay(10000);
}

void flushSerial() {
  while (Serial.available())
    Serial.read();
}

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout) {
  uint16_t buffidx = 0;
  boolean timeoutvalid = true;
  if (timeout == 0) timeoutvalid = false;

  while (true) {
    if (buffidx > maxbuff) {
      //Serial.println(F("SPACE"));
      break;
    }

    while (Serial.available()) {
      char c =  Serial.read();

      //Serial.print(c, HEX); Serial.print("#"); Serial.println(c);

      if (c == '\r') continue;
      if (c == 0xA) {
        if (buffidx == 0)   // the first 0x0A is ignored
          continue;

        timeout = 0;         // the second 0x0A is the end of the line
        timeoutvalid = true;
        break;
      }
      buff[buffidx] = c;
      buffidx++;
    }

    if (timeoutvalid && timeout == 0) {
      //Serial.println(F("TIMEOUT"));
      break;
    }
    delay(1);
  }
  buff[buffidx] = 0;  // null term
  return buffidx;
}
