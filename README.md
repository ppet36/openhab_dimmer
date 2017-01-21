# OpenHAB dimmer(s)

This repository contains simple dimmers for LED lighting based on potentiometer or cheap
chinese RF LED controller such as
http://www.ebay.com/itm/Mini-12V-RF-Wireless-Remote-Switch-Controller-Dimmer-for-LED-Strip-Light.

As always is there Cadsoft's Eagle schematic, PCB and firmware for ESP8266 that can be programmed using Arduino IDE.

Both dimmers are based on MOSFET transistor at output and cheap DC/DC converter with LM2596 from eBay for powering ESP.
In my case i used IPA040N06N with 0.04ohm resistance in threshold state. PWM frequency is set to 120Hz so any MOSFET with
reasonable parameters can be used.
