# OpenHAB dimmer(s)

This repository contains simple dimmers for LED lighting based on potentiometer or cheap
chinese RF LED controller such as
http://www.ebay.com/itm/Mini-12V-RF-Wireless-Remote-Switch-Controller-Dimmer-for-LED-Strip-Light.

As always is there Cadsoft's Eagle schematic, PCB and firmware for ESP8266 that can be programmed using Arduino IDE.
Potentiometer version uses Average library https://github.com/MajenkoLibraries/Average for nice fade efect and jitter elimination.

Both dimmers are based on MOSFET transistor at output and cheap DC/DC converter with LM2596 from eBay (set at 3.3V!!!) for powering ESP.
In my case i used IPA040N06N with 0.004ohm resistance in threshold state. PWM frequency is set only to 120Hz so any MOSFET with
reasonable parameters can be used.

Like other my OpenHAB projects is there set of default parameters in firmare as well as HTML configuration interface at IP address of device.

## Potentiometer version
Potentiometer version uses in my case slider potentiometer and ESP07 with ADC input.
![alt](/eagle/lightdimmer_pot_sch.png)

## RF version
RF version uses standard receiver available from eBay such as http://www.ebay.com/sch/i.html?_nkw=433mhz+receiver. Remote control codes can be set in firmware. This version uses my favorite ESP01 :)
![alt](/eagle/lightdimmer_rc_sch.png)

## Some project images
### RF version
![alt](/images/2017-01-18%2015.52.22.jpg?raw=true)
### POT version
![alt](/images/2017-01-21%2015.13.14.jpg?raw=true)
![alt](/images/2017-01-21%2016.29.21.jpg?raw=true)
