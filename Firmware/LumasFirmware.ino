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

// Struct to hold the coefficients for a specific Color
struct PolyFitData {
    int color;
    double a0; // Intercept
    double a1; // Coefficient for x (Brightness^1)
    double a2; // Coefficient for x^2 (Brightness^2)
    double a3; // Coefficient for x^3 (Brightness^3)
};


#define MAX_GROUP_MEMBERS 20


//Rotary Encoder setup
#define ENCODER_PIN_IN1 18
#define ENCODER_PIN_IN2 19
RotaryEncoder *encoder = nullptr; //A pointer to the dynamic created rotary encoder instance. This will be done in setup()
IRAM_ATTR void checkEncoderPosition() //ISR called on any change of one of the input signals
{
  encoder->tick(); // just call tick() to check the state.
}

WiFiManager wifiManager;

String macAddress;

unsigned long devTimer=0;

//for use in changing colors/brightness
int newColorRed;
int newColorGreen;
int newColorBlue;
int stripColors[12][3]={{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}};

int colorIndex=0;

int lastBrighnessFactor=500; //initialized to a value very different from what it will immediately be set to

int rainbowEffectFirstPixelHue=0;

bool configPortalActive=false;
unsigned long configPortalStartedAt=0;

bool firstConnectAttempt=true; //set to false after first connection attempt so initial boot actions aren't repeated
unsigned long firstConnectAttemptAt=0;

bool waitingToSendConflictResolution=false;

const String FirmwareVer={"0.35"}; //used to compare to GitHub firmware version to know whether to update


//CLIENT SPECIFIC VARIABLES----------------
char clientName[25];
char hardwareVersion[10];
char MQTTPassword[15];

int numOtherClientsInGroup;//=1;
char otherClientsInGroup[MAX_GROUP_MEMBERS+1][25]; //08:3A:8D:CC:DE:62 is assembled, 7C:87:CE:BE:36:0C is bare board
bool otherClientsOnlineStatus[MAX_GROUP_MEMBERS+1]={false};
char groupName[25];//="PHUSSandbox";
//int modelNumber; //oh no... removing this initialization causes a seg fault if you try to start the WiFi portal (by holding the botton button) and no other clients in the group are online. .......Even though this variable isn't used anywhere anymore 😭
//.......wait, now that behavior is no longer there even if I comment out the initialization 🙃

//END CLIENT SPECIFIC VARIABLES------------

//unsigned long otherClientsLastPingReceived[6]={4294000000,4294000000,4294000000,4294000000,4294000000,4294000000}; //Updated whenever we receive a ping, and used to determine online status. The order follows otherClientsInGroup. --- Initialized to near max value to avoid indicator being green at boot
//NOTE!! the above variable replaces lastPingReceived

///These are oversized. I tried to shrink them with ~5 characters of margin, and it cuased a runtime error "Stack smashing protect failure!". No idea why. I put made them large again and the error went away 😅.
char groupTopic[70]; //LumasHearts/groups/[up to 24 char name]/color
char multiColorTopic[84]; //LumasHearts/groups/[up to 24 char name]/multicolorMode
char onlineStatusTopic[84]; //LumasHearts/groups/[up to 24 char name]/onlineStatus
char adminTopic[70]; //LumasHearts/admin
char consoleTopic[70]; //LumasHearts/console
char dbUpdateTopic[70];

bool receivedColorMode=false; //This variable is set true whenever we receive the multicolor mode, and false whenever we disconnect. This is to prevent this client from re-affirming the mode (publishing it to the broker to keep it active) incorrectly before we've actually received the current mode. It will probably be obsolete once we store this value in the database

#define NUMPIXELS 13 //13th pixel is the status LED on v3.1 onwards. On v3.0 there is no 13th LED connected, so commands to it just get sent into the abyss, which is fine.
Adafruit_NeoPixel lights(NUMPIXELS, 27, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel indicator(1, 4, NEO_GRB + NEO_KHZ800); //only for hardware version 3.0
int indicatorColor[3]={0,0,0};
bool evenSecond=false;

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

unsigned long lastReceivedColorAt=0;
unsigned long lastSentColorAt=0;
bool currentlyChangingColor=false;

char sendVal[60]; //array to store value to send (must be long enough to hold [color number; this client name; this MAC address --17 chars]) //now 5+25+18+3 (ish)           //OLD COMMENT: //array to store value to send to other heart MUST BE [5] FOR 4 CHAR VALUE!! Due to because of termination char?

unsigned long lastPingSent; //time the last ping was sent
unsigned long lastPingReceived;
int timeout=30000; //time in milliseconds between pings


boolean isDark;

long brightnessLastChangedAt=0;

int brightnessThresholdOffset = 100; //as of 9/28 working experimental values are 100/0/300 (on v3.0). as of 12/2 almost working values for 3.1 are 200/0/600 and are set later in setup
int goDimOffset = 0;
int goBrightOffset = 300;

//BELOW CODE IS FOR GOOGLE SHEETS "DATABASE" -- temporary solution that should hopefully replace EEPROM until we get a real database
//String googleSheetURL ="https://docs.google.com/spreadsheets/d/1FMWpVuE9PxkHIEMgdaKUi_d1UH7pvPvcPBorXB6OsQY/gviz/tq?tqx=out:csv&sheet=Active&range="; //append a range, eg: "a1:b4" to use this URL
//goodbye google sheet

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

//deprecated as we're moving to an on-demand, non-blocking setup. New function below this one.
void setup_wifi() {
  WiFiManager manager;
  manager.setDebugOutput(false);
  //manager.resetSettings();
  
  Serial.println("Attempting to connect to saved network");
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
      statusLEDs(150,60,0);

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


void startConfigPortal(){

  char bufNetName[41];
  strcpy(bufNetName,clientName);
  strcat(bufNetName,"'s Lumas Setup");
  String networkName=String(bufNetName);
  Serial.print("Starting config AP with SSID: ");
  Serial.println(networkName);

  // Switch wifiManager config portal IP from default 192.168.4.1 to 8.8.8.8. This ensures auto-load on some older android devices which have 8.8.8.8 hard-coded in the OS.
  wifiManager.setAPStaticIPConfig(IPAddress(8,8,8,8), IPAddress(8,8,8,8), IPAddress(255,255,255,0));
  wifiManager.setTitle("Lumas Config");
  wifiManager.setMac(macAddress); //Ok. So. setMac is a custom function I added to the library. Declared in WiFiManager.h, used in WiFiManager.cpp, and affecting wm_strings_en.h (ctrl+f for "blaineModified")

  wifiManager.startConfigPortal(networkName.c_str(),"");
  configPortalActive=true;
  configPortalStartedAt=millis();
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
    Serial.println("Usually restarting fixes this, so we will restart now...");
    Serial.println("If you are encountering this message in a loop, it probably means that the WiFi network you're connected to doesn't have internet access (perhaps a captive portal), or you have weak WiFi signal.");
    for(int i=0;i<10;i++){ //flash indicator rapidly to indicate this happened ERROR FLASH
      delay(100);
      statusLEDs(100,100,0);
      delay(100);
      statusLEDs(0,0,0);
    }
    ESP.restart();
    return;
  }

  // Access the values
  const char* id = doc["id"];
  const char* group = doc["group"];
  const char* heartName = doc["name"];
  const char* hardware_version = doc["hardware_version"];
  const char* mqtt_psswd = doc["mqtt_password"];
  Serial.printf("Extracted values: id=%s, group=%s, name=%s, hardware_version=%s, mqtt_password=%s\n", id, group, heartName, hardware_version, mqtt_psswd);
  Serial.println(group);



  //STEP 2: Using this client's group name, look up others!
  int countNumOtherClientsInGroup=0;
  char otherClients[MAX_GROUP_MEMBERS+1][25]; //used to be char* otherClients[6]
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
        //otherClients[countNumOtherClientsInGroup] = (char*)heartID;  //used to be =(char*)heartID
        strcpy(otherClients[countNumOtherClientsInGroup],heartID); //added
        Serial.print(heartID);
        Serial.print(" --> ");
        Serial.println(otherClients[countNumOtherClientsInGroup]);
        countNumOtherClientsInGroup++;
      }
    }
    if(countNumOtherClientsInGroup>MAX_GROUP_MEMBERS){ //ERROR FLASH
      while(true){ //flash indicator rapidly to indicate this happened
      delay(100);
      statusLEDs(100,0,0);
      delay(100);
      statusLEDs(0,0,0);
      }
    }
  }

  //Clear arrays (if the group is updated to fewer members, we don't want the old ones to stick around)
  for(int i=0;i<MAX_GROUP_MEMBERS;i++){
    //otherClientsInGroup[i]='\0'; //this doesn't work
    otherClientsOnlineStatus[i]=false;
  }
  memset(otherClientsInGroup, 0, sizeof(otherClientsInGroup));

  //STEP 3: load variables in to local memory
  Serial.print("Putting value ");
  Serial.print(heartName);
  Serial.print(" ----> ");
  strcpy(clientName,heartName);
  Serial.println(clientName);

  Serial.println(mqtt_psswd);
  strcpy(hardwareVersion,hardware_version);
  //strcpy(MQTTPassword,mqtt_psswd);

  Serial.print("Hardware Version: ");
  Serial.println(hardware_version); //BLAINEEEEEEE
  //COPY THIS NAME IN TO EEPROM IF IT DIFFERS, cause it is used in wifi setup pre-network connection
  EEPROM.begin(173);
  char ch_clientName[25];
  EEPROM.get(3,ch_clientName);
  char ch_hardware_version[10];
  EEPROM.get(29,ch_hardware_version);
  char ch_mqtt_password[15];
  EEPROM.get(40,ch_mqtt_password);
  Serial.print("Read in EEPROM value: ");
  Serial.println(ch_clientName);
  EEPROM.end();
  if(strcmp(ch_clientName,clientName)!=0){ //if different, put new name in to EEPROM
    Serial.println("Name updated, updating local EEPROM value");
    EEPROM.begin(173);
    EEPROM.put(3,clientName);
    EEPROM.end();
  }
  if(strcmp(ch_hardware_version,hardwareVersion)!=0){
    Serial.println("Hardware Version updated, updating local EEPROM value");
    EEPROM.begin(173);
    EEPROM.put(29,hardwareVersion);
    EEPROM.end();
  }
  /*Serial.print("EEPROM MQTT PASSWORD ----------- ");
  Serial.println(ch_mqtt_password);
  Serial.print("AWS MQTT PASSWORD ----------- ");
  Serial.println(mqtt_psswd);*/
  if(strcmp(ch_mqtt_password,mqtt_psswd)!=0 && strcmp(mqtt_psswd,"USELOCAL")!=0){
    Serial.println("MQTT Password updated, updating local EEPROM value");
    EEPROM.begin(173);
    char ch_AWSMQTT[15];
    strcpy(ch_AWSMQTT,mqtt_psswd); //it freaks out if you try to put a char* in eeprom, so copy it to fixed length first
    EEPROM.put(40,ch_AWSMQTT);
    EEPROM.end();
    strcpy(MQTTPassword,mqtt_psswd);
  }

  Serial.print("Putting value ");
  Serial.print(group);
  Serial.print(" ----> ");
  strcpy(groupName,group);
  Serial.println(groupName);
  //modelNumber=atoi((httpResult.substring(nthIndex(httpResult,'\"',5)+1,nthIndex(httpResult,'\"',6))).c_str());

  
  if(strcmp(group,"None")!=0){
    numOtherClientsInGroup=countNumOtherClientsInGroup;
    for(int i=0;i<countNumOtherClientsInGroup;i++){
      Serial.print("Copying ");
      Serial.print(otherClients[i]);
      Serial.print(" in to array as ");
      strcpy(otherClientsInGroup[i],otherClients[i]);
      Serial.println(otherClientsInGroup[i]);
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
void BubbleSort (char arry[][25], int m){ //m is number of elements
    char valA[25];
    char valB[25];
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
    }else if(payload.equals("")){
        Serial.println("Latest firmware version undefined. Likely you have a bad network connection.");
    }else {
        Serial.println("New firmware detected");
          statusLEDs(150,150,150); //all white indicates we're in a firmware update
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

/*
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
*/

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
  strcpy(onlineStatusTopic,"");
  strcat(onlineStatusTopic,groupTopic);
  strcat(onlineStatusTopic,"/onlineStatus");
  Serial.println(groupName);
  strcpy(multiColorTopic,""); //re-initalize this to empty!! Otherwise it overflows when updated
  strcat(multiColorTopic,groupTopic);
  strcpy(dbUpdateTopic,"");
  strcat(dbUpdateTopic,groupTopic);
  strcat(dbUpdateTopic,"/dbUpdate");
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
  

  
  //finsish lights setup
  lights.begin();
  lights.clear();
  //startup animation
  /*
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
  */

  //new startup animation
  int ambientBrightness=analogRead(34);
  if(ambientBrightness<1400){ //if we've booted in a dark room, it may be a reboot due to network failure. Don't flood the room with rainbow in the middle of the night (<3 u Kenzie)
    lights.setBrightness(5);
  }else{
    lights.setBrightness(255);
  }
  rainbowEffect(); //start the animation here. Keep calling this at least every 10 milliseconds until network connection sequence has completed


  //Serial.print("This client's MAC address is: ");
  //Serial.println(WiFi.macAddress());

  Serial.println("Checking EEPROM configuration...");
  //read in client name before wifi setup because the variable is used for the network name
  EEPROM.begin(173);
  //Serial.println(EEPROM.read(0));
  //Serial.println(EEPROM.read(1));
  //Serial.println(EEPROM.read(2));
  if(!(EEPROM.read(0)=='A' && EEPROM.read(1)=='B' && EEPROM.read(2)=='C')){
    Serial.println("EEPROM has never been initialized!! Please run the New Lumas Setup script to configure this heart to work with the system");
    while(true){
      //flash indicator rapidly to indicate this happened ERROR FLASH
      delay(100);
      statusLEDs(0,0,100);
      delay(100);
      statusLEDs(0,0,0);
    }
    /*Serial.println("EEPROM has never been initialized, initializing 173 bytes now.");
    EEPROM.write(0,'A');
    EEPROM.write(1,'B');
    EEPROM.write(2,'C');
    for(int i=3;i<173;i++){
      EEPROM.write(i,0);
    }
    EEPROM.put(3,"New Heart");
    EEPROM.commit();
    */
  }else{
    Serial.println("EEPROM is already set up");
  }
  EEPROM.end();

  //now open EEPROM again for actual usage
  EEPROM.begin(173);
  char ch_clientName[25];
  EEPROM.get(3,ch_clientName);

  char ch_hardware_version[10];
  EEPROM.get(29,ch_hardware_version);

  char ch_MQTT_Password[15];
  EEPROM.get(40,ch_MQTT_Password);

  EEPROM.end();
  strcpy(clientName,ch_clientName);
  strcpy(hardwareVersion,ch_hardware_version);
  strcpy(MQTTPassword,ch_MQTT_Password);


  if(strcmp(hardwareVersion,"3.0")==0){ //only initialize if we're using v3.0 which has a seperate "strip" for the indicator
    indicator.begin();
    indicator.clear();
    indicator.show();

    brightnessThresholdOffset=100;
    goDimOffset=0;
    goBrightOffset=300;
  }

  if(strcmp(hardwareVersion,"3.1")==0){
    brightnessThresholdOffset=200;
    goDimOffset=0;
    goBrightOffset=600;
  }

  //must set status LED after reading in EEPROM hardware version, since hardware for status LEDs are different depending on hardware version
  statusLEDs(100,0,0);

  
  //setup_wifi(); //switching to on-demand config
  // if you get here you have connected to the WiFi
  //Serial.println("Connected to WiFi");

  wifiManager.setConfigPortalBlocking(false);
  Serial.println("Will now try to connect to saved wifi network...");
  WiFi.begin(); //attempt to connect to saved network (blank parameter uses EEPROM values)
  macAddress=WiFi.macAddress(); //for some reason this seems to be the only time in wifi setup & retries that we can reliably get the MAC -_-
  unsigned long beginTime=millis();
  while(WiFi.status()!=WL_CONNECTED){
    rainbowEffect();
    yield(); //prevent WDT reset
    if(millis()-beginTime>6000){ //if not connected after 6 seconds, we're probably not going to
      break;
    }
  }
  if(WiFi.status()==WL_CONNECTED){ 
    Serial.println("Succesfully connected to saved network, config portal off");
  }else{
    Serial.println("failed to connect, starting config portal");
    statusLEDs(150,50,0);
    startConfigPortal();
  }

  unsigned long initialConfigTimeoutTimer=millis();
  while(WiFi.status()!=WL_CONNECTED){ //stay here while wifi config portal is running for the first time
    rainbowEffect(); 
    wifiManager.process(); //to let wifimanager config portal run in the background
    if(WiFi.softAPgetStationNum()>0){ //if someone's connected to the AP, don't reset
      initialConfigTimeoutTimer=millis();
    }
    if(millis()-initialConfigTimeoutTimer>60000*5){
      Serial.println("temporarily shutting off config AP to try connecting to saved WiFi again");
      //WiFi.softAPdisconnect(true);
      WiFi.disconnect(true);
      WiFi.begin(); //this tries again to connect to saved network in case it's available and the connection just failed the first time (or there was a temporary outage)
      beginTime=millis();
      while(WiFi.status()!=WL_CONNECTED && millis()-beginTime<6000){
        yield();
        rainbowEffect(); //unfortunately the animation can't continue after this because the httpGet function is blocking, and is 99% of the wait from network connection to all set up (due to downloading data from database)
      }
      if(WiFi.status()==WL_CONNECTED){
        Serial.println("reconnect attempt succesful!");
        break;
      }else{
        Serial.println("reconnect attempt failed");
      }
      initialConfigTimeoutTimer=millis(); //reset so we don't try again until after another wait period
      delay(100);
      Serial.println("restarting config portal");
      WiFi.disconnect(true); //this seems to be necessary for the next instance of the config portal to work
      //WiFi.begin(); //....but this is needed to get the MAC for the config portal -_-. If this causes it to crash too, let's just save the MAC in a variable the first time.
      startConfigPortal(); //restart the config portal
    }
  }
  Serial.println();

  Serial.println("Network connection success!");
  Serial.println("shutting off AP");
  WiFi.softAPdisconnect(true);
  configPortalActive=false;

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
  //currentColor=analogRead(A0);
  
  currentColor=0;
  
  lastColorKnobVal=currentColor;

  lights.setBrightness(255); //If heart was booted in the dark, strip got set to dim mode. Set it back to full brightness in preperation for regular operation

  //commenting this out because it seems to do nothing really
  /*  for(int i=0;i<NUMPIXELS;i++){
      lights.setPixelColor(i, lights.Color(getColor(currentColor,'r'),getColor(currentColor,'g'),getColor(currentColor,'b')));
    }
    //lights.setBrightness(analogRead(35)/16);
    lights.show();
  */
}


void statusLEDs(int red, int green, int blue){
  //set global variables so that if we have to flash it, we know what color it's supposed to be
  indicatorColor[0]=red;
  indicatorColor[1]=green;
  indicatorColor[2]=blue;

  if(!configPortalActive){
    //Serial.print("HARDWARE VERSION IS ");
    //Serial.println(hardwareVersion);
    if(strcmp(hardwareVersion,"3.0")==0){
      indicator.setPixelColor(0,indicator.Color(red,green,blue));
      indicator.show();
    }else if(strcmp(hardwareVersion,"3.1")==0){
      lights.setPixelColor(12,indicator.Color(red,green,blue));
      lights.show();
    }
  }else{ //if the config portal is active, alternate between showing the normal status and that the config portal is launched.
    int startingWith=evenSecond;
    if(((millis()/1000)/2)%2==0){ //swap every 2 seconds
      evenSecond=true;
    }else{
      evenSecond=false;
    }
    if(startingWith!=evenSecond){
      //Serial.println("swapping!");
      if(evenSecond){
        //Serial.println("general status");
        if(strcmp(hardwareVersion,"3.0")==0){
          indicator.setPixelColor(0,indicator.Color(indicatorColor[0],indicatorColor[1],indicatorColor[2]));
          indicator.show();
        }else if(strcmp(hardwareVersion,"3.1")==0){
          lights.setPixelColor(12,indicator.Color(indicatorColor[0],indicatorColor[1],indicatorColor[2]));
          lights.show();
        }
      }else{
        //Serial.println("WiFi portal status");
        if(strcmp(hardwareVersion,"3.0")==0){
          indicator.setPixelColor(0,indicator.Color(150,60,0));
          indicator.show();
        }else if(strcmp(hardwareVersion,"3.1")==0){
          lights.setPixelColor(12,indicator.Color(150,60,0));
          lights.show();
        }
      }
    }

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

  //payload[length] = '\0'; // Add a NULL to the end of the char* to make it a string. //future Blaine here -- is this not overwriting the last character?? Or the first one of the next variable in memory?? We're gonna try to redo this below

  char payloadCStr[length+1]; 
  memcpy(payloadCStr, payload, length);
  payloadCStr[length] = '\0'; // Add a null terminator so from here forward we can treat the payload like a cstring and use strcpy etc

  //Serial.println("Received msg");
  
  if(strcmp(topic,multiColorTopic)==0){
    receivedColorMode=true;
    Serial.println("multicolor topic");
    
    //char* payloadChar = (char *)payload; //......what kind of line is this?? ...but I'm scared to delete it   //newer and smarter blaine has replaced this with the new payloadStr above
    if(strcmp(payloadCStr,"true")==0){
      multiColorMode=true;
    }else{
      multiColorMode=false;
    }
    Serial.println(multiColorMode);
  }else if(strcmp(topic,onlineStatusTopic)==0 && strcmp(groupTopic,"LumasHearts/groups/None/color")!=0){

    //Serial.print("Received online status: ");
    //Serial.println(String((char*)payload));
    
    String strPayload = String(payloadCStr); //used to be String((char*)payload)
    int firstCommaIndex=strPayload.indexOf(',');
    int secondCommaIndex=strPayload.indexOf(',',firstCommaIndex+1);
    String thisHeartOnline=strPayload.substring(0,firstCommaIndex);
    String thisHeartMAC=strPayload.substring(firstCommaIndex+1);

    //TODO: Blaine you are here
    //now check if thisHeartMac is present in otherClientsInGroup, and if so (cause it should be), set the corresponding index of otherClientsOnlineStatus
    for(int i=0;i<numOtherClientsInGroup;i++){
      //Serial.print("Checking if received message matches other client: ");
      //Serial.println(otherClientsInGroup[i]);
      if(strcmp(thisHeartMAC.c_str(),otherClientsInGroup[i])==0){
        //Serial.print("match found, heart ");
        //Serial.println(i);
        if(strcmp(thisHeartOnline.c_str(),"Online")==0){
          otherClientsOnlineStatus[i]=true;
          //Serial.println("setting another heart to online!");
        }else if(strcmp(thisHeartOnline.c_str(),"Offline")==0){
          otherClientsOnlineStatus[i]=false;
        }
      }
    }

    //otherClientsOnlineStatus

  }else if(strcmp(topic,dbUpdateTopic)==0){
    if(strcmp(payloadCStr,"DATABASE_UPDATE")==0){ //this is the same as the admin command, but the webapp doesn't have permission to send to /admin, so we can receive it here too. This also targets just this group.
      Serial.println("Will update all local variables with the values from the AWS DB...");
      loadClientSpecificVariables();
      //unsubscribe from old topics in case they were updated
      Serial.print("Unsubscribing from group ");
      Serial.println(groupTopic);
      client.unsubscribe(groupTopic);
      client.unsubscribe(multiColorTopic);
      client.unsubscribe(onlineStatusTopic);
      client.unsubscribe(dbUpdateTopic);
      updateTopicVariables();
      loadClientSpecificVariables(); //this updates variables like the array of other online clients
      //now subscribe to new ones
      client.subscribe(groupTopic);
      client.subscribe(multiColorTopic);
      client.subscribe(onlineStatusTopic);
      client.subscribe(dbUpdateTopic);
      Serial.print("Now subscribed to group ");
      Serial.println(groupTopic);
    }
  }else if(strcmp(topic,adminTopic)==0){ //an admin command
    String strPayload = String(payloadCStr);
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
          statusLEDs(150,150,150); //all white indicates we're in a firmware update
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
        Serial.println("Will update all local variables with the values from the AWS DB...");
        loadClientSpecificVariables();
        //unsubscribe from old topics in case they were updated
        Serial.print("Unsubscribing from group ");
        Serial.println(groupTopic);
        client.unsubscribe(groupTopic);
        client.unsubscribe(multiColorTopic);
        client.unsubscribe(onlineStatusTopic);
        client.unsubscribe(dbUpdateTopic);
        updateTopicVariables();
        //now subscribe to new ones
        client.subscribe(groupTopic);
        client.subscribe(multiColorTopic);
        client.subscribe(onlineStatusTopic);
        client.subscribe(dbUpdateTopic);
        Serial.print("Now subscribed to group ");
        Serial.println(groupTopic);
      }
      if(command=="WHOSTHERE"){
        client.publish(onlineStatusTopic,("Online,"+WiFi.macAddress()).c_str());
      }
      if(command=="DEV"){ //to be used ONLY in development. This block should be blank in prod hearts.
        brightnessThresholdOffset=adminPayload.substring(0,3).toInt(); 
        goDimOffset=adminPayload.substring(3,6).toInt();
        goBrightOffset=adminPayload.substring(6).toInt();
        Serial.print("Changing auto-dim parameters... ");
        Serial.print(brightnessThresholdOffset);
        Serial.print("\t");
        Serial.print(goDimOffset);
        Serial.print("\t");
        Serial.println(goBrightOffset);

      }
    }
    
  }else if(strcmp(topic,groupTopic)==0 && strcmp(groupTopic,"LumasHearts/groups/None/color")!=0){ //unclaimed hearts go in the "None" group. So ignore incoming commands if we're in that group, because they are not supposed to be "connected"
    

    String strPayload = String(payloadCStr);
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
      if(rcvNum==-1){ //if other heart just came online, ignore the value (-1), but ping it to let it know we're here too and give it what color we're in.
        itoa(currentColor, sendVal,10);
        strcat(sendVal,",");
        strcat(sendVal,WiFi.macAddress().c_str());
        strcat(sendVal,",");
        strcat(sendVal,clientName);
        client.publish(onlineStatusTopic,("Online,"+WiFi.macAddress()).c_str());
        client.publish(groupTopic,sendVal); //send this second. Hopefully that gives the other heart a chance to update status first so it doesn't re-request online status with "-2" since it just came online and didn't get the online message yet
      }else if(rcvNum==-2){ //-2 means the other heart is just checking online status, no need for color update
        client.publish(onlineStatusTopic,("Online,"+WiFi.macAddress()).c_str());
      }else{ //only update color from remote heart if it wasn't it's first ping to say it's online
    
        int currentColorRemote; //the color of the other heart
        currentColorRemote = rcvNum; 

        lastReceivedColorAt=millis();

        if(multiColorMode){
          //create an array of client MAC addresses including our own to be sorted 
          char clientsIncludingMe[numOtherClientsInGroup+1][25];
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
          
          if(millis()-lastSentColorAt<1000 && currentColor!=currentColorRemote){ //if we just sent a value, the received value might have crossed mid-air and we should handle the conflict before continuing.
            bool ignoring=false;
            //Serial.print("Conflict between ");
            //Serial.print(currentColor);
            //Serial.print(" & ");
            //Serial.print(currentColorRemote);
            if(currentColor>currentColorRemote){ //Use higher value of the two.
              currentColorRemote=currentColor;
              ignoring=true;
            }
            //Serial.print("\t Resolution: ");
            //Serial.print(currentColorRemote);
            if(!ignoring){
              //Serial.print("\tUsing my color, I will re-send");
              waitingToSendConflictResolution=true;
            }
            //Serial.println();
          }

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
      //we no longer check online status with heartbeats
      /*
      for(int i=0;i<numOtherClientsInGroup;i++){
        if(strcmp(fromClientMac.c_str(),otherClientsInGroup[i])==0){ //find index to update
          otherClientsLastPingReceived[i]=millis();
        }
      }
      */

      //if we received a color but we think nobody else is online, we're probably wrong (unless it came from the webapp). So let's ask everyone else to send their status to check
      //necessary because every once in awhile the initial "who's online" handshake doesn't work properly, so this is a failsafe so we're not stuck thinking everyone's offline
      bool knowOfOthersOnline=false;
      for(int i=0;i<(sizeof(otherClientsOnlineStatus)/sizeof(otherClientsOnlineStatus[0]));i++){ //check if we already know that others are online
        if(otherClientsOnlineStatus[i]){
          knowOfOthersOnline=true;
          //Serial.println("detected another client online");
          break; //no need to check every single one
        }
      }
      if(!knowOfOthersOnline && !(strcmp(ch_fromClientName,"WEB_APP")==0) && rcvNum!=-1 && rcvNum!=-2){ //if we didn't know, re-run check algorithm
        /*Serial.println();
        Serial.println();
        Serial.println();
        Serial.println();
        Serial.println("CHECKING STATUS BECAUSE RECEIVED MSG:");
        Serial.println(strPayload);*/

        client.publish(onlineStatusTopic,("Online,"+WiFi.macAddress()).c_str()); //tell others we're here in case they don't know
        client.publish(groupTopic,("-2,"+WiFi.macAddress()+","+clientName).c_str()); //ask who else is there (this will trigger the callback that sents the appropriate otherClientsOnlineStatus value to true)
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
    String macHyphens=WiFi.macAddress();
    macHyphens.replace(":", "-");
    if (client.connect(WiFi.macAddress().c_str(),macHyphens.c_str(),MQTTPassword,onlineStatusTopic,2,false,("Offline,"+WiFi.macAddress()).c_str())){
      Serial.println("connected");
      if(firstConnectAttempt){
        client.publish(onlineStatusTopic,("Online,"+WiFi.macAddress()).c_str());
        //client.publish("startLocationUpdater","start"); //10/26/25 -- WE'VE CHANGED ARCHETECTURE! This is no longer needed, and instead handled by a lambda that monitors log files     //tell EC2 locationUpdater script to run. It will see this heart in the mosquitto logs and update it's IP and location in the AWS database
      }
      firstConnectAttempt=false;
      firstConnectAttemptAt=millis();
      receivedColorMode=false;
      // Once connected, publish an announcement and re-subscribe
      //Serial.println(groupTopic);
      client.subscribe(adminTopic);
      client.subscribe(multiColorTopic);
      client.subscribe(onlineStatusTopic);
      client.subscribe(dbUpdateTopic);
      client.subscribe(groupTopic); //subscribe first so that when we send -1 below, we can receive the response right away
      client.loop(); //this may be necessary to ensure we can actually receive messages before we publish -1 asking for a response
      itoa(-1, sendVal,10);
      strcat(sendVal,",");
      strcat(sendVal,WiFi.macAddress().c_str());
      strcat(sendVal,",");
      strcat(sendVal,clientName);
      client.publish(groupTopic,sendVal); // -1 indicates we just came online and are requesting other heart's value
      
      
    } else {
      //statusLEDs(100,0,0,0);
      //for now, this pattern means we are offline
        statusLEDs(100,0,0);
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

  //BELOW IS OLD ONLINE DETERMINATION USING HEARTBEAT. SCROLL FURTHER DOWN FOR NEW METHOD USING ONLINE STATUS MQTT TOPIC
  /*
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
*/

  //ABOVE IS OLD CODE FOR SINGLE STATUS LIGHT. BELOW IS NEW CODE WITH MULTIPLE INDICATOR LIGHTS

  //We can assume otherClientsInGroup is sorted, as it is sorted whenever it is updated in loadClientSpecificVariables()
  //We will use the other client's position in the array as the assigned number indicator
  //the data updating happens in the callback for processing an incoming color
  //----Take above comments with a grain of salt, I'm about to modify what's below for use with single status indicator

  bool aClientIsOnline=false;
  for(int i=0;i<numOtherClientsInGroup;i++){
    if(otherClientsOnlineStatus[i]){
      aClientIsOnline=true;
    }
  }
  if(aClientIsOnline){
    statusLEDs(0,80,0); //25 is almost not visible next to a window. 50 is faint but solidly visible. 80 is perhaps on the dimmer side, but a solidly acceptable color for an indicator light
  }else{
    statusLEDs(0,0,80); 
  }
  
}

void confirmColorMode(){ //every day, re-publish the current color mode to the MQTT broker since it only retains the last message for 3 days
  if(millis()-confirmColorModeTimer>60000*60*24 && client.connected() && receivedColorMode){ //last condition is to only publish the mode if we're already confident in what it is
    char tempmultiColorMode[5];
    if(multiColorMode){
      strcpy(tempmultiColorMode, "true");
    }else{
      strcpy(tempmultiColorMode, "false");
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
        Serial.println("SHORT BTN PRESS (note, this fires on button press, not un-press. So long press will always trigger this first).");
        //client.publish("LumasHearts/hearts/verify",("shortPress, " + WiFi.macAddress()).c_str()); //changed to use new topic below
        String macHyphens = WiFi.macAddress(); 
        macHyphens.replace(":", "-"); //since colons can't be part of an MQTT username, the username has to use hyphens. And since the Mosquitto ACL grants topic access based on username, this topic has to be formatted the same way.
        String verifyTopic="LumasHearts/hearts/verify/"+macHyphens;
        client.publish(verifyTopic.c_str(),("shortPress, " + WiFi.macAddress()).c_str());
        shortPressMsgSent=true;
      }
      if(millis()-btnPressedAt>2000 && !configPortalActive){
        Serial.println("Starting config portal");
        configPortalActive=true;
        configPortalStartedAt=millis();
        statusLEDs(150,50,0); //set it to orange so it immediately shows to tell the user they've done it correctly. Once startConfigPortal() finishes, pingAndStatus() will take over again
        startConfigPortal();
      }
    }
  }else{
    btnCurrentlyPressed=false;
    shortPressMsgSent=false;
  }


  wifiManager.process(); //let config portal run in the background

  if(configPortalActive && WiFi.softAPgetStationNum()>0){ //reset timeout of AP if client is connected to portal
    configPortalStartedAt=millis();
  }
  if(configPortalActive && millis()-configPortalStartedAt>3*60000){
    Serial.println("Config portal inactive, shutting off AP");
    WiFi.softAPdisconnect(true);
    configPortalActive=false;
  }


  //WIFIMANAGER ON-DEMAND HANDLING
  
  
  encoder->tick(); // just call tick() to check the state.

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  resolveColorConflict();
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
  
  bool brightnessChangedThisLoop=false;
  int brighnessFactor=lastBrighnessFactor;
  int tempBrightnessReading=4096-analogRead(35);
  int antiJitterValue=80;
  int minBrightnessFactor=80; // 80/4096 = 5/255
  if(tempBrightnessReading>lastBrighnessFactor+antiJitterValue || tempBrightnessReading<lastBrighnessFactor-antiJitterValue || (lastBrighnessFactor!=minBrightnessFactor && tempBrightnessReading<2)){
    brighnessFactor=tempBrightnessReading;
    if(tempBrightnessReading<2){ //if knob is at minimum, set to "minimum brightness" instead of turning completely off
      brighnessFactor=minBrightnessFactor; 
    }
    lastBrighnessFactor=brighnessFactor;
    brightnessChangedThisLoop=true;
  }
   

  colorKnob=colorIndex; //these variables represent the same thing. It used to be called colorKnob, but the new code I wrote it as colorIndex, so for the time being I'm just setting the old name equal to the new name until I ensure the code's working right

  currentColor=colorIndex; //works with multicolor mode. On single color mode, heart changes it's local color and sends correct color, but remote heart doesn't receive it properly
  

  //The below figures out the ambient brightness of the room, based on a 10th order polynomial fit of data collection
  int b;
  if(isDark){
    b=5;
  }else{
    b=brighnessFactor/16;
  }
  int c=colorIndex;
  double threshold=calculateBrightnessThreshold(b,c); //calculateBrightnessThreshold

  if(strcmp(hardwareVersion,"3.0")==0){
    threshold=calculateBrightnessThresholdv3_0(b,c);
  }

  threshold=threshold+brightnessThresholdOffset; //seems like the experiment was done slightly too bright. Experimentally, subtracting (adding?) from threshold makes it more reliably enter dim mode in dark rooms

  rawBrightness=analogRead(34);

/*
  if(millis()-devTimer>1000){
    String sendDat="Color: "+String(c)+"  Brightness: "+String(b)+"  Measured Brightness: "+String(rawBrightness)+"  Threshold: "+String(calculateBrightnessThreshold(b,c));
    client.publish("LumasHearts/console",sendDat.c_str());
    devTimer=millis();
  }
*/

  /*
  Serial.println(c);
  Serial.print("\t");
  Serial.print(b);
  Serial.print("\t");
  
  Serial.print(rawBrightness);
  Serial.print("\t");
  Serial.print(threshold);
  Serial.println("  :)\t");
  */
  
  bool updateLightsRequiredThisLoop=false;
  if(digitalRead(23)){ //only do auto-dim if auto-dim switch is set.
    if(millis()-brightnessLastChangedAt>1000){ //photoresistor takes some time to settle, so after changing brightness wait a little bit before attempting to read again (also just nice to user not to rapidly flash)
      if(rawBrightness<threshold-goDimOffset){
        //Serial.println("dim");
        if(isDark==false){ //if it just became dark
          updateLightsRequiredThisLoop=true;
          brightnessLastChangedAt=millis();
        }
        isDark=true;
      }else{
        //if(rawBrightness>threshold+goBrightOffset){ //hysterisis attempt
        int thresholdValDim=1000; //default val that's close to right either way in case if statement fails
        if(strcmp(hardwareVersion,"3.0")==0){
          thresholdValDim=1410;
        }else if(strcmp(hardwareVersion,"3.1")==0){
          thresholdValDim=988+brightnessThresholdOffset;
        }

        if(rawBrightness>thresholdValDim+goBrightOffset){ //calcs at low brightness are prone to being wrong, and low brightness isn't much affected by color, so just compare to a static value that seems to be right according to data collection for brightness level 6
          //Serial.print("bright");
          if(isDark==true){ //if it just became bright
            updateLightsRequiredThisLoop=true;
            brightnessLastChangedAt=millis();
          }
          isDark=false;
        }
      }
    }
  }else{ //if we're not in auto-dim mode
    if(isDark){ //if we were dimmed, un-dim
      updateLightsRequiredThisLoop=true;
    }
    isDark=false;
  }

  if(isDark){
    brighnessFactor=5*16;
  }
  //Serial.println("");




  if(posChangedBy!=0 && millis()-lastSentColorAt>100){ //while user is turning knob, only send value every ____ to avoid flooding MQTT topic  
    itoa(currentColor, sendVal,10);
    strcat(sendVal,",");
    strcat(sendVal,WiFi.macAddress().c_str());
    strcat(sendVal,",");
    strcat(sendVal,clientName);
    if(strcmp(groupTopic,"LumasHearts/groups/None/color")!=0){ //don't send color data to the None group
      client.publish(groupTopic,sendVal,true);
    }
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
    if(strcmp(groupTopic,"LumasHearts/groups/None/color")!=0){ //don't send color data to the None group
      client.publish(groupTopic,sendVal,true);
    }
  }

  if(multiColorMode){

    //create an array of client MAC addresses including our own to be sorted 
    char clientsIncludingMe[numOtherClientsInGroup+1][25];
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

  //if nothing changed, no need to re-set the strip. (The only real point in doing this is to prevent v3.0 hearts without the level shifter from flickering so much, cause they flicker a small percentage of the time but only during strip updates. If this line weren't there, strip updates would be constant)
  //actually, there's a chance this helps WiFi reception too
  if((posChangedBy!=0 || !brightnessChangedThisLoop || updateLightsRequiredThisLoop) && ((!firstConnectAttempt && millis()-firstConnectAttemptAt>1000) || currentColor!=0)){ //!firstConnectAttempt means we've attempted broker connection already: only start setting the color if we've already connected to the broker (this prevents it from always flashing red on boot before syncing up). Give it 1 second to receive remote color, or as soon as the color iesn't the default (must've beenr receicved early)
    for(int i=0;i<12;i++){
      lights.setPixelColor(i, lights.Color(stripColors[i][0]*(brighnessFactor/4096.0),stripColors[i][1]*(brighnessFactor/4096.0),stripColors[i][2]*(brighnessFactor/4096.0)));
      lights.show();
    }
  }
  /*Serial.println(stripColors[0][0]);
  Serial.println(stripColors[0][1]);
  Serial.println(stripColors[0][2]);
  Serial.println(brighnessFactor);delay(1);*/
  
}

void resolveColorConflict(){ //if two users are changing the color simultaniously, their MQTT messages cross in the air and the hearts wind up different colors. This resolves that.
  //Plan: Every time we send a message, start a timer. If we receive a different color within 1 second... If received value is lower, ignore it. If higher, change to that color. If no more values received within 2 seconds, send that color back to group to let others know this was the resolved color.

  if(millis()-lastSentColorAt>2000 && millis()-lastReceivedColorAt>2000 && waitingToSendConflictResolution){

    itoa(currentColor, sendVal,10);
    strcat(sendVal,",");
    strcat(sendVal,WiFi.macAddress().c_str());
    strcat(sendVal,",");
    strcat(sendVal,clientName);
    client.publish(groupTopic,sendVal,true);
    waitingToSendConflictResolution=false;
    Serial.println("sent resolution");

    lastSentColorAt=millis(); //this (hopefully) ensures that if both clients sent conflicting resolutions at the same time, that will be resolved too.
  }
  
}


//updating this function to use coefficients from the new v3.1 experiment. Same ehhh method of generating the curve fit though.

//Give the current brightness b and color c that the heart is set to, and it will return the threshold of a "dark" room.
//The parameters (constants of the polynomial equation) are obtained by by running the PhotoResistor Calibration experiment and analysis
double calculateBrightnessThresholdv3_0(double b, double c) {
    // The 66 optimized coefficients from your program's output
    // Note: The order must match the nested loops below
    const double params[66] = {
        1.31260566e+03, 1.39699585e+01, -4.20581546e-01, 5.01252483e-03,
        -3.08280756e-05, 1.10143121e-07, -2.41435488e-10, 3.29285435e-13,
        -2.72378638e-16, 1.25049204e-19, -2.44413125e-23, -5.54570392e+00,
        -3.03356962e-02, 2.59819500e-03, -2.65535455e-05, 1.13793417e-07,
        -2.51375966e-10, 3.03049590e-13, -1.92876409e-16, 5.45759984e-20,
        -3.18204103e-24, 8.25842200e-01, -1.52211431e-03, -1.91011028e-07,
        4.35345433e-08, -2.55831872e-10, 6.50123755e-13, -8.19414948e-16,
        5.05988589e-19, -1.22590347e-22, -1.55226197e-02, 2.58293808e-05,
        -1.21797977e-08, 6.23759178e-11, -3.21706550e-13, 3.54992266e-16,
        -1.08815972e-19, -4.38295556e-24, 1.09740655e-04, -3.13036441e-07,
        3.95177732e-11, 8.56445926e-13, -2.24146956e-16, -4.53832488e-19,
        1.66168603e-22, 2.72089061e-07, 2.18322148e-09, -2.28287647e-12,
        -2.99955502e-15, 2.48815017e-18, -1.12114511e-22, -1.04106868e-08,
        -7.27187228e-12, 1.34277984e-14, 8.79370785e-19, -2.73002363e-21,
        7.92046912e-11, 7.88794501e-15, -2.76874625e-17, 4.97463965e-21,
        -3.00462844e-13, 1.01706579e-17, 1.70705313e-20, 5.88290581e-16,
        -2.05928088e-20, -4.74723407e-19
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






void rainbowEffect() {
  for (int i = 0; i < 12; i++) { // For each pixel in strip...
    int pixelHue = rainbowEffectFirstPixelHue + (i * 65536L / 12);
    //strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    uint32_t tempPixelHue = lights.gamma32(lights.ColorHSV(pixelHue));
    stripUpdateHSV(i, tempPixelHue); //store current color in stripCopy even though we're not using stripUpdate()
  }
  lights.show(); // Update strip with new contents
  delay(11);  // Pause for a moment

  rainbowEffectFirstPixelHue += 256; //emulating for loop
  if (rainbowEffectFirstPixelHue >= 5 * 65536) { //emulating for loop
    rainbowEffectFirstPixelHue = 0;
  }
}

void stripUpdateHSV(int pixel, uint32_t c) {
  int r;
  int g;
  int b;
  r = (uint8_t)(c >> 16),
  g = (uint8_t)(c >>  8),
  b = (uint8_t)c;
  lights.setPixelColor(pixel, lights.Color(r,g,b));
}




///NEW BRIGHTNESS THRESHOLD CALCS! These are based on a series of 2D curve fits instead of a surface


// Array of all fitted data points: {color, a0, a1, a2, a3}
const PolyFitData FITTED_DATA[257] = {
    { 0   , /* a0 */ +8.734685818631e+02, /* a1 */ +1.788591701406e+01, /* a2 */ -6.929107216000e-02, /* a3 */ +1.056579485336e-04 },
    { 4   , /* a0 */ +8.695005325759e+02, /* a1 */ +1.797008779529e+01, /* a2 */ -6.982925213512e-02, /* a3 */ +1.068554568874e-04 },
    { 8   , /* a0 */ +8.732620162436e+02, /* a1 */ +1.792101248618e+01, /* a2 */ -6.948255782224e-02, /* a3 */ +1.061254902839e-04 },
    { 12  , /* a0 */ +8.728252570703e+02, /* a1 */ +1.796423333077e+01, /* a2 */ -6.983763181504e-02, /* a3 */ +1.071388222273e-04 },
    { 16  , /* a0 */ +8.752527533491e+02, /* a1 */ +1.796351751089e+01, /* a2 */ -6.988774515907e-02, /* a3 */ +1.073098789817e-04 },
    { 20  , /* a0 */ +8.730255102999e+02, /* a1 */ +1.798995389325e+01, /* a2 */ -6.983052252909e-02, /* a3 */ +1.070907771812e-04 },
    { 24  , /* a0 */ +8.695655093952e+02, /* a1 */ +1.799330092169e+01, /* a2 */ -6.936146157356e-02, /* a3 */ +1.056134522163e-04 },
    { 28  , /* a0 */ +8.696581204168e+02, /* a1 */ +1.803730924692e+01, /* a2 */ -6.950039221088e-02, /* a3 */ +1.056052860332e-04 },
    { 32  , /* a0 */ +8.709403293094e+02, /* a1 */ +1.802076905974e+01, /* a2 */ -6.909427783179e-02, /* a3 */ +1.045579163484e-04 },
    { 36  , /* a0 */ +8.686316742684e+02, /* a1 */ +1.808513619167e+01, /* a2 */ -6.932565210932e-02, /* a3 */ +1.047652201658e-04 },
    { 40  , /* a0 */ +8.658197708558e+02, /* a1 */ +1.816855011634e+01, /* a2 */ -6.957981128175e-02, /* a3 */ +1.048755686434e-04 },
    { 44  , /* a0 */ +8.634822158912e+02, /* a1 */ +1.820183229684e+01, /* a2 */ -6.946741259690e-02, /* a3 */ +1.041878983694e-04 },
    { 48  , /* a0 */ +8.581943360042e+02, /* a1 */ +1.834317075747e+01, /* a2 */ -7.034999785563e-02, /* a3 */ +1.059840488939e-04 },
    { 52  , /* a0 */ +8.567595287557e+02, /* a1 */ +1.837083646858e+01, /* a2 */ -7.012763515281e-02, /* a3 */ +1.049801376026e-04 },
    { 56  , /* a0 */ +8.542565419779e+02, /* a1 */ +1.852774726959e+01, /* a2 */ -7.124742682788e-02, /* a3 */ +1.072586100247e-04 },
    { 60  , /* a0 */ +8.519535242185e+02, /* a1 */ +1.862622485743e+01, /* a2 */ -7.183667799258e-02, /* a3 */ +1.084210025778e-04 },
    { 64  , /* a0 */ +8.518657627101e+02, /* a1 */ +1.866642695612e+01, /* a2 */ -7.186812097953e-02, /* a3 */ +1.080872400622e-04 },
    { 68  , /* a0 */ +8.502162232517e+02, /* a1 */ +1.874279775465e+01, /* a2 */ -7.225764811867e-02, /* a3 */ +1.087761956070e-04 },
    { 72  , /* a0 */ +8.473126634428e+02, /* a1 */ +1.886217185141e+01, /* a2 */ -7.296070137672e-02, /* a3 */ +1.099776979141e-04 },
    { 76  , /* a0 */ +8.446124456329e+02, /* a1 */ +1.900962353440e+01, /* a2 */ -7.410157226474e-02, /* a3 */ +1.126615813312e-04 },
    { 80  , /* a0 */ +8.398403351985e+02, /* a1 */ +1.913391715470e+01, /* a2 */ -7.473964813770e-02, /* a3 */ +1.135899341944e-04 },
    { 84  , /* a0 */ +8.372270915553e+02, /* a1 */ +1.921926657085e+01, /* a2 */ -7.509034977689e-02, /* a3 */ +1.140179751024e-04 },
    { 88  , /* a0 */ +8.347956321095e+02, /* a1 */ +1.934833735524e+01, /* a2 */ -7.595683112468e-02, /* a3 */ +1.157976607201e-04 },
    { 92  , /* a0 */ +8.546555164279e+02, /* a1 */ +1.914735250819e+01, /* a2 */ -7.483352074942e-02, /* a3 */ +1.136593182824e-04 },
    { 96  , /* a0 */ +8.528148577579e+02, /* a1 */ +1.929264990609e+01, /* a2 */ -7.592247728781e-02, /* a3 */ +1.160036279306e-04 },
    { 100 , /* a0 */ +8.452746730291e+02, /* a1 */ +1.944924927441e+01, /* a2 */ -7.675895997220e-02, /* a3 */ +1.174191049558e-04 },
    { 104 , /* a0 */ +8.483499399145e+02, /* a1 */ +1.949658657501e+01, /* a2 */ -7.722940509996e-02, /* a3 */ +1.185750515539e-04 },
    { 108 , /* a0 */ +8.535082088380e+02, /* a1 */ +1.944519192096e+01, /* a2 */ -7.683476367195e-02, /* a3 */ +1.175715108164e-04 },
    { 112 , /* a0 */ +8.481449545262e+02, /* a1 */ +1.963002237850e+01, /* a2 */ -7.820102459512e-02, /* a3 */ +1.206680772290e-04 },
    { 116 , /* a0 */ +8.449628690478e+02, /* a1 */ +1.974646915395e+01, /* a2 */ -7.898806572607e-02, /* a3 */ +1.223564908865e-04 },
    { 120 , /* a0 */ +8.364595391682e+02, /* a1 */ +1.992235853391e+01, /* a2 */ -7.994785052302e-02, /* a3 */ +1.240566371902e-04 },
    { 124 , /* a0 */ +8.384451186518e+02, /* a1 */ +1.995710711338e+01, /* a2 */ -8.025330551741e-02, /* a3 */ +1.247815056462e-04 },
    { 128 , /* a0 */ +8.420427131840e+02, /* a1 */ +1.999488721443e+01, /* a2 */ -8.068832547410e-02, /* a3 */ +1.258250727216e-04 },
    { 132 , /* a0 */ +8.407125127170e+02, /* a1 */ +2.007025626724e+01, /* a2 */ -8.105899445861e-02, /* a3 */ +1.263537567726e-04 },
    { 136 , /* a0 */ +8.315353625271e+02, /* a1 */ +2.024784089462e+01, /* a2 */ -8.210534218069e-02, /* a3 */ +1.284794527805e-04 },
    { 140 , /* a0 */ +8.325939480090e+02, /* a1 */ +2.031996030531e+01, /* a2 */ -8.269770836895e-02, /* a3 */ +1.296830962721e-04 },
    { 144 , /* a0 */ +8.295988572662e+02, /* a1 */ +2.044864700012e+01, /* a2 */ -8.364526265230e-02, /* a3 */ +1.317758008629e-04 },
    { 148 , /* a0 */ +8.263579230052e+02, /* a1 */ +2.054147364747e+01, /* a2 */ -8.406985927412e-02, /* a3 */ +1.322511531767e-04 },
    { 152 , /* a0 */ +8.237075198180e+02, /* a1 */ +2.063308710391e+01, /* a2 */ -8.473246889910e-02, /* a3 */ +1.338823615502e-04 },
    { 156 , /* a0 */ +8.293047878914e+02, /* a1 */ +2.063678574887e+01, /* a2 */ -8.491309855063e-02, /* a3 */ +1.343357037184e-04 },
    { 160 , /* a0 */ +8.242929517336e+02, /* a1 */ +2.077209488362e+01, /* a2 */ -8.576481127382e-02, /* a3 */ +1.359982010152e-04 },
    { 164 , /* a0 */ +8.249061492885e+02, /* a1 */ +2.082795092022e+01, /* a2 */ -8.611026912821e-02, /* a3 */ +1.366513416541e-04 },
    { 168 , /* a0 */ +8.189037108762e+02, /* a1 */ +2.096887547308e+01, /* a2 */ -8.693468601126e-02, /* a3 */ +1.381891126148e-04 },
    { 172 , /* a0 */ +8.159733443206e+02, /* a1 */ +2.081675508941e+01, /* a2 */ -8.563702127088e-02, /* a3 */ +1.353071388210e-04 },
    { 176 , /* a0 */ +8.154422547761e+02, /* a1 */ +2.047929353333e+01, /* a2 */ -8.316956765140e-02, /* a3 */ +1.300071898552e-04 },
    { 180 , /* a0 */ +8.173316744391e+02, /* a1 */ +2.012318738538e+01, /* a2 */ -8.068526100693e-02, /* a3 */ +1.249109959780e-04 },
    { 184 , /* a0 */ +8.174330494920e+02, /* a1 */ +1.968983945669e+01, /* a2 */ -7.737334527558e-02, /* a3 */ +1.177228992876e-04 },
    { 188 , /* a0 */ +8.134861415047e+02, /* a1 */ +1.945528524705e+01, /* a2 */ -7.566521321958e-02, /* a3 */ +1.141329550350e-04 },
    { 192 , /* a0 */ +8.120011398319e+02, /* a1 */ +1.912160720063e+01, /* a2 */ -7.325700131008e-02, /* a3 */ +1.090379418896e-04 },
    { 196 , /* a0 */ +8.171938921909e+02, /* a1 */ +1.872743657985e+01, /* a2 */ -7.061358557694e-02, /* a3 */ +1.036054252225e-04 },
    { 200 , /* a0 */ +8.182976028110e+02, /* a1 */ +1.824367075273e+01, /* a2 */ -6.687464503885e-02, /* a3 */ +9.529380865322e-05 },
    { 204 , /* a0 */ +8.119355886158e+02, /* a1 */ +1.805549663042e+01, /* a2 */ -6.570144642795e-02, /* a3 */ +9.328277522720e-05 },
    { 208 , /* a0 */ +8.147154935203e+02, /* a1 */ +1.756517932408e+01, /* a2 */ -6.193257536381e-02, /* a3 */ +8.479752949517e-05 },
    { 212 , /* a0 */ +8.230299228277e+02, /* a1 */ +1.713811810655e+01, /* a2 */ -5.942849224533e-02, /* a3 */ +8.027727320492e-05 },
    { 216 , /* a0 */ +8.273259419936e+02, /* a1 */ +1.661759152773e+01, /* a2 */ -5.553630272852e-02, /* a3 */ +7.172434027829e-05 },
    { 220 , /* a0 */ +8.280043327268e+02, /* a1 */ +1.623277354340e+01, /* a2 */ -5.284195660875e-02, /* a3 */ +6.600622094806e-05 },
    { 224 , /* a0 */ +8.295066695742e+02, /* a1 */ +1.572009253117e+01, /* a2 */ -4.897365451636e-02, /* a3 */ +5.750828585048e-05 },
    { 228 , /* a0 */ +8.303642921520e+02, /* a1 */ +1.529662828301e+01, /* a2 */ -4.608290770285e-02, /* a3 */ +5.164737918946e-05 },
    { 232 , /* a0 */ +8.383054762287e+02, /* a1 */ +1.472982236667e+01, /* a2 */ -4.204365584566e-02, /* a3 */ +4.269204730037e-05 },
    { 236 , /* a0 */ +8.427609199395e+02, /* a1 */ +1.422255628099e+01, /* a2 */ -3.868021661582e-02, /* a3 */ +3.609978329256e-05 },
    { 240 , /* a0 */ +8.471212574595e+02, /* a1 */ +1.382534559848e+01, /* a2 */ -3.636294115553e-02, /* a3 */ +3.188816560084e-05 },
    { 244 , /* a0 */ +8.474142676569e+02, /* a1 */ +1.331804481265e+01, /* a2 */ -3.294278776131e-02, /* a3 */ +2.500002516405e-05 },
    { 248 , /* a0 */ +8.450926225437e+02, /* a1 */ +1.286225369424e+01, /* a2 */ -2.980231133373e-02, /* a3 */ +1.860993602453e-05 },
    { 252 , /* a0 */ +8.569215560092e+02, /* a1 */ +1.217863842238e+01, /* a2 */ -2.528609883393e-02, /* a3 */ +9.569707192995e-06 },
    { 256 , /* a0 */ +8.611891741148e+02, /* a1 */ +1.171546181737e+01, /* a2 */ -2.257653374156e-02, /* a3 */ +4.455768738540e-06 },
    { 260 , /* a0 */ +8.641326024355e+02, /* a1 */ +1.118180907293e+01, /* a2 */ -1.916058666781e-02, /* a3 */ -2.181199731408e-06 },
    { 264 , /* a0 */ +8.676452669025e+02, /* a1 */ +1.065022751422e+01, /* a2 */ -1.592516784800e-02, /* a3 */ -8.268776076455e-06 },
    { 268 , /* a0 */ +8.725278121543e+02, /* a1 */ +1.008579249142e+01, /* a2 */ -1.256113845045e-02, /* a3 */ -1.432361301835e-05 },
    { 272 , /* a0 */ +8.899965600206e+02, /* a1 */ +9.526712888983e+00, /* a2 */ -1.008793044204e-02, /* a3 */ -1.782277813781e-05 },
    { 276 , /* a0 */ +8.933689009819e+02, /* a1 */ +8.902476008587e+00, /* a2 */ -6.114738006332e-03, /* a3 */ -2.543289540474e-05 },
    { 280 , /* a0 */ +9.053945862466e+02, /* a1 */ +8.257122510654e+00, /* a2 */ -2.620948362933e-03, /* a3 */ -3.147166132818e-05 },
    { 284 , /* a0 */ +9.081687872974e+02, /* a1 */ +7.838470691656e+00, /* a2 */ -1.137184985018e-03, /* a3 */ -3.250077774097e-05 },
    { 288 , /* a0 */ +9.167016132611e+02, /* a1 */ +7.211335088861e+00, /* a2 */ +2.184305869681e-03, /* a3 */ -3.815717607186e-05 },
    { 292 , /* a0 */ +9.244280570198e+02, /* a1 */ +6.759200095397e+00, /* a2 */ +3.614730263506e-03, /* a3 */ -3.882146132459e-05 },
    { 296 , /* a0 */ +9.319982695551e+02, /* a1 */ +6.328975079827e+00, /* a2 */ +4.688144517097e-03, /* a3 */ -3.849346899347e-05 },
    { 300 , /* a0 */ +9.386593443171e+02, /* a1 */ +5.900756715950e+00, /* a2 */ +5.616532552048e-03, /* a3 */ -3.792228140150e-05 },
    { 304 , /* a0 */ +9.344140995200e+02, /* a1 */ +5.642833517111e+00, /* a2 */ +5.289044332159e-03, /* a3 */ -3.430987524520e-05 },
    { 308 , /* a0 */ +9.445553610079e+02, /* a1 */ +5.429714007818e+00, /* a2 */ +3.750088730487e-03, /* a3 */ -2.745420726353e-05 },
    { 312 , /* a0 */ +9.527652684558e+02, /* a1 */ +5.336683508369e+00, /* a2 */ +1.312429761571e-03, /* a3 */ -1.935045547592e-05 },
    { 316 , /* a0 */ +9.518431781637e+02, /* a1 */ +5.422559920260e+00, /* a2 */ -2.327023225959e-03, /* a3 */ -8.876351672525e-06 },
    { 320 , /* a0 */ +9.489236910070e+02, /* a1 */ +5.487240529741e+00, /* a2 */ -4.831188281753e-03, /* a3 */ -2.750591491137e-06 },
    { 324 , /* a0 */ +9.573654938617e+02, /* a1 */ +5.272999233004e+00, /* a2 */ -4.500058401296e-03, /* a3 */ -4.740938311160e-06 },
    { 328 , /* a0 */ +9.570161603497e+02, /* a1 */ +5.139324501130e+00, /* a2 */ -4.416055105145e-03, /* a3 */ -5.165549963670e-06 },
    { 332 , /* a0 */ +9.544057259590e+02, /* a1 */ +5.194557543739e+00, /* a2 */ -6.071475210797e-03, /* a3 */ -1.139072868729e-06 },
    { 336 , /* a0 */ +9.596456492646e+02, /* a1 */ +4.987813465089e+00, /* a2 */ -4.894324443830e-03, /* a3 */ -4.187156477827e-06 },
    { 340 , /* a0 */ +9.647275925521e+02, /* a1 */ +4.986566318862e+00, /* a2 */ -5.665070328507e-03, /* a3 */ -1.892507564778e-06 },
    { 344 , /* a0 */ +9.652073892345e+02, /* a1 */ +4.961942952900e+00, /* a2 */ -5.206897937478e-03, /* a3 */ -3.166463862324e-06 },
    { 348 , /* a0 */ +9.657580082686e+02, /* a1 */ +5.020555258530e+00, /* a2 */ -5.414919975266e-03, /* a3 */ -2.639341363944e-06 },
    { 352 , /* a0 */ +9.665602096676e+02, /* a1 */ +5.005103849570e+00, /* a2 */ -4.601063561041e-03, /* a3 */ -4.986695713122e-06 },
    { 356 , /* a0 */ +9.626702207288e+02, /* a1 */ +5.040491026714e+00, /* a2 */ -4.189294156095e-03, /* a3 */ -6.296532253470e-06 },
    { 360 , /* a0 */ +9.628466002199e+02, /* a1 */ +5.186352747015e+00, /* a2 */ -5.225815866598e-03, /* a3 */ -2.953687610160e-06 },
    { 364 , /* a0 */ +9.654133578398e+02, /* a1 */ +5.169035117928e+00, /* a2 */ -4.233531718116e-03, /* a3 */ -5.449788018320e-06 },
    { 368 , /* a0 */ +9.632096053135e+02, /* a1 */ +5.195234818627e+00, /* a2 */ -3.141995027215e-03, /* a3 */ -8.856134559051e-06 },
    { 372 , /* a0 */ +9.620181528151e+02, /* a1 */ +5.165031286649e+00, /* a2 */ -1.642067257434e-03, /* a3 */ -1.310688533939e-05 },
    { 376 , /* a0 */ +9.570390291073e+02, /* a1 */ +5.253043936039e+00, /* a2 */ -1.004856596921e-03, /* a3 */ -1.578486043111e-05 },
    { 380 , /* a0 */ +9.586564381939e+02, /* a1 */ +5.318061602561e+00, /* a2 */ -3.020868056846e-04, /* a3 */ -1.869314820751e-05 },
    { 384 , /* a0 */ +9.510204930765e+02, /* a1 */ +5.597241559898e+00, /* a2 */ -1.275792243729e-03, /* a3 */ -1.765078239833e-05 },
    { 388 , /* a0 */ +9.482690212382e+02, /* a1 */ +5.759526253938e+00, /* a2 */ -1.464229917292e-03, /* a3 */ -1.845711760792e-05 },
    { 392 , /* a0 */ +9.455690078384e+02, /* a1 */ +5.966586295023e+00, /* a2 */ -2.088877589888e-03, /* a3 */ -1.805815070115e-05 },
    { 396 , /* a0 */ +9.421893119529e+02, /* a1 */ +6.202532977895e+00, /* a2 */ -2.997853965325e-03, /* a3 */ -1.685094541331e-05 },
    { 400 , /* a0 */ +9.412564937559e+02, /* a1 */ +6.418234293761e+00, /* a2 */ -3.896410025255e-03, /* a3 */ -1.563217642191e-05 },
    { 404 , /* a0 */ +9.386372304688e+02, /* a1 */ +6.662491692797e+00, /* a2 */ -5.134314909507e-03, /* a3 */ -1.341162985678e-05 },
    { 408 , /* a0 */ +9.323798396981e+02, /* a1 */ +7.010945017984e+00, /* a2 */ -7.011905640193e-03, /* a3 */ -1.013594597907e-05 },
    { 412 , /* a0 */ +9.277584674446e+02, /* a1 */ +7.225374669492e+00, /* a2 */ -7.767908468884e-03, /* a3 */ -9.261805968410e-06 },
    { 416 , /* a0 */ +9.268600597100e+02, /* a1 */ +7.463277783974e+00, /* a2 */ -9.061739003688e-03, /* a3 */ -6.917180836212e-06 },
    { 420 , /* a0 */ +9.221254971562e+02, /* a1 */ +7.697397795137e+00, /* a2 */ -1.010348586357e-02, /* a3 */ -5.526383436876e-06 },
    { 424 , /* a0 */ +9.109864372380e+02, /* a1 */ +8.086770297348e+00, /* a2 */ -1.228764182259e-02, /* a3 */ -1.427883575221e-06 },
    { 428 , /* a0 */ +9.107683529578e+02, /* a1 */ +8.287550864051e+00, /* a2 */ -1.329591226274e-02, /* a3 */ +2.454867203871e-07 },
    { 432 , /* a0 */ +9.110204678986e+02, /* a1 */ +8.512774963973e+00, /* a2 */ -1.452049305395e-02, /* a3 */ +2.418820818588e-06 },
    { 436 , /* a0 */ +9.134147571317e+02, /* a1 */ +8.696241646300e+00, /* a2 */ -1.551804952481e-02, /* a3 */ +4.231188902950e-06 },
    { 440 , /* a0 */ +9.116694410684e+02, /* a1 */ +8.900287551522e+00, /* a2 */ -1.637590240858e-02, /* a3 */ +5.209635591723e-06 },
    { 444 , /* a0 */ +9.048789529251e+02, /* a1 */ +9.249943762673e+00, /* a2 */ -1.874904696751e-02, /* a3 */ +1.037392268584e-05 },
    { 448 , /* a0 */ +9.009888666887e+02, /* a1 */ +9.489028816174e+00, /* a2 */ -1.998188922493e-02, /* a3 */ +1.240669090892e-05 },
    { 452 , /* a0 */ +8.963465579723e+02, /* a1 */ +9.728459190388e+00, /* a2 */ -2.119716812624e-02, /* a3 */ +1.446620247159e-05 },
    { 456 , /* a0 */ +8.907151256674e+02, /* a1 */ +1.001817944713e+01, /* a2 */ -2.292504141751e-02, /* a3 */ +1.777006896814e-05 },
    { 460 , /* a0 */ +8.947850975194e+02, /* a1 */ +1.017410082752e+01, /* a2 */ -2.376722855928e-02, /* a3 */ +1.921522482798e-05 },
    { 464 , /* a0 */ +8.955372090463e+02, /* a1 */ +1.038118605107e+01, /* a2 */ -2.510001884862e-02, /* a3 */ +2.188221584809e-05 },
    { 468 , /* a0 */ +8.942201531326e+02, /* a1 */ +1.059614883243e+01, /* a2 */ -2.641771730197e-02, /* a3 */ +2.449261295123e-05 },
    { 472 , /* a0 */ +8.853141689938e+02, /* a1 */ +1.091050263398e+01, /* a2 */ -2.825459242541e-02, /* a3 */ +2.801204278679e-05 },
    { 476 , /* a0 */ +8.870988192861e+02, /* a1 */ +1.111421411826e+01, /* a2 */ -2.961451508749e-02, /* a3 */ +3.077110194429e-05 },
    { 480 , /* a0 */ +8.837878457476e+02, /* a1 */ +1.133534242503e+01, /* a2 */ -3.082412927761e-02, /* a3 */ +3.298662223259e-05 },
    { 484 , /* a0 */ +8.869738218465e+02, /* a1 */ +1.146483979537e+01, /* a2 */ -3.160731597548e-02, /* a3 */ +3.441598148909e-05 },
    { 488 , /* a0 */ +8.789928383905e+02, /* a1 */ +1.177454803731e+01, /* a2 */ -3.346488282893e-02, /* a3 */ +3.795868433976e-05 },
    { 492 , /* a0 */ +8.814623974962e+02, /* a1 */ +1.198278522268e+01, /* a2 */ -3.486232346215e-02, /* a3 */ +4.090400788926e-05 },
    { 496 , /* a0 */ +8.850458561499e+02, /* a1 */ +1.206490681082e+01, /* a2 */ -3.519203920072e-02, /* a3 */ +4.120962792154e-05 },
    { 500 , /* a0 */ +8.800139805439e+02, /* a1 */ +1.235517943505e+01, /* a2 */ -3.703497612522e-02, /* a3 */ +4.481293357663e-05 },
    { 504 , /* a0 */ +8.734510188074e+02, /* a1 */ +1.258976078723e+01, /* a2 */ -3.830174388792e-02, /* a3 */ +4.716753565831e-05 },
    { 508 , /* a0 */ +8.685039175054e+02, /* a1 */ +1.287386563707e+01, /* a2 */ -4.024046255008e-02, /* a3 */ +5.124785131846e-05 },
    { 512 , /* a0 */ +8.752577104016e+02, /* a1 */ +1.285975543387e+01, /* a2 */ -4.035627416290e-02, /* a3 */ +5.166967028383e-05 },
    { 516 , /* a0 */ +8.752389166177e+02, /* a1 */ +1.273242201699e+01, /* a2 */ -3.938377883790e-02, /* a3 */ +4.952560153077e-05 },
    { 520 , /* a0 */ +8.719403882000e+02, /* a1 */ +1.263636495955e+01, /* a2 */ -3.850564422190e-02, /* a3 */ +4.740451271002e-05 },
    { 524 , /* a0 */ +8.725442422060e+02, /* a1 */ +1.253956731658e+01, /* a2 */ -3.810262189631e-02, /* a3 */ +4.701566649399e-05 },
    { 528 , /* a0 */ +8.704572996183e+02, /* a1 */ +1.247350551016e+01, /* a2 */ -3.762684248596e-02, /* a3 */ +4.594512646643e-05 },
    { 532 , /* a0 */ +8.824545505367e+02, /* a1 */ +1.225713764641e+01, /* a2 */ -3.637689495125e-02, /* a3 */ +4.348800233826e-05 },
    { 536 , /* a0 */ +8.751248634421e+02, /* a1 */ +1.227418185082e+01, /* a2 */ -3.659295597659e-02, /* a3 */ +4.422822403992e-05 },
    { 540 , /* a0 */ +8.785796717318e+02, /* a1 */ +1.213666654688e+01, /* a2 */ -3.569823139248e-02, /* a3 */ +4.236573039486e-05 },
    { 544 , /* a0 */ +8.803409095952e+02, /* a1 */ +1.201761716123e+01, /* a2 */ -3.500943802461e-02, /* a3 */ +4.108163100534e-05 },
    { 548 , /* a0 */ +8.844794821724e+02, /* a1 */ +1.190141417810e+01, /* a2 */ -3.442230867757e-02, /* a3 */ +4.012179974182e-05 },
    { 552 , /* a0 */ +8.861376439833e+02, /* a1 */ +1.177631157020e+01, /* a2 */ -3.359429503242e-02, /* a3 */ +3.839278758169e-05 },
    { 556 , /* a0 */ +8.864270153389e+02, /* a1 */ +1.169540239661e+01, /* a2 */ -3.312655150305e-02, /* a3 */ +3.752923868768e-05 },
    { 560 , /* a0 */ +8.848095021269e+02, /* a1 */ +1.160794994100e+01, /* a2 */ -3.245496212910e-02, /* a3 */ +3.603773560856e-05 },
    { 564 , /* a0 */ +8.917344844597e+02, /* a1 */ +1.140861146597e+01, /* a2 */ -3.125093011603e-02, /* a3 */ +3.370229031528e-05 },
    { 568 , /* a0 */ +8.940318214096e+02, /* a1 */ +1.133790252021e+01, /* a2 */ -3.105086974233e-02, /* a3 */ +3.362758033390e-05 },
    { 572 , /* a0 */ +8.950431616914e+02, /* a1 */ +1.125062942145e+01, /* a2 */ -3.049527022142e-02, /* a3 */ +3.246176599236e-05 },
    { 576 , /* a0 */ +8.896673798973e+02, /* a1 */ +1.121560727846e+01, /* a2 */ -3.019047450654e-02, /* a3 */ +3.185839809470e-05 },
    { 580 , /* a0 */ +8.987038331808e+02, /* a1 */ +1.100796654385e+01, /* a2 */ -2.900159886902e-02, /* a3 */ +2.960919101773e-05 },
    { 584 , /* a0 */ +8.978545333816e+02, /* a1 */ +1.092761702359e+01, /* a2 */ -2.858847493107e-02, /* a3 */ +2.891705543112e-05 },
    { 588 , /* a0 */ +9.029901072321e+02, /* a1 */ +1.076894818466e+01, /* a2 */ -2.767849170338e-02, /* a3 */ +2.717105100179e-05 },
    { 592 , /* a0 */ +8.991623131717e+02, /* a1 */ +1.073896136291e+01, /* a2 */ -2.754249419880e-02, /* a3 */ +2.699620563870e-05 },
    { 596 , /* a0 */ +9.008251644669e+02, /* a1 */ +1.062288071110e+01, /* a2 */ -2.682704887905e-02, /* a3 */ +2.551850825328e-05 },
    { 600 , /* a0 */ +9.048559296863e+02, /* a1 */ +1.050111021836e+01, /* a2 */ -2.624686022467e-02, /* a3 */ +2.471474731880e-05 },
    { 604 , /* a0 */ +9.074498136838e+02, /* a1 */ +1.039916338298e+01, /* a2 */ -2.577208212947e-02, /* a3 */ +2.403391875493e-05 },
    { 608 , /* a0 */ +9.115662980172e+02, /* a1 */ +1.027503810882e+01, /* a2 */ -2.521264341951e-02, /* a3 */ +2.320300659174e-05 },
    { 612 , /* a0 */ +9.163756064025e+02, /* a1 */ +1.013821653734e+01, /* a2 */ -2.454761812309e-02, /* a3 */ +2.220543671641e-05 },
    { 616 , /* a0 */ +9.192952258497e+02, /* a1 */ +9.987892246293e+00, /* a2 */ -2.351915250028e-02, /* a3 */ +1.987395177459e-05 },
    { 620 , /* a0 */ +9.216611392003e+02, /* a1 */ +9.899858957551e+00, /* a2 */ -2.322489998848e-02, /* a3 */ +1.967728667643e-05 },
    { 624 , /* a0 */ +9.184498124036e+02, /* a1 */ +9.864952213057e+00, /* a2 */ -2.321039615767e-02, /* a3 */ +2.002810399799e-05 },
    { 628 , /* a0 */ +9.222315954062e+02, /* a1 */ +9.705946048639e+00, /* a2 */ -2.226291982981e-02, /* a3 */ +1.829753121857e-05 },
    { 632 , /* a0 */ +9.259218133356e+02, /* a1 */ +9.720751047075e+00, /* a2 */ -2.320132503167e-02, /* a3 */ +2.130649667320e-05 },
    { 636 , /* a0 */ +9.274057037683e+02, /* a1 */ +9.547901730581e+00, /* a2 */ -2.188560297277e-02, /* a3 */ +1.826756023456e-05 },
    { 640 , /* a0 */ +9.282618391450e+02, /* a1 */ +9.534387150395e+00, /* a2 */ -2.240079486259e-02, /* a3 */ +2.025965248404e-05 },
    { 644 , /* a0 */ +9.255518715263e+02, /* a1 */ +9.500597261222e+00, /* a2 */ -2.224422958523e-02, /* a3 */ +1.999569221288e-05 },
    { 648 , /* a0 */ +9.315608007074e+02, /* a1 */ +9.399130469091e+00, /* a2 */ -2.207600623859e-02, /* a3 */ +2.017622188904e-05 },
    { 652 , /* a0 */ +9.379134363606e+02, /* a1 */ +9.441677986586e+00, /* a2 */ -2.323882864761e-02, /* a3 */ +2.361357547084e-05 },
    { 656 , /* a0 */ +9.373767108147e+02, /* a1 */ +9.443409593539e+00, /* a2 */ -2.355256833509e-02, /* a3 */ +2.453233407224e-05 },
    { 660 , /* a0 */ +9.339718519985e+02, /* a1 */ +9.476557335486e+00, /* a2 */ -2.399494742989e-02, /* a3 */ +2.551337650398e-05 },
    { 664 , /* a0 */ +9.326584020675e+02, /* a1 */ +9.480710229952e+00, /* a2 */ -2.401365264649e-02, /* a3 */ +2.518283603251e-05 },
    { 668 , /* a0 */ +9.384313531866e+02, /* a1 */ +9.369169991384e+00, /* a2 */ -2.346321979414e-02, /* a3 */ +2.410647728449e-05 },
    { 672 , /* a0 */ +9.367589987403e+02, /* a1 */ +9.458390841542e+00, /* a2 */ -2.444127107474e-02, /* a3 */ +2.658237924687e-05 },
    { 676 , /* a0 */ +9.356542114463e+02, /* a1 */ +9.426673518204e+00, /* a2 */ -2.412049985759e-02, /* a3 */ +2.573765765645e-05 },
    { 680 , /* a0 */ +9.408641380976e+02, /* a1 */ +9.362798819500e+00, /* a2 */ -2.375178752725e-02, /* a3 */ +2.493830976502e-05 },
    { 684 , /* a0 */ +9.378151389818e+02, /* a1 */ +9.455388983615e+00, /* a2 */ -2.405451271529e-02, /* a3 */ +2.535781206947e-05 },
    { 688 , /* a0 */ +9.392265892782e+02, /* a1 */ +9.567491443791e+00, /* a2 */ -2.465042766541e-02, /* a3 */ +2.676140430682e-05 },
    { 692 , /* a0 */ +9.375944950942e+02, /* a1 */ +9.606361120918e+00, /* a2 */ -2.416649663250e-02, /* a3 */ +2.554420060164e-05 },
    { 696 , /* a0 */ +9.365976914030e+02, /* a1 */ +9.717044382466e+00, /* a2 */ -2.392633770635e-02, /* a3 */ +2.441015494312e-05 },
    { 700 , /* a0 */ +9.376243257453e+02, /* a1 */ +9.780107700765e+00, /* a2 */ -2.384297776083e-02, /* a3 */ +2.544525621886e-05 },
    { 704 , /* a0 */ +9.340949278121e+02, /* a1 */ +9.824969222596e+00, /* a2 */ -2.236296085085e-02, /* a3 */ +2.108291649267e-05 },
    { 708 , /* a0 */ +9.371186803555e+02, /* a1 */ +9.751422583876e+00, /* a2 */ -1.967518612180e-02, /* a3 */ +1.300900221506e-05 },
    { 712 , /* a0 */ +9.271577367744e+02, /* a1 */ +9.978803796579e+00, /* a2 */ -1.904484984470e-02, /* a3 */ +9.212979679261e-06 },
    { 716 , /* a0 */ +9.289762205718e+02, /* a1 */ +1.010058011715e+01, /* a2 */ -1.800954795978e-02, /* a3 */ +4.514080083419e-06 },
    { 720 , /* a0 */ +9.260185001844e+02, /* a1 */ +1.036563204910e+01, /* a2 */ -1.813495041203e-02, /* a3 */ +2.518784457503e-06 },
    { 724 , /* a0 */ +9.191960442584e+02, /* a1 */ +1.071306540996e+01, /* a2 */ -1.906022662839e-02, /* a3 */ +2.443889665943e-06 },
    { 728 , /* a0 */ +9.095583482125e+02, /* a1 */ +1.118370328917e+01, /* a2 */ -2.094997910410e-02, /* a3 */ +4.368725011199e-06 },
    { 732 , /* a0 */ +9.088335435756e+02, /* a1 */ +1.153882568816e+01, /* a2 */ -2.260957605099e-02, /* a3 */ +6.762667392921e-06 },
    { 736 , /* a0 */ +8.974506031593e+02, /* a1 */ +1.217065527537e+01, /* a2 */ -2.625945747528e-02, /* a3 */ +1.324027815277e-05 },
    { 740 , /* a0 */ +8.973444908267e+02, /* a1 */ +1.250162018761e+01, /* a2 */ -2.778838932888e-02, /* a3 */ +1.521685474772e-05 },
    { 744 , /* a0 */ +8.905692416939e+02, /* a1 */ +1.296151085757e+01, /* a2 */ -3.021472373569e-02, /* a3 */ +1.918168551411e-05 },
    { 748 , /* a0 */ +8.810744799704e+02, /* a1 */ +1.354985617042e+01, /* a2 */ -3.392979853456e-02, /* a3 */ +2.640734814021e-05 },
    { 752 , /* a0 */ +8.739014397472e+02, /* a1 */ +1.407656517933e+01, /* a2 */ -3.725673109136e-02, /* a3 */ +3.292941788202e-05 },
    { 756 , /* a0 */ +8.678403949426e+02, /* a1 */ +1.461876872940e+01, /* a2 */ -4.086722121894e-02, /* a3 */ +4.022043890779e-05 },
    { 760 , /* a0 */ +8.598301274939e+02, /* a1 */ +1.511550654928e+01, /* a2 */ -4.402807513602e-02, /* a3 */ +4.665058145329e-05 },
    { 764 , /* a0 */ +8.515005658619e+02, /* a1 */ +1.566614542366e+01, /* a2 */ -4.756850471856e-02, /* a3 */ +5.354356615688e-05 },
    { 768 , /* a0 */ +8.550431087752e+02, /* a1 */ +1.601015906556e+01, /* a2 */ -5.008001302758e-02, /* a3 */ +5.901147490655e-05 },
    { 772 , /* a0 */ +8.500706529346e+02, /* a1 */ +1.649336236475e+01, /* a2 */ -5.342510197157e-02, /* a3 */ +6.585279676476e-05 },
    { 776 , /* a0 */ +8.456341370051e+02, /* a1 */ +1.694568196491e+01, /* a2 */ -5.648631936109e-02, /* a3 */ +7.216614954464e-05 },
    { 780 , /* a0 */ +8.393787677184e+02, /* a1 */ +1.740099618442e+01, /* a2 */ -5.948925654638e-02, /* a3 */ +7.825826773983e-05 },
    { 784 , /* a0 */ +8.352826808710e+02, /* a1 */ +1.782901386088e+01, /* a2 */ -6.238183282944e-02, /* a3 */ +8.415618123904e-05 },
    { 788 , /* a0 */ +8.334585986255e+02, /* a1 */ +1.826108995620e+01, /* a2 */ -6.565096230750e-02, /* a3 */ +9.135930262367e-05 },
    { 792 , /* a0 */ +8.343091406751e+02, /* a1 */ +1.860971389114e+01, /* a2 */ -6.803081747362e-02, /* a3 */ +9.609460235761e-05 },
    { 796 , /* a0 */ +8.340388882820e+02, /* a1 */ +1.896571605191e+01, /* a2 */ -7.051487052093e-02, /* a3 */ +1.012016496325e-04 },
    { 800 , /* a0 */ +8.311956900613e+02, /* a1 */ +1.936630046942e+01, /* a2 */ -7.347426985073e-02, /* a3 */ +1.075730636102e-04 },
    { 804 , /* a0 */ +8.367117659158e+02, /* a1 */ +1.977685544904e+01, /* a2 */ -7.660858472776e-02, /* a3 */ +1.142951091658e-04 },
    { 808 , /* a0 */ +8.423671620021e+02, /* a1 */ +2.018587218540e+01, /* a2 */ -7.974780957520e-02, /* a3 */ +1.210601837580e-04 },
    { 812 , /* a0 */ +8.481541205499e+02, /* a1 */ +2.059370334907e+01, /* a2 */ -8.286454039097e-02, /* a3 */ +1.277500854099e-04 },
    { 816 , /* a0 */ +8.536159910179e+02, /* a1 */ +2.100197437828e+01, /* a2 */ -8.600406296121e-02, /* a3 */ +1.345316519771e-04 },
    { 820 , /* a0 */ +8.592713871042e+02, /* a1 */ +2.141099111464e+01, /* a2 */ -8.914328780866e-02, /* a3 */ +1.412967265692e-04 },
    { 824 , /* a0 */ +8.647874629587e+02, /* a1 */ +2.182154609426e+01, /* a2 */ -9.227760268568e-02, /* a3 */ +1.480187721249e-04 },
    { 828 , /* a0 */ +8.602585028643e+02, /* a1 */ +2.225481458270e+01, /* a2 */ -9.540737320282e-02, /* a3 */ +1.546821161808e-04 },
    { 832 , /* a0 */ +8.556627424757e+02, /* a1 */ +2.256132506504e+01, /* a2 */ -9.753839130803e-02, /* a3 */ +1.590845149133e-04 },
    { 836 , /* a0 */ +8.474109847192e+02, /* a1 */ +2.290887925196e+01, /* a2 */ -9.986798754610e-02, /* a3 */ +1.639306774198e-04 },
    { 840 , /* a0 */ +8.437642122656e+02, /* a1 */ +2.325497449663e+01, /* a2 */ -1.024997294787e-01, /* a3 */ +1.697498757837e-04 },
    { 844 , /* a0 */ +8.399123345089e+02, /* a1 */ +2.354312619269e+01, /* a2 */ -1.045411553735e-01, /* a3 */ +1.740475160798e-04 },
    { 848 , /* a0 */ +8.304416415457e+02, /* a1 */ +2.398067972135e+01, /* a2 */ -1.077478023143e-01, /* a3 */ +1.810350395902e-04 },
    { 852 , /* a0 */ +8.320755993186e+02, /* a1 */ +2.403128365592e+01, /* a2 */ -1.082345344583e-01, /* a3 */ +1.822201143259e-04 },
    { 856 , /* a0 */ +8.290561824892e+02, /* a1 */ +2.390207922574e+01, /* a2 */ -1.072307066972e-01, /* a3 */ +1.800796540382e-04 },
    { 860 , /* a0 */ +8.250672991574e+02, /* a1 */ +2.376626299527e+01, /* a2 */ -1.059530207684e-01, /* a3 */ +1.769643125915e-04 },
    { 864 , /* a0 */ +8.235495129150e+02, /* a1 */ +2.362401101590e+01, /* a2 */ -1.048820676712e-01, /* a3 */ +1.746713307036e-04 },
    { 868 , /* a0 */ +8.212060936412e+02, /* a1 */ +2.348240826815e+01, /* a2 */ -1.036286663390e-01, /* a3 */ +1.717367924148e-04 },
    { 872 , /* a0 */ +8.204898571604e+02, /* a1 */ +2.332876128885e+01, /* a2 */ -1.024008861788e-01, /* a3 */ +1.689598962749e-04 },
    { 876 , /* a0 */ +8.182566439696e+02, /* a1 */ +2.322712101406e+01, /* a2 */ -1.016731748723e-01, /* a3 */ +1.675125786960e-04 },
    { 880 , /* a0 */ +8.180771577858e+02, /* a1 */ +2.305108695012e+01, /* a2 */ -1.002521567757e-01, /* a3 */ +1.642535837374e-04 },
    { 884 , /* a0 */ +8.158549225717e+02, /* a1 */ +2.293610307267e+01, /* a2 */ -9.940739167440e-02, /* a3 */ +1.625962965943e-04 },
    { 888 , /* a0 */ +8.176491788602e+02, /* a1 */ +2.273050248118e+01, /* a2 */ -9.781950543703e-02, /* a3 */ +1.590645582142e-04 },
    { 892 , /* a0 */ +8.222825959490e+02, /* a1 */ +2.252157054703e+01, /* a2 */ -9.641210964993e-02, /* a3 */ +1.562130855669e-04 },
    { 896 , /* a0 */ +8.197836320822e+02, /* a1 */ +2.242010510133e+01, /* a2 */ -9.575022276315e-02, /* a3 */ +1.549740518594e-04 },
    { 900 , /* a0 */ +8.179584889525e+02, /* a1 */ +2.224519848021e+01, /* a2 */ -9.422850014420e-02, /* a3 */ +1.514085706875e-04 },
    { 904 , /* a0 */ +8.169564324755e+02, /* a1 */ +2.212020804692e+01, /* a2 */ -9.338558941938e-02, /* a3 */ +1.497221610071e-04 },
    { 908 , /* a0 */ +8.168348647735e+02, /* a1 */ +2.189714610705e+01, /* a2 */ -9.163290710173e-02, /* a3 */ +1.459229938981e-04 },
    { 912 , /* a0 */ +8.172511739714e+02, /* a1 */ +2.173566991913e+01, /* a2 */ -9.030905105165e-02, /* a3 */ +1.426310663258e-04 },
    { 916 , /* a0 */ +8.156332148124e+02, /* a1 */ +2.161419544328e+01, /* a2 */ -8.961478403351e-02, /* a3 */ +1.417156091062e-04 },
    { 920 , /* a0 */ +8.180491075086e+02, /* a1 */ +2.139802816141e+01, /* a2 */ -8.810906118856e-02, /* a3 */ +1.386763123811e-04 },
    { 924 , /* a0 */ +8.158668278619e+02, /* a1 */ +2.123654738876e+01, /* a2 */ -8.664404638804e-02, /* a3 */ +1.350979196981e-04 },
    { 928 , /* a0 */ +8.218724536215e+02, /* a1 */ +2.104813277604e+01, /* a2 */ -8.565287976110e-02, /* a3 */ +1.334556563157e-04 },
    { 932 , /* a0 */ +8.231584784546e+02, /* a1 */ +2.081378951122e+01, /* a2 */ -8.376107450114e-02, /* a3 */ +1.291546082319e-04 },
    { 936 , /* a0 */ +8.253441191331e+02, /* a1 */ +2.068752787026e+01, /* a2 */ -8.321387076983e-02, /* a3 */ +1.284915027774e-04 },
    { 940 , /* a0 */ +8.261690514516e+02, /* a1 */ +2.048614990373e+01, /* a2 */ -8.180280181242e-02, /* a3 */ +1.255786087474e-04 },
    { 944 , /* a0 */ +8.283229509484e+02, /* a1 */ +2.032319980221e+01, /* a2 */ -8.099881520979e-02, /* a3 */ +1.244175910700e-04 },
    { 948 , /* a0 */ +8.304321285795e+02, /* a1 */ +2.001666874292e+01, /* a2 */ -7.844784111515e-02, /* a3 */ +1.184864948099e-04 },
    { 952 , /* a0 */ +8.321769577285e+02, /* a1 */ +1.985593481289e+01, /* a2 */ -7.751606236065e-02, /* a3 */ +1.169322598758e-04 },
    { 956 , /* a0 */ +8.357519083969e+02, /* a1 */ +1.960540861115e+01, /* a2 */ -7.575127356822e-02, /* a3 */ +1.131574027723e-04 },
    { 960 , /* a0 */ +8.388091065357e+02, /* a1 */ +1.941931347893e+01, /* a2 */ -7.471425622002e-02, /* a3 */ +1.114242883863e-04 },
    { 964 , /* a0 */ +8.409324648534e+02, /* a1 */ +1.925176460082e+01, /* a2 */ -7.372781413250e-02, /* a3 */ +1.095829639071e-04 },
    { 968 , /* a0 */ +8.438460527079e+02, /* a1 */ +1.909113737813e+01, /* a2 */ -7.290019638995e-02, /* a3 */ +1.083381152127e-04 },
    { 972 , /* a0 */ +8.462132471425e+02, /* a1 */ +1.890381863759e+01, /* a2 */ -7.182528491230e-02, /* a3 */ +1.063637404724e-04 },
    { 976 , /* a0 */ +8.479795628269e+02, /* a1 */ +1.876305383706e+01, /* a2 */ -7.114962199480e-02, /* a3 */ +1.054895481921e-04 },
    { 980 , /* a0 */ +8.459637716103e+02, /* a1 */ +1.860699810747e+01, /* a2 */ -7.003446638146e-02, /* a3 */ +1.031492334307e-04 },
    { 984 , /* a0 */ +8.497103866808e+02, /* a1 */ +1.853039018913e+01, /* a2 */ -7.044533232145e-02, /* a3 */ +1.054182469318e-04 },
    { 988 , /* a0 */ +8.532686384492e+02, /* a1 */ +1.841712455157e+01, /* a2 */ -7.015800342537e-02, /* a3 */ +1.053761494133e-04 },
    { 992 , /* a0 */ +8.574067629457e+02, /* a1 */ +1.828372323151e+01, /* a2 */ -6.982909054462e-02, /* a3 */ +1.054706303502e-04 },
    { 996 , /* a0 */ +8.612570297457e+02, /* a1 */ +1.822132339612e+01, /* a2 */ -7.000892200899e-02, /* a3 */ +1.064706120917e-04 },
    { 1000, /* a0 */ +8.660463046573e+02, /* a1 */ +1.814761161507e+01, /* a2 */ -7.014166432548e-02, /* a3 */ +1.073486713849e-04 },
    { 1004, /* a0 */ +8.674291208913e+02, /* a1 */ +1.810727049548e+01, /* a2 */ -7.022943439469e-02, /* a3 */ +1.077750340672e-04 },
    { 1008, /* a0 */ +8.680932596717e+02, /* a1 */ +1.801629643852e+01, /* a2 */ -6.946335282051e-02, /* a3 */ +1.054927581023e-04 },
    { 1012, /* a0 */ +8.691425353173e+02, /* a1 */ +1.797265882836e+01, /* a2 */ -6.950092207846e-02, /* a3 */ +1.059788163390e-04 },
    { 1016, /* a0 */ +8.703396878286e+02, /* a1 */ +1.793988472610e+01, /* a2 */ -6.950487224082e-02, /* a3 */ +1.061859185453e-04 },
    { 1020, /* a0 */ +8.701844914583e+02, /* a1 */ +1.794754873845e+01, /* a2 */ -6.974644994192e-02, /* a3 */ +1.069141546535e-04 },
    { 1024, /* a0 */ +8.727428750905e+02, /* a1 */ +1.787492252658e+01, /* a2 */ -6.918195745122e-02, /* a3 */ +1.055369986722e-04 }
};
const int DATA_SIZE = sizeof(FITTED_DATA) / sizeof(FITTED_DATA[0]);

/**
 * Finds the PolyFitData entry with the closest color value to the requested color.
 * This implements the "nearest available data point" requirement for the Color axis.
 * @param targetColor The requested Color value.
 * @return A reference to the PolyFitData struct with the closest Color.
 */
const PolyFitData& findClosestFitData(int targetColor) {
    if (DATA_SIZE == 0) {
        // Fallback for empty data array (should not happen if Python script is run)
        // Returns an entry with all zero coefficients.
        static const PolyFitData ZERO_FIT = {0, 0.0, 0.0, 0.0, 0.0};
        return ZERO_FIT;
    }

    int min_diff = std::numeric_limits<int>::max();
    int closest_index = 0;

    for (int i = 0; i < DATA_SIZE; ++i) {
        int diff = std::abs(FITTED_DATA[i].color - targetColor);
        if (diff < min_diff) {
            min_diff = diff;
            closest_index = i;
        }
    }
    return FITTED_DATA[closest_index];
}


/**
 * Evaluates the 3rd-order polynomial for the given brightness (B) and a set of coefficients.
 * M = a3*B^3 + a2*B^2 + a1*B + a0
 * @param B The input Brightness value (x).
 * @param fit The PolyFitData containing the coefficients.
 * @return The calculated Measured Brightness (M).
 */
double evaluatePolynomial(double B, const PolyFitData& fit) {
    // Horner's method for efficient polynomial evaluation:
    // M = (((a3 * B + a2) * B + a1) * B + a0)
    
    double M = fit.a3;
    M = M * B + fit.a2;
    M = M * B + fit.a1;
    M = M * B + fit.a0;
    
    return M;
}


/**
 * Arduino function to evaluate the predicted Measured Brightness based on 
 * the input Color and Brightness.
 * * @param color The input color (0-255). The closest multiple-of-4 color will be used.
 * @param brightness The input brightness (0-255).
 * @return The predicted measured brightness (e.g., 0-1023 analog reading), rounded to the nearest integer.
 */
int calculateBrightnessThreshold(int brightness, int color) {
    // 1. Find the coefficients for the closest available Color data point
    const PolyFitData& fit = findClosestFitData(color);

    // 2. Evaluate the 3rd-order polynomial using the input brightness (B)
    double predicted_M = evaluatePolynomial(static_cast<double>(brightness), fit);

    // 3. Return the result rounded to the nearest integer
    return static_cast<int>(std::round(predicted_M));
}