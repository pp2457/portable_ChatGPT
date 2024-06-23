/*
 Example guide:
 1. To run Server on PC with GPU:
    python AmebaPro2_Whisper_LLM_server.py
    
 2. To run client on AMB82-mini:   
    upload this code to AMB82-mini, and hit reset to start the client on AMB82-mini,
    press button for 2 seconds to speak to AMB82-mini
    AMB82-mini will record voice to MP4 file on SDcard, 
    then send to AmebaPro2_Whisper_LLM_server
*/
#include "string.h"
#include "StreamIO.h"
#include "AudioStream.h"
#include "AudioEncoder.h"
#include "MP4Recording.h"
#include "WiFi.h"
#include "AmebaFatFS.h"
#include "Base64.h"
#include "ArduinoJson.h"
#include "SPI.h"
#include "AmebaILI9341.h"

// For all supported boards (AMB` 21/AMB22, AMB23, BW16/BW16-TypeC, AW-CU488_ThingPlus),
// Select 2 GPIO pins connect to TFT_RESET and TFT_DC. And default SPI_SS/SPI1_SS connect to TFT_CS.
#define TFT_RESET 5
#define TFT_DC    4
#define TFT_CS    SPI_SS

AmebaILI9341 tft = AmebaILI9341(TFT_CS, TFT_DC, TFT_RESET);

#define ILI9341_SPI_FREQUENCY 20000000

#define FILENAME "TestRecordingAudioOnly.mp4"

char ssid[] = "HITRON-DF90-5G";              // your network SSID (Home WiFi or Smartphone Hotspot)
char pass[] = "0972211921";        // your network password
int status = WL_IDLE_STATUS;

//char server[] = "123.195.32.57";   // the server IP running HTTP server on PC
//#define PORT 5000
char server[] = "5dc9-35-193-250-216.ngrok-free.app";   // the server IP running HTTP server on PC
#define PORT 80

AmebaFatFS fs;
WiFiClient wifiClient;

char buf[512];
char *p;
String filepath;
File file;

// Default audio preset configurations:
// 0 :  8kHz Mono Analog Mic
// 1 : 16kHz Mono Analog Mic
// 2 :  8kHz Mono Digital PDM Mic
// 3 : 16kHz Mono Digital PDM Mic
AudioSetting configA(0);
Audio audio;
AAC aac;
MP4Recording mp4;
StreamIO audioStreamer1(1, 1);    // 1 Input Audio -> 1 Output AAC
StreamIO audioStreamer2(1, 1);    // 1 Input AAC -> 1 Output MP4

const int buttonPin = 1;          // the number of the pushbutton pin
int buttonState;                          // variable for reading the pushbutton status
unsigned long buttonPressTime = 0;        // variable to store the time when button was pressed
bool buttonPressedFor2Seconds = false;    // flag to indicate if button is pressed for at least 2 seconds
int recordingstate = -1;
int previousRecordingState = -1;

// Receiving Buffer
#define OFFSET 94              // skip HTTP response headers
#define PAGE_CHAR_LENGTH 26*15 // Font 2 = 26 characters per line, total 14 lines
char textbuffer[26*15*3];      // 3 pages buffer 
int  textcount = 0;
int  skipcount = 0;
int  displaycount =0;
//int  pos=0; // for finding "LLM:"

void setup()
{
    Serial.begin(115200);

    // Connection to internet
    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
        status = WiFi.begin(ssid, pass);
        delay(2000);
    }

    SPI.setDefaultFrequency(ILI9341_SPI_FREQUENCY);
    tft.begin();
    tft.clr();
    tft.setCursor(0, 0);
    tft.setForeground(ILI9341_WHITE);
    tft.setFontSize(2);
    tft.setRotation(3);
    tft.println("Portable ChatGPT - Llama3");

    // list files under root directory
    fs.begin();

    // initialize the pushbutton pin as an input:
    pinMode(buttonPin, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LED_G, OUTPUT);

    // Configure audio peripheral for audio data output
    audio.configAudio(configA);
    audio.begin();
    // Configure AAC audio encoder
    aac.configAudio(configA);
    aac.begin();

    // Configure MP4 recording settings
    mp4.configAudio(configA, CODEC_AAC);
    mp4.setRecordingDuration(5);
    mp4.setRecordingFileCount(1);
    mp4.setRecordingFileName("TestRecordingAudioOnly");
    mp4.setRecordingDataType(STORAGE_AUDIO);    // Set MP4 to record audio only

    // Configure StreamIO object to stream data from audio channel to AAC encoder
    audioStreamer1.registerInput(audio);
    audioStreamer1.registerOutput(aac);
    if (audioStreamer1.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }

    // Configure StreamIO object to stream data from AAC encoder to MP4
    audioStreamer2.registerInput(aac);
    audioStreamer2.registerOutput(mp4);
    if (audioStreamer2.begin() != 0) {
        Serial.println("StreamIO link start failed");
    }
}

void loop()
{
    // Button state
    int newButtonState = digitalRead(buttonPin);
    if (newButtonState != buttonState) {
        buttonPressTime = millis();
    }
    // update button state
    buttonState = newButtonState;

    // update recording state
    recordingstate = (int)(mp4.getRecordingState());

    // check if the button has been held for at least 2 seconds
    if (buttonState == HIGH && millis() - buttonPressTime >= 2000) {
        // button has been pressed for at least 2 seconds
        buttonPressedFor2Seconds = true;
    } else {
        // button was released before 2 seconds
        buttonPressedFor2Seconds = false;
    }
    // if button has been pressed for at least 2 seconds
    if (buttonPressedFor2Seconds) {
        if (recordingstate == 1) {
            digitalWrite(LED_BUILTIN, HIGH);
        } else {
            mp4.begin();
            Serial.println("Recording");
            tft.clr();
            tft.setCursor(0,0);
        }
    }
    if (recordingstate == 1 && previousRecordingState == 0) {
        // Change from 0 to 1
        digitalWrite(LED_BUILTIN, HIGH);
    } else if (recordingstate == 0 && previousRecordingState == 1) {
        encodeMP4andsendHttpPostRequest();
        // Change from 1 to 0
        digitalWrite(LED_BUILTIN, LOW);  
    }

    // Check if there are incoming bytes available from the server
    textcount =0;    
    skipcount =0;
    while (wifiClient.available()) {
        char c = wifiClient.read();
        textbuffer[textcount] = c;
        textcount++;
        skipcount++;
        Serial.write(c);
        if (skipcount>OFFSET) tft.print(c);
    }

// finding indexOf LLM:     
    // if (pos<=0) {
    //     String s = String(textbuffer);
    //     pos = s.indexOf("LLM:");
    //     Serial.print("LLM indexOf = ");
    //     erial.println(pos);
    //  }
    previousRecordingState = recordingstate;
}

void encodeMP4andsendHttpPostRequest()
{
    memset(buf, 0, sizeof(buf));
    fs.readDir(fs.getRootPath(), buf, sizeof(buf));
    filepath = String(fs.getRootPath()) + String(FILENAME);
    p = buf;
    while (strlen(p) > 0) {
        /* list out file name image will be saved as "TestRecordingAudioOnly.mp4"*/
        if (strstr(p, FILENAME) != NULL) {
            Serial.println("[INFO] Found 'TestRecordingAudioOnly.mp4' in the string.");
            Serial.println("[INFO] Processing file...");
        } else {
            // Serial.println("Substring 'image.jpg' not found in the
            // string.");
        }
        p += strlen(p) + 1;
    }
    uint8_t *fileinput;
    file = fs.open(filepath);
    unsigned int fileSize = file.size();
    fileinput = (uint8_t *)malloc(fileSize + 1);
    file.read(fileinput, fileSize);
    fileinput[fileSize] = '\0';
    file.close();

    // Encode the file data as Base64
    int encodedLen = base64_enc_len(fileSize);
    char *encodedData = (char *)malloc(encodedLen);
    base64_encode(encodedData, (char *)fileinput, fileSize);

    JsonDocument doc;

    //Change "base64_string" to the key that you set in your server.
    doc["base64_string"] = encodedData;
    String jsonString;
    serializeJson(doc,jsonString);

    if (wifiClient.connect(server, PORT)) {
        wifiClient.println("POST /audio HTTP/1.1");
        wifiClient.println("Host: " + String(server));
        wifiClient.println("Content-Type: application/json");    // Use appropriate content type
        wifiClient.println("Content-Length: " + String(jsonString.length()));              // Specify the length of the content
        wifiClient.println("Connection: keep-alive");
        wifiClient.println();             // Empty line indicates the end of headers
        wifiClient.print(jsonString);    // Send the Base64 encoded audio data directly
        
        Serial.println("Binary sent");
        tft.println("Voice sent to LLM server !");
        tft.println("wait a moment for LLM.....");
    }
}
