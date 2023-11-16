// =====================================================================================
// Update/Add any #define values to match your build and board type if not using D1 Mini
// =====================================================================================
// v1.01

#define WIFIMODE 2                            // 0 = Only Soft Access Point, 1 = Only connect to local WiFi network with UN/PW, 2 = Both
#define WIFIHOSTNAME "ANALOG_CLOCK"           // Host name for WiFi/Router
#define MQTTMODE 1                            // 0 = Disable MQTT, 1 = Enable (will only be enabled if WiFi mode = 1 or 2 - broker must be on same network)
#define MQTTCLIENT "analog_clock"             // MQTT Client Name
#define MQTT_TOPIC_SUB "cmnd/aclock"          // Default MQTT subscribe topic for commands to this device
#define MQTT_TOPIC_SUB2 "stat/tm1638"         // Secondary MQTT subscribe topic for general shared data/topics (e.g. date, time, etc. from TM1638 via Home Assistant)
#define MQTT_TOPIC_PUB "stat/aclock"          // Default MQTT publish topic
#define OTA_HOSTNAME "ANALOG_CLOCK_OTA"       // Hostname to broadcast as port in the IDE of OTA Updates
#define SERIAL_DEBUG 1                        // 0 = Disable (must be disabled if using RX/TX pins), 1 = enable

// ---------------------------------------------------------------------------------------------------------------
// Options - Defaults upon boot-up or any other custom ssttings
// ---------------------------------------------------------------------------------------------------------------
// OTA Settings
bool ota_flag = true;                    // Must leave this as true for board to broadcast port to IDE upon boot
uint16_t ota_boot_time_window = 2500;    // minimum time on boot for IP address to show in IDE ports, in millisecs
uint16_t ota_time_window = 20000;        // time to start file upload when ota_flag set to true (after initial boot), in millsecs

// Other options - boot defaults
bool blinkCurMinute = true;              //Blink current minute LED per interval cycle
uint16_t blinkInterval = 1000;           //Blink interval (1/2 cycle) in milliseconds
bool sweepMinutes = true;                //Sweep minute refresh on each update (redraws each minute from 1 to current - only dispMode 0)
byte dispMode = 0;                       //Default display mode at boot, currently 0-4 (see docs for modes)
