# WireCitter on ESP8266

## Info

This repository contains the software for my ESP8266 based wire cutter.
I'm using an D1 mini(ESP8266) for controlling the stepper and the servo.

![alt tag](https://github.com/RafalVonau/ESP8266_WireCutter/blob/main/blob/assets/wirecutter.jpg)

The project was built on D1 mini board connected to PC computer over USB as /dev/ttyUSB0 under Linux.
Uncomment and modify Wifi client settings in secrets.h file:
* #define WIFI_SSID                "SSID"
* #define WIFI_PASS                "PASS"

# Parts needed:
* D1 mini (ESP8266),
* DC/DC converter 12V to 5V,
* DC/DC converter 5V to 3.3V,
* 1 x TMC2208/A4988/equivalent,
* 1 x Stepper mottor Nema 17
* Servo SG90/equivalent
* Extruder MK8
* wires
* connectors

# D1 mini CONNECTIONS
* STEP      - GPIO5  (D1)
* DIR       - GPIO4  (D2)
* MOTTOR EN - GPIO2  (D4)
* SERVO     - GPIO12 (D6)
* BUTTON    - GPIO14 (D5)

# Wiring

![alt tag](https://github.com/RafalVonau/ESP8266_WireCutter/blob/main/blob/assets/schematic.svg)

## Building

The project uses platformio build environment. 
[PlatformIO](https://platformio.org/) - Professional collaborative platform for embedded development.

* install PlatformIO
* enter project directory
* connect Webmos D1 mini board to PC computer over USB cable.
* type in terminal:
  platformio run -t upload

You can also use IDE to build this project on Linux/Windows/Mac. My fvorite ones:
* [Code](https://code.visualstudio.com/) 
* [Atom](https://atom.io/)

Enjoy :-)

