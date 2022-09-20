#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#ifdef ESP32
#include <AsyncTCP.h>
#else
#include <ESPAsyncTCP.h>
#endif
#include <AsyncMqttClient.h>

#include <EEPROM.h> 
#include <ArduinoOTA.h>


#include "index_html.h"
#include "style_css.h"
#include "version.h"

#ifdef ESP32
extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}
TimerHandle_t WiFiTimer;
#else
#include <Ticker.h>
Ticker WiFiTimer;
Ticker MQTTTimer;
#endif

AsyncWebServer server(80);
AsyncMqttClient mqttClient;

struct Configuration
{
  char WIFI_SSID[60] = "";
  char WIFI_PASS[60] = "";
  char MQTT_USER[60] = "";
  char MQTT_PASS[60] = "";
  char MQTT_SERVER[250] = "";
  char PROJECTNAME[30] = "project";
  byte CRC = 0;
} stConfig;

struct ProjectConfiguration
{
  int test = 0; 
} stProjectConfig;

unsigned long delaiWiFi = 0;
byte WiFiMode = -1;
const char* APWiFiPass = "1234567890";
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttConnect(bool sessionPresent);
void thWifi();
String processor(const String& var);

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) 
{
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  //mqttClient.publish("test/lol", 0, true, "test 1");
}


void thWifi()
{
  Serial.println("thWifi");
  if (stConfig.WIFI_SSID[0]!=0)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      if ((WiFiMode!=3)&&(WiFiMode!=4))
      {
        Serial.println("Connexion au Wifi");
        WiFiMode = 3;
        WiFi.softAPdisconnect(false);
        WiFi.mode(WIFI_STA);
        WiFi.begin(stConfig.WIFI_SSID,stConfig.WIFI_PASS);
      }
    }
    else{
      if(WiFiMode!=2)
      {
        Serial.println("Wifi connecte");
        WiFiMode = 2;
      }
      if (!mqttClient.connected())
      {
        mqttClient.connect();
      }
    }
  }
  else{
    if (WiFiMode!=1)
    {
      Serial.println("Creation AP");
      WiFiMode = 1;
      WiFi.mode(WIFI_AP);
      WiFi.softAP(stConfig.PROJECTNAME,APWiFiPass);
      Serial.print("[Server Connected] ");
      Serial.println(WiFi.softAPIP());
    }
  }

  if(millis()-delaiWiFi>60000*1)
  {
    if (WiFiMode==4)
    {
      WiFiMode = 0;
    }
    if (WiFiMode==3)
    {
      Serial.println("WiFi TimeOut");
      Serial.println("Creation AP");
      WiFiMode = 4;
      WiFi.mode(WIFI_AP);
      WiFi.softAP(stConfig.PROJECTNAME,APWiFiPass);
      Serial.print("[Server Connected] ");
      Serial.println(WiFi.softAPIP());
      delaiWiFi = millis();
    }
  }
}


String processor(const String& var) {
  if(var == "VERSION")
    return VERSION;
  if(var == "NAME")
    return stConfig.PROJECTNAME;
  if(var == "SSID")
    return stConfig.WIFI_SSID;
  if(var == "WIFIPASS")
    return stConfig.WIFI_PASS;
  if(var == "MQTTSERVER")
    return stConfig.MQTT_SERVER;
  if(var == "MQTTUSER")
    return stConfig.MQTT_USER;
  if(var == "MQTTPASS")
    return stConfig.MQTT_PASS;

  return String();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.softAP("azerty"); 
  Serial.println("Startup...\r\nReading settings");
  EEPROM.begin(sizeof(stConfig)+sizeof(stProjectConfig));
  EEPROM.get(0, stConfig);
  EEPROM.get(sizeof(stConfig), stProjectConfig);
  if (stConfig.CRC!=0x31)
  {
    memset(&stConfig, 0, sizeof(stConfig));
    memset(&stProjectConfig, 0, sizeof(stProjectConfig));
    stConfig.CRC = 0x31;
    strcpy(stConfig.PROJECTNAME, "project");
    EEPROM.put(0,stConfig);
    EEPROM.put(sizeof(stConfig), stProjectConfig);
    EEPROM.commit();
  }
  EEPROM.end();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html,processor);
    request->send(response);
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", style_css);
    request->send(response);
  });
  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        if (p->name() == "ssid") {
          strcpy(stConfig.WIFI_SSID,p->value().c_str());
        }
        if (p->name() == "pass") {
          strcpy(stConfig.WIFI_PASS,p->value().c_str());
        }
        if (p->name() == "mqttserver") {
          strcpy(stConfig.MQTT_SERVER,p->value().c_str());
        }
        if (p->name() == "mqttuser") {
          strcpy(stConfig.MQTT_USER,p->value().c_str());
        }
        if (p->name() == "mqttpass") {
          strcpy(stConfig.MQTT_PASS,p->value().c_str());
        }
      }
    }
    EEPROM.begin(sizeof(stConfig)+sizeof(stProjectConfig));
    EEPROM.put(0,stConfig);
    EEPROM.commit();  
    request->send(200, "text/plain", "Done");
    ESP.restart();
  });

  Serial.println("Starting WebServer");
  //server.begin();
  mqttClient.setServer(stConfig.MQTT_SERVER,1883);
  mqttClient.setCredentials(stConfig.MQTT_USER,stConfig.MQTT_PASS);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onMessage(onMqttMessage);

  Serial.println("Starting Wifi thread");
#ifdef ESP32
  WiFiTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(1000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(thWifi));
  xTimerStart(WiFiTimer,10);
#else
  WiFiTimer.attach(1,thWifi);
#endif
  Serial.println("Starting OTA");
  ArduinoOTA.begin();
  Serial.println("Setup Done");

}

void loop() {
  ArduinoOTA.handle();
}