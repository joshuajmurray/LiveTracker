#include <Adafruit_FONA.h>
#include <EEPROM.h>

#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4
#define POST_URL "http://wilsonja.pythonanywhere.com/?"
//#define TEST_MODE true
#define TEST_MODE false
#define LOOP_TIME 5000
#define POST_TIME 16250
#define DIGITAL_READ_TIME 100
#define OK_TIME 10000
#define DEBOUNCE_DELAY 5000
#define TRIP_MEM_LOCATION 0

//state machine states
#define INIT_STATE 0
#define ERROR_STATE 1
#define GET_DATA_STATE 2
#define WAIT_STATE 3
#define POST_STATE 4
#define READ_DIGITAL_STATE 5
#define CHECK_SIGNAL 6

#define OK_BUTTON 6
#define HELP_BUTTON 7
#define OK_LED 8
#define HELP_LED 9

#define GPS_OFF 0
#define GPS_NO_FIX 1
#define GPS_2D_FIX 2
#define GPS_3D_FIX 3
#define GPS_MIN_SAT_COUNT 4

#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
char gpsData[120];
char gsmData[50];// this is a large buffer for replies
char tempBuff[180];//this is as small as we can make this.. 
int8_t stat;
uint8_t type;
int ok;
int help;
boolean gsmLocation;

struct TRACKER_DATA {
  char id[16] = "";//imei number (like MAC)
  char* ts;//time stamp
  char* lat;
  char* lon;
  char* alt;//meters
  char* spd;
  byte gps_sig;//course 0-3 value
  byte cell_sig;//not currently used
  unsigned int batt;//battery % charge
  byte trp;//trip number
  byte sts;//status
  byte satCount;//not used in POST
};
TRACKER_DATA postData; 

int state;
unsigned long loopTimer;
unsigned long postTimer;
unsigned long digitalReadTimer;
unsigned long okTimer;
unsigned long debounce;

void setup() {
  while (!Serial);

  pinMode(OK_BUTTON, INPUT);
  pinMode(HELP_BUTTON, INPUT);
  pinMode(OK_LED, OUTPUT);
  pinMode(HELP_LED, OUTPUT);
  digitalWrite(OK_LED, HIGH);
  digitalWrite(HELP_LED, HIGH);

  state = INIT_STATE;
  loopTimer = 0;
  postTimer = 0;
  digitalReadTimer = 0;
  okTimer = 0;
  debounce = 0;

  ok = 0;
  help = 0;
  gsmLocation = false;
  
  Serial.begin(115200);
  Serial.println(F("FONA basic test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  postData.ts = "0";
  postData.lat = "0";
  postData.lon = "0";
  postData.alt = "0";
  postData.spd = "0";
  postData.gps_sig = 0;
  postData.cell_sig = 0;
  postData.batt = 0;
  postData.trp = 0;
  postData.sts = 0;
  postData.satCount = 0;

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
  
  uint8_t imeiLen = fona.getIMEI(postData.id);
  if (imeiLen > 0) {
    Serial.print(F("Module IMEI: ")); Serial.println(postData.id);
  }

//  Serial.println(F("-------------------------------------"));

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
  byte val = (byte)EEPROM.read(TRIP_MEM_LOCATION);
  postData.trp = val++; //increment the trip on powerup
  EEPROM.write(TRIP_MEM_LOCATION, val);//write back to eeprom (it's only 1 byte!!!!!!!!!!!!!!!!!!!!!!!!!!!!)
  digitalWrite(OK_LED, LOW);
  digitalWrite(HELP_LED, LOW);
}

void loop() {

  switch(state) {
    case INIT_STATE://*************************************************************************
      Serial.println(F("INIT STUFF"));
//      init stuff here
      state = WAIT_STATE;
      break;
    case ERROR_STATE://*************************************************************************
      Serial.println(F("ERROR"));

      state = INIT_STATE;
      break;
    case GET_DATA_STATE://*************************************************************************
//      Serial.println(F("GET INFO"));
      for(int i = 0; i < 120; i++) {//init buffer before using each time
        gpsData[i] = '\0';
      }

      fona.getGPS(0, gpsData, 120);//get GPS info
      stat = fona.GPSstatus();
      Serial.print(F("stat: "));Serial.println(stat);
      postData.gps_sig = stat;
      Serial.print(F("gps_sig: "));Serial.println(postData.gps_sig);
      
      uint16_t vbat;
      fona.getBattPercent(&vbat);
      postData.batt = vbat; 

//       check for GSMLOC (requires GPRS)*******start******
        uint16_t returncode;  
        if (!fona.getGSMLoc(&returncode, gsmData, 250)) {
          Serial.println(F("GPS Failed!"));
        }
        if (returncode != 0) {
          Serial.print(F("Fail code #")); Serial.println(returncode);
        }
//     // check for GSMLOC (requires GPRS)*******end******
      state = CHECK_SIGNAL;
      break;
    case CHECK_SIGNAL://*************************************************************************
      if(stat != "3" && (postData.satCount) < 4) {
        gsmLocation = true;
      } else {
        gsmLocation = false;
      }
      state = WAIT_STATE;
      break;
    case WAIT_STATE://*************************************************************************
//      Serial.println(F("WAIT  "));
      if(millis() >= postTimer) {
        state = POST_STATE;
        postTimer = (millis() + POST_TIME);
      }else if(millis() >= loopTimer) {
        state = GET_DATA_STATE;
        loopTimer = (millis() + LOOP_TIME);
      }else if(millis() >= digitalReadTimer) {
        state = READ_DIGITAL_STATE;
        digitalReadTimer = (millis() + DIGITAL_READ_TIME);
      }
      
      if(millis() >= okTimer){
        digitalWrite(OK_LED, LOW);
      }
      
      break;
    case POST_STATE://*************************************************************************
//      Serial.println(F("POST SOME DATA NOW"));
      if(stat == 2 || stat == 3) {//if you have a 2d or 3d GPS fix
//        Serial.print(F("GPSDATA: "));Serial.println(gpsData);
        parseGPS(gpsData);
        if(gsmLocation) {//if flag is set use lat,lon and time from GSM rather than GPS
          parseGSM(gsmData);
        }

//        Serial.println(F("Data to POST: "));
        buildPost(); 
             
        uint16_t statuscode;
        int16_t length;
        char data[1] = " ";//need to look into the adafruit library but this works for now..     
        flushSerial();

        if(!TEST_MODE) {        
//          Serial.println(F("****"));
          if (!fona.HTTP_POST_start(tempBuff, F("text/plain"), (uint8_t *) data, strlen(data), &statuscode, (uint16_t *)&length)) {
            Serial.println(F("Failed!"));
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
          
//            Serial.println(F("\n****"));
            fona.HTTP_POST_end();
          }
      
//          Serial.println(F("*******POST DONE*******"));
        }
      }//end of case for needing 3dfix      state = WAIT_STATE;

      state = WAIT_STATE;
      break;
    case READ_DIGITAL_STATE://*************************************************************************
//      Serial.println("READ DIGITALS");
      ok = digitalRead(OK_BUTTON);
      help = digitalRead(HELP_BUTTON);
      if(debounce < millis()) {//limits how often the button can call the post state
        if(ok && !help) {
          postData.sts = 1;
          digitalWrite(OK_LED, HIGH);
          okTimer = (millis() + OK_TIME);
          state = POST_STATE;
        } else if (!ok && help) {
          postData.sts = 2;
          digitalWrite(HELP_LED, HIGH);//latch red LED on, nowhere in code will this turn off again.
          state = POST_STATE;
        } else if (ok && help) {
          postData.sts = 3;
          state = POST_STATE;
        }
        debounce = millis() + DEBOUNCE_DELAY;
      } else {
        postData.sts = 0;
        state = WAIT_STATE;
      }      
      break;
    default://*************************************************************************
      Serial.println(F("DEFAULT"));
      state = ERROR_STATE;
      break;
  }
}

void buildPost() {//build post
  sprintf(tempBuff, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%d%s%d%s%d%s%d%s%d", POST_URL, "id=",
  postData.id, "&ts=", postData.ts, "&lat=", postData.lat, "&lon=", postData.lon,"&alt=",
  postData.alt,"&spd=",postData.spd,"&gps_sig=",postData.gps_sig,"&cell_sig=",
  postData.cell_sig,"&batt=",postData.batt,"&trp=",postData.trp,"&sts=",postData.sts);
//  printPostData();
}

void printPostData() {
  Serial.print(F("ID: "));Serial.println(postData.id);
  Serial.print(F("TS: "));Serial.println(postData.ts);
  Serial.print(F("Lat: "));Serial.println(postData.lat);
  Serial.print(F("Lon: "));Serial.println(postData.lon);
  Serial.print(F("Alt: "));Serial.println(postData.alt);
  Serial.print(F("Spd: "));Serial.println(postData.spd);
  Serial.print(F("gSig: "));Serial.println(postData.gps_sig);
  Serial.print(F("cSig: "));Serial.println(postData.cell_sig);
  Serial.print(F("Batt: "));Serial.println(postData.batt);
  Serial.print(F("Trp: "));Serial.println(postData.trp);
  Serial.print(F("Sts: "));Serial.println(postData.sts);
  Serial.print(F("Sat: "));Serial.println(postData.satCount);
}

void parseGPS(char* gpsChar) {
  char* temp;
  temp = strtok(gpsChar,",");
  int loc = 1;
  while(temp != NULL) {
    switch(loc) {
      case 3:
        postData.ts = temp;
      break;
      case 4:
        postData.lat = temp;
      break;
      case 5:
        postData.lon = temp;
      break;
      case 6:
        postData.alt = temp;
      break;
      case 7:
        postData.spd = temp;
      break;
      case 13:
        postData.satCount = atoi(temp);
      break;
    default:
      break;
    }
    temp = strtok(NULL,",");
    loc++;
  }
//  printPostData();
}

void parseGSM(char* gsmChar) {
  char* temp;
  char* pre;
  char* post;
  temp = strtok(gsmChar,",");
  int loc = 1;
  while(temp != NULL) {
    switch(loc) {
      case 1:
        postData.lon = temp;
      break;
      case 2:
        postData.lat = temp;
      break;
      case 3:
        pre = temp;
      break;
      case 4:
        post = temp;
    default:
      break;
    }
    temp = strtok(NULL,",");
    loc++;
  }
  strcpy(postData.ts,pre);
  strcat(postData.ts,post);
  parseGSMTime();
}

void parseGSMTime() {
  char* temp;
  temp = strtok(postData.ts,"/:");
  if(temp != NULL) {
    strcpy(postData.ts, temp);
  }
  while(temp != NULL) {
    temp = strtok(NULL,"/:");
    strcat(postData.ts, temp);
  }
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
