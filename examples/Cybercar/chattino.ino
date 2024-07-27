/*  
    
    This code is part of https://github.com/LorenzoSiena/Johnny-The-CyberCar-Assistant project and under GNU General Public License v3.0

    Chattino is an Arduino/Esp32 client for connecting to the WebSocket API of Cheshire Cat application. 
    The client will to send and receive audio files, via WebSocket.


    This code will be flashed on Lilygo t-sim7600 (just a ESP32 WROOVER-E connected on a 4g module SIM7600)
    https://github.com/Xinyuan-LilyGO/T-SIM7600X
    this main board will be connected to:
        [keyword spotter]
        a DFROBOT VOICE RECOGNITION MODULE [SKU:SEN0539-EN] on I2C
        
        [audio player]
        a DFPLAYER mini sending on the dedicated serial(SerialMp3Player) the audio needed to be played.
        
        [audio recorder]
        a microfone module or a slave board(like the Nicla voice) for recording the vocal request of the user.

    actively listen to user in order to: 
        make a single local action (like "roll down the car windows" if you connect an esternal relay )
        make a remote request to a server running a Cheshire Cat instance (a personal assistant)
        recording an audio from the user ("Jhonny what's the wheater in lisbona?")
        and playing the answer ("Right now it's 36 degree celsius Meow!")
    Ideally all data (json) will be sent from this client to a Cheshire cat websocket server endpoint , behind a cloudflare tunnel and authenticated by cookie. 
    
    It's still a WIP
   
    TODO
    -initialize and add Voice Recording DF2301QG V1.0  Keyword spotting 
    
        -case (keywork)
            local_action_1
            local_action_2
            local_action_3
            
            pass_me_remote_ai_assistant
                rec(your_voice)
                play(responce)
            stop

    -recoder
    -player
    -SD card filesystem for storing your_voice.mp3 and responce.mp3
    -whatever needed

*/

#define TINY_GSM_MODEM_SIM7600

// Monitor Serial
#define SerialMon Serial

// AT command Serial
#define SerialAT Serial1

// Serial DfrobotPlayerMp3
#define SerialMp3Player Serial2

// Debug
#define TINY_GSM_DEBUG SerialMon

// Libreria gsm
#include <TinyGsmClient.h>

// pin della sim(?)
#define GSM_PIN ""

// user ccat Config
#define CAT_SERVER_IP "your_nice_domain_on_cloudflare.xyz"
#define CAT_SERVER_PORT 443
#define USER_ID "user16661"
#define AUTH_KEY ""
#define SECURE_CONNECTION true

#include <ArduinoJson.h>
#include <esp32-hal.h>
#include <Wire.h>
#include <Ticker.h>
#include "utilities.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoHttpClient.h>
#include "SSLClient.h"
// #include "certs.h"
#include "secrets.h"
// #include <WebSocketsClient.h>
#include "WebSocketClientWithCookie.h"

/* 
TODO 
--PORTING LIBRARY FROM PYTHON CLIENT--
import datetime (in order for the request to be sent as timestamp.mp3)
import base64 (in order to "compress" timestamp.mp3 )
from bs4 import BeautifulSoup (for reading path where your vocal response is )
something like playsound 
something like record audio
import threading (in order to play audio without blocking everything)
 */

TinyGsm modem(SerialAT);

// CatServer HTTPS
const char server[] = CAT_SERVER_IP;
const char resource[] = "/";
const int port = CAT_SERVER_PORT;

// Credenziali apn
const char apn[] = "iliad";
const char gprsUser[] = "";
const char gprsPass[] = "";

// HTTPS Transport
TinyGsmClient base_client(modem, 0);
SSLClient secure_layer(&base_client);
HttpClient client = HttpClient(secure_layer, server, port);

//----------------WEBSOCKET-----------------------------------
//NO COOKIE
// WebSocketClient wclient = WebSocketClient(secure_layer,"your_nice_domain_on_cloudflare.xyz",443);
int countx = 0;
WebSocketClientWithCookie wclient = WebSocketClientWithCookie(secure_layer, "your_nice_domain_on_cloudflare.xyz", 443);

//Cookie and cloudflare auth
String CF_Authorization = "";
String CF_cookie_expiration = "";

//----------------End WEBSOCKET-----------------------------------

//---------------------Python code to port used for receiving and extracting audio_path_responce------------------------
/*
                my_responce_json {
                    "type" :
                        "chat",
                    "content" :
                        "<audio controls autoplay><source
                        src='/admin/assets/voice/voice_20240617_140731.wav'
                        type='audio/mp3'
                        >
                        Your browser does not support the audio element.</audio>"
                    }
                
                message = json.loads(message)

                if message["type"] == "chat" and "type='audio/mp3" in message["content"]:
                    html_string = message["content"]
                    url_audio_to_play = get_audio_file_path(
                        html_string
                    )
                    print("The file you are looking for is at :--->" + url_audio_to_play)
                    thread = threading.Thread(target=playsound, args=(url_audio_to_play,))
                    thread.start()

                    #playsound(url_audio_to_play) was blocking the thread and the connection was closed by the server because of a timeout

                elif message["type"] == "chat" and "type='audio/wav" not in message["content"]:

                    print("Your user input was :")
                    print(message["why"]['input'])
                    print("Your response :")
                    print(message["content"])
    }
}
*/

//-------------Initializer functions---------------------------------
boolean powerUpModem()
{
    // Set GSM module baud rate
    SerialAT.begin(UART_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);

    /*
      MODEM_PWRKEY IO: (PIN 4) The power-on signal of the modulator must be given to it,
      otherwise the modulator will not reply when the command is sent
    */
    pinMode(MODEM_PWRKEY, OUTPUT);
    digitalWrite(MODEM_PWRKEY, HIGH);
    delay(300); // Need delay
    digitalWrite(MODEM_PWRKEY, LOW);

    /*
      MODEM_FLIGHT IO:(PIN 25) Modulator flight mode control,
      need to enable modulator, this pin must be set to high
    */
    pinMode(MODEM_FLIGHT, OUTPUT);
    digitalWrite(MODEM_FLIGHT, HIGH);
    return true;
}

boolean initModemWIFI()
{
    return false;
}

boolean initSDcard()
{
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS))
    {
        Serial.println("SDCard MOUNT FAIL");
        return false;
    }
    else
    {
        uint32_t cardSize = SD.cardSize() / (1024 * 1024);
        String str = "SDCard Size: " + String(cardSize) + "MB";
        Serial.println(str);
        return true;
    }
}

//---------------End Initializer functions---------------------------------

//-------------Necessary functions---------------------------------
String cookieExtraction(String headerValue, String key)
{
    /*  [Extract the cookie from CF_Authorization]
        headervalue= "CF_Authorization=xxxxxx" <----
        Expires=Mon, 01 Jul 2024 14:45:44 GMT;
        Path=/;
        Secure;
        HttpOnly;
        SameSite=none"*/

    int startIndex = headerValue.indexOf(key) + key.length();
    int endIndex = headerValue.indexOf(';', startIndex);
    String token = headerValue.substring(startIndex, endIndex);
    DBG("token:");
    DBG(token);
    return token;
};
boolean httpRequestCookie()
{

    DBG("Requesting a cookie");

    // Invio Credenziali
    Serial.println("Loading Credential");
    client.beginRequest();
    client.get("/");
    client.sendHeader("CF-Access-Client-Id", CF_ACCESS_CLIENT_ID);         // from secrets.h
    client.sendHeader("CF-Access-Client-Secret", CF_ACCESS_CLIENT_SECRET); // from secrets.h
    client.endRequest();

    DBG("Connecting to ", server);
    Serial.println("Sending GET request...");
    client.get(resource);
    int status_code = client.responseStatusCode();

    Serial.print("Status code: ");
    Serial.println(status_code);

    if (status_code == 200) //  OK
    {
        // Read the headers
        String headerName = "";
        String headerValue = "";
        String cookie = "";
        String cookie_expiration = "";

        while (client.headerAvailable())
        {
            headerName = client.readHeaderName();
            headerValue = client.readHeaderValue();

            DBG(headerName);
            DBG("headerName");
            DBG(headerValue);
            DBG("headerValue");

            if (headerName.equals("Set-Cookie"))
            {
                cookie = cookieExtraction(headerValue, "CF_Authorization=");
                cookie_expiration = cookieExtraction(headerValue, "Expires=");

                String response = client.responseBody();

                DBG("Response: ");
                DBG(response);

                CF_Authorization = cookie;
                CF_cookie_expiration = cookie_expiration;
                client.stop();
                return true;

                /*
                CF_Authorization=xxxx...authcookie...xxx"
                Expires=Mon, 01 Jul 2024 14:45:44 GMT;
                Path=/;
                Secure;
                HttpOnly;
                SameSite=none*/
            }
        }

        DBG("Cookie not found");
        client.stop();
        return false;
    }
    else
    {
        Serial.print("Failed Request: ");
        Serial.println(status_code);
        String response = client.responseBody();
        Serial.println(response);
        Serial.println("Try again");
        CF_Authorization = "";
        CF_cookie_expiration = "";
        client.stop();
        return false;
    }
}
void light_sleep(uint32_t sec)
{
    esp_sleep_enable_timer_wakeup(sec * 1000000ULL);
    esp_light_sleep_start();
}
boolean cookie_invalid = false; // CHECK WHEN ITS VALID OR NOT , ALWAYS VALID [must be implemented better]
int messageSize = 0;
//-------------End Necessary functions---------------------------------

void setup()
{

    // LED pin 12
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Set console baud rate
    SerialMon.begin(115200);
    delay(10);
    SerialMp3Player.begin(115200, SERIAL_8N1, 39, 0);
    delay(10);

    while (!SerialMon)
    {
    }
    DBG("Debugging is active!");

    if (psramInit())
    {
        DBG("PSRAM Enabled");
        Serial.printf("PSRam Total heap %u, PSRam Free Heap %u \n\n", ESP.getPsramSize(), ESP.getFreePsram());
    }
    else
    {

        Serial.printf("ERROR: Something very wrong happened with psram activation.");
    }


    /* 
    //TESTING WORK IN PROGRESS SERIAL CONNECTION BETWEEN BOARDS (This should be implemented and target should be DFPlayer mini for playing audio responce)
    while (!SerialMp3Player)
    {
    }
    String message;

    while (true)
    {
        if (SerialMp3Player.available())
        {
            SerialMon.println("Received a message!"); 

            SerialMon.println(SerialMp3Player.readStringUntil('\n'));
        }
        message = "Hello from the main board! ";
        SerialMp3Player.println(message); 
        delay(2000);
    }
    */


    if (initSDcard())
    {
        SerialMon.println("SD initialized");
    }
    else
    {
        SerialMon.println("SD NOT FOUND");
    }
    if (!powerUpModem())
    {
        DBG("Failed powering up modem! Resetting in 10 seconds");
        delay(10000);
        ESP.restart();
    }

    // initModemGPS();
    //---------------------set up modem-------------------------------------
    DBG("Initializing modem...");
    if (!modem.init())
    {
        do
        {
            DBG("Failed to start modem, delaying 1s and retrying");
            delay(1000);
        } while (!modem.restart());
    }

    String modemInfo = modem.getModemInfo();
    String modemName = modem.getModemName();
    String ModemManufacturer = modem.getModemManufacturer();
    String ModemRevision = modem.getModemRevision();
    String ModemModel = modem.getModemModel();

    DBG("Modem Manufacturer: ");
    DBG(ModemManufacturer);
    DBG("Modem getModemRevision: ");
    DBG(ModemRevision);
    DBG("Modem Model: ");
    DBG(ModemModel);
    DBG("Modem Name: ");
    DBG(modemName);
    DBG("Modem Info: ");
    DBG(modemInfo);

    // unlock sim if necessary
    if (GSM_PIN && modem.getSimStatus() != 3)
    {
        modem.simUnlock(GSM_PIN);
    }

    // NOT 4G ONLY WIFI
    // modem.networkConnect(wifiSSID, wifiPass)

    int ret;
    DBG("LTE setup:");
    do
    {
        ret = modem.setNetworkMode(38); // 38 <= LTE
        delay(500);
    } while (ret != 1);
    DBG("setNetworkMode:", ret);

    DBG("Waiting for network registration...");
    if (!modem.waitForNetwork(600000L))
    {
        DBG("Network registration failed...");
        light_sleep(1);
        return;
    }

    SerialMon.print(F("Connecting to "));
    SerialMon.print(apn);

    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
    { // Connessione alla rete GPRS
        SerialMon.println(" Connection to apn failed");
        SerialMon.println(" Resetting???");
        delay(1000);
        return;
    }
    if (modem.isGprsConnected())
    {
        SerialMon.println("GPRS connected");
    }

    // get the cookie
    while (!httpRequestCookie())
    {
        Serial.println("Can't get auth cookie !\n Retrying in 2 seconds");
        delay(2000);
    }

    // adding user id
    String userIdStr = String(USER_ID);
    String fullString = "user_id: " + userIdStr + "\r\n";
    const char *user_id = fullString.c_str();

    // Testing the cookie
    // wclient.beginMessage();
    // wclient.beginRequest();
    // wclient.sendHeader("Cookie", "CF_Authorization=" + CF_Authorization);
    // wclient.endRequest();
    // wclient.endMessage();
    // using the cookie
    // webSocket.setExtraHeaders(cookies);
    // webSocket.setExtraHeaders(user_id);

    /* json_message = {
            "text":"",
            "user_id":"user_69",
            "audio_key": "",  # Base64 encoded audio file
            "audio_type": "audio/ogg",  # MIME type of the audio file
            "audio_name": "msg45430839-160807.ogg",  # final part of the audio file path , should be better use timestamp
            "encodedBase64": True,  # Flag to indicate that the audio is encoded in base64
        }
    */

    DBG("CF_Authorization");
    DBG(CF_Authorization);
    DBG("Connecting to:");
    DBG(CAT_SERVER_IP);

    DBG("Setup complete:");
}

void loop()
{
    /* WARNING! INCOMPLETE CODE!
       At the moment the loop function is able to send text via json 
       but it is necessary to implement the sending of vocal_user_request.mp3 
       and to play the response vocal_agent_response.mp3
     */


    Serial.println("starting WebSocket client");
    //This will connect to ws endpoint with user16661 as user
    if (wclient.begin("/ws/user16661", CF_Authorization.c_str()) != 0)
    {
        Serial.println("Error");
    }
    else
    {
        Serial.println("Connected!");
        Serial.println(wclient.connected());
    }

    while (wclient.connected())
    {
        Serial.print("Sending hello ");
        Serial.println(countx);

        // send a hello #
        // wclient.beginMessage(TYPE_TEXT);

        String jsonino;
        StaticJsonDocument<32> doc; // FIXED SIZE FOR SENDING TEXT AND USER ID

        // StaticJsonDocument<256> doc;

        doc["text"] = "Hello BRO!";
        doc["user_id"] = "user16661";

        // doc["audio_key"] = "binary_data_audio_in_base_64";
        // doc["audio_type"] = "audio/ogg";
        // doc["audio_name"] = "msg45430839-160807.ogg";
        // doc["encodedBase64"] = true;
        // doc["auth_key"] = "IF-CHESHIRE-CAT-HAS-API-KEY";

        serializeJson(doc, jsonino);
        // wclient.print(jsonino);
        // wclient.endMessage();

        // Arduino assistant want accurate managing of message size to send (Look for enough memory)
        // https://arduinojson.org/v6/assistant/#/step4

        //TODO
        // Record audio--->make it base64(audio)--->Send the complete json as
        //    {
        //       "text": "Hello BRO!",
        //       "user_id": "user16661" // your user
        //       "audio_key": "audio_key",  # Base64 encoded audio file
        //       "audio_type": "audio/ogg",  # MIME type of the audio file
        //       "auth_key":   "IF-CHESHIRE-CAT-HAS-API-KEY",
        //       "audio_name": "msg45430839-160807.ogg",  # final part of the audio file path , should be better use timestamp
        //       "encodedBase64": true,  # Flag to indicate that the audio is encoded in base64
        //    }


        //----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------//----------------------------------------------------------------------

        // increment count for next message
        countx++;

        // check if a message is available to be received

        messageSize = wclient.parseMessage();
        Serial.println("Message size is' :");
        Serial.println(messageSize);

        if (messageSize > 0)
        {
            Serial.println("Received a message:");
            Serial.println(wclient.readString()); // Will consume the message
        }
        // wait 5 seconds
        delay(5000);
    }
}

// TODO
String get_audio_file_path(String(html_string))
{
    String url_audio = "path";
    return url_audio;
}

// never used will be needed maybe in the future
boolean initModemGPS()
{
    uint8_t mode = modem.getGNSSMode();
    DBG("GNSS Mode:", mode);
    modem.setGNSSMode(2, 1);
    light_sleep(1);
    return true;
}
void gpsGetPosition()
{
    DBG("Enabling GPS/GNSS/GLONASS");
    modem.enableGPS();
    light_sleep(2);

    float lat2 = 0;
    float lon2 = 0;
    float speed2 = 0;
    float alt2 = 0;
    int vsat2 = 0;
    int usat2 = 0;
    float accuracy2 = 0;
    int year2 = 0;
    int month2 = 0;
    int day2 = 0;
    int hour2 = 0;
    int min2 = 0;
    int sec2 = 0;
    DBG("Requesting current GPS/GNSS/GLONASS location");
    for (;;)
    {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        if (modem.getGPS(&lat2, &lon2, &speed2, &alt2, &vsat2, &usat2, &accuracy2,
                         &year2, &month2, &day2, &hour2, &min2, &sec2))
        {
            DBG("Latitude:", String(lat2, 8), "\tLongitude:", String(lon2, 8));
            DBG("Speed:", speed2, "\tAltitude:", alt2);
            DBG("Visible Satellites:", vsat2, "\tUsed Satellites:", usat2);
            DBG("Accuracy:", accuracy2);
            DBG("Year:", year2, "\tMonth:", month2, "\tDay:", day2);
            DBG("Hour:", hour2, "\tMinute:", min2, "\tSecond:", sec2);
            break;
        }
        else
        {

            light_sleep(2);
        }
    }
    DBG("Retrieving GPS/GNSS/GLONASS location again as a string");
    String gps_raw = modem.getGPSraw();
    DBG("GPS/GNSS Based Location String:", gps_raw);
    DBG("Disabling GPS");
    modem.disableGPS();
}
void disconnect()
{
    /*Never called!*/
    modem.gprsDisconnect();
    SerialMon.println(F("GPRS disconnected"));

    // Shutdown
    client.stop();
    SerialMon.println(F("Server disconnected"));
}
