# Outage Tracker


## Overview

The Outage Tracker is designed to be run on a Teensy microcontroller, although with minimal tweaks it can likely run on an platform from the Arduino family.

Its core function is to log when the power in your house goes out. So using a backup battery power source (and a power management chip to signal to the microcontroller when it has switched over), it tracks when events happen. It syncs time to an NTP server on a schedule using a network module, and writes log events to flat files on a memory card thanks to an SD Card module.


## Configurable Options

A list of configurable options:

```c
boolean adjustForDST = true;  // During a specified date/time window, it'll adjust the time accordingly
boolean dhcpEnabled  = true;  // Specifying DHCP as Enabled allows it to handle Lease Renewals
boolean WIZ820io     = true;  // Needs to take special care when starting up with the WIZ820io
boolean webService   = true;  // Web service gives access to the log file and resetting it
EthernetServer eServer(80);   // Specifying what port the Web Service will listen on
```


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

I installed a Coin Battery Holder on the Teensy (as shown here: https://www.pjrc.com/teensy/td_libs_Time.html), but if I were to do things over again I would skip that component as I make up for it with how I handle time management in software.


## Planned Improvements

Instead of being limited to a Web Service, I'd like to introduce support for uploading results to Google Docs. I saw some mentions of this with an Arduino here:
 - https://www.instructables.com/id/Post-to-Google-Docs-with-Arduino/
 - https://temboo.com/arduino/others/update-google-spreadsheet
