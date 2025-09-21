#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <DNSServer.h>
//#include <WiFiManager.h>         // quotation marks usues library in sketch folder which i can customize the webpage for. PLEASE NOTE -- in earlier versions, WiFiManager.cpp had digitalWrite() and analogWrite() lines manually added by Blaine for the status LED. Now that the status LEDs use neopixel, the library version with those lines should NOT be used, otherwise the LED strip will flicker along with other unexpected behavior. /// BREAKING NEWS: We have once again modified the library, this time to display the MAC in the captive portal. This version of the library has been renamed to WiFiManagerLumas
#include "src/WiFiManagerLumas/WiFiManagerLumas.h"
#include <PubSubClient.h> //for mqtt
#include <EEPROM.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
//#include <WiFiUdp.h> //for getting google sheet
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <RotaryEncoder.h>

//to mark new code as valid and prevent rollback. See  esp_ota_mark_app_valid_cancel_rollback() in code
#include <esp_ota_ops.h>

//Rotary Encoder setup
#define ENCODER_PIN_IN1 18
#define ENCODER_PIN_IN2 19
RotaryEncoder *encoder = nullptr; //A pointer to the dynamic created rotary encoder instance. This will be done in setup()
IRAM_ATTR void checkEncoderPosition() //ISR called on any change of one of the input signals
{
  encoder->tick(); // just call tick() to check the state.
}

//for use in changing colors/brightness
int newColorRed;
int newColorGreen;
int newColorBlue;
int stripColors[12][3]={{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}};

int colorIndex=0;





bool firstConnectAttempt=true; //set to false after first connection attempt so initial boot actions aren't repeated

const String FirmwareVer={"0.17"}; //used to compare to GitHub firmware version to know whether to update

//CLIENT SPECIFIC VARIABLES----------------
char clientName[20];//="US";

int numOtherClientsInGroup;//=1;
char otherClientsInGroup[6][20]; //08:3A:8D:CC:DE:62 is assembled, 7C:87:CE:BE:36:0C is bare board
char groupName[20];//="PHUSSandbox";
int modelNumber;//=2; //1 is the original from 2021. 2 is the triple indicator neopixel version developed in 2024
//END CLIENT SPECIFIC VARIABLES------------

unsigned long otherClientsLastPingReceived[6]={4294000000,4294000000,4294000000,4294000000,4294000000,4294000000}; //Updated whenever we receive a ping, and used to determine online status. The order follows otherClientsInGroup. --- Initialized to near max value to avoid indicator being green at boot
//NOTE!! the above variable replaces lastPingReceived

char groupTopic[70]; //70 should be large enough
char multiColorTopic[84];
char adminTopic[70];
char consoleTopic[70]; //this topic is for hearts to publish to in response to admin commands, etc

bool receivedColorMode=false; //This variable is set true whenever we receive the multicolor mode, and false whenever we disconnect. This is to prevent this client from re-affirming the mode (publishing it to the broker to keep it active) incorrectly before we've actually received the current mode. It will probably be obsolete once we store this value in the database

#define NUMPIXELS 12
Adafruit_NeoPixel lights(NUMPIXELS, 27, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel indicator(1, 4, NEO_GRB + NEO_KHZ800);

const char* mqtt_server = "lumas.live";
WiFiClient espClient;
PubSubClient client(espClient);

//this rediculous and terrifying array is being replaced by getColor()
//int colors[1025][3] = {{255,0,0},{255,1,0},{255,3,0},{255,4,0},{255,6,0},{255,7,0},{255,9,0},{255,10,0},{255,12,0},{255,13,0},{255,15,0},{255,16,0},{255,18,0},{255,19,0},{255,21,0},{255,22,0},{255,24,0},{255,25,0},{255,27,0},{255,28,0},{255,30,0},{255,31,0},{255,33,0},{255,34,0},{255,36,0},{255,37,0},{255,39,0},{255,40,0},{255,42,0},{255,43,0},{255,45,0},{255,46,0},{255,48,0},{255,49,0},{255,51,0},{255,52,0},{255,54,0},{255,55,0},{255,57,0},{255,58,0},{255,60,0},{255,61,0},{255,63,0},{255,64,0},{255,66,0},{255,67,0},{255,69,0},{255,70,0},{255,72,0},{255,73,0},{255,75,0},{255,76,0},{255,78,0},{255,79,0},{255,81,0},{255,82,0},{255,84,0},{255,85,0},{255,87,0},{255,88,0},{255,90,0},{255,91,0},{255,93,0},{255,94,0},{255,96,0},{255,97,0},{255,99,0},{255,100,0},{255,102,0},{255,103,0},{255,105,0},{255,106,0},{255,108,0},{255,109,0},{255,111,0},{255,112,0},{255,114,0},{255,115,0},{255,117,0},{255,118,0},{255,120,0},{255,121,0},{255,123,0},{255,124,0},{255,126,0},{255,127,0},{255,129,0},{255,130,0},{255,132,0},{255,133,0},{255,135,0},{255,136,0},{255,138,0},{255,139,0},{255,141,0},{255,142,0},{255,144,0},{255,145,0},{255,147,0},{255,148,0},{255,150,0},{255,151,0},{255,153,0},{255,154,0},{255,156,0},{255,157,0},{255,159,0},{255,160,0},{255,162,0},{255,163,0},{255,165,0},{255,166,0},{255,168,0},{255,169,0},{255,171,0},{255,172,0},{255,174,0},{255,175,0},{255,177,0},{255,178,0},{255,180,0},{255,181,0},{255,183,0},{255,184,0},{255,186,0},{255,187,0},{255,189,0},{255,190,0},{255,192,0},{255,193,0},{255,195,0},{255,196,0},{255,198,0},{255,199,0},{255,201,0},{255,202,0},{255,204,0},{255,205,0},{255,207,0},{255,208,0},{255,210,0},{255,211,0},{255,213,0},{255,214,0},{255,216,0},{255,217,0},{255,219,0},{255,220,0},{255,222,0},{255,223,0},{255,225,0},{255,226,0},{255,228,0},{255,229,0},{255,231,0},{255,232,0},{255,234,0},{255,235,0},{255,237,0},{255,238,0},{255,240,0},{255,241,0},{255,243,0},{255,244,0},{255,246,0},{255,247,0},{255,249,0},{255,250,0},{255,252,0},{255,253,0},{255,255,0},{253,255,0},{252,255,0},{250,255,0},{249,255,0},{247,255,0},{246,255,0},{244,255,0},{243,255,0},{241,255,0},{240,255,0},{238,255,0},{237,255,0},{235,255,0},{234,255,0},{232,255,0},{231,255,0},{229,255,0},{228,255,0},{226,255,0},{225,255,0},{223,255,0},{222,255,0},{220,255,0},{219,255,0},{217,255,0},{216,255,0},{214,255,0},{213,255,0},{211,255,0},{210,255,0},{208,255,0},{207,255,0},{205,255,0},{204,255,0},{202,255,0},{201,255,0},{199,255,0},{198,255,0},{196,255,0},{195,255,0},{193,255,0},{192,255,0},{190,255,0},{189,255,0},{187,255,0},{186,255,0},{184,255,0},{183,255,0},{181,255,0},{180,255,0},{178,255,0},{177,255,0},{175,255,0},{174,255,0},{172,255,0},{171,255,0},{169,255,0},{168,255,0},{166,255,0},{165,255,0},{163,255,0},{162,255,0},{160,255,0},{159,255,0},{157,255,0},{156,255,0},{154,255,0},{153,255,0},{151,255,0},{150,255,0},{148,255,0},{147,255,0},{145,255,0},{144,255,0},{142,255,0},{141,255,0},{139,255,0},{138,255,0},{136,255,0},{135,255,0},{133,255,0},{132,255,0},{130,255,0},{129,255,0},{127,255,0},{126,255,0},{124,255,0},{123,255,0},{121,255,0},{120,255,0},{118,255,0},{117,255,0},{115,255,0},{114,255,0},{112,255,0},{111,255,0},{109,255,0},{108,255,0},{106,255,0},{105,255,0},{103,255,0},{102,255,0},{100,255,0},{99,255,0},{97,255,0},{96,255,0},{94,255,0},{93,255,0},{91,255,0},{90,255,0},{88,255,0},{87,255,0},{85,255,0},{84,255,0},{82,255,0},{81,255,0},{79,255,0},{78,255,0},{76,255,0},{75,255,0},{73,255,0},{72,255,0},{70,255,0},{69,255,0},{67,255,0},{66,255,0},{64,255,0},{63,255,0},{61,255,0},{60,255,0},{58,255,0},{57,255,0},{55,255,0},{54,255,0},{52,255,0},{51,255,0},{49,255,0},{48,255,0},{46,255,0},{45,255,0},{43,255,0},{42,255,0},{40,255,0},{39,255,0},{37,255,0},{36,255,0},{34,255,0},{33,255,0},{31,255,0},{30,255,0},{28,255,0},{27,255,0},{25,255,0},{24,255,0},{22,255,0},{21,255,0},{19,255,0},{18,255,0},{16,255,0},{15,255,0},{13,255,0},{12,255,0},{10,255,0},{9,255,0},{7,255,0},{6,255,0},{4,255,0},{3,255,0},{1,255,0},{0,255,0},{0,255,0},{0,255,1},{0,255,3},{0,255,4},{0,255,6},{0,255,7},{0,255,9},{0,255,10},{0,255,12},{0,255,13},{0,255,15},{0,255,16},{0,255,18},{0,255,19},{0,255,21},{0,255,22},{0,255,24},{0,255,25},{0,255,27},{0,255,28},{0,255,30},{0,255,31},{0,255,33},{0,255,34},{0,255,36},{0,255,37},{0,255,39},{0,255,40},{0,255,42},{0,255,43},{0,255,45},{0,255,46},{0,255,48},{0,255,49},{0,255,51},{0,255,52},{0,255,54},{0,255,55},{0,255,57},{0,255,58},{0,255,60},{0,255,61},{0,255,63},{0,255,64},{0,255,66},{0,255,67},{0,255,69},{0,255,70},{0,255,72},{0,255,73},{0,255,75},{0,255,76},{0,255,78},{0,255,79},{0,255,81},{0,255,82},{0,255,84},{0,255,85},{0,255,87},{0,255,88},{0,255,90},{0,255,91},{0,255,93},{0,255,94},{0,255,96},{0,255,97},{0,255,99},{0,255,100},{0,255,102},{0,255,103},{0,255,105},{0,255,106},{0,255,108},{0,255,109},{0,255,111},{0,255,112},{0,255,114},{0,255,115},{0,255,117},{0,255,118},{0,255,120},{0,255,121},{0,255,123},{0,255,124},{0,255,126},{0,255,127},{0,255,129},{0,255,130},{0,255,132},{0,255,133},{0,255,135},{0,255,136},{0,255,138},{0,255,139},{0,255,141},{0,255,142},{0,255,144},{0,255,145},{0,255,147},{0,255,148},{0,255,150},{0,255,151},{0,255,153},{0,255,154},{0,255,156},{0,255,157},{0,255,159},{0,255,160},{0,255,162},{0,255,163},{0,255,165},{0,255,166},{0,255,168},{0,255,169},{0,255,171},{0,255,172},{0,255,174},{0,255,175},{0,255,177},{0,255,178},{0,255,180},{0,255,181},{0,255,183},{0,255,184},{0,255,186},{0,255,187},{0,255,189},{0,255,190},{0,255,192},{0,255,193},{0,255,195},{0,255,196},{0,255,198},{0,255,199},{0,255,201},{0,255,202},{0,255,204},{0,255,205},{0,255,207},{0,255,208},{0,255,210},{0,255,211},{0,255,213},{0,255,214},{0,255,216},{0,255,217},{0,255,219},{0,255,220},{0,255,222},{0,255,223},{0,255,225},{0,255,226},{0,255,228},{0,255,229},{0,255,231},{0,255,232},{0,255,234},{0,255,235},{0,255,237},{0,255,238},{0,255,240},{0,255,241},{0,255,243},{0,255,244},{0,255,246},{0,255,247},{0,255,249},{0,255,250},{0,255,252},{0,255,253},{0,255,255},{0,253,255},{0,252,255},{0,250,255},{0,249,255},{0,247,255},{0,246,255},{0,244,255},{0,243,255},{0,241,255},{0,240,255},{0,238,255},{0,237,255},{0,235,255},{0,234,255},{0,232,255},{0,231,255},{0,229,255},{0,228,255},{0,226,255},{0,225,255},{0,223,255},{0,222,255},{0,220,255},{0,219,255},{0,217,255},{0,216,255},{0,214,255},{0,213,255},{0,211,255},{0,210,255},{0,208,255},{0,207,255},{0,205,255},{0,204,255},{0,202,255},{0,201,255},{0,199,255},{0,198,255},{0,196,255},{0,195,255},{0,193,255},{0,192,255},{0,190,255},{0,189,255},{0,187,255},{0,186,255},{0,184,255},{0,183,255},{0,181,255},{0,180,255},{0,178,255},{0,177,255},{0,175,255},{0,174,255},{0,172,255},{0,171,255},{0,169,255},{0,168,255},{0,166,255},{0,165,255},{0,163,255},{0,162,255},{0,160,255},{0,159,255},{0,157,255},{0,156,255},{0,154,255},{0,153,255},{0,151,255},{0,150,255},{0,148,255},{0,147,255},{0,145,255},{0,144,255},{0,142,255},{0,141,255},{0,139,255},{0,138,255},{0,136,255},{0,135,255},{0,133,255},{0,132,255},{0,130,255},{0,129,255},{0,127,255},{0,126,255},{0,124,255},{0,123,255},{0,121,255},{0,120,255},{0,118,255},{0,117,255},{0,115,255},{0,114,255},{0,112,255},{0,111,255},{0,109,255},{0,108,255},{0,106,255},{0,105,255},{0,103,255},{0,102,255},{0,100,255},{0,99,255},{0,97,255},{0,96,255},{0,94,255},{0,93,255},{0,91,255},{0,90,255},{0,88,255},{0,87,255},{0,85,255},{0,84,255},{0,82,255},{0,81,255},{0,79,255},{0,78,255},{0,76,255},{0,75,255},{0,73,255},{0,72,255},{0,70,255},{0,69,255},{0,67,255},{0,66,255},{0,64,255},{0,63,255},{0,61,255},{0,60,255},{0,58,255},{0,57,255},{0,55,255},{0,54,255},{0,52,255},{0,51,255},{0,49,255},{0,48,255},{0,46,255},{0,45,255},{0,43,255},{0,42,255},{0,40,255},{0,39,255},{0,37,255},{0,36,255},{0,34,255},{0,33,255},{0,31,255},{0,30,255},{0,28,255},{0,27,255},{0,25,255},{0,24,255},{0,22,255},{0,21,255},{0,19,255},{0,18,255},{0,16,255},{0,15,255},{0,13,255},{0,12,255},{0,10,255},{0,9,255},{0,7,255},{0,6,255},{0,4,255},{0,3,255},{0,1,255},{0,0,255},{0,0,255},{1,0,255},{3,0,255},{4,0,255},{6,0,255},{7,0,255},{9,0,255},{10,0,255},{12,0,255},{13,0,255},{15,0,255},{16,0,255},{18,0,255},{19,0,255},{21,0,255},{22,0,255},{24,0,255},{25,0,255},{27,0,255},{28,0,255},{30,0,255},{31,0,255},{33,0,255},{34,0,255},{36,0,255},{37,0,255},{39,0,255},{40,0,255},{42,0,255},{43,0,255},{45,0,255},{46,0,255},{48,0,255},{49,0,255},{51,0,255},{52,0,255},{54,0,255},{55,0,255},{57,0,255},{58,0,255},{60,0,255},{61,0,255},{63,0,255},{64,0,255},{66,0,255},{67,0,255},{69,0,255},{70,0,255},{72,0,255},{73,0,255},{75,0,255},{76,0,255},{78,0,255},{79,0,255},{81,0,255},{82,0,255},{84,0,255},{85,0,255},{87,0,255},{88,0,255},{90,0,255},{91,0,255},{93,0,255},{94,0,255},{96,0,255},{97,0,255},{99,0,255},{100,0,255},{102,0,255},{103,0,255},{105,0,255},{106,0,255},{108,0,255},{109,0,255},{111,0,255},{112,0,255},{114,0,255},{115,0,255},{117,0,255},{118,0,255},{120,0,255},{121,0,255},{123,0,255},{124,0,255},{126,0,255},{127,0,255},{129,0,255},{130,0,255},{132,0,255},{133,0,255},{135,0,255},{136,0,255},{138,0,255},{139,0,255},{141,0,255},{142,0,255},{144,0,255},{145,0,255},{147,0,255},{148,0,255},{150,0,255},{151,0,255},{153,0,255},{154,0,255},{156,0,255},{157,0,255},{159,0,255},{160,0,255},{162,0,255},{163,0,255},{165,0,255},{166,0,255},{168,0,255},{169,0,255},{171,0,255},{172,0,255},{174,0,255},{175,0,255},{177,0,255},{178,0,255},{180,0,255},{181,0,255},{183,0,255},{184,0,255},{186,0,255},{187,0,255},{189,0,255},{190,0,255},{192,0,255},{193,0,255},{195,0,255},{196,0,255},{198,0,255},{199,0,255},{201,0,255},{202,0,255},{204,0,255},{205,0,255},{207,0,255},{208,0,255},{210,0,255},{211,0,255},{213,0,255},{214,0,255},{216,0,255},{217,0,255},{219,0,255},{220,0,255},{222,0,255},{223,0,255},{225,0,255},{226,0,255},{228,0,255},{229,0,255},{231,0,255},{232,0,255},{234,0,255},{235,0,255},{237,0,255},{238,0,255},{240,0,255},{241,0,255},{243,0,255},{244,0,255},{246,0,255},{247,0,255},{249,0,255},{250,0,255},{252,0,255},{253,0,255},{255,0,255},{255,0,253},{255,0,252},{255,0,250},{255,0,249},{255,0,247},{255,0,246},{255,0,244},{255,0,243},{255,0,241},{255,0,240},{255,0,238},{255,0,237},{255,0,235},{255,0,234},{255,0,232},{255,0,231},{255,0,229},{255,0,228},{255,0,226},{255,0,225},{255,0,223},{255,0,222},{255,0,220},{255,0,219},{255,0,217},{255,0,216},{255,0,214},{255,0,213},{255,0,211},{255,0,210},{255,0,208},{255,0,207},{255,0,205},{255,0,204},{255,0,202},{255,0,201},{255,0,199},{255,0,198},{255,0,196},{255,0,195},{255,0,193},{255,0,192},{255,0,190},{255,0,189},{255,0,187},{255,0,186},{255,0,184},{255,0,183},{255,0,181},{255,0,180},{255,0,178},{255,0,177},{255,0,175},{255,0,174},{255,0,172},{255,0,171},{255,0,169},{255,0,168},{255,0,166},{255,0,165},{255,0,163},{255,0,162},{255,0,160},{255,0,159},{255,0,157},{255,0,156},{255,0,154},{255,0,153},{255,0,151},{255,0,150},{255,0,148},{255,0,147},{255,0,145},{255,0,144},{255,0,142},{255,0,141},{255,0,139},{255,0,138},{255,0,136},{255,0,135},{255,0,133},{255,0,132},{255,0,130},{255,0,129},{255,0,127},{255,0,126},{255,0,124},{255,0,123},{255,0,121},{255,0,120},{255,0,118},{255,0,117},{255,0,115},{255,0,114},{255,0,112},{255,0,111},{255,0,109},{255,0,108},{255,0,106},{255,0,105},{255,0,103},{255,0,102},{255,0,100},{255,0,99},{255,0,97},{255,0,96},{255,0,94},{255,0,93},{255,0,91},{255,0,90},{255,0,88},{255,0,87},{255,0,85},{255,0,84},{255,0,82},{255,0,81},{255,0,79},{255,0,78},{255,0,76},{255,0,75},{255,0,73},{255,0,72},{255,0,70},{255,0,69},{255,0,67},{255,0,66},{255,0,64},{255,0,63},{255,0,61},{255,0,60},{255,0,58},{255,0,57},{255,0,55},{255,0,54},{255,0,52},{255,0,51},{255,0,49},{255,0,48},{255,0,46},{255,0,45},{255,0,43},{255,0,42},{255,0,40},{255,0,39},{255,0,37},{255,0,36},{255,0,34},{255,0,33},{255,0,31},{255,0,30},{255,0,28},{255,0,27},{255,0,25},{255,0,24},{255,0,22},{255,0,21},{255,0,19},{255,0,18},{255,0,16},{255,0,15},{255,0,13},{255,0,12},{255,0,10},{255,0,9},{255,0,7},{255,0,6},{255,0,4},{255,0,3},{255,0,1},{255,0,0},{255,0,0},{255,0,0}};

int lastColorKnobVal; //use this to determine if the color has changed since last time, re-send if it has

int currentColor=0;
int lastColor=0;
int lastBrightness=0; //stored before going in to dark mode

int brightness=0;
int rawBrightness=0;
int colorKnob=0;
unsigned long analogReadTimer=0;
bool colorReadTurn=true;

bool multiColorMode=false;
unsigned long confirmColorModeTimer=60000*60*24-60000*15; //within first 15 minutes of being on, refresh this value (give it 15 mins to let user connect to wifi, hoping to avoid publishing the value before we know it)
//....I just fixed a bug where it did exactly what the line above was trying to avoid. This time I fixed it with a variable that checks if we've received it already. So I think the above is unnecessary

unsigned long lastSentColorAt=0;
bool currentlyChangingColor=false;

char sendVal[50]; //array to store value to send (must be long enough to hold [color number; this client name; this MAC address --17 chars])           //OLD COMMENT: //array to store value to send to other heart MUST BE [5] FOR 4 CHAR VALUE!! Due to because of termination char?

unsigned long lastPingSent; //time the last ping was sent
unsigned long lastPingReceived;
int timeout=30000; //time in milliseconds between pings

boolean isDark;


//BELOW CODE IS FOR GOOGLE SHEETS "DATABASE" -- temporary solution that should hopefully replace EEPROM until we get a real database
String googleSheetURL ="https://docs.google.com/spreadsheets/d/1FMWpVuE9PxkHIEMgdaKUi_d1UH7pvPvcPBorXB6OsQY/gviz/tq?tqx=out:csv&sheet=Active&range="; //append a range, eg: "a1:b4" to use this URL

//---------------------

//takes in a number 0-1024 (the potentiometer range) and returns the red, green, or blue component of that "index" of the rainbow
//component should be 'r', 'g', or 'b'
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


void setup_wifi() {
  WiFiManager manager;
  manager.setDebugOutput(false);
  //manager.resetSettings();
  
  Serial.println("Attempting to connect to saved network");
  //WiFi.begin();
  WiFi.begin();
  unsigned long startTime=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-startTime<14000){
    Serial.print(".");
    delay(500);
    if(WiFi.status()==WL_NO_SSID_AVAIL || WiFi.status()==WL_CONNECT_FAILED){
      break;
    }
  }
  Serial.println();

  if(WiFi.status()!=WL_CONNECTED){ //launch wifi manager in this block
    //String networkName=strcat(strcat(clientName,"'s Lumas Setup - "),WiFi.macAddress().c_str());

    char bufNetName[30];
    strcpy(bufNetName,clientName);
    strcat(bufNetName,"'s Heart Setup");
    String networkName=String(bufNetName);
    Serial.print("Connection to saved network failed, starting config AP on: ");
    Serial.println(networkName);

    //set lights to indicate that we are in config mode
    for(int i=0;i<3;i++){
      statusLEDs(150,60,0,i);
    }

    // Switch wifiManager config portal IP from default 192.168.4.1 to 8.8.8.8. This ensures auto-load on some android devices which have 8.8.8.8 hard-coded in the OS.
    manager.setAPStaticIPConfig(IPAddress(8,8,8,8), IPAddress(8,8,8,8), IPAddress(255,255,255,0));
    //manager.setTitle("Lumas Config <br>MAC: - "+WiFi.macAddress());
    manager.setTitle("Lumas Config");

    manager.setMac(WiFi.macAddress()); //Ok. So. setMac is a custom function I added to the library. Declared in WiFiManager.h, used in WiFiManager.cpp, and affecting wm_strings_en.h (ctrl+f for "blaineModified")

    manager.setTimeout(60*5); //if no pages are loaded on the setup AP within this many seconds, reboot in an attempt to connect to the saved network again.
    if(!manager.autoConnect(networkName.c_str(),"")){
      Serial.println("WifiManager portal timeout. Resetting now to attempt connection again. Will launch AP again on reboot if connection fails again");
      Serial.println("\n\n");
      ESP.restart();
    }
    //ESP.restart(); //temporary workaround to mitigate the flicker issue. When using WiFiManager to connect to a new network, the LED strip will flicker and flash until reboot unless D8 has digitalWrite() called every loop
  }

  Serial.print("Succesfully connected to ");
  Serial.println(WiFi.SSID());
}

String httpGet(String url){
  String httpResult;
  HTTPClient https;
  Serial.println("Requesting " + url);
  if (https.begin(url)) {
    int httpCode = https.GET();
    Serial.println("============== Response code: " + String(httpCode));
    if (httpCode > 0) {
      //Serial.println(https.getString());
      //Serial.println("-----END RESULT-----");
      httpResult=https.getString();
      Serial.println(httpResult);
      https.end();
      }else {
        Serial.printf("[HTTPS] Unable to connect\n");
    }
    return httpResult;
  }else{
    Serial.println("some error probably");
  }
}


void loadClientSpecificVariables(){


  //STEP 1: Find this client in the database via MAC Address
  String firstURL= "http://lumas.live:4000/api/hearts/" + WiFi.macAddress();
  String heartRow = httpGet(firstURL);

  // Parse JSON
  StaticJsonDocument<512> doc; // Adjust size as needed
  DeserializationError error = deserializeJson(doc, heartRow);
  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Access the values
  const char* id = doc["id"];
  const char* group = doc["group"];
  const char* heartName = doc["name"];
  Serial.printf("Extracted values: id=%s, group=%s, name=%s\n", id, group, heartName);
  Serial.println(group);



  //STEP 2: Using this client's group name, look up others!
  int countNumOtherClientsInGroup=0;
  char* otherClients[6];
  if(strcmp(group,"None")!=0){
    String groupURL = "http://lumas.live:4000/api/groups/";
  
    String fullURL = groupURL + group;
    Serial.print("FULL URL: ");
    Serial.println(fullURL);
    
    String groupInfo = httpGet(fullURL);
  
    // Parse JSON
    StaticJsonDocument<256> docTwo; // Adjust size as needed
    DeserializationError errorTwo = deserializeJson(docTwo, groupInfo);
    if (errorTwo) {
      Serial.print("JSON deserialization failed: ");
      Serial.println(errorTwo.c_str());
      return;
    }
  
    // Extract array
    JsonArray heartIDs = docTwo["hearts"].as<JsonArray>();
    Serial.println("Heart IDs:");
    countNumOtherClientsInGroup=0;
    for (const char* heartID : heartIDs) {
      if(strcmp(WiFi.macAddress().c_str(),heartID)!=0){ //As long as this isn't our own MAC
        otherClients[countNumOtherClientsInGroup] = (char*)heartID;  
        Serial.println(heartID);
        countNumOtherClientsInGroup++;
      }
    }

  }

  //STEP 3: load variables in to local memory

  strcpy(clientName,heartName);
  //COPY THIS NAME IN TO EEPROM IF IT DIFFERS, cause it is used in wifi setup pre-network connection
  EEPROM.begin(173);
  char ch_clientName[20];
  EEPROM.get(3,ch_clientName);
  EEPROM.end();
  if(strcmp(ch_clientName,clientName)!=0){ //if different, put new name in to EEPROM
    Serial.println("Name updated, updating local EEPROM value");
    EEPROM.begin(173);
    EEPROM.put(3,clientName);
    EEPROM.end();
  }
  strcpy(groupName,group);
  //modelNumber=atoi((httpResult.substring(nthIndex(httpResult,'\"',5)+1,nthIndex(httpResult,'\"',6))).c_str());
  modelNumber=2; //currently not in AWS database
  
  if(strcmp(group,"None")!=0){
    numOtherClientsInGroup=countNumOtherClientsInGroup;
    for(int i=0;i<countNumOtherClientsInGroup;i++){
      strcpy(otherClientsInGroup[i],otherClients[i]);
    }
  }

  //getGoogleSheet(); //eventually should use a real database, not google sheets
  
  //EEPROM method has been deprecated and replaced with Google Sheets

  BubbleSort(otherClientsInGroup,numOtherClientsInGroup); //sort the array we just loaded so that indicator lights are consistant

  
  //NOTE -- the below values are now offset by 3 due to eeprom initialization checking 
/*  
  //for now using EEPROM, but this will eventually be a call to an external database
  //WARNING: executing this code on an uninitialized EEPROM will cause the program to crash or behave erratically. There is protection against otherClientsInGroup having empty values, but other values or the whole eeprom being uninitialized (aka, not calling EEPROM.begin() & EEPROM.put() previously on this specific board) will cause problems
  EEPROM.begin(170);
  int address=0;

  char ch_clientName[20];
  EEPROM.get(address,ch_clientName);
  address+=20;
  char ch_groupName[20];
  EEPROM.get(address,ch_groupName);
  address+=20;
  char ch_modelNumber[3];
  EEPROM.get(address,ch_modelNumber);
  address+=3;
  char ch_numOtherClientsInGroup[3];
  EEPROM.get(address,ch_numOtherClientsInGroup);
  address+=3;
  char ch_otherClientsInGroup0[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup0,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup0); 
  }
  address+=20;
  char ch_otherClientsInGroup1[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup1,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup1); 
  }
  address+=20;
  char ch_otherClientsInGroup2[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup2,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup2); 
  }
  address+=20;
  char ch_otherClientsInGroup3[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup3,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup3); 
  }
  address+=20;
  char ch_otherClientsInGroup4[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup4,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup4); 
  }
  address+=20;
  char ch_otherClientsInGroup5[20];
  if(EEPROM.read(address)==255){ //check if first byte is initialized before reading the whole thing. Reading in uninitialized values breaks things (I think what happens is EEPROM.get() doesn't stop reading until it runs in to a null terminator, so it reads in values much longer than 20 characters, causing all sorts of memory overflow errors)
    strcpy(ch_otherClientsInGroup5,"");
  }else{
    EEPROM.get(address,ch_otherClientsInGroup5); 
  }
  
  EEPROM.end();

  strcpy(clientName,ch_clientName);
  strcpy(groupName,ch_groupName);
  modelNumber=atoi(ch_modelNumber);
  numOtherClientsInGroup=atoi(ch_numOtherClientsInGroup);
  strcpy(otherClientsInGroup[0],ch_otherClientsInGroup0);
  strcpy(otherClientsInGroup[1],ch_otherClientsInGroup1);
  strcpy(otherClientsInGroup[2],ch_otherClientsInGroup2);
  strcpy(otherClientsInGroup[3],ch_otherClientsInGroup3);
  strcpy(otherClientsInGroup[4],ch_otherClientsInGroup4);
  strcpy(otherClientsInGroup[5],ch_otherClientsInGroup5);

  Serial.println("LOADED VALUES:");
  Serial.println(clientName);
  Serial.println(groupName);
  Serial.println(modelNumber);
  Serial.println(numOtherClientsInGroup);
  Serial.println(otherClientsInGroup[0]);
  Serial.println(otherClientsInGroup[1]);
  Serial.println(otherClientsInGroup[2]);
  Serial.println(otherClientsInGroup[3]);
  Serial.println(otherClientsInGroup[4]);
  Serial.println(otherClientsInGroup[5]);
*/

  //if you need to write values, use the below code (or eventually you should be able to do it as an admin command via MQTT)
  //write char arrays
  /*
  EEPROM.begin(170);
  int address = 0;
  EEPROM.put(address,clientName);
  address+=20;
  EEPROM.put(address,groupName);
  address+=20;
  EEPROM.put(address,modelNumber);
  address+=3;
  EEPROM.put(address,numOtherClientsInGroup);
  address+=3;
  EEPROM.put(address,otherClientsInGroup[0]);
  address+=20;
  EEPROM.put(address,otherClientsInGroup[1]);
  address+=20;
  ....up to otherClientsInGroup[5] if necessary
  EEPROM.commit();
  EEPROM.end();
  */
}

//for reasons I don't understand, this doesn't need a return value. Whatever array gets passed in gets sorted in place
void BubbleSort (char arry[][20], int m){ //m is number of elements
    char valA[20];
    char valB[20];
    int i, j;
    for (i = 0; i < m; ++i){
        for (j = 0; j < m-i-1; ++j){
            // Comparing consecutive elements and switching values when value at j > j+1.
            if (strcmp(arry[j],arry[j+1])>0){     //if (arry[j] > arry[j+1])
                //swap values
                strcpy(valA,arry[j]);
                strcpy(valB,arry[j+1]);
                strcpy(arry[j],valB);
                strcpy(arry[j+1],valA);
            }
        }
    }  
}

void firmwareUpdate(){

    #define URL_fw_Version "https://raw.githubusercontent.com/BlaineAtkins/Lumas/main/Firmware/currentFirmwareVersion.txt"
    #define URL_fw_Bin "https://raw.githubusercontent.com/BlaineAtkins/Lumas/main/Firmware/LumasFirmware.bin"
    
    WiFiClientSecure client;
    client.setInsecure(); //prevents having the update the CA certificate periodically

    String payload;
    int httpCode;
    String fwurl = "";
    fwurl += URL_fw_Version;
    fwurl += "?";
    fwurl += String(rand());


    // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
    HTTPClient https;

    if (https.begin(client, fwurl)) 
    { // HTTPS      
      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(100);
      httpCode = https.GET();
      delay(100);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version
        Serial.println("Received msg ");
        Serial.print(payload);
        Serial.print(" from ");
        Serial.println(fwurl);
      } else {
        Serial.print("error in downloading version file:");
        Serial.println(httpCode);
        if(httpCode==-1 || httpCode==-101){
          Serial.println("HELP: This probably means the ESP is stuck in a captive portal. Make sure it is registered on the network.");
          Serial.print("Your MAC address for registration is ");
          Serial.println(WiFi.macAddress());
        }
      }
      https.end();
    }else{
      Serial.println("some error in http begin");
    }
    
    payload.trim();
    Serial.print("Newest firmware version is ");
    Serial.println(payload);
    if(payload.equals(FirmwareVer)) {
        Serial.println("Device already on latest firmware version");
    }
    else {
        Serial.println("New firmware detected");
        for(int i=0;i<3;i++){
          statusLEDs(150,150,150,i); //all white indicates we're in a firmware update
        }
        Serial.println("Current firmware version "+FirmwareVer);
        Serial.println("Firmware version "+payload+" is avalable");
        //httpUpdate.setLedPin(LED_BUILTIN, LOW);
        t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);
        Serial.println("Update firmware to version "+payload);
        switch (ret) {
            case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;

            case HTTP_UPDATE_NO_UPDATES:
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            break;

            case HTTP_UPDATE_OK:
            Serial.println("HTTP_UPDATE_OK");
            break;
        }
    }
}

void getGoogleSheet(){

  int numrow=0;
  
  WiFiClientSecure client;
    client.setInsecure();

    String httpResult;
    HTTPClient https;
    String MacSearchURL = googleSheetURL+"a6:a25";
    Serial.println("Requesting " + MacSearchURL);
    if (https.begin(client, MacSearchURL)) {
      int httpCode = https.GET();
      Serial.println("============== Response code: " + String(httpCode));
      if (httpCode > 0) {
        //Serial.println(https.getString());
        //Serial.println("-----END RESULT-----");
        httpResult=https.getString();

        //find which row our MAC is
        numrow=0;
        int pos= httpResult.indexOf(WiFi.macAddress());
        if(pos!=-1){
          for(int i=0;i<pos;++i){
            if(httpResult[i]=='\n'){
              numrow++;
            }
          }
          numrow+=6; //offset in google sheets
        }else{
          Serial.println("HEART NOT FOUND IN DATABASE! Will not function properly. Please insert this heart's MAC in to the database");
          while(true){
            for(int i=0;i<3;i++){
              statusLEDs(100,0,0,i);
            }
            delay(500);
            for(int i=0;i<3;i++){
              statusLEDs(0,0,0,i);
            }
            delay(500);
          }
        }

      }else{
        ESP.restart(); //sometimes -1 is returned. Reboot to try again. Not sure what happens with captive portals, we should look at that
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
    }

    //if we got data from above, use that index to get the actual data
    if(numrow>0){
      String databaseURL = googleSheetURL+"b"+String(numrow)+":k"+String(numrow);

      Serial.println("Requesting " + databaseURL);
      if (https.begin(client, databaseURL)) {
        int httpCode = https.GET();
        Serial.println("============== Response code: " + String(httpCode));
        if (httpCode > 0) {
          //Serial.println(https.getString());
          //Serial.println("-----END RESULT-----");
          httpResult=https.getString();
        }else{
        ESP.restart(); //sometimes -1 is returned. Reboot to try again. Not sure what happens with captive portals, we should look at that
      }
        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }

      strcpy(clientName,(httpResult.substring(nthIndex(httpResult,'\"',1)+1,nthIndex(httpResult,'\"',2))).c_str());
      //COPY THIS NAME IN TO EEPROM IF IT DIFFERS, cause it is used in wifi setup pre-network connection
      EEPROM.begin(173);
      char ch_clientName[20];
      EEPROM.get(3,ch_clientName);
      EEPROM.end();
      if(strcmp(ch_clientName,clientName)!=0){ //if different, put new name in to EEPROM
        Serial.println("Name updated, updating local EEPROM value");
        EEPROM.begin(173);
        EEPROM.put(3,clientName);
        EEPROM.end();
      }
      strcpy(groupName,(httpResult.substring(nthIndex(httpResult,'\"',3)+1,nthIndex(httpResult,'\"',4))).c_str());
      modelNumber=atoi((httpResult.substring(nthIndex(httpResult,'\"',5)+1,nthIndex(httpResult,'\"',6))).c_str());
      numOtherClientsInGroup=atoi((httpResult.substring(nthIndex(httpResult,'\"',7)+1,nthIndex(httpResult,'\"',8))).c_str());
      strcpy(otherClientsInGroup[0],(httpResult.substring(nthIndex(httpResult,'\"',9)+1,nthIndex(httpResult,'\"',10))).c_str());
      strcpy(otherClientsInGroup[1],(httpResult.substring(nthIndex(httpResult,'\"',11)+1,nthIndex(httpResult,'\"',12))).c_str());
      strcpy(otherClientsInGroup[2],(httpResult.substring(nthIndex(httpResult,'\"',13)+1,nthIndex(httpResult,'\"',14))).c_str());
      strcpy(otherClientsInGroup[3],(httpResult.substring(nthIndex(httpResult,'\"',15)+1,nthIndex(httpResult,'\"',16))).c_str());
      strcpy(otherClientsInGroup[4],(httpResult.substring(nthIndex(httpResult,'\"',17)+1,nthIndex(httpResult,'\"',18))).c_str());
      strcpy(otherClientsInGroup[5],(httpResult.substring(nthIndex(httpResult,'\"',19)+1,nthIndex(httpResult,'\"',20))).c_str());
    }
    
}

int nthIndex(String str,char ch, int N){
    int occur = 0;
    // Loop to find the Nth
    // occurrence of the character
    for (int i = 0; i < str.length(); i++) {
        if (str[i] == ch) {
            occur += 1;
        }
        if (occur == N)
            return i;
    }
    return -1;
}

void updateTopicVariables(){
  strcpy(groupTopic,"LumasHearts/groups/");
  strcat(groupTopic,groupName);
  Serial.println(groupName);
  strcpy(multiColorTopic,""); //re-initalize this to empty!! Otherwise it overflows when updated
  strcat(multiColorTopic,groupTopic);
  strcat(groupTopic,"/color");
  strcat(multiColorTopic,"/multicolorMode");
  strcpy(adminTopic,"LumasHearts/admin");
  strcpy(consoleTopic,"LumasHearts/console");
}








void setup() {
  //Prevent rollback to previous firmware
  //WEIRD NOTE: automatic rollback only occors automatically sometimes. Maybe dependant on the last computer that uploaded serial code to it?
  esp_ota_mark_app_valid_cancel_rollback();

  Serial.begin(9600);
  delay(200);

  encoder = new RotaryEncoder(ENCODER_PIN_IN1, ENCODER_PIN_IN2, RotaryEncoder::LatchMode::TWO03);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_IN1), checkEncoderPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_IN2), checkEncoderPosition, CHANGE);

  //pinMode(34,INPUT); //light sensor input

  int darkThresholdValue=1500;
  if(analogRead(34)<darkThresholdValue){
    isDark=true;
  }else if(analogRead(34)>darkThresholdValue+40){ //adjust this parameter for hysterisis
    isDark=false;
  }
  

  indicator.begin();
  indicator.clear();
  indicator.show();
  for(int i=0;i<3;i++){
    statusLEDs(100,0,0,i);
  }
  
  
  //finsish lights setup
  lights.begin();
  lights.clear();
  //startup animation
  for(int i=0;i<200;i++){
    lights.setBrightness(i);
    for(int i=0;i<NUMPIXELS;i++){ //must set the color every time because I'm using setBrightness() as an animation and shouldn't be
      lights.setPixelColor(i, lights.Color(getColor((1024/NUMPIXELS)*i,'r'),getColor((1024/NUMPIXELS)*i,'g'),getColor((1024/NUMPIXELS)*i,'b')));
    }
    lights.show();
    if(i<100){ //slower at lower brigtnesses cause apparent brightness change is bigger per step
      delay(20);
    }else{
      delay(10);
    }
    lastBrightness=i; //leave this variable at wherever we end up according to loop ctr end
  }
  lights.setBrightness(255); //added for v2 hearts. since we're no longer using this to change the brightness in other parts, we need to leave it at max


  Serial.print("This client's MAC address is: ");
  Serial.println(WiFi.macAddress());

  Serial.println("Checking EEPROM configuration...");
  //read in client name before wifi setup because the variable is used for the network name
  EEPROM.begin(173);
  //Serial.println(EEPROM.read(0));
  //Serial.println(EEPROM.read(1));
  //Serial.println(EEPROM.read(2));
  if(!(EEPROM.read(0)=='A' && EEPROM.read(1)=='B' && EEPROM.read(2)=='C')){
    Serial.println("EEPROM has never been initialized, initializing 173 bytes now.");
    EEPROM.write(0,'A');
    EEPROM.write(1,'B');
    EEPROM.write(2,'C');
    for(int i=3;i<173;i++){
      EEPROM.write(i,0);
    }
    EEPROM.put(3,"New Heart");
    EEPROM.commit();
  }else{
    Serial.println("EEPROM is already set up");
  }
  EEPROM.end();

  //now open EEPROM again for actual usage
  EEPROM.begin(173);
  char ch_clientName[20];
  EEPROM.get(3,ch_clientName);
  EEPROM.end();
  strcpy(clientName,ch_clientName);
  //Serial.print("read in ");
  //Serial.println(clientName);
  
  setup_wifi();
  
  // if you get here you have connected to the WiFi
  Serial.println("Connected to WiFi");

  loadClientSpecificVariables();

  //set topic variable
  updateTopicVariables();
  
  firmwareUpdate();

  //END WIFI SETUP, BEGIN MQTT CONNECTION
  client.setServer(mqtt_server, 1883);

  client.setCallback(Received_Message);

  //set lights to current knob value, set variables so that the first loop doesn't trigger an update to be sent to remote heart (first we want to RECEIVE the color, not send it)
  /*pinMode(D5,INPUT); //brightness knob 1
  pinMode(D7,INPUT); //brightness knob 2
  pinMode(D1,OUTPUT); //color knob 1
  pinMode(D2,OUTPUT); //color knob 2
  digitalWrite(D1,HIGH);
  digitalWrite(D2,LOW);*/
//  currentColor=analogRead(A0);
  
  currentColor=0;
  
  lastColorKnobVal=currentColor;
  for(int i=0;i<NUMPIXELS;i++){
    lights.setPixelColor(i, lights.Color(getColor(currentColor,'r'),getColor(currentColor,'g'),getColor(currentColor,'b')));
  }
   
  //lights.setBrightness(analogRead(35)/16);
  lights.show();

}


void statusLEDs(int red, int green, int blue, int indicatorNum){
  if(indicatorNum<3){ //ignore out of bounds 
    indicator.setPixelColor(indicatorNum,indicator.Color(red,green,blue));
    indicator.show();
  }
}

void Received_Message(char* topic, byte* payload, unsigned int length) {
  /*
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  */

  payload[length] = '\0'; // Add a NULL to the end of the char* to make it a string.

  //Serial.println("Received msg");
  
  if(strcmp(topic,multiColorTopic)==0){
    receivedColorMode=true;
    Serial.println("multicolor topic");
    
    char* payloadChar = (char *)payload; //......what kind of line is this?? ...but I'm scared to delete it
    if(strcmp(payloadChar,"true")==0){
      multiColorMode=true;
    }else{
      multiColorMode=false;
    }
    Serial.println(multiColorMode);
  }else if(strcmp(topic,adminTopic)==0){ //an admin command
    String strPayload = String((char*)payload);
    int firstCommaIndex=strPayload.indexOf(',');
    int secondCommaIndex=strPayload.indexOf(',',firstCommaIndex+1);
    String target=strPayload.substring(0,firstCommaIndex);
    String command=strPayload.substring(firstCommaIndex+1,secondCommaIndex); //turns out that in substring "-1" is interpreted as the end of the string. and indexOf() returns -1 when not found. So this line still works fine when there is no second comma, and the payload variable just winds up with the whole message, which is fine cause it's unused in that case
    String adminPayload=strPayload.substring(secondCommaIndex+1);

    if(target=="all" || target==WiFi.macAddress()){
      Serial.println("Received an admin command directed to this client");
      Serial.println(target);
      Serial.println(command);
      Serial.println(adminPayload);
      Serial.println("----------");


      if(command=="FIRMWARE_UPDATE"){
        if(adminPayload==strPayload){ //if there's no payload (due to parsing above, no payload means payload becomes whole message)
          Serial.println("Checking for update from main firmware");
          firmwareUpdate();
        }else if(adminPayload=="alt"){
          Serial.println("updating from alt firmware");
          #define URL_fw_Bin "https://raw.githubusercontent.com/BlaineAtkins/RemoteHearts/main/sandboxFirmware.bin"
          WiFiClientSecure client;
          client.setInsecure(); //prevents having to update the CA certificate periodically
          for(int i=0;i<3;i++){
            statusLEDs(150,150,150,i); //all white indicates we're in a firmware update
          }
          //httpUpdate.setLedPin(LED_BUILTIN, LOW);
          t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);
        }else{
          Serial.println("invalid update source specified. Pass no parameter, or \"alt\"");
        }
      }
      if(command=="GET_EEPROM"){
        if(adminPayload==strPayload){ //get entire eeprom
          Serial.println("printing whole EEPROM");
          char eepromContents[173];
          EEPROM.begin(173);
          for(int i=0;i<173;i++){
            if(EEPROM.read(i)==0){ //replace null terminators with unusual character so we can send this string without it getting cut off
              eepromContents[i]=126;
            }else if(EEPROM.read(i)==255){
              eepromContents[i]=126;
            }else{
              eepromContents[i]=EEPROM.read(i);
            }
          }
          eepromContents[172]='\0';
          client.publish(consoleTopic,eepromContents);
          EEPROM.end();
        }
      }
      if(command=="DATABASE_UPDATE"){
        Serial.println("Will update all local variables with the values from the google sheet...");
        loadClientSpecificVariables();
        //unsubscribe from old topics in case they were updated
        Serial.print("Unsubscribing from group ");
        Serial.println(groupTopic);
        client.unsubscribe(groupTopic);
        client.unsubscribe(multiColorTopic);
        updateTopicVariables();
        //now subscribe to new ones
        client.subscribe(groupTopic);
        client.subscribe(multiColorTopic);
        Serial.print("Now subscribed to group ");
        Serial.println(groupTopic);
      }
    }
    
  }else if(strcmp(topic,groupTopic)==0 && strcmp(groupTopic,"None")!=0){ //unclaimed hearts go in the "None" group. So ignore incoming commands if we're in that group, because they are not supposed to be "connected"
    

    String strPayload = String((char*)payload);
    int firstCommaIndex=strPayload.indexOf(',');
    int secondCommaIndex=strPayload.indexOf(',',firstCommaIndex+1);
    String receivedNumber = strPayload.substring(0,firstCommaIndex);
    String fromClientMac = strPayload.substring(firstCommaIndex+1,secondCommaIndex);
    String fromClientName = strPayload.substring(secondCommaIndex+1);
    /*
    Serial.print("Received number ");
    Serial.print(receivedNumber);
    Serial.print(" from client ");
    Serial.println(fromClientName);
    Serial.print(" with MAC ");
    Serial.println(fromClientMac);
    */
    
    char ch_fromClientMac[20];
    fromClientMac.toCharArray(ch_fromClientMac,fromClientMac.length()+1);
    char ch_fromClientName[31];
    fromClientName.toCharArray(ch_fromClientName,fromClientName.length()+1);

    if((strcmp(ch_fromClientMac,WiFi.macAddress().c_str())!=0) || (strcmp(ch_fromClientName,"WEB_APP")==0)){ //DO NOT PROCESS MESSAGE IF IT IS FROM OURSELVES
      char buf[receivedNumber.length()+1];
      receivedNumber.toCharArray(buf, receivedNumber.length()+1);
  
      int rcvNum = atoi(buf);
      if(rcvNum==-1){ //if other heart just came online, ignore the value (-1), but ping it to let it know we're here too and give it our value
        itoa(currentColor, sendVal,10);
        strcat(sendVal,",");
        strcat(sendVal,WiFi.macAddress().c_str());
        strcat(sendVal,",");
        strcat(sendVal,clientName);
        client.publish(groupTopic,sendVal);
      }else{ //only update color from remote heart if it wasn't it's first ping to say it's online
    
        int currentColorRemote; //the color of the other heart
        currentColorRemote = rcvNum; 

        if(multiColorMode){
          //create an array of client MAC addresses including our own to be sorted 
          char clientsIncludingMe[numOtherClientsInGroup+1][20];
          memcpy(clientsIncludingMe, otherClientsInGroup, numOtherClientsInGroup*20);
          strcpy(clientsIncludingMe[numOtherClientsInGroup],WiFi.macAddress().c_str());
          BubbleSort(clientsIncludingMe,numOtherClientsInGroup+1); //sort by MAC first in order to ensure consistant placement of each user across hearts

          //figure out what section to update based on the sorted MACs
          int sectionNumber=0;
          for(int i=0;i<numOtherClientsInGroup+1;i++){
            if(strcmp(ch_fromClientMac,clientsIncludingMe[i])==0){
              sectionNumber=i;
            }
          }

          int sectionSize=12/(numOtherClientsInGroup+1); //no remainder except for when we have 5 total clients
          int fiveClientSectionSize[]={3,2,2,2,3}; //for 5 clients, section size is as follows to preserve symetry: 3,2,2,2,3

          if(numOtherClientsInGroup+1!=5){
            for(int i=sectionSize*sectionNumber;i<sectionSize*(sectionNumber+1);i++){
              //lights.setPixelColor(i, lights.Color(getColor(currentColorRemote,'r'),getColor(currentColorRemote,'g'),getColor(currentColorRemote,'b')));
              stripColors[i][0]=getColor(currentColorRemote,'r');
              stripColors[i][1]=getColor(currentColorRemote,'g');
              stripColors[i][2]=getColor(currentColorRemote,'b');
            }
          }else{ //special case because 12 is not divisible by 5 -- NOTE: untested at this time (4/1/24)
            int fiveClientSectionSize[]={3,2,2,2,3}; //for 5 clients, section size is as follows to preserve symetry: 3,2,2,2,3
            int startAt=0;
            for(int i=0;i<sectionNumber;i++){
              startAt+=fiveClientSectionSize[i];
            }
            for(int i=startAt;i<startAt+fiveClientSectionSize[sectionNumber];i++){
              //lights.setPixelColor(i, lights.Color(getColor(currentColor,'r'),getColor(currentColor,'g'),getColor(currentColor,'b')));
              stripColors[i][0]=getColor(currentColor,'r');
              stripColors[i][1]=getColor(currentColor,'g');
              stripColors[i][2]=getColor(currentColor,'b');
            }
          }

        }else{
          //TODO eventually: before this loop, define RGB values of the previous color and the future color. Then surround the loop with another loop that takes less than 500ms to fade from the first to the second color
          for(int i=0;i<NUMPIXELS;i++){
            //lights.setPixelColor(i, lights.Color(getColor(currentColorRemote,'r'),getColor(currentColorRemote,'g'),getColor(currentColorRemote,'b')));
            stripColors[i][0]=getColor(currentColorRemote,'r');
            stripColors[i][1]=getColor(currentColorRemote,'g');
            stripColors[i][2]=getColor(currentColorRemote,'b');
          }
          //Serial.println(currentColorRemote);
          currentColor=currentColorRemote;
        }
        lights.show();
        
      }
      
      lastPingReceived=millis();
      //statusLEDs(0,100,0,0); //this should be handled in pingAndStatus() now

      //Do parsing to store values for who's online
      for(int i=0;i<numOtherClientsInGroup;i++){
        if(strcmp(fromClientMac.c_str(),otherClientsInGroup[i])==0){ //find index to update
          otherClientsLastPingReceived[i]=millis();
        }
      }

      
    }
  }
}

void reconnect() {
  int retryCtr=0;
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(WiFi.macAddress().c_str(),"","","LumasHearts/onlineStatus",0,false,("Offline,"+WiFi.macAddress()).c_str())){
      Serial.println("connected");
      if(firstConnectAttempt){
        client.publish("LumasHearts/onlineStatus",("Online,"+WiFi.macAddress()).c_str());
        client.publish("startLocationUpdater","start"); //tell EC2 locationUpdater script to run. It will see this heart in the mosquitto logs and update it's IP and location in the AWS database
      }
      firstConnectAttempt=false;
      receivedColorMode=false;
      // Once connected, publish an announcement and re-subscribe
      //Serial.println(groupTopic);
      client.subscribe(adminTopic);
      client.subscribe(multiColorTopic);
      client.subscribe(groupTopic); //subscribe first so that when we send -1 below, we can receive the response right away
      itoa(-1, sendVal,10);
      strcat(sendVal,",");
      strcat(sendVal,WiFi.macAddress().c_str());
      strcat(sendVal,",");
      strcat(sendVal,clientName);
      client.publish(groupTopic,sendVal); // -1 indicates we just came online and are requesting other heart's value
      
      
    } else {
      //statusLEDs(100,0,0,0);
      //for now, this pattern means we are offline
      for(int i=0;i<3;i++){
        statusLEDs(100,0,0,i);
      }
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in  seconds");
      // Wait 5 seconds before retrying
      delay(5000);

      retryCtr++;
      if(retryCtr>18){ //if disconnected from wifi for over 1.5 minutes (actually around 3 mins when factoring attempt time), restart to try to reconnect (since if left on for long enough, it may disconnect and not reconnect automatically -- unknown why. Firmware bug? MQTT issue?)
        ESP.restart();
      }
    }
  }
}

void pingAndStatus(){
  if(millis()-lastPingReceived>timeout+5000){
    //other heart is offline
    //statusLEDs(0,0,100,0);
  }

  if(millis()-lastPingSent>timeout){
    //send a ping
    itoa(currentColor, sendVal,10);
    strcat(sendVal,",");
    strcat(sendVal,WiFi.macAddress().c_str());
    strcat(sendVal,",");
    strcat(sendVal,clientName);
    client.publish(groupTopic,sendVal); 
    lastPingSent=millis();
  }


  //ABOVE IS OLD CODE FOR SINGLE STATUS LIGHT. BELOW IS NEW CODE WITH MULTIPLE INDICATOR LIGHTS

  //We can assume otherClientsInGroup is sorted, as it is sorted whenever it is updated in loadClientSpecificVariables()
  //We will use the other client's position in the array as the assigned number indicator
  //the data updating happens in the callback for processing an incoming color

  bool aClientIsOnline=false;
  for(int i=0;i<numOtherClientsInGroup;i++){
    if(millis()-otherClientsLastPingReceived[i]>timeout+5000){
      //statusLEDs(0,0,50,i); //this client is offline
    }else{
      //statusLEDs(0,50,0,i); //this client is online
      aClientIsOnline=true;
    }
  }
  if(aClientIsOnline){
    statusLEDs(0,80,0,0); //25 is almost not visible next to a window. 50 is faint but solidly visible. 80 is perhaps on the dimmer side, but a solidly acceptable color for an indicator light
  }else{
    statusLEDs(0,0,25,0); 
  }
  
}

void confirmColorMode(){ //every day, re-publish the current color mode to the MQTT broker since it only retains the last message for 3 days
  if(millis()-confirmColorModeTimer>60000*60*24 && client.connected() && receivedColorMode){ //last condition is to only publish the mode if we're already confident in what it is
    char* tempmultiColorMode;
    if(multiColorMode){
      tempmultiColorMode="true";
    }else{
      tempmultiColorMode="false";
    }
    client.publish(multiColorTopic,tempmultiColorMode,true);
    confirmColorModeTimer=millis();
  }
}

unsigned long btnPressedAt=0;
bool btnCurrentlyPressed=false;
bool shortPressMsgSent=false;
int shortPressTime=50;

void loop(){

  if(!digitalRead(14)){
    if(!btnCurrentlyPressed){
      btnCurrentlyPressed=true;
      btnPressedAt=millis();
    }else{
      if(millis()-btnPressedAt>shortPressTime && !shortPressMsgSent){
        Serial.println("SHORT BTN PRESS");
        client.publish("LumasHearts/hearts/verify",("shortPress, " + WiFi.macAddress()).c_str());
        shortPressMsgSent=true;
      }
    }
  }else{
    btnCurrentlyPressed=false;
    shortPressMsgSent=false;
  }
  
  encoder->tick(); // just call tick() to check the state.

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  confirmColorMode();
  pingAndStatus();

  int posChangedBy=0;
  
  static int pos = 0;
  int newPos = encoder->getPosition();

  if(!multiColorMode){
    colorIndex=currentColor;
  }
  
  if (pos != newPos) {
    posChangedBy=newPos-pos;
/*    Serial.print("pos:");
    Serial.print(newPos);
    Serial.print(" dir:");
    Serial.println((int)(encoder->getDirection()));
*/    pos = newPos;
    
    colorIndex=colorIndex+(11*posChangedBy);
    if(colorIndex>=1024){
      colorIndex=colorIndex-1024;
    }
    if(colorIndex<0){
      colorIndex=1025+colorIndex;
    }
    //Serial.println(colorIndex);

    if(!multiColorMode){
      currentColor=colorIndex;
    }
  }
  
  int brighnessFactor=4096-analogRead(35); 
  //Serial.println(brighnessFactor);
  //Serial.println(brighnessFactor/4096.0);
  


  colorKnob=colorIndex; //these variables represent the same thing. It used to be called colorKnob, but the new code I wrote it as colorIndex, so for the time being I'm just setting the old name equal to the new name until I ensure the code's working right



  currentColor=colorIndex; //works with multicolor mode. On single color mode, heart changes it's local color and sends correct color, but remote heart doesn't receive it properly
  //colorIndex=currentColor; //just flat out wrong

  
  //brightness=rawBrightness; //because brightness is modified every loop, it doesn't hold it's value in non-analogRead cycles. rawBrightness retains it's og value

  if(isDark){
    int temp=0; //see commented out code below to modify
  }
  int thresholdBrightness=180;


  //The below figures out the ambient brightness of the room, based on a 10th order polynomial fit of data collection
  int b=brighnessFactor/16;
  int c=colorIndex;
  double threshold=calculateBrightnessThreshold(b,c); //calculateBrightnessThreshold

  /*Serial.println(c);
  Serial.print("\t");
  Serial.print(b);
  Serial.print("\t");

  Serial.print(rawBrightness);
  Serial.print("\t");
  Serial.print(threshold);
  Serial.print("\t");
  

  if(rawBrightness<threshold){
    Serial.println("dim");
  }else{
    Serial.print("bright");
  }
  //Serial.println("");
*/


  /*
  if(isDark && digitalRead(D0) && digitalRead(D6)){ //if it's dark, dim the lights and ignore the brightness knob
    lights.setBrightness(5);
    lights.show();
    isDark=false;
  }else if(!isDark && !digitalRead(D0) && !digitalRead(D6)){
    lights.setBrightness(lastBrightness);
    isDark=true;
  }
  */

  if(posChangedBy!=0 && millis()-lastSentColorAt>100){ //while user is turning knob, only send value every ____ to avoid flooding MQTT topic  
    itoa(currentColor, sendVal,10);
    strcat(sendVal,",");
    strcat(sendVal,WiFi.macAddress().c_str());
    strcat(sendVal,",");
    strcat(sendVal,clientName);
    client.publish(groupTopic,sendVal);
    lastSentColorAt=millis();
    currentlyChangingColor=true;
  }
    

  if(millis()-lastSentColorAt>100 && currentlyChangingColor){ //if user has just finished turning the knob, send one final color update (because the final color they settled on could have been within the last 500ms and therefore not sent)
    currentlyChangingColor=false;

    itoa(currentColor, sendVal,10);
    strcat(sendVal,",");
    strcat(sendVal,WiFi.macAddress().c_str());
    strcat(sendVal,",");
    strcat(sendVal,clientName);
    client.publish(groupTopic,sendVal);
  }

  if(multiColorMode){

    //create an array of client MAC addresses including our own to be sorted 
    char clientsIncludingMe[numOtherClientsInGroup+1][20];
    memcpy(clientsIncludingMe, otherClientsInGroup, numOtherClientsInGroup*20);
    strcpy(clientsIncludingMe[numOtherClientsInGroup],WiFi.macAddress().c_str());
    BubbleSort(clientsIncludingMe,numOtherClientsInGroup+1); //sort by MAC first in order to ensure consistant placement of each user across hearts

    //figure out what section to update based on the sorted MACs
    int sectionNumber=0;
    for(int i=0;i<numOtherClientsInGroup+1;i++){
      if(strcmp(WiFi.macAddress().c_str(),clientsIncludingMe[i])==0){
        sectionNumber=i;
      }
    }

    int sectionSize=12/(numOtherClientsInGroup+1); //no remainder except for when we have 5 total clients

    if(numOtherClientsInGroup+1!=5){
      for(int i=sectionSize*sectionNumber;i<sectionSize*(sectionNumber+1);i++){
        //lights.setPixelColor(i, lights.Color(getColor(colorIndex,'r')*(brighnessFactor/4096.0),getColor(colorIndex,'g')*(brighnessFactor/4096.0),getColor(colorIndex,'b')*(brighnessFactor/4096.0)));
        stripColors[i][0]=getColor(colorIndex,'r');
        stripColors[i][1]=getColor(colorIndex,'g');
        stripColors[i][2]=getColor(colorIndex,'b');
      }
    }else{ //special case because 12 is not divisible by 5 -- NOTE: untested at this time (4/1/24)
      int fiveClientSectionSize[]={3,2,2,2,3}; //for 5 clients, section size is as follows to preserve symetry: 3,2,2,2,3
      int startAt=0;
      for(int i=0;i<sectionNumber;i++){
        startAt+=fiveClientSectionSize[i];
      }
      for(int i=startAt;i<startAt+fiveClientSectionSize[sectionNumber];i++){
        //lights.setPixelColor(i, lights.Color(getColor(colorIndex,'r')*(brighnessFactor/4096.0),getColor(colorIndex,'g')*(brighnessFactor/4096.0),getColor(colorIndex,'b')*(brighnessFactor/4096.0)));
        stripColors[i][0]=getColor(colorIndex,'r');
        stripColors[i][1]=getColor(colorIndex,'g');
        stripColors[i][2]=getColor(colorIndex,'b');
      }
    }

  }else{
    for(int i=0;i<12;i++){
      //lights.setPixelColor(i, lights.Color(getColor(colorIndex,'r')*(brighnessFactor/4096.0),getColor(colorIndex,'g')*(brighnessFactor/4096.0),getColor(colorIndex,'b')*(brighnessFactor/4096.0)));
      stripColors[i][0]=getColor(currentColor,'r');
      stripColors[i][1]=getColor(currentColor,'g');
      stripColors[i][2]=getColor(currentColor,'b');
    }
    
  }

  for(int i=0;i<NUMPIXELS;i++){
    lights.setPixelColor(i, lights.Color(stripColors[i][0]*(brighnessFactor/4096.0),stripColors[i][1]*(brighnessFactor/4096.0),stripColors[i][2]*(brighnessFactor/4096.0)));
  }
  /*Serial.println(stripColors[0][0]);
  Serial.println(stripColors[0][1]);
  Serial.println(stripColors[0][2]);
  Serial.println(brighnessFactor);delay(1);*/
  lights.show();
}

//Give the current brightness b and color c that the heart is set to, and it will return the threshold of a "dark" room.
//The parameters (constants of the polynomial equation) are obtained by by running the PhotoResistor Calibration experiment and analysis
double calculateBrightnessThreshold(double b, double c) {
    // The 66 optimized coefficients from your program's output
    // Note: The order must match the nested loops below
    const double params[66] = {
        1.40661474e+03, -8.89724555e-01, -2.10193974e-02, 2.46326838e-04,
        -6.42576675e-07, -2.83601051e-09, 2.34030601e-11, -6.76396117e-14,
        1.00579625e-16, -7.66603184e-20, 2.37064871e-23, -3.89479094e+00,
        -1.19406348e-01, 2.60265737e-03, -1.10591439e-05, -2.41880700e-08,
        2.72285718e-10, -7.31926760e-13, 9.06326708e-16, -5.28297420e-19,
        1.13347866e-22, 9.12202162e-01, 1.59873048e-03, -4.27394787e-05,
        2.72696775e-07, -8.27112834e-10, 1.19205151e-12, -5.85548703e-16,
        -2.64756572e-19, 2.58909964e-22, -1.87959983e-02, 8.22251686e-06,
        2.24891931e-07, -1.03892880e-09, 2.58902993e-12, -4.01288622e-15,
        3.32190622e-18, -1.09541462e-21, 1.51212575e-04, -3.00454234e-07,
        -7.48563771e-10, 2.82924917e-12, -2.63329387e-15, 1.07750517e-18,
        -2.40538516e-22, 3.75189283e-08, 2.75900790e-09, 2.96533441e-14,
        -7.72570754e-15, 5.94040407e-18, -1.08724127e-21, -1.02094005e-08,
        -1.18971541e-11, 1.07818546e-14, 7.04151744e-18, -4.71521571e-21,
        8.40415379e-11, 2.37404048e-14, -3.02032433e-17, 1.28943661e-21,
        -3.27772689e-13, -1.49909539e-17, 2.44321530e-20, 6.49770875e-16,
        -6.80858148e-21, -5.26938205e-19
    };

    //Below this is horner's method. It's a way to calculate the above polynomial without impercise calculation of very large and very small numbers accumulating and causing wildly innacurate results
    double z = 0.0;
    int k = 0;
    int total_degree = 10;
    
    for (int i = 0; i <= total_degree; ++i) {
        for (int j = 0; j <= total_degree - i; ++j) {
            double term_val = params[k] * pow(b, i) * pow(c, j);
            z += term_val;
            k++;
        }
    }
    
    return z;
}
