#include "Adafruit_NeoPixel.h"

#include "ESP8266WiFi.h"
#include "WiFiUDP.h"
#include "ArduinoJson.h"

#include "ST7565.h"

#include "Time.h"

#define OFFSET_DST -4
#define OFFSET_NO_DST -5

#define _SSID "BigMcLargeHuge"
#define _PASS "pastrami"
#define W_API "90acf72d271b69ce"
#define W_LOC "Canada/Kanata"
#define NTP_PACKET_SIZE 48
#define NTP_SERVER_NAME "ca.pool.ntp.org"
#define LOCAL_PORT 2390
#define HTTP_PORT 80

IPAddress timeServerIP;

byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;
int count;

time_t dstStart, dstEnd;
static char respBuf[4096];

// HTTP request
#define WUNDERGROUND "api.wunderground.com"
const char WUNDERGROUND_REQ[] =
    "GET /api/" W_API "/conditions/q/" W_LOC ".json HTTP/1.1\r\n"
    "User-Agent: ESP8266/0.1\r\n"
    "Accept: */*\r\n"
    "Host: " WUNDERGROUND "\r\n"
    "Connection: close\r\n"
    "\r\n";

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
  updateWeather();
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

void updateWeather()
{
  // Use WiFiClient class to create TCP connections
  WiFiClient httpclient;
  if (!httpclient.connect(WUNDERGROUND, HTTP_PORT)) {
    Serial.println(F("connection failed"));
    return;
  }

  //Sends a request for the weather
  httpclient.print(WUNDERGROUND_REQ);
  httpclient.flush();

  int respLen = 0;
  bool skipHeaders = true;
  while (httpclient.connected() || httpclient.available()){
    if (skipHeaders) {
      String aLine = httpclient.readStringUntil('\n');
      if (aLine.length() <= 1) {
        skipHeaders = false;
      }
    }
    else {
      int bytesIn;
      bytesIn = httpclient.read((uint8_t *)&respBuf[respLen], sizeof(respBuf) - respLen);
      if (bytesIn > 0) {
        respLen += bytesIn;
        if (respLen > sizeof(respBuf))
          respLen = sizeof(respBuf);
      }
      else if (bytesIn < 0) {
        Serial.println("Error reading http");
      }
    }
    delay(1);
  }
  httpclient.stop();

  if (respLen >= sizeof(respBuf)) {
    Serial.println("Shit's too big");
  }
  respBuf[respLen++] = '\0';

  if (!showWeather(respBuf)) {
    Serial.println("Oh no error");
  }
  
}

bool showWeather(char *json)
{
  StaticJsonBuffer<3*1024> jsonBuffer;
  // Skip characters until first '{' found
  // Ignore chunked length, if present
  char *jsonstart = strchr(json, '{');
  //Serial.print(F("jsonstart ")); Serial.println(jsonstart);
  if (jsonstart == NULL) {
    Serial.println(F("JSON data missing"));
    return false;
  }
  json = jsonstart;

  // Parse JSON
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println(F("jsonBuffer.parseObject() failed"));
    return false;
  }

  // Extract weather info from parsed JSON
  JsonObject& current = root["current_observation"];
  const char *place = current["display_location"]["full"];
  Serial.print(place);
  const int temp_c = current["temp_c"];
  const float temp_cf = current["temp_c"];
  Serial.print(temp_c); Serial.print(F(" C, "));
  Serial.print(temp_cf, 1); Serial.print(F(" C, "));
  const char *weather = current["weather"];
  Serial.println(weather);

  return true;
}







