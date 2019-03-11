#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <cstdlib>
#include <bitset>
#include "images.h"

#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "DHTesp.h"
#define MAX_TEMP 27
#define MIN_TEMP 22
SSD1306 display(0x3c, D1, D2);
DHTesp dht;
WiFiUDP Udp;

const char* ssid     = "";
const char* password = "";
IPAddress staticIP(192, 168, 1, 151); //ESP static ip
IPAddress gateway(192, 168, 1, 1);   //IP Address of your WiFi Router (Gateway)
IPAddress broadcastIP(192, 168, 1, 255);
IPAddress subnet(255, 255, 255, 0);  //Subnet mask
IPAddress dns(8, 8, 8, 8);  //DNS
const char* deviceName = "temp_sensor1";

int target_temp = MAX_TEMP+1;
int room_temp = 25;
int room_humidity;
bool ac_on = false;
char ac_stat = '!';
bool stat_is_on() {
  switch (ac_stat) {
  case 'c':
  case 'i':
  case 'w':
    return true;
  }
  return false;
}
unsigned rx_count = 0;
unsigned tx_count = 0;

static std::bitset<DISPLAY_WIDTH> power_history;
static byte temp_history[DISPLAY_WIDTH]{};
int last_history_idx = 0;

void add_ac_history() {
  temp_history[last_history_idx] = room_temp;
  power_history[last_history_idx++] = ac_on;
  if (last_history_idx >= DISPLAY_WIDTH)
    last_history_idx = 0;
}

void draw_history() {
  for (int i = 0; i < DISPLAY_WIDTH; ++i) {
    int idx = (last_history_idx + 1 + i) % DISPLAY_WIDTH;
    byte y = temp_history[idx] - 21;
    y = y > 8?8:y;
    display.setPixel(DISPLAY_WIDTH-i-1, DISPLAY_HEIGHT-6-y);
    if (power_history[idx]) {
      display.setPixel(DISPLAY_WIDTH-i-1, DISPLAY_HEIGHT-2);
      display.setPixel(DISPLAY_WIDTH-i-1, DISPLAY_HEIGHT-1);
    }
  }
}

void set_ac(bool v) {
  ac_on = v;
  digitalWrite(2, !v);
}

void one_packet(char val) {
  Udp.beginPacket("192.168.1.150", 7000);
  Udp.write(&val, 1);
  Udp.endPacket();
  ++tx_count;
}

bool br[] = {1,0,0,0};

char incoming[10];
void send_ac() {
  for (int i = 0; i < 5; ++i) {
    one_packet(ac_on?'1':'0');
    one_packet(br[0]?'A':'a');
    one_packet(br[1]?'B':'b');
    one_packet(br[2]?'C':'c');
    one_packet(br[3]?'D':'d');
    delay(100);
    if (Udp.parsePacket()) {
      int len = Udp.read(incoming, sizeof incoming);
      if(!len) incoming[0] = '!';
      else rx_count++;
      ac_stat = incoming[0];
      incoming[1] = 0;
      Serial.println(incoming);
      break;
    }
  }
}

void send_readings() {
  Udp.beginPacket("192.168.1.19", 7001);
  char buf[50];
  strncpy(buf, deviceName, sizeof buf);
  snprintf(buf, sizeof buf, "%s,%d,%d,%d,%d", deviceName, room_temp, room_humidity, ac_on, ac_stat);
  Udp.write(buf, strlen(buf));
  Udp.endPacket();
}

int nclick = 0;
unsigned long last_click = 0;

void handleInterrupt() {
  if ((millis() - last_click) < 20) return;
  last_click = millis();
  ++nclick;
}

void setup() {
  power_history.reset();
  pinMode(2, OUTPUT);
  pinMode(0, INPUT);
  attachInterrupt(digitalPinToInterrupt(0), handleInterrupt, FALLING);

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

struct update_tag{enum{dly = 10000};};
struct readings_tag{enum{dly = 2000};};
struct display_tag{enum{dly = 500};};
struct log_tag{enum{dly = 180000};};

void draw_readings() {
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

void draw_ac() {
  display.drawRect(DISPLAY_WIDTH-25, 1, 4, 8);
  if (ac_on)
    display.fillRect(DISPLAY_WIDTH-25, 1, 4, 4);
  else
    display.drawRect(DISPLAY_WIDTH-25, 1+4, 4, 4);
  display.setFont(ArialMT_Plain_10);
  if ((ac_on != stat_is_on()) || ac_stat == '!') {
    display.drawString(DISPLAY_WIDTH-28, 0, "!");
  }
  char s[5];
  for(int i = 0; i < 4; ++i) {
    s[i] = br[i]?'O':'.';
  }
  s[4] = 0;
  display.drawString(40, 0, s);
}

void loop() {
  if (should_do<readings_tag>()) {
    room_temp = dht.getTemperature();
    room_humidity = dht.getHumidity();
    if (target_temp > MAX_TEMP) {
      set_ac(0);
    } else if (!ac_on && room_temp > target_temp+1) {
      set_ac(1);
    } else if (room_temp < target_temp) {
      set_ac(0);
    }
  }
  if (should_do<update_tag>()) {
    send_ac();
    send_readings();
  }
  if (should_do<display_tag>()) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, (target_temp<=MAX_TEMP?String(target_temp)+String("C"):String("OFF")));
    
    draw_readings();
    
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    draw_bars();
    draw_ac();
    draw_history();
    display.display();
  }

  if (should_do<log_tag>()) {
    add_ac_history();
  }

  if (millis() - last_click > 500) {
    switch(nclick) {
      case 1:
        target_temp = target_temp > MAX_TEMP? MIN_TEMP : target_temp + 1;
        break;
      case 2:
        br[1] = !br[1];
        break;
      case 3:
        break;
    }
    nclick = 0;
  }
}
