## Source Code Files

These are the source code files for the clock controller.  All three files are needed and must be placed in the same folder when compiling.  You ***must*** edit at least the Credentials.h file to enter in your WiFi and MQTT broker information.  It is recommended that you also check the Settings.h file and make any desired changes.

| File | Description
|------|----------
|analog_clock.ino| The main controller code
|Credential.h | File containing WiFi and MQTT connection information
|Settings.h | Miscellenous settings and boot options

Note that the first install must be done via USB. After the initial upload, and future updates can be done over-the-air (OTA)

### Please see the Wiki for information on installation and configuration options within the Credentials and Settings files.