//Interrupt Dependencies
#include <avr/io.h>
#include <avr/interrupt.h>
 
//NTP & Networking Dependencies
#include <TimeLib.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>

//SD Card Dependencies
#include <SD.h>
//#include <SPI.h>

//Interrupt Vars
const int backupPin   = 0;                  // Pin that Backup signal is attached to (MAX1555 module uses Pin 0)
boolean onBB          = false;              // flag to indicate when we're on Battery Backup power

// NTP Vars
IPAddress timeServer(204,9,54,119);         // pool.ntp.org (204.9.54.119)
const int NTP_PACKET_SIZE = 48;             // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];         // Buffer to hold incoming & outgoing packets

// Network Vars
EthernetUDP Udp;
byte mac[]={0xDE,0xAD,0xFE,0xED,0xBE,0xEF}; // NIC's MAC Address
unsigned int localPort = 8888;              // local port to listen for UDP packets
boolean WIZ820io       = true;              // flag to indicate if we are using the WIZ820io (vs the WIZ850io)
boolean webService     = true;              // true = Web Server On, false = ignore connections
EthernetServer eServer(80);                 // setup web server to listen on port 80
boolean dhcpEnabled    = true;              // true = DHCP, false = Static Configuration
const int leaseTime    = 5;                 // number of minutes in DHCP lease time
IPAddress ip(192, 168, 0, 2);               // Static IP of arduino
IPAddress dnserver(192, 168, 0, 1);         // DNS server ip
IPAddress gateway(192, 168, 0, 1);          // Router's gateway address
IPAddress subnet(255, 255, 255, 0);         // Subnet Mask

// Time Vars
const int UPDATE_FREQ = SECS_PER_HOUR;      // Frequency to update System Time (default: once per hour)
const int TIMEZONE    = -5;                 // Eastern Standard Time (USA) ... go Red Sox
boolean adjustForDST  = true;               // true = update on a schedule to +1 to TIMEZONE
int dstAdjustment     = 0;                  // DST adjustment
time_t lastUpdate     = 0;                  // when the digital clock was last udpated
time_t outageStarted  = 0;                  // when the outage began

// SD Vars
const int chipSelect  = 4;                  // Wiz820+SD board: Pin 4
const String tmpFile = "out.tmp";           // temp file used during an outage
const String fullLog  = "outage.log";       // Permanent log file


void setup() {
  // General setup
  Serial.begin(9600);
  while (!Serial) ;                         // wait for serial port to connect. Needed for Leonardo only.
  delay(250);

  Serial.println("Outage Tracker Starting Up");
  Serial.println("**************************");

  //Reset WIZ820io (WIZ850io handles quick power resets better)
  if (WIZ820io) {
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);                   // begin reset the WIZ820io
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);                 // de-select WIZ820io
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);                  // de-select the SD Card
    digitalWrite(9, HIGH);                  // end reset pulse
  }
  
  //Setup Networking
  Serial.println("Initializing network interface...");
  if (dhcpEnabled) {
    while (Ethernet.begin(mac) == 0) {                  // For DHCP
      // Retries to account for when the router is starting up after an outage too
      Serial.println("Failed to establish communication with the router. Retrying in 10 seconds...");
      delay(10000); //msec
    }
  }
  else {
    Ethernet.begin(mac, ip, dnserver, gateway, subnet); // For Static IP
  }
  Serial.print("Teensy IP Address: ");
  Serial.println(Ethernet.localIP());
  Udp.begin(localPort);
  
  // Sync NTP on every boot up
  setSyncProvider(getNtpTime);
  delay(100);
  lastUpdate = now();
  Serial.print("Current system time: ");
  Serial.println(assembleTime(lastUpdate));
  
  // Setup SD Card
  Serial.println("Initializing SD card...");
  String startFile = "startup.tmp";
  if (!SD.begin(chipSelect)) {
    Serial.println("SD initialization failed to start!");
    return;
  }
  // Clean up lingering cruft
  if (SD.exists(startFile.c_str())) {  
    SD.remove(startFile.c_str());                        
  }
  // Test Write
  // NOTE: We can only open one file at a time!
  File myFile = SD.open(startFile.c_str(), FILE_WRITE);  
  if (myFile) {
    Serial.print("Testing write to the SD card...");   	// if the file opened okay, write to it
    myFile.print("I'm writing already calm down...");
    myFile.println(assembleTime(now()));
    myFile.close();                                     // close the file, so we can open others
    SD.remove(startFile.c_str());
	
    // Create History File If It Does Not Already Exist
    recreateHistoryFile(false);
    Serial.println("SD Card initialization done.");
  }
  else {
    // If the file didn't open, print an error (would prefer a try/catch, but not available)
    Serial.print("SD initialization failed to write to card! ");
    Serial.println(startFile);
    return;
  }
  
  // Check if we're on Battery Backup or if power is on/restored
  Serial.println("Initializing power restoration logic...");
  pinMode(backupPin, INPUT);        					          // Sets the digital pin as input
  
  // When starting up following an outage, if the temp outage file exists assume we were in an outage when power failed
  if (SD.exists(tmpFile.c_str())) {  								
    if (digitalRead(backupPin) == LOW) {				        // Read from the digital pin (returns either HIGH or LOW)
      //coming out of the outage
      outageFinished();
    }
    else {
      // Still in the outage
      onBB = true;
    }
  }
  
  Serial.println("All Setup Processes Completed Successfully!");
}

void loop() {
  // Process interrupts here as opposed to when Pin state is changing as we have ~infinite time here
  checkOutageStatus();

  // Renew lease from DHCP server
  if (!onBB && dhcpEnabled) {
    Ethernet.maintain();
  }
  
  // When on line power, compare current time last NTP sync time to update system time at scheduled interval
  if (!onBB && now() > (lastUpdate + UPDATE_FREQ)) { 
    setSyncProvider(getNtpTime);
    delay(100);
    lastUpdate = now();
    Serial.print("System time updated to: ");
    Serial.println(assembleTime(lastUpdate));
  }

  // Process web requests
  if (webService) {
    webServerService();
  }
}

void checkOutageStatus() {
  if (digitalRead(backupPin) == LOW && onBB) {
    // We just came off battery backup power, so stop the timer and record outage event
    boolean leaseExpired = outageFinished();
	
    // If outside of lease time window, restart the network connection completely (for DHCP only)
    // Don't worry about lease renewal here, because it's the next operation that will happen in loop
    if (dhcpEnabled && leaseExpired) {
      while (Ethernet.begin(mac) == 0) {
        // Retries to account for when the router is starting up after an outage too
        Serial.println("Failed to establish communication with the router. Retrying in 10 seconds...");
        delay(10000); //msec
      }
    }
	
    // Clear the flag
    onBB = false;
  }
  else if (digitalRead(backupPin) == HIGH && !onBB) {
    // We just went on battery backup power, so start a timer
    outageStarting();
	
    // Set the flag
    onBB = true;
  }
}

void outageStarting() {
  outageStarted = now();
  Serial.print("An outage has begun: ");
  Serial.println(assembleTime(outageStarted));
  
  // Start fresh as we don't have any use for stale data and only want 1 line in this file any ways
  if (SD.exists(tmpFile.c_str())) {
    SD.remove(tmpFile.c_str());
  }
  
  // Write Start Time to temp file
  File myFile = SD.open(tmpFile.c_str(), FILE_WRITE);
  myFile.print(outageStarted);
  myFile.close();
}

bool outageFinished() {
  time_t outageEnded = now();
  
  // Local var takes precedence over temp file
  if (outageStarted == 0) {
    if (!SD.exists(tmpFile.c_str())) {
      Serial.println("ERROR: Trying to record an event without a start time.");
      return false;
    }
	
    // Read from temp file
    File myFile = SD.open(tmpFile.c_str(), FILE_READ);
    outageStarted = myFile.read();
    myFile.close();
  }
  
  // Write same to long term log
  File myFile = SD.open(fullLog.c_str(), FILE_WRITE); 	// Again note: we can only open one file at a time
  String message = "Outage Event - Started: " + assembleTime(outageStarted) +
	           " Ended: " + assembleTime(outageEnded) +
                   " Duration: " + breakoutTime(outageEnded-outageStarted) + "<br>";
  Serial.println(message);
  myFile.println(message);
  myFile.close();
	
  // Clear tracker
  outageStarted = 0;
  
  // Delete temp file
  if (SD.exists(tmpFile.c_str())) {
    SD.remove(tmpFile.c_str());
  }
  
  // Determine if the lease would have expired
  if (dhcpEnabled) {
    return (outageEnded-outageStarted) > leaseTime*60;
  }
  
  return false;
}

String breakoutTime(long t) {
  int secInMin = 60;
  int secInHour = 60*secInMin;
  
  // subtract out number of hours
  int hours = t/secInHour;
  t -= (hours * secInHour);
  
  // subtract out number of minutes
  int minutes = t/secInMin;
  t -= (minutes * secInMin);
  
  // remainder of seconds
  int seconds = t;

  String time = hours;
  time += ":";
  if(minutes < 10)
    time += "0";
  time += minutes;
  time += ":";
  if(seconds < 10)
    time += "0";
  time += seconds;
  
  return time;
}

String assembleTime(time_t t) {
  String time = "";
  time += month(t);
  time += "/";
  time += day(t);
  time += "/";
  time += year(t);
  time += " ";
  time += hourFormat12(t);                              // switch to hour(t) for 24-hour format
  time += ":";
  if(minute(t) < 10)
    time += "0";
  time += minute(t);
  time += ":";
  if(second(t) < 10)
    time += "0";
  time += second(t);
   
  return time;
}

void webServerService() {
  // listen for incoming clients
  
  EthernetClient client = eServer.available();          // Start Web Server Service
  String requestUrl = "";                               // Where we'll store the request URL string
  
  if (client) {
    Serial.println("New client connection received.");
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {                         // client data available to read
        char c = client.read();                         // read 1 byte (character) from client
		
        // last line of client request is blank and ends with \n
        // respond to client only after last line received
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          break;
        }
      
        // every line of text received from the client ends with \r\n
        if (c == '\n') {
          // last character on line of received text
          // starting new line with next character read
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // a text character was received from client
          currentLineIsBlank = false;
		  
          //read char by char HTTP request
          if (requestUrl.length() < 100) {
            requestUrl += c;  //store characters to string 
          }
        }
      }
    }
	
    // Send web page content
    if (requestUrl.length() != 0 && requestUrl.startsWith("GET")) {
      requestUrl = requestUrl.substring(4,requestUrl.length());     // Remove "GET " from the front
      requestUrl = requestUrl.substring(0,requestUrl.indexOf(" ")); // Trim at the space
    }
    Serial.print("Request URL: ");
    Serial.println(requestUrl);
    // For any blank or request that asks for the "log" file we'll feed it up
    if ( requestUrl.length() == 0 || 
      requestUrl.toLowerCase().indexOf("get / ") != -1 || 
      requestUrl.toLowerCase().indexOf("log") != -1) {
      File webFile = SD.open(fullLog.c_str(), FILE_READ);        // open web page file
      if (webFile) {
        while(webFile.available()) {
          client.write(webFile.read());                          // actual sending of web page to client
        }
        webFile.close();
      }
      else {
        Serial.println("File not found!");
        client.println("HTTP/1.1 404 NOT FOUND");
      }
    }
    // For any request that asks to "reset" the log file we'll recreate it fresh
    else if ( requestUrl.toLowerCase().indexOf("reset") != -1) {
      recreateHistoryFile(true);
    }
	
    delay(250);                                         // give the web browser time to receive the data
    client.stop();                                      // close the connection
    Serial.println("client disconnected");
  }
}

void recreateHistoryFile(boolean createAnew) {
  // Create new History Log if it doesn't already exist or if explicitly told
  if (createAnew || !SD.exists(fullLog.c_str())) {
    if (SD.exists(fullLog.c_str())) {
      SD.remove(fullLog.c_str());
    }
	
    File myFile = SD.open(fullLog.c_str(), FILE_WRITE);
    myFile.println("Outage Tracker Log Event History<br>");
    myFile.println("********************************<br>");
    myFile.close();
  }
}

time_t getNtpTime() {
  while (1) {
    Serial.println("Starting NTP sync");
    while (Udp.parsePacket() > 0) ;                     // discard any previously received packets
    Serial.println("Transmiting NTP Request");
    sendNTPpacket(timeServer);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {               // try for up to 1.5 seconds
      int size = Udp.parsePacket();
      if (size >= NTP_PACKET_SIZE) {
        Serial.println("Receiving NTP Response");
      
        // read packet into the buffer
        Udp.read(packetBuffer, NTP_PACKET_SIZE);  
        unsigned long secsSince1900;
    
        // convert four bytes starting at location 40 to a long integer
        secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
        secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
        secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
        secsSince1900 |= (unsigned long)packetBuffer[43];
		
        // Check if we need to enable/disable DST
        if (adjustForDST) {
          long utcEpoch = secsSince1900 - 2208988800UL;
          dstCheck(utcEpoch);
        }

        return secsSince1900 - 2208988800UL + ((TIMEZONE + dstAdjustment) * SECS_PER_HOUR);
      }
    }
    // Retries to account for when we have no internet yet, thanks for nothing modem
    Serial.println("Failed to contact NTP server, maybe internet isn't working. Retrying in 10 seconds...");
    delay(10000); //msec
  }
  
  return 0; // return 0 if unable to get the time
}

void dstCheck(long utcEpoch) {
  int hr      = hour(utcEpoch);
  int dy      = day(utcEpoch);
  int mnth    = month(utcEpoch);
  int yr      = year(utcEpoch) % 100;                   // Need a 2 digit year
  int xSunday = (yr + yr/4 + 2) % 7;                    // Remainder will identify which day of month is Sunday
                                                        // by subtracting x from the one or two week window.
                                                        // First two weeks for March and first week for November.
								  
  // *********** DST BEGINS on 2nd Sunday of March @ 2:00 AM *********
  if(mnth == 3 && dy == (14 - xSunday) && hr >= 2) {
    dstAdjustment = 1;
  }
  else if((mnth == 3 && dy > (14 - xSunday)) || mnth > 3) {
    dstAdjustment = 1;
  }
  // ************* DST ENDS on 1st Sunday of Nov @ 2:00 AM ************
  else if(mnth == 11 && dy == (7 - xSunday) && hr >= 2) {
    dstAdjustment = 0;
  }
  else if((mnth == 11 && dy > (7 - xSunday)) || mnth > 11 || mnth < 3) {
    dstAdjustment = 0;
  }

  if (dstAdjustment == 1) {
    Serial.println("You are in DST.");
  }
  else {
    Serial.println("You are not in DST currently.");
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;                         // LI, Version, Mode
  packetBuffer[1] = 0;                                  // Stratum, or type of clock
  packetBuffer[2] = 6;                                  // Polling Interval
  packetBuffer[3] = 0xEC;                               // Peer Clock Precision
  
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:                 
  Udp.beginPacket(address, 123);                        //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
