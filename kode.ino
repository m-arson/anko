#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

#define IN_SUHU     35
#define IN_TEKANAN  34
#define OUT_PEMANAS 33
#define OUT_KATUP   25
#define OUT_SPRAY   26
#define AVG         (250)
#define ON          (1)
#define OFF         (2)
#define INTERVAL    (1000)

WiFiClient wificlient;
PubSubClient mqtt(wificlient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long timer, last, mqtt_reconnect_attempt;

byte last_spray,
     last_katup,
     last_update;

const char* ssid = "Tepi Cerita";
const char* password = "kalausayangbilang";

byte set_interval_spray = 4,
     set_interval_katup = 16;

float c1 = 1.009249522e-03,
      c2 = 2.378405444e-04,
      c3 = 2.019202697e-07;
float R1 = 10000;
float kalibrasi = 1.80;

byte treshold_suhu = 35;
float treshold_tekanan = 0.00;

float get_suhu() {
  float val = 0;
  size_t i;

  for (i = 0; i < AVG; ++i) {
    int adc_val = analogRead(IN_SUHU);
    float R2 = R1 * (1023.0 / (float) adc_val - 1.0);
    float logR2 = log(R2);
    float celcius = (1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2)) - 273.15 + kalibrasi;
    val += celcius;
    delay(1);
  }

  val /= AVG;
  return val;
}

float get_tekanan() {
  float val = 0;
  size_t i;

  for (i = 0; i < AVG; ++i) {
    val += analogRead(IN_TEKANAN);
    delay(1);
  }
  val /= (AVG * 10.00);
  val -= treshold_tekanan;
  if (val < 0) val = 0.00;
  return val;
}

void set_output(byte pin, byte state) {
  if (state == ON) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  else pinMode(pin, INPUT);
}

boolean reconnect() {
  if (mqtt.connect("andi-elektro-unhas-2022"))
    mqtt.subscribe("andi-elektro-unhas-2022/recv");
  return mqtt.connected();
}

void callback(char* unusedtopic, byte* payload, unsigned int length) {
  char cmd = payload[0];
  String val = "";
  size_t i;
  for (i = 1; i < length; ++i)
    val += String((char) payload[i]);
  switch (cmd) {
    case 'K':
      set_interval_katup = val.toInt();
      break;
    case 'S':
      set_interval_spray = val.toInt();
      break;
    case 'T':
      treshold_suhu = val.toInt();
      break;
  }
}

void setup() {
  analogReadResolution(10);

  pinMode(IN_SUHU, INPUT);
  pinMode(IN_TEKANAN, INPUT);

  set_output(OUT_PEMANAS, OFF);
  set_output(OUT_KATUP, OFF);
  set_output(OUT_SPRAY, OFF);

  Serial.begin(115200);
  delay(1000);

  lcd.init();
  lcd.init();
  lcd.backlight();
  lcd.clear();

  treshold_tekanan = get_tekanan();
  mqtt_reconnect_attempt = 0;
  timer = 0;
  last = 0;
  last_spray = 0;
  last_katup = 0;
  last_update = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting...");
  }
  Serial.printf("\nConnected : IP : %s\n", WiFi.localIP());

  mqtt.setServer("broker.hivemq.com", 1883);
  mqtt.setCallback(callback);
}

void loop() {
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - mqtt_reconnect_attempt > 5000) {
      mqtt_reconnect_attempt = now;
      if (reconnect()) mqtt_reconnect_attempt = 0;
    }
  }
  else mqtt.loop();

  byte interval_spray = 60 / set_interval_spray;
  byte interval_katup = 60 / set_interval_katup;

  timer = millis();

  if (timer - last >= INTERVAL) {
    ++last_spray;
    ++last_katup;
    ++last_update;
  }

  float cek_suhu = get_suhu();
  if (cek_suhu >= treshold_suhu) set_output(OUT_PEMANAS, OFF);
  else set_output(OUT_PEMANAS, ON);

  if (last_spray >= interval_spray && last_spray <= interval_spray + 5)
    set_output(OUT_SPRAY, ON);
  else if (last_spray >= interval_spray + 5) {
    set_output(OUT_SPRAY, OFF);
    last_spray = 0;
  }

  if (last_katup >= interval_katup && last_katup <= interval_katup + 2)
    set_output(OUT_KATUP, ON);
  else if (last_katup >= interval_katup + 2) {
    set_output(OUT_KATUP, OFF);
    last_katup = 0;
  }
  if (last_update >= 2) {
    last_update = 0;
    float suhu = get_suhu();
    float tekanan = get_tekanan();
    lcd.setCursor(0, 0);
    lcd.print("Suhu    : ");
    lcd.print(suhu);
    lcd.print(" ");
    lcd.setCursor(0, 1);
    lcd.print("Tekanan : ");
    lcd.print(tekanan);
    lcd.print(" ");
    char msg[60];
    snprintf(msg, 60, "{\"suhu\":%.2f,\"tekanan\":%.2f,\"is\":%d,\"ik\":%d,\"ts\":%d}", suhu, tekanan, set_interval_spray, set_interval_katup, treshold_suhu);
    mqtt.publish("andi-elektro-unhas-2022/send", msg);
    Serial.printf("Suhu: %.2f degC, Tekanan: %.2f cmH2O\n", suhu, tekanan);
  }

}
