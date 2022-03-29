#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "base64.hpp"
#include "CRC.h"
#include <LittleFS.h>

#define UDP_CONFIG_PORT 58911
#define UDP_CLIENT_PORT 58001
#define PROG_VERSION "2.2"

char ssid[32] = "";
char password[64] = "";

// пожалуйста используйте свой сервер для отправки данных
char Server_IP[16] = "62.173.140.213";
uint16_t Server_Port = 58032;

uint16_t Sleep_TIME = 600;    // время между отправкой сообщений в секундах
char line_buffer[65] = "";
char ssid_ap[24] = "ESP_01DS18B20-FFFF";
const char Config_File[] = "/config.txt";

// создаём объект для работы с библиотекой OneWire
OneWire  ds(2);  // on pin 2 
// создадим объект для работы с библиотекой DallasTemperature
DallasTemperature sensor(&ds);
// создадим объект для работы с библиотекой WiFiUdp
WiFiUDP Udp;

IPAddress local_ip;
byte local_mac[6];

char esp_mac[16];
char esp_ip[16];
char temp_c[10];
char crc_8[4];
uint8_t u_crc;
uint16_t timeout = 0;
float c_temp;

char packetBuffer[2048];          //buffer to hold incoming packet
//char ReplyBuffer[] = "ACK";       // a string to send back
char ReplyOK[] = "OK";
char ReplyErr[] = "ERR";

char UDP_MSG[255] = "";
char b64msg[256] = "";

boolean Error_mode = false;
//boolean Led_On = false;

void Error_Mode() {
  timeout = 0;

  while(true) {
    digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
      // but actually the LED is on; this is because
      // it is active low on the ESP-01)
    delay(1000);                      // Wait for a second
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
    delay(2000);                      // Wait for two seconds (to demonstrate the active low LED)
    timeout++;
    if(timeout>200) break;
  };   

  ESP.reset();
}

void Parse_Config(const char * c_line) {

  uint8_t len = strlen(c_line);
  
  if(strncmp(c_line,"SSID:",5) == 0) {
    for (uint8_t pos=5; pos<=len; pos++) {
      ssid[pos-5] = c_line[pos];
    }
    Serial.print("SET SSID=");
    Serial.println(ssid);
    return;
  }

  if(strncmp(c_line,"PASS:",5) == 0) {
    for (uint8_t pos=5; pos<=len; pos++) {
      password[pos-5] = c_line[pos];
    }
    Serial.println("SET PASSWORD=*****");
//    Serial.println(password);
//    Serial.println("*****");
    return;
  }

  if(strncmp(c_line,"SERVER_IP:",10) == 0) {
    for (uint8_t pos=10; pos<=len; pos++) {
      Server_IP[pos-10] = c_line[pos];
    }
    Serial.print("SET SERVER IP=");
    Serial.println(Server_IP);
    return;
  }

  if(strncmp(c_line,"SERVER_PORT:",12) == 0) {
    char server_p[6];
    for (uint8_t pos=12; pos<=len; pos++) {
      server_p[pos-12] = c_line[pos];
    }
    Server_Port = atoi(server_p);
    Serial.print("SET SERVER PORT=");
    Serial.println(Server_Port);
    return;
  }
  
}

void SaveConfig() {   // записываем полученную конфигурацию WiFi и сервера в файл конфигурации
  
//  Serial.println("FORMAT LittleFS File Systems");
  LittleFS.format();
//  Serial.println("Mount LittleFS");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    Error_mode = true;
    return;
  }

  File config_f = LittleFS.open(Config_File, "w");
  if (!config_f) {
    Serial.println("Config file open failed! Error_mode!!!");
    Error_mode = true;
    return;
  }

  config_f.print("SSID:");
  config_f.print(ssid);
  config_f.write('\n');

  config_f.print("PASS:");
  config_f.print(password);
  config_f.write('\n');

  config_f.print("SERVER_IP:");
  config_f.print(Server_IP);
  config_f.write('\n');

  config_f.print("SERVER_PORT:");  
  config_f.print(Server_Port);
  config_f.write('\n');

    // Закрываем конфигурационный файл 
  config_f.close();

  LittleFS.end();

}

void ReadConfig() {   // читаем сохраненную конфигурацию WiFi и сервера
    
    File config_f = LittleFS.open(Config_File, "r");
       if (!config_f) {
          Serial.println("Config file exists but can't opened!");
          Error_mode = true;
          return;
       } else {
          // Читаем конфигурациооный файл
          while (config_f.available()) {
            config_f.readBytesUntil('\n', line_buffer, 64);
//            Serial.println(line_buffer);
            Parse_Config(line_buffer);
            memset(line_buffer, 0, 64);
          }

          // Закрываем конфигурационный файл
          config_f.close();
       }
    
}



void Device_Config() {

  IPAddress remoteIp;
  WiFi.mode(WIFI_AP);

  ssid_ap[14] = esp_mac[8];
  ssid_ap[15] = esp_mac[9];
  ssid_ap[16] = esp_mac[10];
  ssid_ap[17] = esp_mac[11];

  Serial.print("Start AP with SSID: ");
  Serial.println(ssid_ap);

  WiFi.softAP(ssid_ap);

  Udp.begin(UDP_CONFIG_PORT);
  digitalWrite(LED_BUILTIN, LOW);  // Зажигаем светодиод, показывая что мы в режиме конфигурации

  uint32_t start_conf_time = millis();
  // Псевдо loop цикл для обработки принятых UDP пакетов
  while (true) {
        // if there's data available, read a packet
        int packetSize = Udp.parsePacket();
        if (packetSize) {
          Serial.print("Received packet of size ");
          Serial.println(packetSize);
          Serial.print("From ");
          remoteIp = Udp.remoteIP();
          Serial.println(remoteIp);

          // read the packet into packetBufffer
          uint16_t len = Udp.read(packetBuffer, 2048);
          if (len > 0) {
            packetBuffer[len] = 0;
          }
          Serial.print("Contents: ");
          Serial.println(packetBuffer);

          // send a reply, to the IP address and port that sent us the packet we received
          Udp.beginPacket(remoteIp, UDP_CLIENT_PORT);

          if(strncmp(packetBuffer,"ATSSID=",7) == 0) {
            for (uint8_t pos=7; pos<=len; pos++) {
              ssid[pos-7] = packetBuffer[pos];
            }
            Udp.write(ReplyOK);
            Udp.endPacket();
            Serial.print("SET SSID=");
            Serial.println(ssid);
            delay(100);
            continue; 
          }

          if(strncmp(packetBuffer,"ATPASS=",7) == 0) {
            for (uint8_t pos=7; pos<=len; pos++) {
              password[pos-7] = packetBuffer[pos];
            }
            Udp.write(ReplyOK);
            Udp.endPacket();
            Serial.print("SET PASSWORD=");
            Serial.println(password);
            delay(100);
            continue; 
          }

          if(strncmp(packetBuffer,"ATSERVER=",9) == 0) {
            for (uint8_t pos=9; pos<=len; pos++) {
              Server_IP[pos-9] = packetBuffer[pos];
            }
            Udp.write(ReplyOK);
            Udp.endPacket();
            Serial.print("SET SERVER IP=");
            Serial.println(Server_IP);
            delay(100);
            continue; 
          }

          if(strncmp(packetBuffer,"ATPORT=",7) == 0) {
            char server_p[6];
            for (uint8_t pos=7; pos<=len; pos++) {
              server_p[pos-7] = packetBuffer[pos];
            }
            Server_Port = atoi(server_p);
            Udp.write(ReplyOK);
            Udp.endPacket();
            Serial.print("SET SERVER PORT=");
            Serial.println(Server_Port);
            delay(100);
            continue; 
          }

          if(strncmp(packetBuffer,"ATRST!",6) == 0) {
            Serial.println("Configuration complete, going to reboot ESP!");
            Udp.write(ReplyOK);
            Udp.endPacket();
            delay(1000);
            break;  
          }

          Udp.write(ReplyErr);
          Udp.endPacket();
          delay(100);

        }
        
        if(millis() >= (start_conf_time+1800000)) ESP.reset();  // Ждали пол часа, пробуем перезагрузиться.
          
  }

  // Записываем конфигурацию
  SaveConfig();
  delay(1000);

  // В конце конфигурации перегружаем ESP'ку
  ESP.reset();
}

void SetupSavedWiFi() {

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount file system!");
    Error_mode = true;
    return;
  }
// else   Serial.println("LittleFS is mounted.");

  if (LittleFS.exists(Config_File)) {
    ReadConfig();
  } else {
    Serial.println("No config file. First run? Go to config AP mode.");
    Device_Config();
  }
 
  LittleFS.end();

}


void WiFidisconnect() {

  WiFi.disconnect(); 
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  
}

boolean WiFireconnect() {
  WiFi.forceSleepWake();
  WiFi.mode(WIFI_STA);
  wifi_station_connect();
  WiFi.begin(ssid, password);

  timeout = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);  // Быстро мигаем светодиодом оповещая что мы соединяемся
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);  //  Быстро мигаем светодиодом оповещая что мы соединяемся

    if(timeout > 1200) {     // в течении 10 минут не смогли подключиться к WIFI сети, ошибка конфигурации?
      Serial.println("Failed reconnect to WiFi network. BUG?");
      return false;
    }
    timeout++;
  }
  digitalWrite(LED_BUILTIN, LOW);  // ...
  return true;
}


void WiFiconnect() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  timeout = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);  //  Быстро мигаем светодиодом оповещая что мы соединяемся
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);  //  Быстро мигаем светодиодом оповещая что мы соединяемся

    if(timeout > 600) {     // в течении 5 минут не смогли подключиться к WIFI сети, ошибка конфигурации?
      Serial.println("Failed to connect to WiFi network. Going to Config Mode.");
      Device_Config();
    }
    timeout++;
  }
  digitalWrite(LED_BUILTIN, LOW);  // ...
}

void setup() {

pinMode(LED_BUILTIN, OUTPUT);

Serial.begin(115200);
//delay(5000);

  digitalWrite(LED_BUILTIN, LOW);  // Зажигаем
  delay(1000);
  Serial.println("3");
  analogWrite(LED_BUILTIN, 64);
  delay(1000);
  Serial.println("2");
  analogWrite(LED_BUILTIN, 128);
  delay(1000);
  Serial.println("1");
  analogWrite(LED_BUILTIN, 192);
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH);  // Гасим

// Получаем свой MAC address
WiFi.macAddress(local_mac);
sprintf(esp_mac, "%02X%02X%02X%02X%02X%02X", local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);
Serial.print("Мой MAC: ");
Serial.println(esp_mac);

SetupSavedWiFi();  

if(Error_mode) Error_Mode();

// Connect to WiFi network
Serial.println();
Serial.print("Connecting to ");
Serial.println(ssid);
//Serial.print("Password: ");
//Serial.println(password);

WiFiconnect();

Serial.println();
Serial.println("WiFi connected");

// Получаем свой IP address
local_ip = WiFi.localIP();
sprintf(esp_ip, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);

// Получаем свой GW address
//local_ip = WiFi.gatewayIP();
//sprintf(esp_gw, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);

// Получаем свою маску сети
//local_ip = WiFi.subnetMask();
//sprintf(esp_mask, "%d.%d.%d.%d", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);

Serial.println(esp_ip);
//Serial.println(esp_gw);
//Serial.println(esp_mask);
Serial.println(esp_mac);

// начинаем работу с датчиком
sensor.begin();
// устанавливаем разрешение датчика от 9 до 12 бит
sensor.setResolution(12);
}

void loop() {

// чистим переменную
memset(UDP_MSG, 0, 254);

// отправляем запрос на измерение температуры
sensor.requestTemperatures();

// считываем данные из регистра датчика
c_temp = sensor.getTempCByIndex(0);
  
Serial.print("Температура = ");
Serial.println(c_temp);

//Serial.println(esp_ip);
//Serial.println(esp_mac);

dtostrf(c_temp, 3, 1, temp_c);

strcpy(UDP_MSG,esp_mac);
strcat(UDP_MSG,",");
strcat(UDP_MSG,temp_c);
strcat(UDP_MSG,",");
strcat(UDP_MSG,PROG_VERSION);
strcat(UDP_MSG,",");
strcat(UDP_MSG,"0");

//Serial.println(strlen(UDP_MSG));
u_crc = crc8((uint8_t *)UDP_MSG, strlen(UDP_MSG), 0x07);

UDP_MSG[strlen(UDP_MSG)] = u_crc;
UDP_MSG[strlen(UDP_MSG)+1] = '\0';
//Serial.println(strlen(UDP_MSG));

if(WiFi.status() != WL_CONNECTED) {
  if(!WiFireconnect()) ESP.reset();
}

Serial.printf("Посылаем на сервер: %s:%d сообщение:", Server_IP, Server_Port);
Serial.println();

encode_base64((unsigned char*)UDP_MSG, strlen(UDP_MSG), (unsigned char*)b64msg);
Serial.println(b64msg);

Udp.beginPacket(Server_IP, Server_Port);

Udp.write(b64msg);
Udp.endPacket();

Serial.println("Пакет в теории отправлен ...");

analogWrite(LED_BUILTIN, 128);
delay(60000);  // Даем отправить UDP пакет.

Serial.println("Засыпаем .....");
WiFidisconnect();
digitalWrite(LED_BUILTIN, HIGH);  //  Гасим светодиод во время сна для экономии энергии

delay((Sleep_TIME*1000) - 60000);  // Задержка для сна
Serial.println("Проснулись!");
if(!WiFireconnect()) ESP.reset();  // Если после сна не подключились перегружаемся

//WiFireconnect();


}
