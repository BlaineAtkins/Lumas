/*
  To collect data, set up the Lumas in a room that's right on the auto-dim threshold. That is, if the room was any darker, you'd want the heart to be dim. Better if the room is slightly too bright than slightly too dark.
  Next, set up a PC on the same network and run the PhotoResistorReceiveValues python program on it. Make sure to change the IP address in both that and this program.
  After that, start this program running. It will take about 12 hours to complete.
  You can generate an equation from the results using the python curve fitting program
*/

//const char* ssid = "WeDontHaveWifi";
//const char* password = "228baldwin";
//const char* ssid = "Blaine-hotspot";
//const char* password = "cowsrock";
//const char* ssid = "Blaine-Laptop-Hotspot";
//const char* password = "12345678";
const char* ssid = "Music Madness";
const char* password = "123eyesonme";
bool logMAC = true; //true = send MAC to python program running on PC. Do not download firmware if MAC logging fails. false = Don't attempt PC connection, just download firmware immediately
const char* serverName = "http://10.0.0.105:8000";

 
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>

WiFiClient espClient;

Adafruit_NeoPixel lights(12, 27, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(9600);
  // put your setup code here, to run once:
  Serial.println();
  Serial.print("Mac Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connecting to ");
  Serial.println(ssid);

  lights.begin();
  lights.clear();
  lights.show();

  WiFi.persistent(false); //don't save configuration to flash, otherwise it won't start a captive portal which lets the user set the strip length on next boot of real firmware
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("successfully connected to wifi.");
  
  int colorVal=0;
  int brightnessVal=0;

  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverName);

  for(int color=0;color<1024;color++){
    for(int brightness=0;brightness<255;brightness=brightness+2){
      if(brightness>255){
        break;
      }

      for(int i=0;i<12;i++){
        lights.setPixelColor(i, lights.Color(getColor(color,'r')*(brightness/255.0),getColor(color,'g')*(brightness/255.0),getColor(color,'b')*(brightness/255.0)));
      }
      lights.show();

      if(brightness==0){
        delay(4000); //settling time for decrease in brightness (1000 is worst case -- full white to full off in the dark, and even then it's probably closer to 500ms). HOWEVER -- first run showed measured brightness still settling after 1s until around 3s.
      }else{
        delay(200); //settling time for increase in brightness (200 is worst case -- full off to full white). Is 200 wrong? Might it be 400?
      }
      int result=analogRead(34);
      
      //sendVal(String(color)+","+String(brightness)+","+String(result));
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.POST(String(color)+","+String(brightness)+","+String(result));
      //Serial.println(result);
    }
  }
  
}

void loop() {
  // put your main code here, to run repeatedly:
  //sendVal("helloooo");
  delay(1000);
}


void sendVal(String data){
  int httpResponseCode=0;

  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverName);
  
  // If you need Node-RED/server authentication, insert user and password below
  //http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");
  
  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // Data to send with HTTP POST
  //String httpRequestData = WiFi.macAddress();           
  // Send HTTP POST request
  httpResponseCode = http.POST(data);
  
  // If you need an HTTP request with a content type: application/json, use the following:
  //http.addHeader("Content-Type", "application/json");
  //int httpResponseCode = http.POST("{\"api_key\":\"tPmAT5Ab3j7F9\",\"sensor\":\"BME280\",\"value1\":\"24.25\",\"value2\":\"49.54\",\"value3\":\"1005.14\"}");

  // If you need an HTTP request with a content type: text/plain
  //http.addHeader("Content-Type", "text/plain");
  //int httpResponseCode = http.POST("Hello, World!");
  
  //Serial.print("HTTP Response code: ");
  //Serial.println(httpResponseCode);
    
  // Free resources
  http.end();
      
  
}


int getColor(int color,char component){
  int r;
  int g;
  int b;

  if(color==0){
    r=255;
    g=0;
    b=0;
  }else if(color<170){
    r=255;
    b=0;
    g=(color/170.0)*255;
  }else if(color<170*2){
    color=color-170;
    b=0;
    g=255;
    r=((170-color)/170.0)*255;
  }else if(color<170*3){
    color=color-170*2;
    r=0;
    g=255;
    b=(color/170.0)*255;
  }else if(color<170*4){
    color=color-170*3;
    r=0;
    b=255;
    g=((170-color)/170.0)*255;
  }else if(color<170*5){
    color=color-170*4;
    g=0;
    b=255;
    r=(color/170.0)*255;
  }else if(color<1025){
    color=color-170*5;
    g=0;
    r=255;
    b=((173-color)/173.0)*255; //173 cause this loop absorbs the remainder of 1024/6
  }else{ //should never be called, but if someone calls for a number outside of the range, we should return black instead of some strange number
    r=0;
    g=0;
    b=0;
  }
  if(component=='r'){
    return r;
  }else if(component=='g'){
    return g;
  }else if(component=='b'){
    return b;
  }else{
    return 0; //return 0 if they call for a component other than r/g/b
  }
}