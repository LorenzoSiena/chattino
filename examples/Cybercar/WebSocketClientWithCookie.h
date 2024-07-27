/*This mini-library encapsulates the WebSocketClient library and adds cookie function  */


#include "b64.h"
#include "WebSocketClient.h"

class WebSocketClientWithCookie  : public WebSocketClient
{
private:
    uint64_t iRxSize;
public:
    WebSocketClientWithCookie(Client& aClient, const char* aServerName, uint16_t aServerPort);
    WebSocketClientWithCookie(Client& aClient, const String& aServerName, uint16_t aServerPort);
    WebSocketClientWithCookie(Client& aClient, const IPAddress& aServerAddress, uint16_t aServerPort);


    int begin(const char* aPath,const char* cookie); 
};

//Override for adding cookie
int WebSocketClientWithCookie::begin(const char* aPath,const char* CF_Authorization)
{
    Serial.println("WebSocketClientWithCookie!");
    // start the GET request
    beginRequest();
    connectionKeepAlive();    
    
    int status = get(aPath);

    if (status == 0)
    {
        String cookie=String(CF_Authorization);
        cookie= "CF_Authorization="+ cookie;

        uint8_t randomKey[16];
        char base64RandomKey[25];
    
        // create a random key for the connection upgrade
        for (int i = 0; i < (int)sizeof(randomKey); i++)
        {
            randomKey[i] = random(0x01, 0xff);
        }
        memset(base64RandomKey, 0x00, sizeof(base64RandomKey));
        b64_encode(randomKey, sizeof(randomKey), (unsigned char*)base64RandomKey, sizeof(base64RandomKey));
        // start the connection upgrade sequence
        sendHeader("Upgrade", "websocket");
        sendHeader("Connection", "Upgrade");
        sendHeader("Sec-WebSocket-Key", base64RandomKey);
        sendHeader("Sec-WebSocket-Version", "13");
        sendHeader("Cookie", cookie);
        endRequest();
        status = responseStatusCode();
        Serial.println(status);
        
        if (status > 0 )
        {
            skipResponseHeaders();
        }

    }

    iRxSize = 0;

    // status code of 101 means success
    return (status == 101) ? 0 : status;
}

WebSocketClientWithCookie::WebSocketClientWithCookie(Client& aClient, const char* aServerName, uint16_t aServerPort) : WebSocketClient(aClient, aServerName, aServerPort) {}
WebSocketClientWithCookie::WebSocketClientWithCookie(Client& aClient, const String& aServerName, uint16_t aServerPort) : WebSocketClient(aClient, aServerName, aServerPort) {}
WebSocketClientWithCookie::WebSocketClientWithCookie(Client& aClient, const IPAddress& aServerAddress, uint16_t aServerPort) : WebSocketClient(aClient, aServerAddress, aServerPort) {}