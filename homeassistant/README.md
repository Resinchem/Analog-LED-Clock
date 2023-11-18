## Home Assistant Related Files

These are ***example*** files from my Home Assistant installation for integrating and controlling the clock in Home Assistant via MQTT.  It is highly unlikely you can just copy/paste these examples directly into your Home Assistant and expect them to work due to difference in entity names and other variations.

I use packages in my Home Assistant.  This means that integrations like sensor:, automation:, etc. can be repeated between packages.  If you are not using packages, then you will need to split out these various integration/entities and place them under the appropriate section of your own configuration.

### analog_clock.yaml

This is the primary package for the integration of the clock.  It shows how the MQTT sensors and switches are used to both receive the state of the clock and to control it.  It also has a couple of example automations using the clock. There is additional documentation withing the file itself.

### dashboard.yaml

![HomeAssistant_Dashboard](https://github.com/Resinchem/Analog-LED-Clock/assets/55962781/a50616c0-5495-4463-825f-f666d3998ce7)

This contains the YAML used to create the above dashboard.  Again, you will need to modify this for your own entities/installation.  It also used two custom components, installed via the Home Assistant Community Store (HACS):

- [Custom Button Card](https://github.com/custom-cards/button-card) by @RomRider (v3.5.0)
- [Text Divider Row](https://github.com/iantrich/text-divider-row) by iantrich (v1.4.0)*

*Note that the current 1.4.1 version of the text divider has a visible background. Install v1.4.0 if you don't want this boarder/background.
