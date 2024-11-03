#include <Wire.h>
#include <AS5600.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EEPROM.h>
#include <Arduino_JSON.h>
#include <SPIFFS.h>
#include <esp32-hal-psram.h>

// User Config
#define DataSubSamplePoint          2       // 80/n SPS
#define FORMAT_SPIFFS_IF_FAILED     false   // First time -> true, and after then -> false
#define EEPROM_SIZE                 64

// IO Port Definition
#define ESP32_BUTTON          38
#define ESP32_LED             13

#define PIN_NEOPIXEL          0
#define NEOPIXEL_I2C_POWER    2

#define pin_ADC_DRDY_DOUT_1   27   // the number of the DATA1 pin
#define pin_ADC_DRDY_DOUT_2   15   // the number of the DATA2 pin
#define pin_ADC_DRDY_DOUT_3   32   // the number of the DATA3 pin
#define pin_ADC_DRDY_DOUT_4   14   // the number of the DATA4 pin

#define pin_SCLK_IN           25  // the number of the SCLK_IN_Pin
#define pin_SCLK_OUT          5   // the number of the SCLK_OUT_Pin

#define pin_PDWN              4   // the number of the PDWN_Pin

//  EEPROM Address Offset (0 ~ 63), 64 bytes
#define   EEPROM_AngleOffset      0
#define   EEPROM_LoadcellOffset1  2
#define   EEPROM_LoadcellOffset2  6
#define   EEPROM_LoadcellOffset3  10
#define   EEPROM_LoadcellOffset4  14
#define   EEPROM_                 18

#define   EEPROM_SSID             32  // 16 Char's
#define   EEPROM_PASS             48  // 16 Char's

// Calibration Gain value
#define   LoadcellCalGain1      0.0006      // R.A
#define   LoadcellCalGain2      0.0006      // L.A
#define   LoadcellCalGain3      0.000965    // R.L
#define   LoadcellCalGain4      0.000871    // L.L

////
////
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
////
AS5600  as5600;

bool  isWire = true; 

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// 
// Json Variable to Hold Sensor Readings
JSONVar readings;

long   LoadcellOffset1 = 0;
long   LoadcellOffset2 = 0;
long   LoadcellOffset3 = 0;
long   LoadcellOffset4 = 0;

short  encoderOffset;

struct sensors {
  volatile long DATA1;
  volatile long DATA2;
  volatile long DATA3;
  volatile long DATA4;
  unsigned long encoderAngle;
} Sensors = { 0 };

#define PACKET_SIZE   (36*60*40)    // 36 min * 60 sec * 40 samples/sec

bool  isPsram = true;
long  packetCounter = 0;
struct sensors *packetPointer;
struct sensors *workingPointer;

volatile long DATA1_Raw = 0;
volatile long DATA2_Raw = 0;
volatile long DATA3_Raw = 0;
volatile long DATA4_Raw = 0;

unsigned short  encoderRaw;

//
unsigned long uDATA1 = 0;
unsigned long uDATA2 = 0;
unsigned long uDATA3 = 0;
unsigned long uDATA4 = 0;

bool t, c;
byte d1, d2, d3;

volatile int idx = 0;
volatile bool blinkFlag = LOW;

unsigned short  DatSampleIdx = 0;
unsigned short  WebSampleIdx = 0;
bool quit = true;
bool save = false;
uint32_t  client_id;

// Interupt Service Routine 
void IRAM_ATTR  SPI_ISR() 
{
  idx++;
  uDATA1 = (uDATA1<<1) + digitalRead(pin_ADC_DRDY_DOUT_1);
  uDATA2 = (uDATA2<<1) + digitalRead(pin_ADC_DRDY_DOUT_2);
  uDATA3 = (uDATA3<<1) + digitalRead(pin_ADC_DRDY_DOUT_3);
  uDATA4 = (uDATA4<<1) + digitalRead(pin_ADC_DRDY_DOUT_4);
  
  if ( idx >= 24 ) {
    DATA1_Raw = (long)(uDATA1 << 8) >> 8;  Sensors.DATA1 = (DATA1_Raw - LoadcellOffset1) >> 6;  // 64
    DATA2_Raw = (long)(uDATA2 << 8) >> 8;  Sensors.DATA2 = (DATA2_Raw - LoadcellOffset2) >> 6;  // 64
    DATA3_Raw = (long)(uDATA3 << 8) >> 8;  Sensors.DATA3 = (DATA3_Raw - LoadcellOffset3) >> 4;  // 16  : signed 24 bits integer => 20 bits
    DATA4_Raw = (long)(uDATA4 << 8) >> 8;  Sensors.DATA4 = (DATA4_Raw - LoadcellOffset4) >> 4;  // 16    
    uDATA1 = uDATA2 = uDATA3 = uDATA4 = 0;    
    digitalWrite(ESP32_LED, blinkFlag = ( blinkFlag == HIGH )? LOW : HIGH);
  }
}

//
String processor(const String& var){
  if(var == "RIGHTARM"){
    return String((float)Sensors.DATA1 * LoadcellCalGain1, 3);
  }
  else if(var == "LEFTARM"){
    return String((float)Sensors.DATA2 * LoadcellCalGain2, 3);
  }
  else if(var == "RIGHTFOOT"){
    return String((float)Sensors.DATA3 * LoadcellCalGain3, 3);
  }
  else if(var == "LEFTFOOT"){
    return String((float)Sensors.DATA4 * LoadcellCalGain4, 3);
  }
  else if(var == "ANGLE"){
    return String((float)Sensors.encoderAngle * AS5600_RAW_TO_DEGREES, 2);
  }
  return String();
}

bool  isSPIFFS = true;

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    isSPIFFS = false;
    Serial.println("An error has occurred while mounting SPIFFS");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
}

char ssid[16] = "RouterName";
char password[16] = "Password";
bool NoNetwork = false;

bool initWIFI( bool bypass ) {
  int   i;

  // Set device as a Wi-Fi Station
  if ( bypass == false ) {
    EEPROM.readString(EEPROM_SSID, ssid, 16);
    Serial.println((String)"SSID : " + ssid);
    EEPROM.readString(EEPROM_PASS, password, 16);
    Serial.println((String)"PASSWORD : " + password);
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ");
  
  while ( true ) {
    for ( i = 0 ; i < 3 ; i++ ) {
      delay(1000);
      if ( WiFi.status() == WL_CONNECTED) break;
      Serial.print(".");
      WiFi.begin(ssid, password);
    }
    Serial.println("");
    
    if ( i == 3 ) {
      pixel.setPixelColor(0, pixel.Color(150, 0, 0));  pixel.show();
      
      delay(100);  WiFi.disconnect();

      Serial.println("scan start");
  
      // WiFi.scanNetworks will return the number of networks found
      int n = WiFi.scanNetworks();
      Serial.println("scan done");
      if (n == 0) {
          Serial.println("no networks found");
          NoNetwork = true;
          break;
      } else {
          Serial.print(n);
          Serial.println(" networks found");
          
          Serial.print(0);  Serial.println(") No Network, stand alone "); 
          for (int i = 0; i < n; ++i) {
              // Print SSID and RSSI for each network found
              Serial.print(i + 1);  Serial.print(") SSID : ");  Serial.print(WiFi.SSID(i));
              Serial.print(", RSSI : ");  Serial.print(WiFi.RSSI(i));  Serial.print(", ");
              Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
          }
          Serial.print("Select No. of SSIDs : ");
          while ( Serial.available() == 0 ) ;  String NoSSIDstr = Serial.readString();  Serial.println("");
          NoSSIDstr.trim(); 
                    
          int NoSSID = NoSSIDstr.toInt();
          if ( NoSSID == 0 ) { NoNetwork = true; break; }
          NoSSID--;
          Serial.print( WiFi.SSID(NoSSID) ); Serial.print(", Enter password : ");
          while ( Serial.available() == 0 ) ;  String passWordstr = Serial.readString();  Serial.println("");
          passWordstr.trim();  
          
          WiFi.SSID(NoSSID).toCharArray( ssid, WiFi.SSID(NoSSID).length() + 1);
          passWordstr.toCharArray( password, passWordstr.length()+1);
          
          Serial.println((String)"SSID : " + ssid);
          Serial.println((String)"PASSWORD : " + password);

          WiFi.mode(WIFI_AP_STA);
          WiFi.begin(ssid, password);
          
          EEPROM.writeString(EEPROM_SSID, WiFi.SSID(NoSSID));
          EEPROM.writeString(EEPROM_PASS, passWordstr);
          EEPROM.commit(); 
      }
    } else {
      Serial.print("Station IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.println();
      NoNetwork = false;
      break;
    }
  }  
  return NoNetwork;
}

void notifyClients(String state) {
  ws.textAll(state);
}

// At server
void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  switch ( type ) {
  case WS_EVT_CONNECT :
    //client connected
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->ping();
    break;
    
  case WS_EVT_DISCONNECT :
    //client disconnected
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
    break;
    
  case WS_EVT_ERROR :
    //error was received from the other end
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    break;
    
  case WS_EVT_PONG :
    //pong message was received (in response to a ping request maybe)
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
    break;
    
  case WS_EVT_DATA :
    // data packet
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if ( info->final && info->index == 0 && info->len == len ) {
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)? "text" : "binary", info->len);
      if ( info->opcode == WS_TEXT ) {
        data[len] = 0;
        Serial.printf("%s\n", (char*)data);

        if ( strcmp((char*)data, "START" ) == 0 ) {
            workingPointer = packetPointer;  packetCounter = 0;
            quit = false;
        } else if ( strcmp((char*)data, "STOP" ) == 0 ) {
            quit = true;
        } else if ( strcmp((char*)data, "SAVE" ) == 0 ) {
            client_id = client->id();
            save = true;;
        }    
      } else {
        for ( size_t i = 0 ; i < info->len ; i++ ) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.printf("\n");
      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if ( info->index == 0 ) {
        if ( info->num == 0 )
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)? "text" : "binary", info->index, info->index + len);
      if ( info->message_opcode == WS_TEXT ) {
        data[len] = 0;
        Serial.printf("%s\n", (char*)data);
      } else {
        for ( size_t i = 0 ; i < len ; i++ ) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.printf("\n");
      }

      if ( (info->index + len) == info->len ) {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if ( info->final ) {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)? "text" : "binary");
        }
      }
    } // if top
  } // switch case
}

void sendSensorsWs(AsyncWebSocketClient * client) {
  readings["s1"] = String((float)Sensors.DATA1 * LoadcellCalGain1, 3);
  readings["s2"] = String((float)Sensors.DATA2 * LoadcellCalGain2, 3); 
  readings["s3"] = String((float)Sensors.DATA3 * LoadcellCalGain3, 3);
  readings["s4"] = String((float)Sensors.DATA4 * LoadcellCalGain4, 3);
  if ( quit ) 
    readings["s5"] = "STOPPED";   // String(777.00, 2);
  else 
    readings["s5"] = ( Sensors.encoderAngle == 8192.0 ) ? "N.C." : String((float)Sensors.encoderAngle * AS5600_RAW_TO_DEGREES, 2);
  readings["s6"] = String(packetCounter);
  
  String buffer = JSON.stringify(readings);
  if ( buffer ) {
    if ( client ) {
        client->text(buffer);
    } else {
        ws.textAll(buffer);
    }
  }
}

//
//
void setup() {
  pinMode(pin_PDWN , OUTPUT);
  digitalWrite(pin_PDWN, LOW);

  Serial.begin(230400); Serial.println("");  Serial.println("");   
  
  // NEOPIXEL
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);

  pixel.begin();                        // INITIALIZE NeoPixel
  pixel.setBrightness(10);              // not so bright
 
  //
  pinMode(ESP32_BUTTON, INPUT);
  pinMode(ESP32_LED, OUTPUT);
  if ( !digitalRead(ESP32_BUTTON) ) { 
    NoNetwork = true;  isPsram = false;
  }
  if ( NoNetwork == false ) {
    pixel.setPixelColor(0, pixel.Color(0, 150, 0));  pixel.show();
  } else {
    pixel.setPixelColor(0, pixel.Color(150, 0, 0));  pixel.show(); 
  }
  while ( !digitalRead(ESP32_BUTTON) ) ; 
  
// Loadcell 1, 2, 3, 4 Data In
  pinMode(pin_ADC_DRDY_DOUT_1, INPUT); 
  pinMode(pin_ADC_DRDY_DOUT_2, INPUT);
  pinMode(pin_ADC_DRDY_DOUT_3, INPUT); 
  pinMode(pin_ADC_DRDY_DOUT_4, INPUT);

  // Clock control for SPI
  pinMode(pin_SCLK_IN, INPUT);
  attachInterrupt(digitalPinToInterrupt(pin_SCLK_IN), SPI_ISR, FALLING);      //SCLK PIN  FALLING INTERRUPT Initial

  pinMode(pin_SCLK_OUT, OUTPUT); 
  digitalWrite(pin_SCLK_OUT, LOW);

  //
  Serial.print("PSRAM found : ");  Serial.println( isPsram = psramFound());
  if ( isPsram ) {
    if ( psramInit() ) {
      Serial.println((String)"Memory available in PSRAM : " + ESP.getFreePsram());
      workingPointer = packetPointer = (struct sensors *)ps_calloc( PACKET_SIZE, sizeof(struct sensors));
    } else {
      Serial.println("PSRAM initilization failed");
      isPsram = false;
    }
  }
  
  // I2C
  Wire.begin();
  
  as5600.begin();
  isWire = as5600.isConnected();
  Serial.print("Connect AS5600 : ");
  Serial.println(isWire);

  //
  EEPROM.begin(EEPROM_SIZE);
  
  if ( isWire ) {
    encoderOffset = EEPROM.readShort(EEPROM_AngleOffset);
    as5600.setOffset( -encoderOffset * AS5600_RAW_TO_DEGREES );
    Serial.println((String)"Rotary Encoder(Angle) Offset read : " + encoderOffset);
  }
  
  LoadcellOffset1 = EEPROM.readLong(EEPROM_LoadcellOffset1);  
  Serial.println((String)"LoadCell_1(Right Arm) Offset read : " + LoadcellOffset1);
  
  LoadcellOffset2 = EEPROM.readLong(EEPROM_LoadcellOffset2);  
  Serial.println((String)"LoadCell_2(Left Arm)  Offset read : " + LoadcellOffset2);
  
  LoadcellOffset3 = EEPROM.readLong(EEPROM_LoadcellOffset3);  
  Serial.println((String)"LoadCell_3(Right Leg) Offset read : " + LoadcellOffset3);
  
  LoadcellOffset4 = EEPROM.readLong(EEPROM_LoadcellOffset4);  
  Serial.println((String)"LoadCell_4(Left Leg)  Offset read : " + LoadcellOffset4);

  initSPIFFS(); 
  if ( isSPIFFS ) {
    // 
    // Set the device as a Station and Soft Access Point simultaneously
    if ( initWIFI( NoNetwork ) == false ) { // No Netwok?

      ws.onEvent(onEvent);
      server.addHandler(&ws);
      
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", "text/html", false, processor);
      });
    
      server.serveStatic("/", SPIFFS, "/");
      server.begin();
    }
  } else {
    NoNetwork = true;
  }
  
  //
  // PWDN Control
  delay(100);  digitalWrite(pin_PDWN, HIGH);
  delay(100);  digitalWrite(pin_PDWN, LOW);
  delay(100);  digitalWrite(pin_PDWN, HIGH);
  pixel.setPixelColor(0, pixel.Color(0, 0, 150));  pixel.show();
  delay(400);
  //
  Serial.println("S : Start of sampling, Q : End of sampling");
  
  t = digitalRead(pin_ADC_DRDY_DOUT_1);
}

#define PACKET_LENGTH   (5*60*40)
//
//  
void loop() {
  c = digitalRead(pin_ADC_DRDY_DOUT_1);
  if ( (t == HIGH) && (c == LOW) ) {
    DatSampleIdx++;  WebSampleIdx++;
    if ( DatSampleIdx >= DataSubSamplePoint ) {
      DatSampleIdx = 0;
      
      idx = 0;
      for ( int i = 0 ; i < 24 ; i++ ) {
        digitalWrite(pin_SCLK_OUT, HIGH);   delayMicroseconds(10);
        digitalWrite(pin_SCLK_OUT, LOW);    delayMicroseconds(10);
      } 
      if ( isWire ) {
        Sensors.encoderAngle = as5600.readAngle();    // 0 ~ 2047
        encoderRaw = as5600.rawAngle();       //
      } else {
        Sensors.encoderAngle = encoderRaw = 8192;
      }
      pixel.setPixelColor(0, Sensors.DATA1 << 8);  pixel.show();    // for monitoring
      
      if ( isPsram ) {
        if ( !quit && packetCounter < PACKET_SIZE ) {
          *workingPointer++ = Sensors;  packetCounter++;
          if ( packetCounter % 400 == 0 ) {
            Serial.println((String)"Time(36 min) : " + packetCounter/40/60 + " min " +  (packetCounter/40)%60 + " sec"); 
          }
          if ( Serial.available() != 0 ) if ( Serial.read() == 'Q' ) { quit = true;  Serial.println("End of sampling"); }
          if ( packetCounter == PACKET_SIZE ) quit = true;
        } 
        if ( quit && save ) {
          Serial.println("Start of sending ...");  
          
          workingPointer = packetPointer;

          int pn = packetCounter / PACKET_LENGTH;
          int pr = packetCounter % PACKET_LENGTH;
          for ( int p = 0 ; p < pn ; p++ ) {
            ws.binary((uint32_t)client_id, (uint8_t*)workingPointer, (size_t)PACKET_LENGTH*sizeof(struct sensors));
            workingPointer += PACKET_LENGTH;
            delay(500); Serial.print(p); Serial.print(" ");
          }
          if ( pr ) {
             ws.binary((uint32_t)client_id, (uint8_t*)workingPointer, (size_t)pr*sizeof(struct sensors));
             workingPointer += pr;
             delay(100); Serial.print("/ "); Serial.println(pr); 
          } 
          ws.binary((uint32_t)client_id, (uint8_t*)workingPointer, (size_t)1);          
          
          Serial.println((String)"Total packet number : " + packetCounter ); 
          save = false;;
          Serial.println("End of sending"); 
        } else {
          if ( Serial.available() != 0 ) if ( Serial.read() == 'S' ) { 
            workingPointer = packetPointer;  packetCounter = 0;  quit = false;
            Serial.println("Start of sampling ... ");
          }          
        }       
      } else {
        Serial.print(Sensors.DATA1); Serial.print(" ");    
        Serial.print(Sensors.DATA2); Serial.print(" "); 
        Serial.print(Sensors.DATA3); Serial.print(" ");
        Serial.print(Sensors.DATA4); Serial.print(" ");
        Serial.println(Sensors.encoderAngle); 
      }
      
      idx = 0;
      c = digitalRead(pin_ADC_DRDY_DOUT_1);
    }
    if ( !NoNetwork && (WebSampleIdx >= 16) ) {
      WebSampleIdx = 0;
      
      // Send sensor messages to clients with the Sensor Readings
      sendSensorsWs(null);    // to All Clients
       
      ws.cleanupClients();
    }  
  }

  if ( !digitalRead(ESP32_BUTTON) && !digitalRead(ESP32_BUTTON) && !digitalRead(ESP32_BUTTON) ) {
    digitalWrite(pin_PDWN, LOW);
    digitalWrite(ESP32_LED, HIGH);
    
    if ( isWire ) {
      as5600.setOffset( -encoderRaw * AS5600_RAW_TO_DEGREES );
      EEPROM.writeShort(EEPROM_AngleOffset, (short)encoderRaw );       
    }
    EEPROM.writeLong(EEPROM_LoadcellOffset1, DATA1_Raw );  LoadcellOffset1 = DATA1_Raw;
    EEPROM.writeLong(EEPROM_LoadcellOffset2, DATA2_Raw );  LoadcellOffset2 = DATA2_Raw;
    EEPROM.writeLong(EEPROM_LoadcellOffset3, DATA3_Raw );  LoadcellOffset3 = DATA3_Raw;
    EEPROM.writeLong(EEPROM_LoadcellOffset4, DATA4_Raw );  LoadcellOffset4 = DATA4_Raw;  
    EEPROM.commit();  
  }
  while ( !digitalRead(ESP32_BUTTON) )  c = digitalRead(pin_ADC_DRDY_DOUT_1);
  digitalWrite(pin_PDWN, HIGH);
   
  t = c;
}

  
