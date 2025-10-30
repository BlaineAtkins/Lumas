/*
 * 
 * You can use this simple file (no external library dependancies) to flash your ESP32. It will then download and install the full program from GitHub
 * Just change the ssid and password below to your network
 * 
 * Optionally, you can enable logMAC, which will send the MAC address to a partner python program running on your computer before doing the firmware update. This is helpful if you are flashing a large batch of lumas and need to record all the new MAC addresses.
 */
const char* ssid = "Blaine-hotspot";
const char* password = "cowsrock";
bool logMAC = true; //true = send MAC to python program running on PC. Do not download firmware if MAC logging fails. false = Don't attempt PC connection, just download firmware immediately
const char* serverName = "http://10.0.0.105:8000";

const char hardwareVersion[]="3.1";
const char* manufacture_date="10/31/25";


char mqttPassword[15];

const char charSet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

 
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <EEPROM.h>
#include <HTTPClient.h>

WiFiClient espClient;


#define URL_fw_Bin "https://raw.githubusercontent.com/BlaineAtkins/Lumas/main/Firmware/LumasFirmware.bin"


void setup() {
  Serial.begin(9600);


  // put your setup code here, to run once:
  Serial.println();
  Serial.print("Mac Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connecting to ");
  Serial.println(ssid);


  WiFi.persistent(false); //don't save configuration to flash, otherwise it won't start a captive portal which lets the user set the strip length on next boot of real firmware
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("successfully connected to wifi.");

  //1: Generate MQTT password
  for(int i=0;i<14;i++){
    int randomIndex = random(sizeof(charSet) - 1); // -1 to exclude the null terminator
    char randomChar = charSet[randomIndex];
    mqttPassword[i]=randomChar;
  }
  //Serial.println(mqttPassword);

  //2: Store values in EEPROM (MQTT password, hardware version, heart name "new heart")
  EEPROM.begin(173);
  EEPROM.write(0,'A'); //ABC tells program that EEPROM is initialized
  EEPROM.write(1,'B');
  EEPROM.write(2,'C');
  for(int i=3;i<173;i++){ //clear EEPROM of any old values
    EEPROM.write(i,0);
  }
  EEPROM.put(3,"New Heart");
  EEPROM.put(29,hardwareVersion);
  EEPROM.put(40,mqttPassword);
  EEPROM.commit();
  EEPROM.end();

  EEPROM.begin(173);
  for(int i=0;i<173;i++){
    //char hi;
    //EEPROM.read(i,hi);
    Serial.print((char)EEPROM.read(i));
  }

  //3: Send above values to python script, along with manufacture date and MAC

  int httpResponseCode=0;
  if(logMAC){

      WiFiClient client;
      HTTPClient http;
    
      // Your Domain name with URL path or IP address with path
      http.begin(client, serverName);
      
      // If you need Node-RED/server authentication, insert user and password below
      //http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");
      
      // Specify content-type header
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      // Data to send with HTTP POST
      String httpRequestData = WiFi.macAddress()+",New Heart,"+String(hardwareVersion)+","+mqttPassword+","+manufacture_date;        
      // Send HTTP POST request
      httpResponseCode = http.POST(httpRequestData);
      
      // If you need an HTTP request with a content type: application/json, use the following:
      //http.addHeader("Content-Type", "application/json");
      //int httpResponseCode = http.POST("{\"api_key\":\"tPmAT5Ab3j7F9\",\"sensor\":\"BME280\",\"value1\":\"24.25\",\"value2\":\"49.54\",\"value3\":\"1005.14\"}");

      // If you need an HTTP request with a content type: text/plain
      //http.addHeader("Content-Type", "text/plain");
      //int httpResponseCode = http.POST("Hello, World!");
      
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
        
      // Free resources
      http.end();
      
  }


    if(httpResponseCode==200 || logMAC==false){

      Serial.println("Will now attempt to download new firmware...");

      WiFiClientSecure client;
      //WiFiClientSecure * client = new WiFiClientSecure;
      //client.setCACert(rootCACertificate);
      client.setInsecure(); //prevents having the update the CA certificate periodically (it expiring breaks github updates which SUCKS cause you have to update each ornament manually with the new certificate
      httpUpdate.setLedPin(LED_BUILTIN, LOW);
      t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);

      switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        Serial.println("This may indicate this device is stuck in a captive portal");
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;
      }
    }else if(httpResponseCode!=200){
      Serial.println("ERROR SENDING MAC TO PC! Please make sure the server is running and try again");
      Serial.println("If you do not want to log the MAC (just download firmware), set logMAC to false");
    }
}

void loop() {
  // put your main code here, to run repeatedly:

}