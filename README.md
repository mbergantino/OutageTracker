# Outage Tracker


## Overview

The Outage Tracker logs when the power in your house goes out. 

Its designed to be run on a Teensy microcontroller, although with minimal tweaks it can likely run on any platform in the Arduino family. By adding a network adapter (to connect to an NTP server and optionally receive Web Requests), an SD Card reader (to record temporary data and historical data), and a battery backup power source and power management chip (to signal to the microcontroller when it has switched over) the Teensy can log outage events.


## Configurable Options

A list of configurable options:
```c
boolean adjustForDST = true;   // During a specified date/time window, it'll adjust the time accordingly
boolean dhcpEnabled  = true;   // Specifying DHCP as Enabled allows it to handle Lease Renewals
boolean WIZ820io     = true;   // Needs to take special care when starting up with the WIZ820io
boolean webService   = true;   // Web service gives access to the log file and resetting it
EthernetServer eServer(80);    // Specifying what port the Web Service will listen on
```


## Usage

The Web Service listens for http GET requests at the following endpoints:
```c
 - /        shows the History Log
 - /log     shows the History Log
 - /reset   recreates the History Log file anew
 - /test1   initiates a test outage for 30sec
 - /test2   initiates a test outage for 5min 30sec
```
For example, if the Teensy gets IP 192.168.0.2, loading 'http://192.168.0.2/log' in a browser from a machine on the same network will show the History Log file.

 
## Parts List

 - Teensy 3.1/3.2 - $20
   - https://www.pjrc.com/store/teensy32.html
 - 32.768 KHz RTC Clock Crystal - <$0.50
   - https://www.digikey.com/products/en?mpart=ECS-.327-12.5-13X
 - WIZ820io/WIZ850io Networking - $20
   - https://www.digikey.com/product-detail/en/wiznet/WIZ850IO/1278-1043-ND/8789619
 - WIZ Adapter with SD card reader - $6
   - https://www.pjrc.com/store/wiz820_sd_adaptor.html
 - Power Management (MAX1555) Module and Battery Harness - $10
   - https://www.tindie.com/products/onehorse/lipo-battery-charger-add-on-for-teensy-31/ 
   - Spec Sheet: https://datasheets.maximintegrated.com/en/ds/MAX1551-MAX1555.pdf
 - JST LiPo battery (these are just some options):
   - 1.2Ah ($10): https://www.adafruit.com/product/258 
   - 2.0Ah ($13): https://www.adafruit.com/product/2011

 
## Technical Notes

I brought my Teensy down from the default (overclocked) 96 MHz to 48MHz since I didn't need too much speed and thought I'd appreciate the power gains more (if they're not completely negligible).

I installed a Coin Battery Holder when I installed the crystal (as shown here: https://www.pjrc.com/teensy/td_libs_Time.html), and at some point I'll probably just remove it, but if I were to do things over again I would just skip that component since I make up for it with how I handle time management in software.


## Planned Improvements

The program should attempt to recover the time in case the coin battery is used (in place of the NTP sync on startup). In that case, if time is ever lost then you can assume if time is ever lost that the coin battery is dead and this fact can be alerted to the user.

Instead of being limited to a Web Service, I'd like to introduce support for uploading results to Google Docs. I saw some mentions of this with an Arduino here:
 - https://www.instructables.com/id/Post-to-Google-Docs-with-Arduino/
 - https://temboo.com/arduino/others/update-google-spreadsheet
