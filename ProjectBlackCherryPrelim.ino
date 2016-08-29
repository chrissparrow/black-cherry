#include "Adafruit_NeoPixel.h"

#include "ESP8266WiFi.h"
#include "WiFiUDP.h"

#include "ST7565.h"

#include "Time.h"

#define OFFSET_DST -4
#define OFFSET_NO_DST -5

#define _SSID "BigMcLargeHuge"
#define _PASS "pastrami"
#define NTP_PACKET_SIZE 48
#define NTP_SERVER_NAME "ca.pool.ntp.org"
#define LOCAL_PORT 2390

IPAddress timeServerIP;

byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;
int count;

time_t dstStart, dstEnd;


void setup()
{
  Serial.begin(115200);
  
  dstStart, dstEnd = 0;

  // Connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(_SSID);
  WiFi.begin(_SSID, _PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(LOCAL_PORT);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  count = 0;

  updateTime();
}

void loop(){
  delay(10000);
  
  count++;

  if (count == 5){
    updateTime();
    Serial.println("time synced");
    count = 0;
  }
  
  time_t t = now();
  Serial.print("It is: ");
  Serial.print(hour(t));
  Serial.print(":");
  Serial.print(minute(t));
  Serial.print(":");
  Serial.print(second(t));
  Serial.print(" ");
  Serial.print(monthStr(month(t)));
  Serial.print(" ");
  Serial.print(dayStr(weekday(t)));
  Serial.print(year(t));
}

void updateTime(){

  int32_t offset;

  //get a random server from the pool
  WiFi.hostByName(NTP_SERVER_NAME, timeServerIP);
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  
  // wait to see if a reply is available
  delay(2000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    
    // We've received a packet, read the data from it
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    time_t epoch = ((word(packetBuffer[40], packetBuffer[41])) << 16 | word(packetBuffer[42], packetBuffer[43])) - 2208988800UL;
    dst(epoch);
    if (dstStart == 0 || dstEnd == 0 || epoch - dstEnd > 70 * SECS_PER_DAY){ //If we haven't set DST times or if its a new year
      dst(epoch);
    }
    offset = (epoch > dstStart && epoch < dstEnd) ? OFFSET_DST : OFFSET_NO_DST;
    
    //Time should be accurate beyond this point
    setTime(epoch += SECS_PER_HOUR * offset); 
  }
  //Time wasn't retrieved, try again later
}

void dst(time_t moment){

  tmElements_t te;
  te.Year = year(moment)-1970;
  te.Month = 3;
  te.Day = 1;
  te.Hour = 0;
  te.Minute = 0;
  te.Second = 0;
  
  dstStart = makeTime(te) - SECS_PER_HOUR; //Arbitrarily move back an hour
  dstStart = nextSunday(dstStart) + SECS_PER_WEEK; //Second sunday of March
  dstStart += SECS_PER_HOUR * 2;

  te.Month = 11;
  dstEnd = makeTime(te) - SECS_PER_HOUR;
  dstEnd = nextSunday(dstEnd); //first Sunday of November
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}








