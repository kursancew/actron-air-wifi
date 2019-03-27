#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <cstdlib>
#include "images.h"

#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "DHTesp.h"
SSD1306 display(0x3c, D1, D2);
DHTesp dht;
WiFiUDP Udp;

/* Create a creds.h file with your credentials */
#include "creds.h"

const char* ssid     = CRED_SSID;
const char* password = CRED_PWD;
IPAddress staticIP(192, 168, 1, 151); //ESP static ip
IPAddress gateway(192, 168, 1, 1);   //IP Address of your WiFi Router (Gateway)
IPAddress broadcastIP(192, 168, 1, 255);
IPAddress subnet(255, 255, 255, 0);  //Subnet mask
IPAddress dns(8, 8, 8, 8);  //DNS
const char* deviceName = "temp_sensor1";

constexpr int button_pin = 0;
void setup() {
  pinMode(2, OUTPUT);
  pinMode(button_pin, INPUT);
  
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);      // DHCP Hostname (useful for finding device for static lease)
  WiFi.config(staticIP, subnet, gateway, dns);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
  dht.setup(D7, DHTesp::DHT11);
  display.init();
  display.flipScreenVertically();
  Udp.begin(7001);
}

template <class Tag>
bool should_do() {
  static unsigned long last_update;
  if (millis() - last_update > Tag::dly) {
    last_update = millis();
    return 1;
  }
  return 0;
}

struct readings_tag{enum{dly = 2000};};
struct display_tag{enum{dly = 500};};

int room_temp = 0;
int room_humidity = 0;
char incoming[10];

void draw_readings() {
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_24);
  display.drawXbm(0, 20, ther_width, ther_height, ther_bits);
  display.drawString(18, 14, String(room_temp));
  
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawXbm(DISPLAY_WIDTH-40, 20, drop_width, drop_height, drop_bits);
  display.drawString(DISPLAY_WIDTH-1, 14, String(room_humidity));
}

void draw_bars() {
  auto rssi = WiFi.RSSI();
  int levels[] = {-69, -65, -55, -40};
  for (int i = 0; i < 4; ++i) {
    int barx = DISPLAY_WIDTH - 17;
    int barw = 3;
    int barspx = 4;
    int barhinc = 2;
    int barh = i*2;
    display.fillRect(barx+i*barspx, 1+8-barh, barw, barh);
    if (rssi < levels[i])
      break;
  }
}

void loop() {
  if (should_do<readings_tag>()) {
    room_temp = dht.getTemperature();
    room_humidity = dht.getHumidity();
  }
  if (should_do<display_tag>()) {
    display.clear();
    draw_readings();
    draw_bars();
    display.display();
  }
  if (Udp.parsePacket()) {
    int len = Udp.read(incoming, sizeof incoming);
    incoming[1] = 0;
    if (len) {
      Serial.println(incoming);
      switch(incoming[0]) {
        default:
          break;
      }
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      char data[10];
      memset(data, 0, sizeof(data));
      snprintf(data, sizeof(data), "t%d", room_temp);
      Udp.write(data, sizeof(data));
      Udp.endPacket();
    }
  }

}
