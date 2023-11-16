
/* ==========================================================================================
   Analog LED Clock
   Sketch to control LEDs for a circular analog-style clock where the LEDs represent minutes
   and hours.  Options include different minute display modes.  See the wiki for more.
   Credentials.h and Settings.h must be located in same folder as this .ino file and both should
   be edited for your wifi/MQTT broker and pin/wiring configuration.

   MQTT is optional, but without it changes to some options (blink and sweep) will require modifying
   the Settings.h file and a recompile/OTA update.  In addition, code/source will need to be added
   to get current time, with an update each minute.  One option would be to implement a call
   to an NTP server.

   See the wiki for complete details  Offered "as-is" for adaption to your own projects.  PR
   and feature enhancement requests will not be accepted for this project.
   
   Board: LOLIN(WEMOS) D1, R2 & mini (other boards or ESP32 may require library/code changes)
   v1.01 Nov. 2023
   Resinchem Tech - licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
   =========================================================================================*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include "Credentials.h"          //File must exist in same folder as .ino.  Edit as needed for project
#include "Settings.h"             //File must exist in same folder as .ino.  Edit as needed for project
#include <Adafruit_MCP23X17.h>    // https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library
#include <Wire.h>                 // for I2C
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  #include <PubSubClient.h>
#endif

#define RED_BUTTON 13   //hour MCP board push button pins
#define YELLOW_BUTTON 14
#define GREEN_BUTTON 15

//GLOBAL VARIABLES (MQTT and OTA Updates)
bool mqttConnected = false;       //Will be enabled if defined and successful connnection made.  This var should be checked upon any MQTT actin.
long lastReconnectAttempt = 0;    //If MQTT connected lost, attempt reconnenct
uint16_t ota_time = ota_boot_time_window;
uint16_t ota_time_elapsed = 0;           // Counter when OTA active

//Other Globals
String haTime = "00:00";  // Provide default time until HA updates at top of each minute
String haDate= "2023-00-00"; //Currently only used for last boot date/time
byte hours = 0;
byte minutes = 0;
bool firstBoot = true;  //Needed for logging boot time, which is not available during setup.

bool ledsOn = true;  // Default at boot - can be toggled via button or MQTT
int numSegments = 0;
int lastSegmentMins = 0;
int curSegment = 0;  // current segment (0-3) for the current minute
int curMinute = 0;   // current minute (0-14) within the segment
unsigned long prevTime = 0;  //Main loop timer

//Define MCP boards
Adafruit_MCP23X17 mcp_hour;  //0x20
Adafruit_MCP23X17 mcp_min[4];  //Array from 0-3 for the four quadrant 0-14, 15-29, 30-44, 45-59

//Array for hour pins, which are physically reversed on clock (first element[0] unused)
byte hour_pin[13] = {0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 12};

/*Array for minute pins, which are off by one (pin 1 is minute 0, pin 2 is minute 1, etc.)
  array[0] is first LED in segment (e.g. 0, 15, 30, 45), then array[1] aligns with first minute in each (e.g. 1, 16, 31, 46).
  Loops should be configured for i = 0; i < 15
*/
byte min_pin[15] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};  //Segment LEDs 0-14, 15-29, 30-44, 45-59, where array[0] = 0, 15, 30 and 45

//Setup Local Access point if enabled via WIFI Mode
#if defined(WIFIMODE) && (WIFIMODE == 0 || WIFIMODE == 2)
  const char* APssid = AP_SSID;        
  const char* APpassword = AP_PWD;  
#endif

//Setup Wifi if enabled via WIFI Mode
#if defined(WIFIMODE) && (WIFIMODE == 1 || WIFIMODE == 2)
  const char *ssid = SID;
  const char *password = PW;
#endif

//Setup MQTT if enabled - only available when WiFi is also enabled
#if (WIFIMODE == 1 || WIFIMODE == 2) && (MQTTMODE == 1)    // MQTT only available when on local wifi
  const char *mqttUser = MQTTUSERNAME;
  const char *mqttPW = MQTTPWD;
  const char *mqttClient = MQTTCLIENT;
  const char *mqttTopicSub = MQTT_TOPIC_SUB;
 // const char *mqttTopicPub = MQTT_TOPIC_PUB;
#endif

WiFiClient espClient;
ESP8266WebServer server;
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  PubSubClient client(espClient);
#endif


//------------------------------
// Setup WIFI
//-------------------------------
void setup_wifi() {
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  //Disable WiFi Sleep
  delay(200);
  // WiFi - AP Mode or both
#if defined(WIFIMODE) && (WIFIMODE == 0 || WIFIMODE == 2) 
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(APssid, APpassword);    // IP is usually 192.168.4.1
#endif
  // WiFi - Local network Mode or both
#if defined(WIFIMODE) && (WIFIMODE == 1 || WIFIMODE == 2) 
  byte count = 0;

  WiFi.hostname(WIFIHOSTNAME);
  WiFi.begin(ssid, password);
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.print("Connecting to WiFi");
  #endif
  while (WiFi.status() != WL_CONNECTED) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.print(".");
    #endif
    // Stop if cannot connect
    if (count >= 60) {
      // Could not connect to local WiFi 
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println();
        Serial.println("Could not connect to WiFi.");   
      #endif  
      return;
    }
    delay(500);
    count++;
  }
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println();
    Serial.println("Successfully connected to Wifi");
    IPAddress ip = WiFi.localIP();
    Serial.println(WiFi.localIP());
  #endif
#endif   
};

// ===================================
//  SETUP MQTT AND CALLBACKS
// ===================================
void setup_mqtt() {
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  byte mcount = 0;
  //char topicPub[32];
  client.setServer(MQTTSERVER, MQTTPORT);
  client.setCallback(callback);
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.print("Connecting to MQTT broker.");
  #endif
  while (!client.connected( )) {
    #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
      Serial.print(".");
    #endif
    client.connect(mqttClient, mqttUser, mqttPW);
    if (mcount >= 60) {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.println();
        Serial.println("Could not connect to MQTT broker. MQTT disabled.");
      #endif
      // Could not connect to MQTT broker
      return;
    }
    delay(500);
    mcount++;
  }
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println();
    Serial.println("Successfully connected to MQTT broker.");
  #endif
  client.subscribe(MQTT_TOPIC_SUB"/#");
  client.subscribe(MQTT_TOPIC_SUB2"/#");
  client.publish(MQTT_TOPIC_PUB"/mqtt", "connected", true);
  mqttConnected = true;
#endif
}

void reconnect() {
  int retries = 0;
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  while (!client.connected()) {
    if(retries < 150)
    {
      #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
        Serial.print("Attempting MQTT connection...");
      #endif
      if (client.connect(mqttClient, mqttUser, mqttPW)) 
      {
        #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
          Serial.println("connected");
        #endif
        // ... and resubscribe
        client.subscribe(MQTT_TOPIC_SUB"/#");
        client.subscribe(MQTT_TOPIC_SUB2"/#");
      } 
      else 
      {
        #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
          Serial.print("failed, rc=");
          Serial.print(client.state());
          Serial.println(" try again in 5 seconds");
        #endif
        retries++;
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    if(retries > 149)
    {
    ESP.restart();
    }
  }
#endif
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String message = (char*)payload;
  /*
   * Add any commands submitted here
   * Example:
   * if (strcmp(topic, "cmnd/matrix/mode")==0) {
   *   MyVal = message;
   *   Do something
   *   return;
   * };
   */
  if (strcmp(topic, MQTT_TOPIC_SUB2"/hatime") == 0) {
     haTime = message;
     int colonLoc = haTime.indexOf(":");
     if (colonLoc > 0) {
       setTimeSegments();
       displayTime();
     }
     if ((firstBoot) && (haDate != "2023-00-00")) {
        updateMqttLastBoot();
        firstBoot = false;
     }
  } else if (strcmp(topic, MQTT_TOPIC_SUB2"/hadate") == 0) {
    haDate = message;
    if ((firstBoot) && (haTime != "00:00")) {
      updateMqttLastBoot();
      firstBoot = false;
    }
  } else if (strcmp(topic, MQTT_TOPIC_SUB"/dispmode") == 0) {
     int newMode = message.toInt();
     if ((newMode >= 0) && (newMode <= 4)) {  //Increment when new modes added and add to displayTime()
        dispMode = newMode;
        updateMqttDispMode();
        displayTime();
     }
  } else if (strcmp(topic, MQTT_TOPIC_SUB"/sweep") == 0) {  //Only used for display mode 0 (all leds)
      if ((message == "ON") || (message == "on") || (message == "1")) {
        sweepMinutes = true;
      } else {
        sweepMinutes = false;
      }
      updateMqttSweep();
      displayTime();
  } else if (strcmp(topic, MQTT_TOPIC_SUB"/blink") == 0) {
      if ((message == "ON") || (message == "on") || (message == "1")) {
        blinkCurMinute = true;
      } else {
        blinkCurMinute = false;
      }
      updateMqttBlink();
      displayTime();
  } else if (strcmp(topic, MQTT_TOPIC_SUB"/leds") == 0) {
    if ((message == "ON") || (message == "on") || (message == "1")) {
      ledsOn = true;
      displayTime();
    } else {
      ledsOn = false;
      allLEDsOff();
    }
    updateMqttLeds();
  }
  
}

/* =============================================
 *  MAIN SETUP
 * =============================================
*/
void setup() {
  byte i;
  byte j;
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.begin(115200);
    Serial.println("Starting setup....");
  #endif
  //Start I2C
  Wire.begin();

  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println("Initializing expansion boards");
  #endif
  
  //*****************************************
  //Initialize all MCP Minute boards in array 
  //(update these if your board is configured differently)
  mcp_min[0].begin_I2C( 0x24 );     //segment 0-14 minutes
  mcp_min[1].begin_I2C( 0x23 );     //segment 15-29
  mcp_min[2].begin_I2C( 0x22 );     //segment 30-44
  mcp_min[3].begin_I2C( 0x21 );     //segment 45-59
  
  //Initialize hour board
  mcp_hour.begin_I2C( 0x20 );   //hour digits
  //*****************************************
 
  //for minute boards, set pins 1-15 for output (array[0] - array[14])
  for (j = 0; j < 4; j++) {              //boards 0-3
    for (i = 0; i < 15; i++) {           //pins 1-15
      mcp_min[j].pinMode(min_pin[i], OUTPUT);
    }
  }
  //for hour board, set pins 1-12 for output 
  for (i = 1; i < 13; i++) {
    mcp_hour.pinMode(hour_pin[i], OUTPUT);
  }
  
  //*******************************************
  //hour expansion board has three input buttons
  //update these if your board is wired differently
  mcp_hour.pinMode(RED_BUTTON, INPUT_PULLUP);  //pin 13
  mcp_hour.pinMode(YELLOW_BUTTON, INPUT_PULLUP); //pin 14
  mcp_hour.pinMode(GREEN_BUTTON, INPUT_PULLUP);  //pin 15
  //********************************************

  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println("All pins configured");
  #endif
  
  // Turn off all LEDS (since some may be on from OTA update)
  allLEDsOff();
  //-----------------------------
  // Setup Wifi
  //-----------------------------
  //Turn on 12 to indicated booting, waiting on wifi
  mcp_hour.digitalWrite(hour_pin[12], HIGH);
  setup_wifi();

  //-----------------------------
  // Setup OTA Updates
  //-----------------------------
  //Turn on 3 to indicate setting up OTA
  mcp_hour.digitalWrite(hour_pin[3], HIGH);
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
  });
  ArduinoOTA.begin();
  // Setup handlers for web calls for OTAUpdate and Restart
  server.on("/restart",[](){
    server.send(200, "text/html", "<h1>Restarting...</h1>");
    delay(1000);
    ESP.restart();
  });
  server.on("/otaupdate",[]() {
    server.send(200, "text/html", "<h1>Analog Clock Ready for Update...<h1><h3>Start upload from IDE now</h3>");
    ota_flag = true;
    ota_time = ota_time_window;
    ota_time_elapsed = 0;
  });
  server.begin();

  //------------------------
  // Setup MQTT if Enabled
  //------------------------
  mcp_hour.digitalWrite(hour_pin[6], HIGH);
  setup_mqtt();

  //If MQTT setup and connected, request current time update from Home Assistant
#if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
  char outVal[5];
  if (mqttConnected) {
    client.publish(MQTT_TOPIC_PUB"/gettime", "1", false);
    delay(250);  //allow time to publish
    //Publish defaults states, in case different from last states before reboot
    updateMqttAll(); 
  }
#endif
  
  //Boot/setup complete, Turn off all hour LEDS
  //Turn off all hours (only 12 should be on)
  for (i = 1; i < 13; i++) {
    mcp_hour.digitalWrite(hour_pin[i], LOW);
  }
  
  #if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
    Serial.println("Starting LED Test");
  #endif
  testLEDS();  
}

// ==================================
//   MAIN LOOP
// =================================

void loop() {
  unsigned long currentMillis = millis(); 
  
  //Handle OTA updates when OTA flag set via HTML call to http://ip_address/otaupdate
  if (ota_flag) {
    updateOTA();  //show OTA update on clock
    uint16_t ota_time_start = millis();
    while (ota_time_elapsed < ota_time) {
      ArduinoOTA.handle();  
      ota_time_elapsed = millis()-ota_time_start;   
      delay(10); 
    }
    ota_flag = false;
    allLEDsOff();
    //showTime(sweepMinutes);
    displayTime();
  }
  //Handle any web calls
  server.handleClient();
  //MQTT Calls
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    if (!client.connected()) 
    {
      reconnect();
    }
    client.loop();
  #endif

  //Process button presses
  //RED Button - Test LEDs
  if (!mcp_hour.digitalRead(RED_BUTTON)) {
    ledsOn = true;
    testLEDS();
    delay(250);
  }
  //YELLOW Button - Toggle LEDs off/on
  if (!mcp_hour.digitalRead(YELLOW_BUTTON)) {
    if (ledsOn) {
      ledsOn = false;
      allLEDsOff();
    } else {
      ledsOn = true;
      //showTime(sweepMinutes);
      displayTime();
    }
    updateMqttLeds();
    delay(250);
  }
  //GREEN Button - Change display mode
  if (!mcp_hour.digitalRead(GREEN_BUTTON)) {
    if (dispMode < 4) {
      dispMode = dispMode + 1;
    } else {
      dispMode = 0;
    }
    displayTime();
    updateMqttDispMode();
    delay(250);
  }
  
  if (ledsOn) {
    if (currentMillis - prevTime >= blinkInterval) {
      prevTime = currentMillis;
      if (blinkCurMinute) {
        blinkMinute();
      }
    }
  }
}

// =================================
//  Time and Time Display Functions
// =================================
void setTimeSegments() {
/* This function takes the current time as a string from MQTT (via Home Assistant) and breaks it 
   apart into hours and minutes as integers, converting 24 hours to 12 hour, and determining the
   the number of minute LED 15-minute segments that should be fully lit, along with the last
   segment and the number of LEDs that should be lit in this last segment for the current minute.
   It is called primarily from the MQTT callback each time a new time gets published (once a minute).
*/
   int colonLoc = haTime.indexOf(":");
   if (colonLoc > 0) {
     hours = (haTime.substring(0, colonLoc)).toInt();
     minutes = (haTime.substring(colonLoc + 1)).toInt();
     //handle 24 hour time reported by HA and just convert to hours 1-12
     if (hours > 12) {
       hours = hours - 12;
     } else if (hours == 0) {
       hours = 12;
     }
    // Determine number of segments fully or partially lit (using array segment values)
     if (minutes < 15) {
       numSegments = 0;
     } else if (minutes < 30) {
       numSegments = 1;
     } else if (minutes < 45) {
       numSegments = 2;
     } else {
       numSegments = 3;
     }
    //Determine number of LEDs in last segment
    lastSegmentMins = (minutes - (15 * (numSegments)) + 1);  //add one since each segment starts with zero
   }
}

void displayTime() {
/*  Provides a commonwrapper to call elsewhere in code so that each call doesn't need to check
    the current display mode.  This function will then call the appropriate function to display
    the time for the current display mode.  Add any new modes here and increment count in MQTT callback 
*/
   switch (dispMode) {
     case 0:   
       showTimeAll(sweepMinutes);
       break;
     case 1:
       showTime01();
       break;
     case 2:
       showTime05();
       break;
     case 3:
       showTime10();
       break;
     case 4:
       showTime15();
       break;
     default:
       break;
   }
}

void showTimeAll(bool sweepMin) {
/*  Default mode (dispMode 0).  Lights up all minute LEDs up to the current minute.
    If the sweepMin boolean is true (the default setting on boot is in the Settings.h file), 
    the LEDs are redrawn (sweep motion) each time the time is updated.  Otherwise,
    all the LEDs for the current minute as simply toggled on, giving the impression of each 
    new LED lighting as the minutes increase.  Can also be anabled/disabled via MQTT.
*/
  //if firstTime flag is true, minute leds will light in sequence with short delay, like the LED test
  byte i;
  byte j;
  byte r;

  if (ledsOn) {
    // HOURS
    //Light up hour LED (and assure all others are off)
    for (i = 1; i < 13; i++) {
      if (i == hours) {
        mcp_hour.digitalWrite(hour_pin[i], HIGH);
      } else {
        mcp_hour.digitalWrite(hour_pin[i], LOW);
      }
    }

    // MINUTES
    //If sweep mode enabled, first turn off all minute LEDs
    if (sweepMin) {
      if (minutes == 0) {
        //if top of hour, reverse sweep all LEDs off
        minLEDsOff(true); 
      } else {
        //just turn off all lit minuted LEDS
        minLEDsOff(false);
      }
    }
    
    //Loop through all four segments, lighting all LEDs if current segment less than the last
    for (j = 0; j < 4; j++) {
      if (j < numSegments) {
        //Light all LEDs in segement
        for (i = 0; i < 15; i++) {
          mcp_min[j].digitalWrite(min_pin[i], HIGH);
           if (sweepMin) {
             delay(50);
           }
        }
      } else if (j == numSegments) {
        //Only light LEDs for lastSegmentMins
        for (i = 0; i < lastSegmentMins; i++) {
          mcp_min[j].digitalWrite(min_pin[i], HIGH);
          if (sweepMin) {
            delay(50);
          }
          r = i;
          // Capture last LED position for blinking
          curSegment = j;
          curMinute = i;
        }
        //Turn off any remaining lights in this segment
        for (i = (r + 1); i < 15; i++) {
          mcp_min[j].digitalWrite(min_pin[i], LOW);
        }
      } else {
        //Turn off all remaining LEDs (needed for rollover to new hour)
        for (i = 0; i < 15; i++) {
          mcp_min[j].digitalWrite(min_pin[i], LOW);
        }
      }
    }
  } else {
    //No current time from HA yet (usually on startup)
    //Request refresh of time from HA via MQTT
  }
}

void showTime01() {
/*  dispMode 1... only show current minute LED (one LED lit at a time).
    Blink mode still supported but will ignore/not use any sweep actions
*/
  byte i;

  if (ledsOn) {
    // HOURS
    //Light up hour LED (and assure all others are off)
    for (i = 1; i < 13; i++) {
      if (i == hours) {
        mcp_hour.digitalWrite(hour_pin[i], HIGH);
      } else {
        mcp_hour.digitalWrite(hour_pin[i], LOW);
      }
    }

    //MINUTES
    //Turn off all minute LEDs (no sweep), this will turn off the previous minute
     minLEDsOff(false);
     //Just turn on proper LED in last segment 
     mcp_min[numSegments].digitalWrite(min_pin[lastSegmentMins-1], HIGH);
     // Capture last LED position for blinking
     curSegment = numSegments;
     curMinute = lastSegmentMins-1; 
  }
}

void showTime05() {
/* dispMode 2... Show a moving 5 minute segment of LEDs, with last LED being the current minute
   Blink mode still supported for current minute LED, but sweep setting is ignored   
*/
  byte i;
  int priorSegmentMins = 0;
  if (ledsOn) {

    // HOURS
    //Light up hour LED (and assure all others are off)
    for (i = 1; i < 13; i++) {
      if (i == hours) {
        mcp_hour.digitalWrite(hour_pin[i], HIGH);
      } else {
        mcp_hour.digitalWrite(hour_pin[i], LOW);
      }
    }

    //MINUTES    
    //Turn off LEDs so previous LED will be off
    minLEDsOff(false);
    //Need to determine if all 5 LEDs are in same segment or split across two.

    //Light up appropriate num LEDs in prior segment
    if (lastSegmentMins < 5) {
      priorSegmentMins = (5 - lastSegmentMins);
      //Light up LEDs in previous segement
      for (i = (15 - priorSegmentMins); i < 15; i++) {
        if (numSegments == 0) {
          //Use last segment (3) since time spanning 0 minute
          mcp_min[3].digitalWrite(min_pin[i], HIGH);
        } else {
          mcp_min[numSegments-1].digitalWrite(min_pin[i], HIGH);
        }
      }
      //Light up remaining LEDs
        for (i = 0; i < lastSegmentMins; i++) {
          mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
        }
    } else {
      //All LEDs in same segment.  Just light up the last 5
      for (i = (lastSegmentMins - 5); i < lastSegmentMins; i++) {
        mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
      }
    }
    // Capture last LED position for blinking
    curSegment = numSegments;
    curMinute = lastSegmentMins-1; 
  }
}

void showTime10() {
/* dispMode 3... Show a moving 10 minute segment of LEDs, with last LED being the current minute
   Blink mode still supported for current minute LED, but sweep setting is ignored   
*/
  byte i;
  int priorSegmentMins = 0;
  if (ledsOn) {
    // HOURS
    //Light up hour LED (and assure all others are off)
    for (i = 1; i < 13; i++) {
      if (i == hours) {
        mcp_hour.digitalWrite(hour_pin[i], HIGH);
      } else {
        mcp_hour.digitalWrite(hour_pin[i], LOW);
      }
    }

    //MINUTES
    //Turn off LEDs so previous LED will be off
    minLEDsOff(false);
    //Need to determine if all 10 LEDs are in same segment or split across two.

    //Light up appropriate num LEDs in prior segment
    if (lastSegmentMins < 10) {
      priorSegmentMins = (10 - lastSegmentMins);
      //Light up LEDs in previous segement
      for (i = (15 - priorSegmentMins); i < 15; i++) {
        if (numSegments == 0) {
          //Use last segment (3) since time spanning 0 minute
          mcp_min[3].digitalWrite(min_pin[i], HIGH);
        } else {
          mcp_min[numSegments-1].digitalWrite(min_pin[i], HIGH);
        }
      }
      //Light up remaining LEDs
        for (i = 0; i < lastSegmentMins; i++) {
          mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
        }
    } else {
      //All LEDs in same segment.  Just light up the last 5
      for (i = (lastSegmentMins - 10); i < lastSegmentMins; i++) {
        mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
      }
    }
    // Capture last LED position for blinking
    curSegment = numSegments;
    curMinute = lastSegmentMins-1; 
  }
}

void showTime15() {
/* dispMode 4... Show a moving 15 minute segment of LEDs, with last LED being the current minute
   Blink mode still supported for current minute LED, but sweep setting is ignored   
*/
  byte i;
  int priorSegmentMins = 0;
  if (ledsOn) {

    // HOURS
    //Light up hour LED (and assure all others are off)
    for (i = 1; i < 13; i++) {
      if (i == hours) {
        mcp_hour.digitalWrite(hour_pin[i], HIGH);
      } else {
        mcp_hour.digitalWrite(hour_pin[i], LOW);
      }
    }

    //MINUTES
    //Turn off LEDs so previous LED will be off
    minLEDsOff(false);
    //Need to determine if all 15 LEDs are in same segment or split across two.

    //Light up appropriate num LEDs in prior segment
    if (lastSegmentMins < 15) {
      priorSegmentMins = (15 - lastSegmentMins);
      //Light up LEDs in previous segement
      for (i = (15 - priorSegmentMins); i < 15; i++) {
        if (numSegments == 0) {
          //Use last segment (3) since time spanning 0 minute
          mcp_min[3].digitalWrite(min_pin[i], HIGH);
        } else {
          mcp_min[numSegments-1].digitalWrite(min_pin[i], HIGH);
        }
      }
      //Light up remaining LEDs
        for (i = 0; i < lastSegmentMins; i++) {
          mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
        }
    } else {
      //All LEDs in same segment.  Just light up the last 5
      for (i = (lastSegmentMins - 15); i < lastSegmentMins; i++) {
        mcp_min[numSegments].digitalWrite(min_pin[i], HIGH);
      }
    }
    // Capture last LED position for blinking
    curSegment = numSegments;
    curMinute = lastSegmentMins-1; 
  }
  
}

void blinkMinute () {
/* This simply blinks the last/current minute LED, toggling the on/off state
   once per second.  The default boot setting can be set in the Settings.h (blinkCurMinute)
   but it can also be toggled off/on via MQTT.
*/  
  // Assure prior minute is turned on, in the event is was off when minute changed (unless single LED mode: dispMode = 1)
  int priorSegment;
  int priorMinute;
  if (ledsOn) {
    if (dispMode != 1) {
      //Turn on last LED in prior segment
      if ((curSegment != 0) && (curMinute != 0)) {  //if top of hour, do not turn on minute 59
        if (curSegment == 0) {
          priorSegment = curSegment - 1;
          priorMinute = 14;
        } else {
          priorSegment = curSegment;
          priorMinute = curMinute - 1;
        }
        mcp_min[priorSegment].digitalWrite(min_pin[priorMinute], HIGH);
      } else {
        mcp_min[3].digitalWrite(min_pin[14], LOW);
      }
    }
    mcp_min[curSegment].digitalWrite(min_pin[curMinute], !mcp_min[curSegment].digitalRead(min_pin[curMinute]));
  }
}
// ===============================
//  LED Misc Display Functions
// ================================

void testLEDS() {
/* This is called at end of initial boot to test all LEDs.  Each minute LED
   will light up in sequence and then turn back off in sequence.  This is 
   followed by each hour LED lighting up one at a time in sequence.  Can also
   be launched via button press (RED) or via MQTT.  Will display current time
   in active display mode when test completes.
*/
  byte i;
  byte j;
  //Test Loop - Test each LED and segment in order - Lights on
  for (j = 0; j < 4; j++) {  //each segment
    for (i = 0; i < 15; i++) {
      mcp_min[j].digitalWrite(min_pin[i], HIGH);
      delay(50);
    }
  }
  delay(500);

  //Now turn off
  for (j = 0; j < 4; j++){
    for (i = 0; i < 15; i++) {
      mcp_min[j].digitalWrite(min_pin[i], LOW);
      delay(50);
    }
  }

  //Now turn on hours one at a time
  for (i = 1; i < 13; i++) {
    mcp_hour.digitalWrite(hour_pin[i], HIGH);
    //turn off prior light, unless last one
    if ((i > 1) && (i < 13)) {
      mcp_hour.digitalWrite( hour_pin[i - 1], LOW);
    }
    delay(150);
  }
  //Turn off all hours (only 12 should be on)
  for (i = 1; i < 13; i++) {
    mcp_hour.digitalWrite(hour_pin[i], LOW);
  }
  delay(500);
  // For testing
  //showTimeAll(true);
  displayTime();
}

void allLEDsOff() {
  //Turn off all LEDs - minutes and hours
  byte i;
  byte j;
  //Turn off all minute LEDs
  for (j = 0; j < 4; j++){
    for (i = 0; i < 15; i++) {
      mcp_min[j].digitalWrite(min_pin[i], LOW);
    }
  }

  //Turn off all hour LEDs
  for (i = 1; i < 13; i++) {  
    mcp_hour.digitalWrite(hour_pin[i], LOW);
  }
}

void minLEDsOff(bool sweepMin) {
  //Turn off all minute LEDs (but not hours) - primarily used for sweep option
  int i;
  int j;
  Serial.println("Turning off minute LEDs");
  if (sweepMin) {
    Serial.println("Sweeping");
    // Turn off all LEDs in reverse order, with delay (reverse sweep)
    for (j = 3; j >= 0; j--) {
      Serial.print("J: ");
      Serial.println(j);
      for (i = 14; i >= 0; i--) {
        Serial.print("  i: ");
        Serial.println(i);
        mcp_min[j].digitalWrite(min_pin[i], LOW);
        delay(50);
      }
    }
  } else {
    // Just turn off all minute LEDs
    Serial.println("Not sweeping - just off");
    for (j = 0; j < 4; j++){
      for (i = 0; i < 15; i++) {
        mcp_min[j].digitalWrite(min_pin[i], LOW);
      }
    }
  }
}

void updateOTA() {
  // turn on quarterly hour LEDs to indicate OTA update is active and ready to receive
  mcp_hour.digitalWrite(3, HIGH);
  mcp_hour.digitalWrite(6, HIGH);
  mcp_hour.digitalWrite(9, HIGH);
  mcp_hour.digitalWrite(12, HIGH);
}

// ===============================
//  MQTT Update Functions
// ================================
void updateMqttAll() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    //Publish all state values (retained unless otherwise noted)
    char outVal[5];
    //Current display mode
    sprintf(outVal, "%3u", dispMode);
    client.publish(MQTT_TOPIC_PUB"/dispmode", outVal, true);
    //Blink
    if (blinkCurMinute) {
      client.publish(MQTT_TOPIC_PUB"/blink", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/blink", "OFF", true);
    }
    //Sweep
    if (sweepMinutes) {
      client.publish(MQTT_TOPIC_PUB"/sweep", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/sweep", "OFF", true);
    }
    //LEDs
    if (ledsOn) {
      client.publish(MQTT_TOPIC_PUB"/leds", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/leds", "OFF", true);
    }
  #endif
}

void updateMqttDispMode() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    char outVal[5];
    sprintf(outVal, "%3u", dispMode);
    client.publish(MQTT_TOPIC_PUB"/dispmode", outVal, true);
  #endif
}

void updateMqttBlink() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    if (blinkCurMinute) {
      client.publish(MQTT_TOPIC_PUB"/blink", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/blink", "OFF", true);
    }
  #endif
}

void updateMqttSweep() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    if (sweepMinutes) {
      client.publish(MQTT_TOPIC_PUB"/sweep", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/sweep", "OFF", true);
    }
  #endif
}

void updateMqttLeds() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    if (ledsOn) {
      client.publish(MQTT_TOPIC_PUB"/leds", "ON", true);
    } else {
      client.publish(MQTT_TOPIC_PUB"/leds", "OFF", true);
    }
  #endif
}

void updateMqttLastBoot() {
  #if defined(MQTTMODE) && (MQTTMODE == 1 && (WIFIMODE == 1 || WIFIMODE == 2))
    //Not called in the update all above, since should only be called once on initial startup
    //byte timeLen = haTime.length() + 1;
    //byte dateLen = haDate.length() + 1;
    String lastBoot = haDate + " " + haTime;
    byte msgLen = lastBoot.length() + 1;
    char outMsg[msgLen];
    lastBoot.toCharArray(outMsg, msgLen);
    client.publish(MQTT_TOPIC_PUB"/lastboot", outMsg, true);
  #endif
}
