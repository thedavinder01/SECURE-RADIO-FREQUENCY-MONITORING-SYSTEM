
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Adafruit_Fingerprint.h>
#include <RTClib.h>
#include <LoRa.h>
#include <RF24.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 oled(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ================= R307 =================
#define FINGER_RX 16
#define FINGER_TX 17

HardwareSerial FingerSerial(2);
Adafruit_Fingerprint finger(&FingerSerial);

// ================= SX1278 =================
#define LORA_SCK   14
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_CS     5
#define LORA_RST    2
#define LORA_DIO0  26

#define LORA_FREQ 433E6

// ================= SD =================
#define SD_CS 13

// ================= NRF24 =================
#define NRF_CE  27
#define NRF_CSN 4

RF24 radio(NRF_CE, NRF_CSN);

const byte radioAddress[6] = "NODE1";

// ================= RTC =================
RTC_DS3231 rtc;

// ================= 7 SEGMENT =================
// Example only.
// Change these according to your 3642BS pinout.
#define SEG_A 25
#define SEG_B 32
#define SEG_C 33
#define SEG_D 12
#define SEG_E 15
#define SEG_F 0
#define SEG_G 34

#define DIGIT1 35
#define DIGIT2 36
#define DIGIT3 39
#define DIGIT4 1

// ================= STATUS =================
bool authenticated = false;

int wifiCount = 0;
int bleCount = 0;

long lastLoRaRSSI = 0;

unsigned long lastScan = 0;
unsigned long lastDisplay = 0;

// =================================================
// OLED helper
// =================================================

void oledMessage(String line1, String line2 = "", String line3 = "") {

  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.println(line1);

  oled.setCursor(0, 20);
  oled.println(line2);

  oled.setCursor(0, 40);
  oled.println(line3);

  oled.display();
}


// =================================================
// SD LOG
// =================================================

void logToSD(String eventText) {

  if (!SD.begin(SD_CS)) {
    return;
  }

  DateTime now = rtc.now();

  File file = SD.open("/system_log.txt", FILE_APPEND);

  if (file) {

    file.print(now.year());
    file.print("-");

    file.print(now.month());
    file.print("-");

    file.print(now.day());

    file.print(" ");

    file.print(now.hour());
    file.print(":");

    file.print(now.minute());
    file.print(":");

    file.print(now.second());

    file.print(" | ");

    file.println(eventText);

    file.close();
  }
}


// =================================================
// FINGERPRINT
// =================================================

bool checkFingerprint() {

  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK) {
    return false;
  }

  p = finger.image2Tz();

  if (p != FINGERPRINT_OK) {
    return false;
  }

  p = finger.fingerSearch();

  if (p == FINGERPRINT_OK) {

    authenticated = true;

    String msg =
      "Fingerprint ID: " +
      String(finger.fingerID);

    oledMessage(
      "ACCESS GRANTED",
      msg,
      "System unlocked"
    );

    logToSD(msg);

    delay(1500);

    return true;
  }

  oledMessage(
    "ACCESS DENIED",
    "Fingerprint",
    "not recognized"
  );

  logToSD("ACCESS DENIED");

  delay(1500);

  return false;
}


// =================================================
// WIFI SCANNER
// =================================================

void scanWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.disconnect();

  delay(100);

  wifiCount = WiFi.scanNetworks();

  Serial.println();
  Serial.println("===== WIFI SCAN =====");

  for (int i = 0; i < wifiCount; i++) {

    Serial.print(i + 1);
    Serial.print("  ");

    Serial.print(WiFi.SSID(i));

    Serial.print("  RSSI:");

    Serial.print(WiFi.RSSI(i));

    Serial.print("  CH:");

    Serial.println(WiFi.channel(i));
  }

  logToSD(
    "WiFi networks detected: " +
    String(wifiCount)
  );

  WiFi.scanDelete();
}


// =================================================
// BLE SCAN
// =================================================

void scanBLE() {

  // BLE scanning can be added using the
  // ESP32 BLE APIs corresponding to the
  // installed ESP32 board package.

  Serial.println();
  Serial.println("===== BLE MONITOR =====");

  Serial.println(
    "BLE scan requested"
  );

  logToSD("BLE scan performed");
}


// =================================================
// LORA
// =================================================

void setupLoRa() {

  SPI.begin(
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    LORA_CS
  );

  LoRa.setPins(
    LORA_CS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQ)) {

    Serial.println(
      "LoRa initialization failed"
    );

    oledMessage(
      "LORA ERROR",
      "Check SX1278"
    );

    return;
  }

  LoRa.setTxPower(17);

  Serial.println(
    "LoRa initialized"
  );
}


// =================================================
// LORA RECEIVE
// =================================================

void checkLoRa() {

  int packetSize = LoRa.parsePacket();

  if (packetSize) {

    String message = "";

    while (LoRa.available()) {

      message +=
        (char)LoRa.read();
    }

    lastLoRaRSSI =
      LoRa.packetRssi();

    Serial.println();
    Serial.println("===== LORA =====");

    Serial.print("Message: ");
    Serial.println(message);

    Serial.print("RSSI: ");
    Serial.println(lastLoRaRSSI);

    logToSD(
      "LoRa RX: " +
      message +
      " RSSI=" +
      String(lastLoRaRSSI)
    );

    oledMessage(
      "LORA RECEIVED",
      message,
      "RSSI: " +
      String(lastLoRaRSSI)
    );

    delay(1200);
  }
}


// =================================================
// LORA SEND
// =================================================

void sendLoRaMessage(String message) {

  if (!authenticated) {

    Serial.println(
      "Fingerprint required"
    );

    return;
  }

  LoRa.beginPacket();

  LoRa.print(message);

  LoRa.endPacket();

  Serial.print(
    "LoRa TX: "
  );

  Serial.println(message);

  logToSD(
    "LoRa TX: " +
    message
  );
}


// =================================================
// NRF24
// =================================================

void setupNRF24() {

  if (!radio.begin()) {

    Serial.println(
      "NRF24 not detected"
    );

    return;
  }

  radio.setPALevel(
    RF24_PA_LOW
  );

  radio.setDataRate(
    RF24_1MBPS
  );

  radio.openWritingPipe(
    radioAddress
  );

  radio.stopListening();

  Serial.println(
    "NRF24 initialized"
  );
}


// =================================================
// NRF24 SEND
// =================================================

void sendNRF24(String message) {

  if (!authenticated) {

    Serial.println(
      "Fingerprint required"
    );

    return;
  }

  char data[32];

  message.toCharArray(
    data,
    sizeof(data)
  );

  radio.write(
    &data,
    sizeof(data)
  );

  Serial.print(
    "NRF24 TX: "
  );

  Serial.println(message);

  logToSD(
    "NRF24 TX: " +
    message
  );
}


// =================================================
// 7 SEGMENT
// =================================================

void setupSevenSegment() {

  pinMode(SEG_A, OUTPUT);
  pinMode(SEG_B, OUTPUT);
  pinMode(SEG_C, OUTPUT);
  pinMode(SEG_D, OUTPUT);
  pinMode(SEG_E, OUTPUT);
  pinMode(SEG_F, OUTPUT);
  pinMode(SEG_G, OUTPUT);

  /*
     IMPORTANT:

     GPIO34, GPIO35, GPIO36 and GPIO39
     are input-only on ESP32.

     Therefore do NOT use them as digit
     output pins.

     Use output-capable GPIOs after checking
     your complete wiring.
  */
}


// =================================================
// SETUP
// =================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "SECURE RF MONITOR"
  );

  // ---------- I2C ----------
  Wire.begin(
    21,
    22
  );

  // ---------- OLED ----------
  if (!oled.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
      )) {

    Serial.println(
      "OLED failed"
    );
  }

  oledMessage(
    "SECURE RF MONITOR",
    "Starting..."
  );

  // ---------- RTC ----------
  if (!rtc.begin()) {

    Serial.println(
      "RTC not detected"
    );

  } else {

    if (rtc.lostPower()) {

      rtc.adjust(
        DateTime(F(__DATE__),
                 F(__TIME__))
      );
    }
  }

  // ---------- Fingerprint ----------
  FingerSerial.begin(
    57600,
    SERIAL_8N1,
    FINGER_RX,
    FINGER_TX
  );

  finger.begin(57600);

  if (finger.verifyPassword()) {

    Serial.println(
      "R307 detected"
    );

  } else {

    Serial.println(
      "R307 not detected"
    );

    oledMessage(
      "FINGERPRINT ERROR",
      "Check R307 wiring"
    );
  }

  // ---------- SD ----------
  if (SD.begin(SD_CS)) {

    Serial.println(
      "SD card OK"
    );

    logToSD(
      "SYSTEM START"
    );

  } else {

    Serial.println(
      "SD card failed"
    );
  }

  // ---------- LoRa ----------
  setupLoRa();

  // ---------- NRF ----------
  setupNRF24();

  // ---------- 7 Segment ----------
  setupSevenSegment();

  oledMessage(
    "SYSTEM LOCKED",
    "Place finger",
    "to continue"
  );

  Serial.println(
    "SYSTEM LOCKED"
  );
}


// =================================================
// LOOP
// =================================================

void loop() {

  // ==========================================
  // FINGERPRINT GATE
  // ==========================================

  if (!authenticated) {

    checkFingerprint();

    delay(300);

    return;
  }


  // ==========================================
  // SYSTEM UNLOCKED
  // ==========================================

  if (millis() - lastScan > 15000) {

    lastScan = millis();

    scanWiFi();

    scanBLE();
  }


  // ==========================================
  // LORA RECEIVE
  // ==========================================

  checkLoRa();


  // ==========================================
  // OLED STATUS
  // ==========================================

  if (millis() - lastDisplay > 3000) {

    lastDisplay = millis();

    DateTime now =
      rtc.now();

    oled.clearDisplay();

    oled.setTextSize(1);

    oled.setTextColor(
      SSD1306_WHITE
    );

    oled.setCursor(0, 0);

    oled.println(
      "SYSTEM: UNLOCKED"
    );

    oled.setCursor(0, 12);

    oled.print(
      "WiFi: "
    );

    oled.println(
      wifiCount
    );

    oled.setCursor(0, 24);

    oled.print(
      "BLE: "
    );

    oled.println(
      bleCount
    );

    oled.setCursor(0, 36);

    oled.print(
      "LoRa: "
    );

    oled.print(
      lastLoRaRSSI
    );

    oled.println(
      " dBm"
    );

    oled.setCursor(0, 48);

    oled.print(
      now.hour()
    );

    oled.print(":");

    if (now.minute() < 10)
      oled.print("0");

    oled.print(
      now.minute()
    );

    oled.display();
  }
}