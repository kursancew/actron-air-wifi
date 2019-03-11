#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "DHTesp.h"

#define MCP4726_CMD_WRITEDAC            (0x40) 

SSD1306 display(0x3c, D1, D2);
uint8_t portArray[] = {16, 5, 4, 0, 2, 14, 12, 13};
//String portMap[] = {"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"}; //for Wemos

const char* ssid     = "";
const char* password = "";
const char* deviceName = "ac_control";
IPAddress staticIP(192, 168, 1, 150); //ESP static ip
IPAddress gateway(192, 168, 1, 1);   //IP Address of your WiFi Router (Gateway)
IPAddress subnet(255, 255, 255, 0);  //Subnet mask
IPAddress dns(8, 8, 8, 8);  //DNS
int AC_PIN = D6;
int led = LED_BUILTIN;
bool ac_on = false;
bool fan_on = false;
bool disable_ctrl = false;
bool bdr[4] = {1,0,0,0};
#define NPULSE 45
struct LedProto {
  enum ClassStatLeds {
    LEAD = 0,
    COOL = 2,
    RUN_BLINK = 3,
    RUN = 5,
    COOL_AUTO = 10,
    FAN_HIGH = 11,
    FAN_MID = 12,
    FAN_LOW = 13,
    ROOM3 = 14,
    ROOM4 = 15,
    ROOM2 = 16,
    HEAT = 17,
    ROOM1 = 23,
  };
  unsigned long last_intr_us;
  unsigned long last_work;
  char pulse_vec[NPULSE];
  volatile unsigned char nlow;
  volatile bool do_work;
  enum {
    HISTERISIS='h',
    STOP='s',
    IDLE='i',
    FAN_ONLY='f',
    RUN_COOL='c',
    RUN_WARM='w'
  } comp_state = HISTERISIS;
  bool prev_run;
  
  void handleIntr() {
    auto nowu = micros();
    unsigned long dtu = nowu - last_intr_us;
    last_intr_us = nowu;
    if (nlow >= NPULSE) nlow = NPULSE;
    pulse_vec[nlow] = dtu < 800;
    ++nlow;
    do_work = 1;
  }
  char state() {
    return comp_state;
  }
  unsigned char rooms() {
    return p[ROOM1] | (p[ROOM2] << 1) | (p[ROOM3] << 2) | (p[ROOM4] << 3);
  }
  void read_state() {
    if (!p[RUN]) {
      if (p[COOL] || p[HEAT]) {
        comp_state = IDLE;
      }else if (p[FAN_HIGH] || p[FAN_MID] || p[FAN_LOW]) {
        comp_state = FAN_ONLY;
      } else {
        comp_state = STOP;
      }
    } else {
      comp_state = p[COOL]?RUN_COOL:RUN_WARM;
    }
    Serial.printf("state: %c\n", comp_state);
  }
  byte comp_histerisis_cnt;
  void rst() {
    nlow = 0;
    switch (comp_state) {
    case HISTERISIS:
      if (p[RUN] == prev_run)
        ++comp_histerisis_cnt;
      if (comp_histerisis_cnt > 15)
        read_state();
      break;
    default:
      if (p[RUN] != prev_run) {
        comp_state = HISTERISIS;
        comp_histerisis_cnt = 0;
      } else {
        read_state();
      }
      break;
    }
    prev_run = p[RUN];
  }
  char p[NPULSE];
  void force_histerisis() {
    comp_state = HISTERISIS;
    comp_histerisis_cnt = 0;
  }
  void mloop() {
    unsigned long now = micros();
    if (do_work) {
      do_work = 0;
      last_work = now;
    } else {
      unsigned long dt = now - last_work;
      if (dt > 40000 && nlow) {
        if (nlow == 42) {
          if(memcmp(p, pulse_vec, sizeof p) != 0) {
            /*for (int n = 0; n < 45; ++n)
              if (p[n] != pulse_vec[n]) Serial.printf("%d: %d, ", n, pulse_vec[n]);
            Serial.println("");*/
            memcpy(p, pulse_vec, sizeof p);
          }
          //Serial.printf("%c %x\n", state(), (unsigned)rooms());
        }
        last_work = now;
        rst();
      }
    }
  }
};
LedProto ledProto;
//b4=615mV
//fan=756mV
//b2=660mV
//b3=700mV
//b1=731
enum ButtonsMV {
  mvb1=731,
  mvb2=660,
  mvb3=700,
  mvb4=615,
  mvfan=756,
  mvpower=2000,
};

void set_dac(long volt_x1000) {
  char addr = 0x60;
  uint16_t output = (volt_x1000*4095)/3300;
  Wire.beginTransmission(addr);
  Wire.write(MCP4726_CMD_WRITEDAC);
  Wire.write(output / 16);                   // Upper data bits          (D11.D10.D9.D8.D7.D6.D5.D4)
  Wire.write((output % 16) << 4);            // Lower data bits          (D3.D2.D1.D0.x.x.x.x)
  Wire.endTransmission();
}

void btoggle(int mv){
  set_dac(mv);
  delay(800);
  set_dac(0);
}

void handleInterrupt() {
  ledProto.handleIntr();  
}

WiFiUDP Udp;
unsigned long next_state_check;

void ac_toggle() {
  Serial.println("toggle");
  digitalWrite(AC_PIN, HIGH);
  delay(400);
  digitalWrite(AC_PIN, LOW);
}

void check_toggle() {
  if (Serial.available() && Serial.read() == 't')
    ac_toggle();
}

void do_display() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, String(ledProto.state()) + String(" ") + String(ac_on));
  display.drawString(0, 25, String(WiFi.RSSI()) + String("dB"));
  display.display();
}

void setup(void) {
  pinMode(led, OUTPUT);
  pinMode(AC_PIN, OUTPUT);
  pinMode(D7, INPUT);
  Wire.begin(D4, D3);

  attachInterrupt(digitalPinToInterrupt(D7), handleInterrupt, FALLING);
  //display.init();display.flipScreenVertically();

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);      // DHCP Hostname (useful for finding device for static lease)
  WiFi.config(staticIP, subnet, gateway, dns);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Udp.begin(7000);
}

char incoming[100];
void loop(void) {
  check_toggle();
  if (Udp.parsePacket()) {
    int len = Udp.read(incoming, sizeof incoming);
    incoming[1] = 0;
    if (len) {
      //Serial.println(incoming);
      switch(incoming[0]) {
      case '1':
        ac_on = true; break;
      case '0':
        ac_on = false; break;
      case 'a': bdr[0] = false; break;
      case 'A': bdr[0] = true; break;
      case 'b': bdr[1] = false; break;
      case 'B': bdr[1] = true; break;
      case 'c': bdr[2] = false; break;
      case 'C': bdr[2] = true; break;
      case 'd': bdr[3] = false; break;
      case 'D': bdr[3] = true; break;
      case 'f': fan_on = false; break;
      case 'F': fan_on = true; break;
      case 'o': fan_on = false; ac_on = false; break;
      case 'n': disable_ctrl = false; break;
      case 'N': disable_ctrl = true; break;
      }
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      char st = ledProto.state();
      Udp.write(&st, 1);
      Udp.endPacket();
    }
  }
  ledProto.mloop();
  if (next_state_check < millis()) {
    //do_display();
    next_state_check = millis() + 5000;
    Serial.println("*** State check:");
    auto rooms = ledProto.rooms();
    int bdrmv[] = {mvb1, mvb2, mvb3, mvb4};
    for (int i = 0; i < 4; ++i) {
      bool rd = rooms & (1<<i);
      Serial.printf("bdr %d need=%d read=%d\n", i, (int)(bdr[i]), (int)rd);
      if (rd != bdr[i]) {
        btoggle(bdrmv[i]);
      }
    }
    Serial.print("Fan state");
    if (!ac_on) {
      if (fan_on) {
        if (ledProto.state() != LedProto::FAN_ONLY) {
          btoggle(mvfan);
        }
      } else {
        if (ledProto.state() == LedProto::FAN_ONLY) {
          btoggle(mvpower);
        }
      }
    } else {
      Serial.println("OK");
    }
    Serial.print("AC state: ");
    if (ledProto.state() != LedProto::HISTERISIS) {
      if (ac_on && ledProto.state() != LedProto::RUN_COOL && ledProto.state() != LedProto::RUN_WARM && ledProto.state() != LedProto::IDLE) {
        Serial.println("should be on");
        ac_toggle();
        btoggle(mvpower);
        ledProto.force_histerisis();
      } else if (!ac_on && ledProto.state() != LedProto::STOP) {
        Serial.println("should be off");
        ac_toggle();
        btoggle(mvpower);
        ledProto.force_histerisis();
      } else {
        Serial.println("ok");
      }
    } else {
      Serial.println("histerisis");
    }
  }
}
