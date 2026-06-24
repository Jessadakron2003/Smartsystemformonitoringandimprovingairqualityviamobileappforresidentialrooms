// ======= Blynk Template (MUST BE FIRST!) =======
#define BLYNK_TEMPLATE_ID "****"
#define BLYNK_TEMPLATE_NAME "****"
#define BLYNK_AUTH_TOKEN "****"

#include <HardwareSerial.h>
#include <DHT.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <HTTPClient.h> //  <-- For ThingsSpeak
#include <MHZ19.h>

// --- Credentials ---
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "****";
char pass[] = "****";

// ======= Telegram Configuration =======
#define BOTtoken "****" //  
#define CHAT_ID "****" //  

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// ======= Global Variables =======
bool telegramEnabled = false;
unsigned long lastTelegramTest = 0;
unsigned long lastCO2Reset = 0;
int consecutive_invalid_readings = 0;
#define RESET_THRESHOLD 5
#define CO2_RESET_INTERVAL 60000

float oxygen_percent = 0.0;

HardwareSerial pmsSerial(2);
struct pmsData {
  uint16_t pm1_0_standard;
  uint16_t pm2_5_standard;
  uint16_t pm10_standard;
};
pmsData data;

#define OXYGEN_PIN 35
#define VREF 3.3
#define OXYGEN_MAX_VOLTAGE 2.0

#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define MHZ19_RX 16
#define MHZ19_TX 17
HardwareSerial co2Serial(1);
MHZ19 myMHZ19;

// CO2 Moving Average
#define CO2_SAMPLES 10
int co2_values[CO2_SAMPLES] = {0};
int co2_index = 0;
int co2_count = 0;
int co2_last_valid = 400;
bool co2_initialized = false;
int consecutive_400_readings = 0;

#define RELAY1 25
#define RELAY2 26
float tempThreshold = 33.0;
bool relayOn = false;
bool manualFanControl = false;

#define LED_GREEN   12
#define LED_YELLOW  13
#define LED_ORANGE  14
#define LED_RED     27

// +++ IMPROVED: FILTER USAGE CALCULATION +++
const float FILTER_CAPACITY_GRAMS = 70.0;
const float AIRFLOW_RATE_M3_PER_HR = 540.0;
const float FILTER_EFFICIENCY = 0.95;
const float AIR_DENSITY = 1.2;
double airVolumePerSample_m3 = 0.0;

struct FilterUsageData {
  double totalDustAccumulated_grams;
  float usagePercentage;
  unsigned long lastUpdateTime;
  unsigned long totalSamplesProcessed;
};
FilterUsageData filter;

// EEPROM layout
#define EEPROM_SIZE 512
#define ADDR_TOTAL_DUST_ACCUMULATED 0
#define ADDR_TOTAL_SAMPLES 8
#define ADDR_MAGIC_NUMBER 16
#define MAGIC_NUMBER 0x12345678

// Timing
unsigned long lastTelegramCheck = 0;
const unsigned long telegramInterval = 2000;
unsigned long lastReadTime = 0;
const unsigned long interval = 2000;
unsigned long lastFilterSave = 0;

// +++ NEW: THINGSPEAK +++
String thingspeakApiKey = "****"; // 🛑🛑🛑 <-- ใส่ Key 
unsigned long lastThingspeakUpdate = 0;
const unsigned long thingspeakInterval = 20000; // ให้อัปเดตทุก 20 วินาที (ห้ามต่ำกว่า 15)

// ตัวนับเวลาพัดลมเปิด (วินาที)
unsigned long fanOnSecondsTotal = 0;
unsigned long lastFanUpdateMs = 0;

// ======= Function Prototypes =======
void sendTelegramMessage(String message);
String getAirQualityStatus(int pm25);
String getCO2Status(int co2);
String getO2Status(float o2);
String getTemperatureStatus(float temp);
void updateAirQualityLED(int pm25);
void checkCO2Alert(int co2);
void checkO2Alert(float o2);
void checkPM25Alert(int pm25);
void checkTemperatureAlert(float temperature); //  <-- NEW
void checkFilterAlert(); // <-- NEW

// ======= Air Quality Standards =======
struct AirQualityStandards {
  int pm25_good = 12;
  int pm25_moderate = 35;
  int pm25_unhealthy_sensitive = 55;
  int pm25_unhealthy = 150;

  int co2_excellent = 400;
  int co2_good = 600;
  int co2_moderate = 1000;
  int co2_poor = 5000;
  int co2_critical = 5000;

  float o2_min_safe = 19.5;
  float o2_normal = 20.9;
  float o2_max_safe = 23.5;

  float temp_good = 25.0;
  float temp_moderate = 30.0;
  float temp_high = 35.0;
  float temp_very_high = 35.0;
};
AirQualityStandards standards;

// Relay control state
bool relay_reason_temp = false;
bool relay_reason_co2 = false;
bool relay_reason_o2 = false;
bool relay_reason_pm25 = false;

// +++ NEW: Alert State Flags +++
// ใช้สำหรับจำว่าส่งแจ้งเตือนไปแล้วหรือยัง
bool tempAlertSent = false;
bool co2AlertSent = false;
bool o2AlertSent = false;
bool pm25AlertSent = false;
bool filterAlertSent = false;


// Virtual Pins for thresholds
#define VPIN_TEMP_THRESHOLD V9
#define VPIN_CO2_MODERATE V10
#define VPIN_CO2_CRITICAL V11
#define VPIN_O2_MIN_SAFE V12
#define VPIN_PM25_MODERATE V13
#define VPIN_PM25_STATUS V14

// ======= Helper Functions =======
String getAirQualityStatus(int pm25) {
  if (pm25 <= standards.pm25_good) return "GOOD";
  else if (pm25 <= standards.pm25_moderate) return "MODERATE";
  else if (pm25 <= standards.pm25_unhealthy_sensitive) return "UNHEALTHY FOR SENSITIVE GROUPS";
  else if (pm25 <= standards.pm25_unhealthy) return "UNHEALTHY";
  else return "VERY UNHEALTHY";
}

String getCO2Status(int co2) {
  if (co2 <= standards.co2_good) return "GOOD";
  else if (co2 <= standards.co2_moderate) return "MODERATE";
  else if (co2 <= standards.co2_poor) return "POOR";
  else return "CRITICAL";
}

String getO2Status(float o2) {
  if (o2 >= standards.o2_normal) return "NORMAL";
  else if (o2 >= standards.o2_min_safe) return "LOW";
  else return "CRITICAL";
}

String getTemperatureStatus(float temp) {
  if (temp <= standards.temp_good) return "COMFORTABLE";
  else if (temp <= standards.temp_moderate) return "WARM";
  else if (temp <= standards.temp_high) return "HOT";
  else return "VERY HOT";
}

String getPM25StatusTH(int pm25) {
  if (pm25 <= standards.pm25_good) return "ดีมาก";
  else if (pm25 <= standards.pm25_moderate) return "ปานกลาง";
  else if (pm25 <= standards.pm25_unhealthy_sensitive) return "เริ่มมีผลกระทบต่อกลุ่มเสี่ยง";
  else if (pm25 <= standards.pm25_unhealthy) return "ไม่ดีต่อสุขภาพ";
  else return "อันตรายต่อสุขภาพ";
}

// ======= EEPROM Helpers (IMPROVED) =======
void saveFilterData() {
  Serial.println("💾 === SAVING FILTER DATA TO EEPROM ===");
  
  EEPROM.put(ADDR_MAGIC_NUMBER, MAGIC_NUMBER);
  EEPROM.put(ADDR_TOTAL_DUST_ACCUMULATED, filter.totalDustAccumulated_grams);
  EEPROM.put(ADDR_TOTAL_SAMPLES, filter.totalSamplesProcessed);
  EEPROM.commit();
  
  Serial.print("✅ Saved - Dust: ");
  Serial.print(filter.totalDustAccumulated_grams, 6);
  Serial.print(" g, Samples: ");
  Serial.print(filter.totalSamplesProcessed);
  Serial.print(", Usage: ");
  Serial.print(filter.usagePercentage, 2);
  Serial.println("%");
  Serial.println("=======================================");
}

void loadFilterData() {
  Serial.println("💾 === LOADING FILTER DATA FROM EEPROM ===");
  
  unsigned long magic = 0;
  EEPROM.get(ADDR_MAGIC_NUMBER, magic);
  
  if (magic == MAGIC_NUMBER) {
    EEPROM.get(ADDR_TOTAL_DUST_ACCUMULATED, filter.totalDustAccumulated_grams);
    EEPROM.get(ADDR_TOTAL_SAMPLES, filter.totalSamplesProcessed);
    
    // คำนวณการใช้งานไส้กรอง
    filter.usagePercentage = (filter.totalDustAccumulated_grams / FILTER_CAPACITY_GRAMS) * 100.0;
    filter.lastUpdateTime = millis();
    
    Serial.print("✅ Loaded - Dust: ");
    Serial.print(filter.totalDustAccumulated_grams, 6);
    Serial.print(" g, Samples: ");
    Serial.print(filter.totalSamplesProcessed);
    Serial.print(", Usage: ");
    Serial.print(filter.usagePercentage, 2);
    Serial.println("%");
  } else {
    // กรณีไม่มีข้อมูล ให้รีเซ็ตค่า
    filter.totalDustAccumulated_grams = 0.0;
    filter.usagePercentage = 0.0;
    filter.totalSamplesProcessed = 0;
    filter.lastUpdateTime = millis();
    
    Serial.println("⚠️ No valid filter data found. Initialized to zero.");
  }
  
  Serial.println("========================================");
}

// +++ UPDATED: resetFilterCalculation +++
void resetFilterCalculation() {
  
  filter.totalDustAccumulated_grams = 0.0;
  filter.usagePercentage = 0.0;
  filter.totalSamplesProcessed = 0;
  filter.lastUpdateTime = millis();
  filterAlertSent = false; // <-- ⭐️⭐️ RESET ALERT FLAG ⭐️⭐️
  saveFilterData();

  Serial.println("🔄 === FILTER RESET COMPLETE ===");
  if (telegramEnabled) {
  sendTelegramMessage("✅ ไส้กรองถูกรีเซ็ตเรียบร้อยแล้ว\nการใช้งาน: 0.00%");
  }
}

// ======= Telegram Functions =======
void sendTelegramMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    if (bot.sendMessage(CHAT_ID, message, "")) {
      telegramEnabled = true;
    } else {
      telegramEnabled = false;
    }
  }
}

// +++ UPDATED: TELEGRAM HANDLER WITH GRAPH COMMANDS +++
// +++ 🛑🛑🛑 UPDATED: TELEGRAM HANDLER (SENDING LINKS) 🛑🛑🛑 +++
void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    if (chat_id == CHAT_ID) {
      // --- General Commands ---
      if (text == "/status" || text == "/start") {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        String statusMsg = "📊 **สถานะปัจจุบัน** 📊\n\n";
        statusMsg += "🌡️ อุณหภูมิ: " + String(isnan(t) ? 0.0 : t, 1) + "°C (" + getTemperatureStatus(t) + ")\n";
        statusMsg += "💧 ความชื้น: " + String(isnan(h) ? 0.0 : h, 1) + "%\n";
        statusMsg += "💨 PM2.5: " + String(data.pm2_5_standard) + " µg/m³ (" + getAirQualityStatus(data.pm2_5_standard) + ")\n";
        statusMsg += "🫁 CO2: " + String(co2_last_valid) + " ppm (" + getCO2Status(co2_last_valid) + ")\n";
        statusMsg += "🌬️ O2: " + String(oxygen_percent, 1) + "% (" + getO2Status(oxygen_percent) + ")\n\n";
        statusMsg += "🔧 **ไส้กรอง**\n";
        statusMsg += "การใช้งาน: " + String(filter.usagePercentage, 2) + "%\n";
        statusMsg += "ฝุ่นสะสม: " + String(filter.totalDustAccumulated_grams, 3) + " g\n";
        statusMsg += "ตัวอย่างที่ประมวลผล: " + String(filter.totalSamplesProcessed) + "\n\n";
        statusMsg += "⏱️ พัดลมเปิด: " + String(fanOnSecondsTotal / 60) + " นาที\n";
        statusMsg += "🌀 พัดลม: " + String(relayOn ? "เปิด" : "ปิด") + (manualFanControl ? " (Manual)" : " (Auto)");
        sendTelegramMessage(statusMsg);
      }
      else if (text == "/help") {
        String helpMsg = "📖 **Available Commands** 📖\n\n";
        helpMsg += "**General:**\n";
        helpMsg += "/status - ดูสถานะปัจจุบัน\n";
        helpMsg += "/help - แสดงคำสั่งทั้งหมด\n";
        helpMsg += "/graph - ขอดูกราฟ\n\n";
        helpMsg += "**Fan Control:**\n";
        helpMsg += "/fan_on - เปิดพัดลม Manual\n";
        helpMsg += "/fan_off - ปิด/Auto พัดลม\n\n";
        helpMsg += "**Filter:**\n";
        helpMsg += "/reset_filter - รีเซ็ตไส้กรอง\n\n";
        helpMsg += "**CO2 Sensor:**\n";
        helpMsg += "/co2_test - ทดสอบเซ็นเซอร์ CO2\n";
        helpMsg += "/co2_reset - รีเซ็ตค่า CO2\n";
        helpMsg += "/co2_calibrate - สอบเทียบ CO2\n";
        helpMsg += "   (ต้องอยู่กลางแจ้ง 20 นาที)";
        sendTelegramMessage(helpMsg);
      }
      // --- Fan & Filter Commands ---
      else if (text == "/reset_filter") {
        resetFilterCalculation();
      }
      else if (text == "/fan_on") {
        manualFanControl = true;
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
        relayOn = true;
        if (Blynk.connected()) Blynk.virtualWrite(V5, 1);
        sendTelegramMessage("✅ เปิดพัดลมแบบ Manual แล้ว");
      }
      else if (text == "/fan_off") {
        manualFanControl = false;
        digitalWrite(RELAY1, HIGH);
        digitalWrite(RELAY2, HIGH);
        relayOn = false;
        if (Blynk.connected()) Blynk.virtualWrite(V5, 0);
        sendTelegramMessage("✅ ปิดพัดลมและกลับสู่โหมด Auto");
      }
      //--- CO2 Commands ---
      else if (text == "/co2_test") {
        int rawCO2 = myMHZ19.getCO2();
        int temp = myMHZ19.getTemperature();
        int errorCode = myMHZ19.errorCode;
        
        String testMsg = "🧪 **CO2 Sensor Test** 🧪\n\n";
        testMsg += "📊 Raw CO2: " + String(rawCO2) + " ppm\n";
        testMsg += "🌡️ Sensor Temp: " + String(temp) + "°C\n";
        testMsg += "📈 Moving Avg: " + String(co2_last_valid) + " ppm\n";
        testMsg += "📦 Sample Count: " + String(co2_count) + "/" + String(CO2_SAMPLES) + "\n";
        testMsg += "🔧 Error Code: " + String(errorCode) + "\n";
        
        if (errorCode == RESULT_OK) {
          testMsg += "✅ Status: OK";
        } else {
          testMsg += "❌ Status: ERROR";
        }
        
        sendTelegramMessage(testMsg);
        Serial.println("🧪 CO2 Test performed via Telegram");
      }
      else if (text == "/co2_reset") {
        for (int j = 0; j < CO2_SAMPLES; j++) co2_values[j] = 0;
        co2_index = 0; co2_count = 0; co2_last_valid = 400; consecutive_400_readings = 0;
        
        String resetMsg = "🔄 **CO2 Reset Complete** 🔄\n\n";
        resetMsg += "✅ Moving average buffer cleared\n";
        resetMsg += "✅ CO2 value reset to 400 ppm\n";
        resetMsg += "✅ Will recalculate from fresh readings";
        
        sendTelegramMessage(resetMsg);
        Serial.println("🔄 CO2 sensor reset via Telegram");
      }
      else if (text == "/co2_calibrate") {
        sendTelegramMessage("🔧 **Starting CO2 Calibration** 🔧\n\n⚠️ Make sure sensor is in fresh air (400 ppm)\n⏱️ Calibration will take 20 minutes...\n\nDO NOT turn off the device!");
        Serial.println("🔧 Starting CO2 calibration...");
        myMHZ19.calibrateZero();
        delay(1000); 
        sendTelegramMessage("✅ **Calibration Command Sent!** ✅\n\n⏱️ Please wait 20 minutes for calibration to complete.\n\n📝 The sensor will automatically adjust to 400 ppm baseline.");
        Serial.println("✅ CO2 calibration command sent");
      }
      
      // --- NEW: GRAPH COMMANDS (SENDING LINKS) ---
      else if (text == "/graph") {
        String msg = "📈 เลือกกราฟที่ต้องการดู:\n\n";
        msg += "⭐️ **กราฟชุด 4 ค่า (ล่าสุด):**\n";
        msg += "/graph_4_values\n\n";
        
        msg += "⚠️ **กราฟรวม 4 ค่า (รูปเดียว/ดูยาก):**\n";
        msg += "/graph_avg_combo\n\n";
        msg += "📊 **แบบล่าสุด (รายชม.):**\n";
        msg += "/graph_pm25\n";
        msg += "/graph_co2\n\n";
        msg += "🗓️ **แบบย้อนหลัง (รายวัน):**\n";
        msg += "/graph_pm25_day\n";
        msg += "/graph_co2_day\n\n";
        msg += "📅 **แบบย้อนหลัง (รายสัปดาห์):**\n";
        msg += "/graph_pm25_week\n";
        msg += "/graph_co2_week\n\n";
        msg += "🈷️ **แบบย้อนหลัง (รายเดือน):**\n";
        msg += "/graph_pm25_month\n";
        msg += "/graph_co2_month\n\n";
        msg += "🔗 **ดูเว็บหลัก:**\n";
        msg += "/graph_all";
        sendTelegramMessage(msg);
      }
      else if (text == "/graph_all") {
        String channelId = "3137676"; // <-- ID ของคุณ
        sendTelegramMessage("ดู Dashboard ทั้งหมดได้ที่นี่:\nhttps://thingspeak.com/channels/" + channelId);
      }
      
      // --- Graph: 4 Values (Separate) ---
      else if (text == "/graph_4_values") {
        String channelId = "3137676"; // <-- ID ของคุณ
        
        // ⭐️ FIXED: O2 is charts/4
        String o2Url   = "https://thingspeak.com/channels/" + channelId + "/charts/4?width=450&height=260&results=200&dynamic=true";
        String co2Url  = "https://thingspeak.com/channels/" + channelId + "/charts/2?width=450&height=260&results=200&dynamic=true";
        String pm25Url = "https://thingspeak.com/channels/" + channelId + "/charts/1?width=450&height=260&results=200&dynamic=true";
        String tempUrl = "https://thingspeak.com/channels/" + channelId + "/charts/3?width=450&height=260&results=200&dynamic=true";
        
        sendTelegramMessage("กำลังสร้างชุดกราฟ 4 ค่า (O2, CO2, PM2.5, Temp)...");
        // ⭐️ CHANGED: Send links instead of photos
        sendTelegramMessage("📊 กราฟ Oxygen (O2):\n" + o2Url);
        sendTelegramMessage("📊 กราฟ CO2:\n" + co2Url);
        sendTelegramMessage("📊 กราฟ PM2.5:\n" + pm25Url);
        sendTelegramMessage("📊 กราฟ อุณหภูมิ (Temp):\n" + tempUrl);
      }
      
      // --- Graph: 4 Values (Combined / Avg) ---
      else if (text == "/graph_avg_combo") {
        String channelId = "3137676"; // <-- ID ของคุณ
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/2"; // Base on CO2 (Field 2)
        
        // ⭐️ FIXED: O2 is field4
        chartUrl += "?field1=1&field3=3&field4=4"; // Add other fields
        chartUrl += "&days=1&average=60&dynamic=true"; // 1 day, 60 min avg
        
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟเฉลี่ยรวม 4 ค่า (ย้อนหลัง 1 วัน):\n" + chartUrl);
      }
      
      // --- Graph: Recent ---
      else if (text == "/graph_pm25") {
        String channelId = "3137676"; // <-- ID ของคุณ
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/1?width=450&height=260&results=200&dynamic=true"; // 200 recent
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ PM2.5 (ล่าสุด):\n" + chartUrl);
      }
      else if (text == "/graph_co2") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/2?width=450&height=260&results=200&dynamic=true";
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ CO2 (ล่าสุด):\n" + chartUrl);
      }
      
      // --- Graph: 1 Day ---
      else if (text == "/graph_pm25_day") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/1?width=450&height=260&days=1&average=60&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ PM2.5 (ย้อนหลัง 1 วัน):\n" + chartUrl);
      }
      else if (text == "/graph_co2_day") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/2?width=450&height=260&days=1&average=60&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ CO2 (ย้อนหลัง 1 วัน):\n" + chartUrl);
      }
      
      // --- Graph: 1 Week ---
      else if (text == "/graph_pm25_week") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/1?width=450&height=260&days=7&average=240&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ PM2.5 (ย้อนหลัง 1 สัปดาห์):\n" + chartUrl);
      }
      else if (text == "/graph_co2_week") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/2?width=450&height=260&days=7&average=240&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ CO2 (ย้อนหลัง 1 สัปดาห์):\n" + chartUrl);
      }
      
      // --- Graph: 1 Month ---
      else if (text == "/graph_pm25_month") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/1?width=450&height=260&days=30&average=1440&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ PM2.5 (ย้อนหลัง 1 เดือน):\n" + chartUrl);
      }
      else if (text == "/graph_co2_month") {
        String channelId = "3137676"; 
        String chartUrl = "https://thingspeak.com/channels/" + channelId + "/charts/2?width=450&height=260&days=30&average=1440&dynamic=true"; 
        // ⭐️ CHANGED: Send link
        sendTelegramMessage("📊 กราฟ CO2 (ย้อนหลัง 1 เดือน):\n" + chartUrl);
      }
    }
  }
}

// ======= Filter Functions (IMPROVED WITH BETTER FORMULA) =======
void updateFilterUsage(float currentPM25) {
  Serial.println("🔬 === UPDATE FILTER USAGE ===");
  Serial.print("Relay Status: "); Serial.println(relayOn ? "ON ✅" : "OFF ❌");
  Serial.print("Current PM2.5: "); Serial.print(currentPM25); Serial.println(" µg/m³");
  Serial.print("Filter Usage: "); Serial.print(filter.usagePercentage, 2); Serial.println("%");
  
  if (!relayOn) {
    Serial.println("❌ SKIPPED: Fan is OFF");
    Serial.println("==============================\n");
    return;
  }
  if (filter.usagePercentage >= 100.0) {
    Serial.println("❌ SKIPPED: Filter is FULL (100%)");
    Serial.println("==============================\n");
    return;
  }
  if (currentPM25 < 1.0) {
    Serial.println("⚠️ SKIPPED: PM2.5 too low (< 1 µg/m³)");
    Serial.println("==============================\n");
    return;
  }
  
  double volumeProcessed_m3 = airVolumePerSample_m3;
  double totalDustInAir_ug = currentPM25 * volumeProcessed_m3;
  double dustCaptured_ug = totalDustInAir_ug * FILTER_EFFICIENCY;
  double dustCaptured_g = dustCaptured_ug / 1000000.0;
  
  filter.totalDustAccumulated_grams += dustCaptured_g;
  filter.totalSamplesProcessed++;
  filter.lastUpdateTime = millis();

  filter.usagePercentage = (filter.totalDustAccumulated_grams / FILTER_CAPACITY_GRAMS) * 100.0;
  if (filter.usagePercentage > 100.0) {
    filter.usagePercentage = 100.0;
  }
  
  Serial.println("📊 CALCULATION RESULTS:");
  Serial.print("  Volume processed: ");
  Serial.print(volumeProcessed_m3, 6);
  Serial.println(" m³");
  Serial.print("Total dust in air: ");
  Serial.print(totalDustInAir_ug, 4);
  Serial.println(" µg");
  Serial.print("Dust captured (95%): ");
  Serial.print(dustCaptured_ug, 4);
  Serial.println(" µg");
  Serial.print("Dust captured: ");
  Serial.print(dustCaptured_g, 8);
  Serial.println(" g");
  Serial.print("✅ NEW TOTAL: ");
  Serial.print(filter.totalDustAccumulated_grams, 6);
  Serial.println(" g");
  Serial.print("✅ NEW USAGE: ");
  Serial.print(filter.usagePercentage, 2);
  Serial.println("%");  
  Serial.print("📈 Total samples: ");
  Serial.println(filter.totalSamplesProcessed);
  Serial.println("==============================\n");
}


// ======= Alert Functions (NEW STATE CHANGE LOGIC) =======

void checkFilterAlert() {
  if (filter.usagePercentage >= 95.0) {
    // ไส้กรองใกล้เต็ม
    if (!filterAlertSent) {
      String alertMsg = "⚠️ **แจ้งเตือนไส้กรอง** ⚠️\n\n";
      alertMsg += "การใช้งาน: " + String(filter.usagePercentage, 1) + "%\n";
      alertMsg += "ฝุ่นสะสม: " + String(filter.totalDustAccumulated_grams, 2) + " g\n\n";
      alertMsg += "⚠️ กรุณาเตรียมเปลี่ยนไส้กรองเร็วๆ นี้!";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      filterAlertSent = true; // ตั้งธงว่าส่งแล้ว
    }
  }
}

void checkTemperatureAlert(float temperature) {
  if (isnan(temperature)) return;

  if (temperature > tempThreshold) {
    // 1. ค่าสูงผิดปกติ
    if (!tempAlertSent) {
      String alertMsg = "🔥 **แจ้งเตือนอุณหภูมิสูง!** 🔥\n";
      alertMsg += "อุณหภูมิปัจจุบัน: " + String(temperature, 1) + "°C";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      tempAlertSent = true; // ตั้งธงว่าส่งแล้ว
    }
  } else {
    // 2. ค่ากลับมาปกติ
    if (tempAlertSent) {
      String alertMsg = "✅ **อุณหภูมิกลับสู่ปกติ**\n";
      alertMsg += "อุณหภูมิปัจจุบัน: " + String(temperature, 1) + "°C";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      tempAlertSent = false; // รีเซ็ตธง
    }
  }
}

void checkCO2Alert(int co2) {
  if (co2 > standards.co2_critical) {
    // 1. ค่าสูงผิดปกติ
    if (!co2AlertSent) {
      String alertMsg = "‼️ **แจ้งเตือน CO2 วิกฤต!** ‼️\n";
      alertMsg += "CO2 ปัจจุบัน: " + String(co2) + " ppm\n";
      alertMsg += "ต้องระบายอากาศทันที!";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      co2AlertSent = true; // ตั้งธงว่าส่งแล้ว
    }
  } else {
    // 2. ค่ากลับมาปกติ
    if (co2AlertSent) {
      String alertMsg = "✅ **CO2 กลับสู่ปกติ**\n";
      alertMsg += "CO2 ปัจจุบัน: " + String(co2) + " ppm";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      co2AlertSent = false; // รีเซ็ตธง
    }
  }
}

void checkO2Alert(float o2) {
  if (o2 < standards.o2_min_safe) {
    // 1. ค่าต่ำผิดปกติ
    if (!o2AlertSent) {
      String alertMsg = "🚨 **แจ้งเตือนออกซิเจนต่ำ!** 🚨\n";
      alertMsg += "O2 ปัจจุบัน: " + String(o2, 1) + "%\n";
      alertMsg += "อันตราย: ระดับออกซิเจนต่ำเกินไป!";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      o2AlertSent = true; // ตั้งธงว่าส่งแล้ว
    }
  } else {
    // 2. ค่ากลับมาปกติ
    if (o2AlertSent) {
      String alertMsg = "✅ **Oxygen กลับสู่ปกติ**\n";
      alertMsg += "O2 ปัจจุบัน: " + String(o2, 1) + "%";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      o2AlertSent = false; // รีเซ็ตธง
    }
  }
}

void checkPM25Alert(int pm25) {
  if (pm25 > standards.pm25_moderate) {
    // 1. ค่าสูงผิดปกติ
    if (!pm25AlertSent) {
      String alertMsg = "😷 **แจ้งเตือนฝุ่น PM2.5 สูง!** 😷\n\n";
      alertMsg += "PM2.5 ปัจจุบัน: " + String(pm25) + " µg/m³\n";
      alertMsg += "สถานะ: " + getPM25StatusTH(pm25);
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      pm25AlertSent = true; // ตั้งธงว่าส่งแล้ว
    }
  } else {
    // 2. ค่ากลับมาปกติ
    if (pm25AlertSent) {
      String alertMsg = "✅ **PM2.5 กลับสู่ปกติ**\n";
      alertMsg += "PM2.5 ปัจจุบัน: " + String(pm25) + " µg/m³";
      if (telegramEnabled) sendTelegramMessage(alertMsg);
      pm25AlertSent = false; // รีเซ็ตธง
    }
  }
}


// ======= LED & Fan Control Functions =======
void updateAirQualityLED(int pm25) {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_ORANGE, LOW);
  digitalWrite(LED_RED, LOW);
  
  if (pm25 <= standards.pm25_good) {
    digitalWrite(LED_GREEN, HIGH);
  } else if (pm25 <= standards.pm25_moderate) {
    digitalWrite(LED_YELLOW, HIGH);
  } else if (pm25 <= standards.pm25_unhealthy_sensitive) {
    digitalWrite(LED_ORANGE, HIGH);
  } else {
    digitalWrite(LED_RED, HIGH);
  }
}

void controlRelay(float temp, int co2, float o2, int pm25) {
  if (manualFanControl) return;
  
  relay_reason_temp = !isnan(temp) && temp > tempThreshold;
  relay_reason_co2 = co2 > standards.co2_moderate;
  relay_reason_o2 = o2 < standards.o2_min_safe;
  relay_reason_pm25 = pm25 > standards.pm25_moderate;
  
  bool shouldBeOn = relay_reason_temp || relay_reason_co2 || relay_reason_o2 || relay_reason_pm25;
  
  if (shouldBeOn && !relayOn) {
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    relayOn = true;
    Serial.println("🌀 Auto Relay: ON");
    if (Blynk.connected()) Blynk.virtualWrite(V5, 1);
  }
  else if (!shouldBeOn && relayOn) {
    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, HIGH);
    relayOn = false;
    Serial.println("🌀 Auto Relay: OFF");
    if (Blynk.connected()) Blynk.virtualWrite(V5, 0);
  }
}

// ======= Sensor Reading Functions =======
bool readPMSDataStable() {
  static byte buffer[32];
  static byte index = 0;
  
  while (pmsSerial.available()) {
    byte c = pmsSerial.read();
    if (index == 0 && c != 0x42) continue;
    if (index == 1 && c != 0x4D) { index = 0; continue; }
    
    buffer[index++] = c;
    
    if (index >= 32) {
      index = 0;
      uint16_t checksum_calc = 0;
      for (int i = 0; i < 30; i++) checksum_calc += buffer[i];
      uint16_t checksum_read = (buffer[30] << 8) | buffer[31];
      
      if (checksum_calc == checksum_read) {
        data.pm1_0_standard = (buffer[4] << 8) | buffer[5];
        data.pm2_5_standard = (buffer[6] << 8) | buffer[7];
        data.pm10_standard  = (buffer[8] << 8) | buffer[9];
        return true;
      }
    }
  }
  return false;
}

int readCO2() {
  int current_co2 = myMHZ19.getCO2();
  if (myMHZ19.errorCode != RESULT_OK || current_co2 <= 0 || current_co2 > 5000) {
    return co2_last_valid;
  }
  
  co2_values[co2_index] = current_co2;
  co2_index = (co2_index + 1) % CO2_SAMPLES;
  if (co2_count < CO2_SAMPLES) co2_count++;
  
  long sum = 0;
  for (int i = 0; i < co2_count; i++) {
    sum += co2_values[i];
  }
  int avg_co2 = sum / co2_count;
  co2_last_valid = avg_co2;
  return avg_co2;
}
 
//======= Blynk Functions =======
BLYNK_WRITE(V5) {
  int value = param.asInt();
  if (value == 1) {
    manualFanControl = true;
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    relayOn = true;
    Serial.println("📱 Blynk: Manual Fan ON");
  } else {
    manualFanControl = false;
    Serial.println("📱 Blynk: Switched to Auto mode");
  }
}

BLYNK_WRITE(V6) {
  if (param.asInt() == 1) {
    resetFilterCalculation();
    Blynk.virtualWrite(V6, 0);
  }
}

BLYNK_WRITE(VPIN_TEMP_THRESHOLD) { tempThreshold = param.asFloat(); }
BLYNK_WRITE(VPIN_CO2_MODERATE) { standards.co2_moderate = param.asInt(); }
BLYNK_WRITE(VPIN_CO2_CRITICAL) { standards.co2_critical = param.asInt(); }
BLYNK_WRITE(VPIN_O2_MIN_SAFE) { standards.o2_min_safe = param.asFloat(); }
BLYNK_WRITE(VPIN_PM25_MODERATE) { standards.pm25_moderate = param.asInt(); }

// ======= Setup =======
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("   AIR QUALITY MONITOR - STARTING");
  Serial.println("========================================");
  
  //Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadFilterData();
  
  // Calculate air volume per sample
  double airflow_m3_per_sec = AIRFLOW_RATE_M3_PER_HR / 3600.0;
  airVolumePerSample_m3 = airflow_m3_per_sec * (interval / 1000.0);
  Serial.println("\n📐 === FILTER CALCULATION PARAMETERS ===");
  Serial.print("Airflow rate: ");
  Serial.print(AIRFLOW_RATE_M3_PER_HR);
  Serial.println(" m³/hr");
  Serial.print("Sample interval: ");
  Serial.print(interval / 1000.0);
  Serial.println(" seconds");
  Serial.print("Air volume per sample: ");
  Serial.print(airVolumePerSample_m3, 6);
  Serial.println(" m³");
  Serial.print("Filter efficiency: ");
  Serial.print(FILTER_EFFICIENCY * 100);
  Serial.println("%");
  Serial.print("Filter capacity: ");
  Serial.print(FILTER_CAPACITY_GRAMS);
  Serial.println(" grams");
  Serial.println("========================================\n");
  
  // Initialize sensors
  pmsSerial.begin(9600, SERIAL_8N1, 32, 33);
  co2Serial.begin(9600, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
  myMHZ19.begin(co2Serial);
  myMHZ19.autoCalibration(true);
  myMHZ19.setRange(5000);
  dht.begin();
  
  // Initialize relay pins
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  
  // Initialize LED pins
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_ORANGE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize Telegram
  client.setInsecure();
  sendTelegramMessage("✅ **ระบบตรวจสอบคุณภาพอากาศ เริ่มทำงาน!**\n\nระบบพร้อมใช้งานแล้ว 🌬️");
  
  // Initialize Blynk
  Blynk.begin(auth, ssid, pass);
  Serial.println("========================================");
  Serial.println("   ✅ SYSTEM READY!");
  Serial.println("========================================\n");
}

// ======= Loop =======
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected! Reconnecting...");
    WiFi.reconnect();
    return;
  }
  Blynk.run();
  
  unsigned long now = millis();
  
  // Check Telegram messages
  if (now - lastTelegramCheck > telegramInterval) {
    handleTelegramMessages();
    lastTelegramCheck = now;
  }
  
  // Main sensor reading loop
  if (now - lastReadTime >= interval) {
    lastReadTime = now;
    
  // Update fan on time counter
    if (relayOn) {
      if (lastFanUpdateMs > 0) {
        fanOnSecondsTotal += (now - lastFanUpdateMs) / 1000;
      }
    }
    lastFanUpdateMs = now;
    
  // Read all sensors
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    float oxygen_voltage = analogRead(OXYGEN_PIN) * (VREF / 4095.0);
    oxygen_percent = (oxygen_voltage / OXYGEN_MAX_VOLTAGE) * 25.0;
    int co2 = readCO2();
    bool pmUpdated = readPMSDataStable();
    
  // Check alerts
    checkTemperatureAlert(t);
    if (co2 > 0) checkCO2Alert(co2);
    checkO2Alert(oxygen_percent);
    checkFilterAlert();
    if (pmUpdated) {
      checkPM25Alert(data.pm2_5_standard); // <-- PM2.5 Alert Check
    }
    
  // Control relay and LED
    controlRelay(t, co2, oxygen_percent, data.pm2_5_standard);
    updateAirQualityLED(data.pm2_5_standard);
    
  // Update filter calculation if PM data is available
    if (pmUpdated) {
      updateFilterUsage(data.pm2_5_standard);
      
  // Send data to Blynk
      if (Blynk.connected()) {
        Blynk.virtualWrite(V0, t);
        Blynk.virtualWrite(V1, h);
        Blynk.virtualWrite(V2, oxygen_percent);
        if (co2 > 0) Blynk.virtualWrite(V3, co2);
        Blynk.virtualWrite(V4, data.pm2_5_standard);
        Blynk.virtualWrite(V7, filter.usagePercentage);
        Blynk.virtualWrite(V8, fanOnSecondsTotal / 3600);
        Blynk.virtualWrite(VPIN_PM25_STATUS, getPM25StatusTH(data.pm2_5_standard));
      }
    }
    
  // Save filter data every 15 minutes when fan is running
    if (relayOn && (now - lastFilterSave > 900000)) {
      saveFilterData(); 
      lastFilterSave = now;
    }
    
  // Print summary to Serial Monitor
    Serial.println("╔════════════════════════════════════════╗");
    Serial.printf("║ 🌡️  Temp: %.1f°C | Hum: %.1f%%     \n", t, h);
    Serial.printf("║ 🫁 O2: %.1f%% | CO2: %d ppm        \n", oxygen_percent, co2);
    Serial.printf("║ 💨 PM2.5: %d µg/m³                 \n", data.pm2_5_standard);
    Serial.printf("║ 🔧 Filter: %.2f%% (%.4f g)         \n", filter.usagePercentage, filter.totalDustAccumulated_grams);
    Serial.printf("║ 🌀 Fan: %s | Time: %lu mins        \n", relayOn ? "ON " : "OFF", fanOnSecondsTotal / 60);
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();

    // +++ NEW: SEND DATA TO THINGSPEAK +++
    if (now - lastThingspeakUpdate > thingspeakInterval) {
      if (WiFi.status() == WL_CONNECTED && !isnan(t) && !isnan(h) && pmUpdated) {
        HTTPClient http;
        String url = "http://api.thingspeak.com/update?api_key=" + thingspeakApiKey;
        url += "&field1=" + String(data.pm2_5_standard); // PM2.5
        url += "&field2=" + String(co2_last_valid);      // CO2
        url += "&field3=" + String(t, 1);                // Temp
        url += "&field4=" + String(oxygen_percent, 1);   // Oxygen

        Serial.println("🌍 Sending data to ThingsSpeak...");
        http.begin(url);
        int httpResponseCode = http.GET();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        http.end();
        
        lastThingspeakUpdate = now;
      }
    }
  }
}
