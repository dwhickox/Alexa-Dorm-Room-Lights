#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <functional>
#include <Wire.h> //I2C library
#include <RtcDS3231.h> //RTC library
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>

// Declare function prototypes
bool connectUDP();
void prepareIds();
void respondToSearch();
void startHttpServer();
//void hierarchy();
//int light();
//void rgb();
//void timecheck();
//int touch();
//int pir();

//Rtc declaration
RtcDS3231<TwoWire> rtcObject(Wire);

//oled decleration
#define OLED_RESET A0
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Change these to whatever you'd prefer:
String device_name = "Dorm Light";  // Name of device

bool debug = false;                       // If you want debug messages
bool squawk = true;                       // For on/off messages
//used fot each module of the lighting system
int alexastate = LOW;
int touchstate = LOW;
int timestate = LOW;
int bulbstate = LOW;
int pirstate = LOW;

//Pin assignments

int relayPin = D0;                        // Pin for relay control
//D1-D2 reserved for rtc and lcd sdl and sda respectivly
int lightsense = D3;                      // Pin for light sensor dat
int touchsense = D4;                      // Pin for touch sensor dat
int ws2812 = D5;                          // Pin for led control
int pirsense = D6;                        // Pin for pir sensor dat

//pins for software tx
int softRX = D7;
int softTX = D8;

//odd variables in progress of changing/removing
int touchcheck = 0;
int lightcheckhigh = 0;
int lightchecklow = 0;
int lightmilli = 0;
int hierchange = 0;
int hierold = 0;
int hiernew = 0;
int hierchangeamount = 0;

// counts since a change, this allows for propper hierachical change and
// timeout delays
int lightcount = 0;
int touchcount = 0;
int pircount = 0;
int rgbcount = 0;
#define PIN D5
#define LED_COUNT 1
#define BRIGHTNESS 50

//setup the bluetooth softserial, soft serial is used to allow programming without
//removing the bluetooth module (and transmition faster that 115200 is not
//required for a simple on off signal
SoftwareSerial blueSerial(softRX, softTX); // RX, TX

//Setup the ws2812 indicator
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(LED_COUNT, PIN, NEO_GRB + NEO_KHZ800);

// Some UDP / WeMo specific variables:
WiFiUDP UDP;
IPAddress ipMulti(239, 255, 255, 250);
unsigned int portMulti = 1900; // local port to listen on
ESP8266WebServer HTTP(80);
String serial; // Where we save the string of the UUID
String persistent_uuid; // Where we save some socket info with the UUID

// Buffer to save incoming packets:
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];

void setup() {
  // Begin Serial:
  Serial.begin(115200);
  delay(1000);
  //added soft serial for bluetooth serial
  blueSerial.begin(9600);
  delay(500);

  pixels.begin();
  pixels.show(); // Initialize all pixels to 'off'
  Serial.println("pixels cleared");
  delay(500);

  //sets up rtc
  rtcObject.Begin();    //Starts I2C
  RtcDateTime currentTime = RtcDateTime(17, 06, 07, 12, 50, 0); //define date and time object
  rtcObject.SetDateTime(currentTime);
  Serial.println("RTC setup");

  //setup the oled
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
  // init done
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.clearDisplay();
  display.println("It's Alive");
  display.display();
  delay(1);
  display.startscrollright(0x00, 0x0F);
  Serial.println("Oled Setup");


  //Setup the pins for input
  pinMode(lightsense, INPUT);
  pinMode(touchsense, INPUT);
  pinMode(pirsense, INPUT);

  // Setup the pin for output:
  pinMode(ws2812, OUTPUT);
  digitalWrite(ws2812, LOW); // makes sure there is no odd data in the led
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Start with light off
  //  digitalWrite(relayPin, LOW); // Start with light on
  //this assumes you use a relay that is high off, and low on

  // Set the UUIDs and socket information:
  prepareIds();

  // Get settings from WiFi Manager:
  WiFiManager wifiManager;
  // wifiManager.resetSettings(); // Uncomment this to test WiFi Manager function
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  // Wait til WiFi is connected properly:
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    counter++;
  }
  Serial.println("Connected to WiFi");

  // Connect to UDP:
  bool udpConnected = connectUDP();
  if (udpConnected) {
    startHttpServer(); // Start the HTTP Server
  }

}

void loop() {
  HTTP.handleClient();
  delay(1);

  // If there are packets, we parse them:
  int packetSize = UDP.parsePacket();

  if (packetSize) {
    if (debug) {
      Serial.println("");
      Serial.print("Received packet of size ");
      Serial.println(packetSize);
      Serial.print("From ");
      IPAddress remote = UDP.remoteIP();

      for (int i = 0; i < 4; i++) {
        Serial.print(remote[i], DEC);
        if (i < 3) {
          Serial.print(".");
        }
      }

      Serial.print(", port ");
      Serial.println(UDP.remotePort());
    }

    int len = UDP.read(packetBuffer, 255);

    if (len > 0) {
      packetBuffer[len] = 0;
    }

    String request = packetBuffer;

    if (request.indexOf('M-SEARCH') > 0) {
      if (request.indexOf("urn:Belkin:device:**") > 0) {
        if (debug) {
          Serial.println("Responding to search request ...");
        }
        respondToSearch();
      }
    }
  }
  rgbcount = rgb(rgbcount);
  //Serial.println("lightcount before light");
  //Serial.println(lightcount);
  lightcount = light(lightcount);
  //Serial.println("lightcount after light");
  //Serial.println(lightcount);
  touch();
  //touchcount = touch(touchcount);//this does not need to have a timer at this point but
  //i may choose to have a touch on only last so long
  //under some modes if nessisary
  pircount = pir(pircount);
  hierarchy();
  delay(10);
}

int absv(int val) { // this is defined because abs is currently incorectly defined in the esp compiler
  if (val < 0) {
    val = - val;
    return val;
  }
}

void prepareIds() {
  uint32_t chipId = ESP.getChipId();
  char uuid[64];
  sprintf_P(uuid, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"),
            (uint16_t) ((chipId >> 16) & 0xff),
            (uint16_t) ((chipId >>  8) & 0xff),
            (uint16_t)   chipId        & 0xff);

  serial = String(uuid);
  persistent_uuid = "Socket-1_0-" + serial;
}

bool connectUDP() {
  boolean state = false;
  Serial.println("Connecting to UDP");

  if (UDP.beginMulticast(WiFi.localIP(), ipMulti, portMulti)) {
    Serial.println("Connection successful");
    state = true;
  }
  else {
    Serial.println("Connection failed");
  }

  return state;
}

void startHttpServer() {
  HTTP.on("/index.html", HTTP_GET, []() {
    if (debug) {
      Serial.println("Got Request index.html ...\n");
    }
    HTTP.send(200, "text/plain", "Hello World!");
  });

  HTTP.on("/upnp/control/basicevent1", HTTP_POST, []() {
    if (debug) {
      Serial.println("########## Responding to  /upnp/control/basicevent1 ... ##########");
    }


    //for (int x=0; x <= HTTP.args(); x++) {
    //  Serial.println(HTTP.arg(x));
    //}

    String request = HTTP.arg(0);
    if (debug) {
      Serial.print("request:");
      Serial.println(request);
    }


    if (request.indexOf("<BinaryState>1</BinaryState>") > 0) {
      if (squawk) {
        Serial.println("Got Alexa on request");
      }

      alexastate = 1;
    }

    if (request.indexOf("<BinaryState>0</BinaryState>") > 0) {
      if (squawk) {
        Serial.println("Got Alexa off request");
      }

      alexastate = 0;
    }

    HTTP.send(200, "text/plain", "");
  });

  HTTP.on("/eventservice.xml", HTTP_GET, []() {
    if (debug) {
      Serial.println(" ########## Responding to eventservice.xml ... ########\n");
    }

    String eventservice_xml = "<?scpd xmlns=\"urn:Belkin:service-1-0\"?>"
                              "<actionList>"
                              "<action>"
                              "<name>SetBinaryState</name>"
                              "<argumentList>"
                              "<argument>"
                              "<retval/>"
                              "<name>BinaryState</name>"
                              "<relatedStateVariable>BinaryState</relatedStateVariable>"
                              "<direction>in</direction>"
                              "</argument>"
                              "</argumentList>"
                              "<serviceStateTable>"
                              "<stateVariable sendEvents=\"yes\">"
                              "<name>BinaryState</name>"
                              "<dataType>Boolean</dataType>"
                              "<defaultValue>0</defaultValue>"
                              "</stateVariable>"
                              "<stateVariable sendEvents=\"yes\">"
                              "<name>level</name>"
                              "<dataType>string</dataType>"
                              "<defaultValue>0</defaultValue>"
                              "</stateVariable>"
                              "</serviceStateTable>"
                              "</action>"
                              "</scpd>\r\n"
                              "\r\n";

    HTTP.send(200, "text/plain", eventservice_xml.c_str());
  });

  HTTP.on("/setup.xml", HTTP_GET, []() {
    if (debug) {
      Serial.println(" ########## Responding to setup.xml ... ########\n");
    }


    IPAddress localIP = WiFi.localIP();
    char s[16];
    sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

    String setup_xml = "<?xml version=\"1.0\"?>"
                       "<root>"
                       "<device>"
                       "<deviceType>urn:Belkin:device:controllee:1</deviceType>"
                       "<friendlyName>" + device_name + "</friendlyName>"
                       "<manufacturer>Belkin International Inc.</manufacturer>"
                       "<modelName>Emulated Socket</modelName>"
                       "<modelNumber>3.1415</modelNumber>"
                       "<UDN>uuid:" + persistent_uuid + "</UDN>"
                       "<serialNumber>221517K0101769</serialNumber>"
                       "<binaryState>0</binaryState>"
                       "<serviceList>"
                       "<service>"
                       "<serviceType>urn:Belkin:service:basicevent:1</serviceType>"
                       "<serviceId>urn:Belkin:serviceId:basicevent1</serviceId>"
                       "<controlURL>/upnp/control/basicevent1</controlURL>"
                       "<eventSubURL>/upnp/event/basicevent1</eventSubURL>"
                       "<SCPDURL>/eventservice.xml</SCPDURL>"
                       "</service>"
                       "</serviceList>"
                       "</device>"
                       "</root>\r\n"
                       "\r\n";

    HTTP.send(200, "text/xml", setup_xml.c_str());
    if (debug) {
      Serial.print("Sending :");
      Serial.println(setup_xml);
    }
  });

  HTTP.begin();
  if (debug) {
    Serial.println("HTTP Server started ..");
  }
}

void respondToSearch() {
  if (debug) {
    Serial.println("");
    Serial.print("Sending response to ");
    Serial.println(UDP.remoteIP());
    Serial.print("Port : ");
    Serial.println(UDP.remotePort());
  }

  IPAddress localIP = WiFi.localIP();
  char s[16];
  sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);

  String response =
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age=86400\r\n"
    "DATE: Tue, 14 Dec 2016 02:30:00 GMT\r\n"
    "EXT:\r\n"
    "LOCATION: http://" + String(s) + ":80/setup.xml\r\n"
    "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
    "01-NLS: b9200ebb-736d-4b93-bf03-835149d13983\r\n"
    "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"
    "ST: urn:Belkin:device:**\r\n"
    "USN: uuid:" + persistent_uuid + "::urn:Belkin:device:**\r\n"
    "X-User-Agent: redsonic\r\n\r\n";

  UDP.beginPacket(UDP.remoteIP(), UDP.remotePort());
  UDP.write(response.c_str());
  UDP.endPacket();
  if (debug) {
    Serial.println("Response sent !");
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println("Soft AP's IP Address:");
  Serial.println(WiFi.softAPIP());
  Serial.println("WiFi Manager: Please connect to AP:");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.println("To setup WiFi Configuration");
}

//int touch(int counttouch) {
void touch() { //this may need timing in the future for proper control
  int val;
  int val1;
  //the combination of these two make sure the input is not a missfire or feathered on
  val = digitalRead(touchsense);
  delay(10); // this value can be adjusted based on how much anti feathering you want,
  //but you will have to hold the button longer to get the light to trigger on
  val1 = digitalRead(touchsense);
  //Serial.println("val and val 1 respectivly in touch");
  //Serial.println(val);
  //Serial.println(val1);
  if (val == HIGH && val1 == HIGH)// checks to see if the finger has been on the switch
    //for more than the delay, this is to reduce the chance of missfires, if it still miss
    //fires increase the time limit, and add another check, if that doesnt help, rewrite
    //the function to use counttouch to make sure it hase been preessed every cycle for a
    //certain time
  {
    if (touchcheck == 0)//checks to make sure that the finger has left the button before
      //(prevents on off on off while holding the button)
    {
      if (touchstate == LOW)
      {
        touchstate = HIGH;
        Serial.println("touchstate on"); \
        touchcheck = 1;
      }
      else
      {
        touchstate == LOW;
        Serial.println("touchstate off");
        touchcheck = 1;
      }
    }
  }
  else if (val == HIGH || val1 == HIGH)
  {
    //insert code here if something is needed when only one is on
  }
  else if (val == LOW && val1 == LOW)
  {
    touchcheck = 0;
    delay(100);
    //allow the finger time to come off the button
  }
  /*if (val == HIGH && touchcheck == 0)
    {
    delay(5);
    val1 = digitalRead(touchsense);
    if (val1 == HIGH)
    {
      touchcheck = 1;
      if (touchstate == LOW)
      {
        touchstate = HIGH;
        Serial.println("touchstate on");
      }
      else
      {
        touchstate = LOW;
        Serial.println("touchstate off");
      }
    }
    }

    else if (val == LOW)
    {
    touchcheck = 0;
    delay(100);// just to make sure that when the finger comes off the output is not feathered
    }
  */
  //return counttouch; //not curently used for timing maybe in the future
}

int light(int countlight) {
  int brightness;
  // PLEASE NOTE BRIGHTNESS IS LOW WHEN THE SENSOR SEES LIGHT!
  int milli;
  brightness = digitalRead(lightsense);
  milli = millis();
  //Serial.println(val);
  if (brightness == LOW) {
    if (bulbstate == LOW) {
      countlight = millis();
    }
    else if (bulbstate == HIGH && abs(countlight - milli) > 60000) {
      // we have just turned on the light if this is at the top of the hierarchy
      Serial.println("Light detected!");
      //the reason this is flipped is becasue the output should be off when there
      //is adequate light
      bulbstate = LOW;
      Serial.println("countlight on =");
      Serial.println(countlight);
    }
  } else {
    if (bulbstate == HIGH) {
      countlight = millis();
    }
    else if ((bulbstate == LOW) && abs(countlight - milli) > 60000) {
      //Serial.println("abscheck");
      //Serial.println(abs(countlight - milli));
      //Serial.println("countlight off");
      //Serial.println(countlight);
      //Serial.println("millis");
      //Serial.println(millis());

      //this may cause the light
      //to a flicker if there is motion when the clock restarts after 40 days but
      //this is very improbable

      // the light will now turn off if something else has not overridden it
      Serial.println("Light not detected!");
      bulbstate = HIGH;
    }
  }
  //Serial.println("countlight before return");
  //Serial.println(countlight);
  return countlight;



  /*  if(digitalRead(lightsense) == HIGH && bulbstate == 1)
    {
      if (lightcheckhigh = 0)
      {
        lightchecklow = 0;
        lightcheckhigh = 1;
        lightmilli = millis();
      }
      else
      {
        if (millis - lightmilli < 0)
        {
          lightmilli = millis();
        }
        else if (millis() - lightmilli >= 60000)
        {
          lightcheckhigh = 0;
          bulbstate == 0;
          Serial.println("bulbstate off");
        }
      }
    }
    else if(digitalRead(lightsense) == HIGH && bulbstate == 0)
    {
      lightchecklow = 0;
      lightcheckhigh = 0;
    }
    else if(digitalRead(lightsense) == LOW && bulbstate == 0)
    {
      if (lightchecklow = 0)
      {
        lightchecklow = 1;
        lightcheckhigh = 0;
        lightmilli = millis();
      }
      else
      {
        if (millis - lightmilli < 0)
        {
          lightmilli = millis();
        }
        else if ((millis() - lightmilli) >= 60000)
        {
          lightchecklow = 0;
          bulbstate == 1;
          Serial.println("bulbstate on");
        }
      }
    }
    else if(digitalRead(lightsense) == LOW && bulbstate == 1)
    {
      lightchecklow = 0;
      lightcheckhigh = 0;
    }
    return countlight;
  */

}
int rgb(int countrgb) {
  //this will be an error indicator in the future, but for now you get a nice purple
  int milli = millis();
  if ( abs(countrgb- milli)>1000)
  {
    pixels.setPixelColor(0, pixels.Color(random(45), random(40), random(40)));
    pixels.show();
    countrgb = millis();
  }
  return countrgb;
}

void hierarchy() {
  //key pirstate, alexastate, touchstate, timestate, bulbstate
  //binary 4 digets
  hiernew = pirstate * 10000 + alexastate * 1000 + touchstate * 100 + timestate * 10 + bulbstate;

  if (hierold != hiernew)
  {
    hierchange = 1;
    hierchangeamount = abs(hiernew - hierold);
    Serial.println("hierarchy change detected");
    Serial.println("heirchangeamount");
    Serial.println(hierchangeamount);
    hierold = hiernew;
  }

  if ((alexastate == HIGH || touchstate == HIGH || timestate == HIGH || bulbstate == HIGH || pirstate == HIGH) and hierchange == 1)
  {
    digitalWrite(relayPin, LOW);   // if Touch sensor is HIGH, then turn on
    Serial.println("light ON");
    Serial.println(hiernew);
    hierchange = 0;
  }
  if (alexastate == 0 && hierchange == 1 && hierchangeamount >= 1000)
  {
    digitalWrite(relayPin, HIGH);
    Serial.println("light off");
  }
}

int pir(int countpir) {
  int val;
  int milli;
  val = digitalRead(pirsense);
  milli = millis();
  if (val == HIGH) {
    if (pirstate == LOW) {
      // we have just turned on the light if this is at the top of the hierarchy
      Serial.println("Motion detected!");
      pirstate = HIGH;
      countpir = millis();
    }
    else
    {
      countpir = millis();// resets the clock every time there is motion detected
    }
  } else {
    if ((pirstate == HIGH) && abs(countpir - milli) > 60000) { //this may lead to a flicker if there is motion when the clock restarts after 40 days but this is very improbable
      // the light will now turn off if something else has not overridden it
      Serial.println("Motion ended!");
      pirstate = LOW;
    }
  }
  return countpir;
}
