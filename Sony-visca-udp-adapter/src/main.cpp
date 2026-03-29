#include <Arduino.h>
#include <WiFiUdp.h>

#include <WiFi.h>
#define VISCA_PORT 52381

WiFiUDP udp;

const char *ssid_instanssi = "InstanssiAV";

const char *password_instanssi = "FreezingCirno9";

#define DTR_PIN 7
#define DSR_PIN 19

// HardwareSerial swSerial(2);
void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    pinMode(DTR_PIN, OUTPUT);
    pinMode(DSR_PIN, INPUT);
    digitalWrite(DTR_PIN, HIGH);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int wifi_timeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_timeout < 5)
    {
        wifi_timeout++;
        Serial.println("Starting wifi");
        delay(1000);
    }
    if (wifi_timeout == 5)
    {
        WiFi.disconnect(true);

        WiFi.begin(ssid_instanssi, password_instanssi);
        while (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("Starting wifi");
            delay(1000);
        }
    }

    udp.begin(VISCA_PORT);
    Serial1.setPins(21, 22);
    Serial1.begin(38400);
}

char incomingPacket[255];
char sendPacket[255];

bool isNewViscaMsg = false;
int serialReceivedCount = 0;
int expectedSerialLen = 0;
IPAddress senderIp;
uint16_t port = 0;

char data[] = {0x01, 0x01, 0x00, 0x04, 0x06, 0x04, 0x13, 0x0a, 0x41, 0x42, 0x53, 0xff};

// the loop routine runs over and over again forever:
/*void loop()
{
    // read the input pin:
    // print out the state of the button:
    Serial1.write(data, 12);
    delay(1000); // delay in between reads for stability
}*/

unsigned long lastSerial = 0;
unsigned long dsrTimeout = 0;

void loop()
{
    int packetSize = udp.parsePacket();
    if (packetSize != 0)
    {
        // receive incoming UDP packets
        Serial.printf("Received %d bytes from %s, port %d\n", packetSize, udp.remoteIP().toString().c_str(), udp.remotePort());
        senderIp = udp.remoteIP();
        port = udp.remotePort();
        int len = udp.read(incomingPacket, 255);
        if (len > 0)
        {
            incomingPacket[len] = 0;
        }
        digitalWrite(DTR_PIN, LOW);
        delay(1);
        // dsrTimeout = millis();
        //  wait for dsr 50ms
        //  NOT sure if needed
        /*while(millis() - dsrTimeout){
            delay(2);
            if(digitalRead(DTR_PIN)){

            }
        }*/
        Serial1.write(incomingPacket, len);
        Serial1.flush(true);
        digitalWrite(DTR_PIN, HIGH);
        // Serial.printf("UDP packet contents: %s\n", incomingPacket);
    }

    while (Serial1.available())
    {
        // just a check if we ever get stuck waiting bytes
        if (serialReceivedCount > 0)
        {
            // if waited longer than 200 ms reset and wait for new message
            if (millis() - lastSerial > 100)
            {
                serialReceivedCount = 0;
            }
        }
        char in = Serial1.read();
        // 1st byte should be 0x01 or 0x02
        sendPacket[serialReceivedCount] = in;
        serialReceivedCount++;
        if (in == 0xff)
        {
            sendPacket[serialReceivedCount] = 0;
            udp.beginPacket(senderIp, port);
            udp.write((uint8_t *)sendPacket, serialReceivedCount);
            udp.endPacket();
            serialReceivedCount = 0;
        }
    }
    delay(10);
}
