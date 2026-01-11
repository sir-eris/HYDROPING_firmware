/* ---------- LIBRARIES ---------- */
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>


/* ---------- DEFINITIONS ---------- */
#define LIS3DH_ADDR 0x18
#define LIS3DH_INT1_PIN 2
#define LIS3DH_INT2_PIN 1
#define SDA_PIN 3
#define SCL_PIN 4
#define TOUCH_1 12
// Custom service & characteristic UUIDs
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"      // service container
#define CREDENTIALS_UUID "12345678-1234-1234-1234-123456789abd"  // write Wi-Fi credentials
#define STATUS_UUID "12345678-1234-1234-1234-123456789abe"       // notify status back


/* ---------- Object initializations ---------- */
// Ticker restartTicker;
Preferences prefs;
BLECharacteristic *pStatusChar;
// pre-warm-up
String homeSSID, homePASS, userID, deviceToken;


/* ---------- Device modes and configurations ---------- */
bool DI = false;
// bool APModeActive = false;
unsigned long APStartMillis = 0;
// known at complie time
constexpr const char *H_V = "1.0";
constexpr const char *F_V = "1.0";
constexpr const char *BLE_SSID = "HydroPing-PG1A2B3F";
// constexpr const char *AP_PASS = "";
constexpr unsigned long long SETUP_TIMEOUT_MS = 2ULL * 60 * 1000;  // 2 min setup mode


/* ---------- Persisit through deep sleep ---------- */
RTC_DATA_ATTR bool isDisconnected = false;
RTC_DATA_ATTR bool ISM = false;
RTC_DATA_ATTR uint64_t deepSleepTimeOut = 12ULL * 60ULL * 60ULL * 1000000ULL;
RTC_DATA_ATTR uint64_t minSleepInterval = 60ULL * 60ULL * 1000000ULL;
RTC_DATA_ATTR uint64_t maxSleepInterval = 24ULL * 60ULL * 60ULL * 1000000ULL;


// pre-define
bool connectToWiFi();
bool AINS(const String &payload);


void sendStatus(bool hasError, const char* msg) {
    String json = String("{\"action\":\"connectWiFi\",\"hasError\":") + (hasError ? "true" : "false") + ",\"errorMessage\":\"" + msg + "\"}";

    pStatusChar->setValue(json);
    pStatusChar->notify();
}

class CredentialsCallbacks : public BLECharacteristicCallbacks {
public:
  CredentialsCallbacks(BLECharacteristic *statusChar) {
    pStatusChar = statusChar;
  }

  void onWrite(BLECharacteristic *pChar) override {
    if (!pChar) return;
    
    String value = pChar->getValue();

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, value);
    
    if (error) {
      sendStatus(true, "invalid_request");
      return;
    }

    String action = doc["action"] | "";
    
    if (action == "connectWiFi") {

      homeSSID = doc["ssid"].as<String>();
      homePASS = doc["password"].as<String>();
      userID = doc["userid"].as<String>();
      deviceToken = doc["devicetoken"].as<String>();

      if (homeSSID.isEmpty() || homePASS.isEmpty() || userID.isEmpty() || deviceToken.isEmpty()) {

        sendStatus(true, "missing_parameters");
        return;
      }

      prefs.begin("wifi", false);
      prefs.putString("ssid", homeSSID);
      prefs.putString("pass", homePASS);
      prefs.putString("userid", userID);
      prefs.putString("devicetoken", deviceToken);
      prefs.end();

      // Serial.printf("Connecting to Wi-Fi: %s / %s / %s\n", homeSSID.c_str(), homePASS.c_str(), userID.c_str());

      if (connectToWiFi()) {
        
        isDisconnected = false;

        sendStatus(false, "");

        delay(250);

        DI = true;

        return;
      } else {
        
        sendStatus(true, "wifi_error");
        return;
      }
    } else {
      
      sendStatus(true, "unknown_action");
      return;
    }
  }
};

/* ---------- LIS3DH functions ---------- */
// I. writeRegister
// input (uint8_t, uint8_t): seletcted register, specific value
// output (void): use I2C to write a configuration to the chip at specific register
void writeRegister(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LIS3DH_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// II. readRegister
// input (uint8_t): seletcted register
// output (void): use I2C to read a configuration from the chip at specific register
uint8_t readRegister(uint8_t reg) {
  Wire.beginTransmission(LIS3DH_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);  // Restart
  Wire.requestFrom(LIS3DH_ADDR, (uint8_t)1);
  return Wire.read();
}

// II. initLIS3DH
// input (): N/A
// output (void): initialize the chip
void initLIS3DH() {
  writeRegister(0x20, 0x47);  // CTRL_REG1: 50Hz, XYZ enabled
  writeRegister(0x21, 0x10);  // CTRL_REG2: HPF disabled (simplify for now)
  writeRegister(0x22, 0x40);  // CTRL_REG3: route INT1 to INT1 pin
  writeRegister(0x23, 0x10);  // CTRL_REG4: ±4g scale, high resolution
  writeRegister(0x24, 0x08);  // CTRL_REG5: latch interrupt on INT1
  writeRegister(0x30, 0x2A);  // INT1_CFG: enable XH, YH, ZH
  writeRegister(0x32, 0x47);  // INT1_THS: threshold ~0.5g (adjust later)
  writeRegister(0x33, 0x05);  // INT1_DURATION: 1 count (20ms at 50Hz)
  (void)readRegister(0x31);
}


/* ---------- WiFi function ---------- */
// I. startBLE
// input (): N/A
// output (void): activate setup mode, start async webserver, listen and process
// endpoints: /info, /connect
void startBLE() {
  // APModeActive = true;
  // APStartMillis = millis();
  WiFi.mode(WIFI_STA);

  delay(500);

  // 1. Initialize BLE
  BLEDevice::init(BLE_SSID);

  // 2. Create BLE server
  BLEServer *pServer = BLEDevice::createServer();

  // 3. Create BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 4a. Status characteristic
  pStatusChar = pService->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);

  // 4b. Credentials characteristic
  BLECharacteristic *pCredentialsChar = pService->createCharacteristic(
    CREDENTIALS_UUID,
    BLECharacteristic::PROPERTY_WRITE);
  pCredentialsChar->setCallbacks(new CredentialsCallbacks(pStatusChar));

  // 5. Start the service
  pService->start();

  // 6. Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE initialized and advertising started!");
}

// II. stopBLE
// input (): N/A
// output (void): deactivate setup mode
void stopBLE() {
  // WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(750);

  BLEDevice::deinit(true);

  delay(250);

  DI = false;
  // APModeActive = false;
}

// III. connectToWiFi
// input (): N/A
// output (bool): try to connect to wifi and return success state
// NOTE: wifi mode set externally before calling this function
bool connectToWiFi() {
  prefs.begin("wifi", true);
  homeSSID = prefs.getString("ssid", "");
  homePASS = prefs.getString("pass", "");
  prefs.end();

  if (homeSSID.isEmpty() || homePASS.isEmpty()) return false;

  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.begin(homeSSID.c_str(), homePASS.c_str());

  for (int i = 0; i < 20; ++i) {  // ≈10 s timeout
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(250);
  }

  WiFi.disconnect(true);   // drop connection + erase STA state
  WiFi.mode(WIFI_OFF);
  
  delay(250);
  
  return false;
}

/* ---------- Backend data communication functions ---------- */
// I. readTouchAvg
// input (int): hardware touch pin number
// output (uint32_t): avg the capacitive level for some (samples) at some (5 microsecond) interval
uint32_t readTouchAvg(int pin, int samples = 8) {
  uint32_t total = 0;
  for (int i = 0; i < samples; i++, delay(5)) total += touchRead(pin);
  return total / samples;
}

// II. sendDataToDB
// input (String, unit32_t): hardware MacAddress, re
// output (void): send moisture value to backend, check and handle resposnse, call to perform any instructions in the response payload
void SDTDB(String macAddress, uint32_t moisture) {
  prefs.begin("wifi", true);
  deviceToken = prefs.getString("devicetoken", "");
  prefs.end();

  if (deviceToken.isEmpty()) {
    return;
  }

  HTTPClient http;
  http.begin("https://q15ur4emu9.execute-api.us-east-2.amazonaws.com/default/enterProbeReading");
  http.addHeader("Authorization", "Bearer " + deviceToken);
  http.addHeader("Content-Type", "application/json");
  String js = "{\"moisture\":" + String(moisture) + "}";

  int cd = http.POST(js);
  yield();

  if (cd > 0) {
    if (cd > 199 && cd < 400) {
      String pl = http.getString();
      yield();

      Serial.println(pl);

      bool success = AINS(pl);  // perform pre-define changes given as intructions in the response payload

      // if (!success) {
        // parse and save or call another API to report
      // }
    }
    // else {
      // parse and save or call another API to report
      // Serial.println("error http code");
    // }

    http.end();
    return;
  }

  // Serial.println("http code error");

  http.end();
  return;
}

// III. AINS
// input (String): API JSON response payload
// output (bool): check payload, execute small snippets based on defined keys in the payload
// bool AINS(String payload) {
bool AINS(const String &payload) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (!error) {
    // Check if the response contains specific key-action pairs
    if (doc.containsKey("deviceToken")) {  // deviceToken: renew JWT token
      String deviceToken = doc["deviceToken"];
      prefs.begin("wifi", false);
      prefs.putString("devicetoken", deviceToken);
      prefs.end();
    } else if (doc.containsKey("sleepTimeout")) {  // sleepTimeout: change deep sleep cycle timeout
      // expected in microseconds
      uint64_t newTimeout = doc["sleepTimeout"];

      if (newTimeout >= minSleepInterval && newTimeout <= maxSleepInterval) {  // safety check: larger than 1 hrs, less than 24 hrs
        deepSleepTimeOut = newTimeout;
      }
    } else if (doc.containsKey("disconnected")) {  // disconnected: set device to disconnected and skip furthuer readings
      isDisconnected = true;
    } else if (doc.containsKey("deletedUser")) {  // deletedUser: the owner user of the device is deleted, set device to disconnected and skip furthuer readings
      isDisconnected = true;
    }

    return true;
  }

  return false;
}

// IV. SNSR
// input (): N/A
// output (void): call readTouchAvg, set wifi mode to STA, connect to wifi, get hardware MacAddress, call sendDataToDB
void SNSR() {
  if (isDisconnected) return;

  uint32_t moisture = readTouchAvg(TOUCH_1);

  delay(250);

  WiFi.mode(WIFI_STA);

  delay(250);

  if (connectToWiFi()) {
    String macAddress = WiFi.macAddress();
    SDTDB(macAddress, moisture);
  }
  else {
    Serial.println("Couldnt connect to wifi");
  }

  // turn wifi off
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(750);
}


/* ---------- Deep sleep functions ---------- */
// I. scheduleNextSleep
// input (): N/A
// output (void): initiate deep sleep timer, allow interruption by specific hardware pins
void SNS() {
  Serial.println(("sleeping!"));

  esp_sleep_enable_ext0_wakeup((gpio_num_t)LIS3DH_INT1_PIN, 1);
  esp_sleep_enable_timer_wakeup(deepSleepTimeOut);
  esp_deep_sleep_start();
}


/* ---------- ESP32 STATIS FUNCTIONS ---------- */
// I. setup
// input (): N/A
// output (void): initialize hardware layers, communicate to backend & go back to sleep, allow setup mode loop cycle
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.setDebugOutput(true);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN); // initialize I2C
  pinMode(LIS3DH_INT1_PIN, INPUT_PULLUP); // activate preferral
  delay(20);
  initLIS3DH(); // initialize preferral
  delay(20);

  // deep sleep interrupted, triggered by specififc pin
  bool WFS = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);

  // loop cycle allwed during setup mode
  if (WFS && !ISM) {

    Serial.println(("Shook!"));

    ISM = true;

    startBLE();

    delay(500);

    unsigned long st = millis();

    while (!DI && millis() - st < SETUP_TIMEOUT_MS) {
      delay(200);
    }

    stopBLE();

    delay(500);

    ISM = false;

    SNS();
  }

  // communicate to backend & go back to sleep
  SNSR();
  SNS();
}


// II. loop
// input (): N/A
// output (void): N/A
void loop() {}
