#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char DOMOTICZ_IN_TOPIC[] = "domoticz/in";
const char DOMOTICZ_OUT_TOPIC[] = "domoticz/out";
char msg_buff[256];
char ssid_ap[30];

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "192.168.1.10";
char mqtt_port[6] = "1883";
char domoticz_idx_str[4] = "1";
int domoticz_idx;

#define TRIGGER_PIN 0

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiClient espClient;
PubSubClient client(espClient);

void reconnect() {
  static unsigned long last_attempt = 0;
  if (millis() < last_attempt)
    return;
  Serial.print("Intentando conectar a broker MQTT...");
  // Create a random client ID
  String clientId = "ESP32";
  clientId += "-" + String(random(0xffff), HEX);
  // Attempt to connect
  if (client.connect(clientId.c_str())) {
    Serial.print(" Conectado!\n");
    client.subscribe(DOMOTICZ_OUT_TOPIC);
  } else {
    Serial.printf(" FALLO!!!, rc=%d\n", client.state());
  }
  last_attempt = millis() + 5000;
}

void mqttLoop() {
  if (client.connected()) {
    client.loop();
  } else {
    reconnect();
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Serial.printf("Mensaje MQTT en topic [%s]\n", topic);
  // for (int i = 0; i < length; i++) {
  //   Serial.print((char)payload[i]);
  // }
  StaticJsonDocument<512> json; // 256 rror NoMemory
  // DynamicJsonDocument jsonBuffer(256);
  DeserializationError err = deserializeJson(json, payload);
  // serializeJson(jsonBuffer, Serial);
  if (err != DeserializationError::Ok) {
    Serial.print("deserializeJson() falló: ");
    Serial.println(err.c_str());
    return;
  }
  if (json["idx"] != domoticz_idx) {
    Serial.println("Otro id");
    return; // El mensaje no es para nosotros
  }

  switch (json["svalue1"].as<int>()) {
  case 0:
    Serial.println("Apagar");
    break;
  case 10:
    Serial.println("Velocidad 1");
    break;
  case 20:
    Serial.println("Velocidad 2");
    break;
  case 30:
    Serial.println("Velocidad 3");
    break;
  default:
    Serial.println("value erroneo ");
    break;
  }
}

void wifi_manager(bool forzarPortal) {
  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
	Serial.println("opened config file");
	size_t size = configFile.size();
	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);
	configFile.readBytes(buf.get(), size);
	DynamicJsonDocument json(256);
	DeserializationError err = deserializeJson(json, buf.get());
	serializeJson(json, Serial);
	if (err == DeserializationError::Ok) {
	  Serial.println("\nparsed json");
	  strcpy(mqtt_server, json["mqtt_server"]);
	  strcpy(mqtt_port, json["mqtt_port"]);
	  strcpy(domoticz_idx_str, json["domoticz_idx_str"]);
	} else {
	  Serial.println("failed to load json config");
	}
      }
    } else {
      Serial.println("/config.json no existe");
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_domoticz_idx_str("domzid", "domoticz id", domoticz_idx_str, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset saved settings
  // wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setConfigPortalTimeout(600);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_domoticz_idx_str);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  boolean status;
  if (forzarPortal)
    status = wifiManager.startConfigPortal(ssid_ap);
  else
    status = wifiManager.autoConnect(ssid_ap);

  if (!status) { //, "password" autoConnect
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(domoticz_idx_str, custom_domoticz_idx_str.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");


	  DynamicJsonDocument json(256);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["domoticz_idx_str"] = domoticz_idx_str;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, configFile);
    serializeJson(json, Serial);

    configFile.close();
    //end save
  }

  if (forzarPortal){
    ESP.restart();
    delay(5000);
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

}

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT);
  String tssid = "ESP_Domoticz_" + String(ESP_getChipId());
  strcpy(ssid_ap, tssid.c_str());

  wifi_manager(false);
  uint16_t port = atoi(mqtt_port);
  client.setServer(mqtt_server, port);
  client.setCallback(mqttCallback);
  domoticz_idx = atoi(domoticz_idx_str);

}

void domoticzSendLog(const char *mensaje) {
  StaticJsonDocument<256> data; // Inside the brackets, is the capacity of the memory pool in bytes.
  data["command"] = "addlogmessage";
  data["message"] = mensaje;
  size_t n = serializeJson(data, msg_buff);
  client.publish(DOMOTICZ_IN_TOPIC, msg_buff, n);
  Serial.print("Enviado: ");
  Serial.println(msg_buff);
}

void domoticzSendValue(int value) {
  StaticJsonDocument<256> data; // Inside the brackets, is the capacity of the memory pool in bytes.
  data["idx"] = domoticz_idx;
  data["nvalue"] = value;
  data["svalue"] = value;  // este se supone que es float
  size_t n = serializeJson(data, msg_buff);
  client.publish(DOMOTICZ_IN_TOPIC, msg_buff, n);
  Serial.print("Enviado: ");
  Serial.println(msg_buff);
}


void loop() {

  if (digitalRead(TRIGGER_PIN) == LOW) {
    //WiFiManager
    //WITHOUT THIS THE AP DOES NOT SEEM TO WORK PROPERLY WITH SDK 1.5 , update to at least 1.5.1
    WiFi.mode(WIFI_STA);
    wifi_manager(true);
    ESP.restart();
    delay(5000);
  }

  // domoticzSendLog("lalala");
  domoticzSendValue(1);
  delay(5000);
  mqttLoop();
}
