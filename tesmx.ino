#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <BH1750.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>
#include <UniversalTelegramBot.h>

// =====================================================
// WIFI
// =====================================================
const char* ssid     = "urwifi";
const char* password = "urpw";

// =====================================================
// TELEGRAM
// =====================================================
#define BOTtoken  "urtoken"
#define CHAT_ID   "urid"

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// =====================================================
// LCD & SENSOR
// =====================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
BH1750 lightMeter;

// =====================================================
// DFPLAYER
// =====================================================
DFRobotDFPlayerMini player;

// =====================================================
// RELAY
// =====================================================
#define RELAY1 25
#define RELAY2 26
#define RELAY3 27
#define RELAY4 14

// =====================================================
// LED
// =====================================================
#define LED_MERAH   32
#define LED_KUNING  33
#define LED_HIJAU    4

// =====================================================
// SENSOR SUARA (MAX4466)
// =====================================================
#define SENSOR_SUARA 34

// =====================================================
// THRESHOLD SENSOR SUARA (berbasis amplitudo peak-to-peak)
// Hasil kalibrasi lapangan:
//   - Tenang     : ~70-200
//   - Ada suara  : ~250-760
// =====================================================
const int THRESHOLD_BERISIK = 250;
const int THRESHOLD_NORMAL  = 150;

// Batas untuk membuang glitch/interferensi (lonjakan tidak wajar)
const int AMPLITUDO_MAKS_VALID = 1000;

// =====================================================
// INTERVAL WARNING
// =====================================================
const unsigned long warningInterval = 30000; // 30 detik
const unsigned long normalDelay     = 30000;  // normal stabil 30 detik

// =====================================================
// STATUS CAHAYA
// =====================================================
enum StatusCahaya {
  GELAP,
  REDUP,
  SEDANG,
  TERANG
};

// =====================================================
// VARIABEL GLOBAL
// =====================================================
int lux = 0;
int suara = 0;

bool berisik = false;

bool dfplayerReady = false;
bool bh1750Ready   = false;

bool warningAktif = false;

unsigned long lastWarningTime = 0;
unsigned long normalStartTime = 0;

unsigned long lastLCD    = 0;
unsigned long lastSerial = 0;

StatusCahaya kondisiCahaya = GELAP;

// =====================================================
// HELPER SENSOR TERHUBUNG (MAX4466 - cek variasi sinyal)
// =====================================================
bool isPinTerhubung(int pin, int samples = 20) {

  int mn = 4095;
  int mx = 0;

  for (int i = 0; i < samples; i++) {

    int v = analogRead(pin);

    if (v < mn) mn = v;
    if (v > mx) mx = v;

    delayMicroseconds(200);
  }

  return (mx - mn) > 0; // MAX4466 selalu punya noise kecil walau diam
}

// =====================================================
// KIRIM STATUS TELEGRAM
// =====================================================
void kirimStatusSensor(String triggerInfo = "") {

  if (WiFi.status() != WL_CONNECTED) return;

  String msg = "📊 *STATUS SENSOR*\n";

  if (triggerInfo.length() > 0) {
    msg += "🔔 _" + triggerInfo + "_\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━\n";

  // BH1750
  msg += "💡 BH1750 : ";

  if (bh1750Ready) {
    msg += "✅ Siap | Lux: " + String(lux) + "\n";
  } else {
    msg += "❌ Tidak Terdeteksi\n";
  }

  // Sensor suara
  msg += "🎤 Sensor Suara : ";

  if (isPinTerhubung(SENSOR_SUARA)) {
    msg += "✅ Siap | Amplitudo: " + String(suara) + "\n";
  } else {
    msg += "⚠️ Tidak Stabil\n";
  }

  // DFPlayer
  msg += "🔊 DFPlayer : ";

  if (dfplayerReady) {
    msg += "✅ Siap\n";
  } else {
    msg += "❌ Tidak Terdeteksi\n";
  }

  bot.sendMessage(CHAT_ID, msg, "Markdown");
}

// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  // =====================================================
  // I2C
  // =====================================================
  Wire.begin(21, 22);

  // =====================================================
  // LCD
  // =====================================================
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Memulai...");

  // =====================================================
  // BH1750
  // =====================================================
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {

    bh1750Ready = true;

    Serial.println("BH1750 SIAP");

  } else {

    bh1750Ready = false;

    Serial.println("BH1750 GAGAL");
  }

  // =====================================================
  // DFPLAYER
  // =====================================================
  Serial2.begin(9600, SERIAL_8N1, 17, 16);

  delay(1500);

  if (player.begin(Serial2, true, false)) {

    dfplayerReady = true;

    player.volume(25);

    Serial.println("DFPlayer SIAP");

  } else {

    dfplayerReady = false;

    Serial.println("DFPlayer GAGAL");
  }

  // =====================================================
  // RELAY
  // =====================================================
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);

  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);
  digitalWrite(RELAY4, HIGH);

  // =====================================================
  // LED
  // =====================================================
  pinMode(LED_MERAH, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_HIJAU, OUTPUT);

  // =====================================================
  // SENSOR SUARA (MAX4466 analog input)
  // =====================================================
  pinMode(SENSOR_SUARA, INPUT);

  // =====================================================
  // WIFI
  // =====================================================
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);

  client.setInsecure();

  lcd.clear();
  lcd.print("Connecting...");

  int retry = 0;

  while (WiFi.status() != WL_CONNECTED && retry < 20) {

    delay(500);

    Serial.print(".");

    retry++;
  }

  lcd.clear();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("\nWiFi Connected");

    lcd.print("WiFi Connected");

    bot.sendMessage(
      CHAT_ID,
      "🟢 *System Online — ESP32*",
      "Markdown"
    );

    delay(300);

    kirimStatusSensor("Startup Sistem");

  } else {

    Serial.println("\nWiFi Gagal");

    lcd.print("WiFi Failed");
  }

  delay(2000);

  lcd.clear();
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  // =====================================================
  // BACA CAHAYA
  // =====================================================
  if (bh1750Ready) {

    int bacaLux = (int)lightMeter.readLightLevel();

    if (bacaLux < 0 || bacaLux > 65000) {
      lux = 0;
    } else {
      lux = bacaLux;
    }
  }

  // =====================================================
  // BACA SENSOR SUARA (MAX4466 - amplitudo peak-to-peak)
  // =====================================================
  int sampleMin = 4095;
  int sampleMax = 0;

  unsigned long sampleStart = millis();

  while (millis() - sampleStart < 20) { // window sampling 20ms

    int sample = analogRead(SENSOR_SUARA);

    if (sample < sampleMin) sampleMin = sample;
    if (sample > sampleMax) sampleMax = sample;
  }

  int amplitudoBaru = sampleMax - sampleMin;

  // Buang glitch/interferensi (misal dari relay/DFPlayer switching)
  if (amplitudoBaru <= AMPLITUDO_MAKS_VALID) {
    suara = amplitudoBaru;
  }
  // jika di luar batas wajar, pertahankan nilai "suara" sebelumnya

  // =====================================================
  // HYSTERESIS SUARA
  // =====================================================
  if (suara >= THRESHOLD_BERISIK) {

    berisik = true;
  }
  else if (suara <= THRESHOLD_NORMAL) {

    berisik = false;
  }

  // =====================================================
  // HYSTERESIS CAHAYA
  // =====================================================
  switch (kondisiCahaya) {

    case GELAP:

      if (lux > 120)
        kondisiCahaya = REDUP;

      break;

    case REDUP:

      if (lux < 80)
        kondisiCahaya = GELAP;

      else if (lux > 270)
        kondisiCahaya = SEDANG;

      break;

    case SEDANG:

      if (lux < 230)
        kondisiCahaya = REDUP;

      else if (lux > 330)
        kondisiCahaya = TERANG;

      break;

    case TERANG:

      if (lux < 280)
        kondisiCahaya = SEDANG;

      break;
  }

  // =====================================================
  // OUTPUT RELAY & LED
  // =====================================================
  if (kondisiCahaya == GELAP) {

    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(LED_KUNING, LOW);
    digitalWrite(LED_HIJAU, LOW);

    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    digitalWrite(RELAY3, LOW);
    digitalWrite(RELAY4, LOW);
  }

  else if (kondisiCahaya == REDUP) {

    digitalWrite(LED_MERAH, LOW);
    digitalWrite(LED_KUNING, LOW);
    digitalWrite(LED_HIJAU, LOW);

    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    digitalWrite(RELAY3, LOW);
    digitalWrite(RELAY4, LOW);
  }

  else if (kondisiCahaya == SEDANG) {

    digitalWrite(LED_MERAH, LOW);
    digitalWrite(LED_KUNING, HIGH);
    digitalWrite(LED_HIJAU, LOW);

    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, HIGH);
    digitalWrite(RELAY3, LOW);
    digitalWrite(RELAY4, LOW);
  }

  else if (kondisiCahaya == TERANG) {

    digitalWrite(LED_MERAH, LOW);
    digitalWrite(LED_KUNING, LOW);
    digitalWrite(LED_HIJAU, HIGH);

    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, HIGH);
    digitalWrite(RELAY3, HIGH);
    digitalWrite(RELAY4, HIGH);
  }

  // =====================================================
  // SPEAKER CONTROL ANTI SPAM
  // =====================================================

  if (dfplayerReady) {

    // ================= BERISIK =================
    if (berisik) {

      // reset timer normal
      normalStartTime = 0;

      // pertama kali warning
      if (!warningAktif) {

        Serial.println("WARNING PERTAMA");

        player.play(1);

        warningAktif = true;

        lastWarningTime = millis();
      }

      // repeat tiap interval
      else if (millis() - lastWarningTime >= warningInterval) {

        Serial.println("WARNING ULANG");

        player.play(1);

        lastWarningTime = millis();
      }
    }

    // ================= NORMAL =================
    else {

      // mulai hitung waktu normal
      if (normalStartTime == 0) {

        normalStartTime = millis();
      }

      // reset jika benar-benar normal
      if (millis() - normalStartTime >= normalDelay) {

        warningAktif = false;
      }
    }
  }

  // =====================================================
  // LCD
  // =====================================================
  if (millis() - lastLCD >= 300) {

    lcd.setCursor(0, 0);
    lcd.print("Lux:");
    lcd.print(lux);
    lcd.print("     ");

    lcd.setCursor(0, 1);

    if (berisik) {

      lcd.print("Suara:BERISIK");
    }
    else {

      lcd.print("Suara:NORMAL ");
    }

    lastLCD = millis();
  }

  // =====================================================
  // SERIAL MONITOR
  // =====================================================
  if (millis() - lastSerial >= 1000) {

    Serial.printf(
      "Lux:%d | Amp:%d | Berisik:%s | Warning:%s\n",
      lux,
      suara,
      berisik ? "YA" : "TIDAK",
      warningAktif ? "AKTIF" : "OFF"
    );

    lastSerial = millis();
  }

  delay(20);
}
