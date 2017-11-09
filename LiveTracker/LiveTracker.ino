#include <Adafruit_FONA.h>
#include <EEPROM.h>

#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4
#define POST_URL "http://wilsonja.pythonanywhere.com/?"
#define TEST_MODE true
//#define TEST_MODE false
#define LOOP_TIME 1000
#define POST_TIME 20000
#define TRIP_MEM_LOCATION 0

//state machine states
#define INIT_STATE 0
#define ERROR_STATE 1
#define GET_DATA_STATE 2
#define WAIT_STATE 3
#define POST_STATE 4

char replybuffer[255];// this is a large buffer for replies

#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
char gpsdata[120];
int8_t stat;
String temp;
uint8_t type;

struct TRACKER_DATA {
  String id;
  String ts;//time stamp
  String lat;
  String lon;
  String alt;
  String spd;
  String gps_sig;
  String cell_sig;
  String batt;
  String trp;
};
TRACKER_DATA postData; 

int state;
unsigned long loopTimer;
unsigned long postTimer;

void setup() {
  while (!Serial);

  state = INIT_STATE;
  loopTimer = 0;
  postTimer = 0;
  
  Serial.begin(115200);
  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  postData.id = "0";
  postData.ts = "0";
  postData.lat = "0";
  postData.lon = "0";
  postData.alt = "0";
  postData.spd = "0";
  postData.gps_sig = "0";
  postData.cell_sig = "0";
  postData.batt = "0";

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
  while(!fona.enableGPRS(true));//enables post ability and cell based location services
  while(!fona.enableGPS(true));//enable gps module
  postData.id = imei;
  byte val = (byte)EEPROM.read(TRIP_MEM_LOCATION);
  postData.trp = val++; //increment the trip on powerup
  EEPROM.write(TRIP_MEM_LOCATION, val);//write back to eeprom (it's only 1 byte!!!!!!!!!!!!!!!!!!!!!!!!!!!!)
}

void loop() {

  delay(100);

  switch(state) {
    case INIT_STATE://*************************************************************************
      Serial.println("INIT STUFF");
//      init stuff here
      state = WAIT_STATE;
      break;
    case ERROR_STATE://*************************************************************************
      Serial.println("ERROR");

      state = INIT_STATE;
      break;
    case GET_DATA_STATE://*************************************************************************
//      Serial.println("GET INFO");
      fona.getGPS(0, gpsdata, 120);
      stat = fona.GPSstatus();
      postData.gps_sig = stat;
//      switch(stat) {
//        case 0:
//          Serial.println(F("GPS off"));
//          break;
//        case 1:
//          Serial.println(F("No fix"));
//          break;
//        case 2:
//          Serial.println(F("2D fix"));
//          break;
//        case 3:
//          Serial.println(F("3D fix"));
//          break;
//        default:
//          Serial.println(F("Failed to query"));
//          break;
//      }
      uint16_t vbat;
      fona.getBattPercent(&vbat);
      postData.batt = vbat; 

//       check for GSMLOC (requires GPRS)*******start******
//        uint16_t returncode;  
//        if (!fona.getGSMLoc(&returncode, replybuffer, 250))
//          Serial.println(F("GPS Failed!"));
//        if (returncode == 0) {
//          Serial.println(replybuffer);
//          //parse GPS data
//        } else {
//          Serial.print(F("Fail code #")); Serial.println(returncode);
//        }
//     // check for GSMLOC (requires GPRS)*******end******

      state = WAIT_STATE;
      break;
    case WAIT_STATE://*************************************************************************
//      Serial.println("WAIT  ");
      if(millis() >= loopTimer) {
        state = GET_DATA_STATE;
        loopTimer = (millis() + LOOP_TIME);
      } else if(millis() >= postTimer) {
        state = POST_STATE;
        postTimer = (millis() + POST_TIME);
      }
      break;
    case POST_STATE://*************************************************************************
//      Serial.println("POST SOME DATA NOW");
      if(stat == 2 || stat == 3) {
        postData = parseGPS(postData, gpsdata);
            
        Serial.print("POST Data: "); 
        Serial.println(postData.id);
        Serial.print("Data to POST: ");
        temp = buildPost(postData);
        Serial.println(temp);
      
        uint16_t statuscode;
        int16_t length;
        char url[160];
        temp.toCharArray(url,temp.length()+1);
        char data[1] = {' '};//need to look into the adafruit library but this works for now..     
        flushSerial();

        if(!TEST_MODE) {        
          Serial.println(F("****"));
          if (!fona.HTTP_POST_start(url, F("text/plain"), (uint8_t *) data, strlen(data), &statuscode, (uint16_t *)&length)) {
            Serial.println("Failed!");
          }else {
            while (length > 0) {
              while (fona.available()) {
                char c = fona.read();
            
            #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
                loop_until_bit_is_set(UCSR0A, UDRE0); /* Wait until data register empty. */
                UDR0 = c;
            #else
                Serial.write(c);
            #endif
            
                length--;
                if (! length) break;
              }
            }
          
            Serial.println(F("\n****"));
            fona.HTTP_POST_end();
          }
      
          Serial.println("*******POST DONE*******");
        }
      }//end of case for needing 3dfix      state = WAIT_STATE;

      state = WAIT_STATE;
      break;
    default://*************************************************************************
      Serial.println("DEFAULT");

      state = ERROR_STATE;
      break;
  }
}

String buildPost(TRACKER_DATA postData) {//build post
  String post;
  post += POST_URL;
  post += "id=";
  post += postData.id;
  post += "&ts=";
  post += postData.ts;
  post += "&lat=";
  post += postData.lat;
  post += "&lon=";
  post += postData.lon;
  post += "&alt=";
  post += postData.alt;
  post += "&spd=";
  post += postData.spd;
  post += "&gps_sig=";
  post += postData.gps_sig;
  post += "&cell_sig=";
  post += postData.cell_sig;
  post += "&batt=";
  post += postData.batt;
  post += "&trp=";
  post += postData.trp;
  return post;
}

TRACKER_DATA parseGPS(TRACKER_DATA data, String gps) {
  int dataStart = (gps.indexOf(',',3)+1);
  int dataEnd = gps.indexOf(',',dataStart+1);
  data.ts = gps.substring(dataStart,dataEnd);
  dataStart = dataEnd+1;
  dataEnd = gps.indexOf(',',dataEnd+1);
  data.lat = gps.substring(dataStart,dataEnd);
  dataStart = dataEnd+1;
  dataEnd = gps.indexOf(',',dataEnd+1);
  data.lon = gps.substring(dataStart,dataEnd);
  dataStart = dataEnd+1;
  dataEnd = gps.indexOf(',',dataEnd+1);
  data.alt = gps.substring(dataStart,dataEnd);
  dataStart = dataEnd+1;
  dataEnd = gps.indexOf(',',dataEnd+1);
  data.spd = gps.substring(dataStart,dataEnd);
  return data;
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
      break;
    }

    while (Serial.available()) {
      char c =  Serial.read();

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
      break;
    }
    delay(1);
  }
  buff[buffidx] = 0;  // null term
  return buffidx;
}
