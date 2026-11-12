// Creature OS — data logger (Phase 1). Writes raw sensor data to SD in binary log format v1.
// Spec: docs/data-protocol.md. Pins: pins.h. UNVERIFIED: written without a board/compiler.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <OneWire.h>
#include "esp_timer.h"
#include "pins.h"
#include "logfmt.h"   // C encoder shared with host tests (logfmt.c)
#include "errcodes.h"

#if __has_include("secrets.h")
#include "secrets.h"   // optional: #define WIFI_SSID "..." / #define WIFI_PASS "..." (NTP at boot only)
#include <WiFi.h>
#include <time.h>
#endif

#define FW_VERSION "logger-0.1"
static_assert(configTICK_RATE_HZ == 1000, "tasks assume a 1 ms RTOS tick");

// ---------- configuration (also written to META) ----------
static const uint8_t MPU_ADDR = 0x68;
static const uint8_t BH_ADDR = 0x23;
static const uint8_t LIGHT_OVERSAMPLE = 1;  // 1 = single raw ADC read per sample

// ---------- record queue ----------
// One global seq across all organs; seq is assigned and the record enqueued under one mutex, so
// file order == seq order, and a full queue shows up as a gap in seq (dropped records consume a seq).
struct Rec { uint8_t len; uint8_t data[LOGFMT_REC_MAX]; };
static QueueHandle_t g_q;
static SemaphoreHandle_t g_logMux;
static uint32_t g_seq = 0;
static volatile uint32_t g_dropped = 0;
static volatile uint32_t g_sdFail = 0;
static volatile bool g_sdDead = false;
static uint32_t g_startUnix = 0;   // 0 = no time source

static bool logAt(uint8_t organ, uint64_t t_us, uint8_t flags, const int16_t* v, uint8_t n) {
  if (n > LOGFMT_MAX_VALUES) return false;
  struct logfmt_record r;
  memset(&r, 0, sizeof(r));
  r.t_us = t_us; r.organ = organ; r.flags = flags; r.n = n;
  for (uint8_t i = 0; i < n; i++) r.v[i] = v[i];
  Rec q;
  xSemaphoreTake(g_logMux, portMAX_DELAY);
  r.seq = g_seq++;
  q.len = (uint8_t)logfmt_record_encode(&r, q.data, sizeof(q.data));
  bool ok = q.len != 0 && xQueueSend(g_q, &q, 0) == pdTRUE;
  if (!ok) g_dropped++;
  xSemaphoreGive(g_logMux);
  return ok;
}
static inline bool logNow(uint8_t organ, uint8_t flags, const int16_t* v, uint8_t n) {
  return logAt(organ, (uint64_t)esp_timer_get_time(), flags, v, n);
}
static void logError(uint8_t code, uint16_t arg) {
  int16_t v[2] = {code, (int16_t)arg};
  logNow(LOGFMT_ORGAN_ERROR, 0, v, 2);
}
static void logMarker(uint8_t code, uint16_t arg) {
  int16_t v[2] = {code, (int16_t)arg};
  logNow(LOGFMT_ORGAN_MARKER, 0, v, 2);
}
static File g_meta;   // human-readable sidecar: /log_NNNNN.meta.txt (config, WHO_AM_I, notes)
static void metaLine(const String& s) { if (g_meta) { g_meta.println(s); g_meta.flush(); } }

// ---------- I2C with mutex ----------
static SemaphoreHandle_t g_i2c;
static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  xSemaphoreTake(g_i2c, portMAX_DELAY);
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val);
  bool ok = Wire.endTransmission() == 0;
  xSemaphoreGive(g_i2c);
  return ok;
}
static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  xSemaphoreTake(g_i2c, portMAX_DELAY);
  Wire.beginTransmission(addr); Wire.write(reg);
  bool ok = Wire.endTransmission(false) == 0 && Wire.requestFrom((int)addr, (int)n) == (int)n;
  if (ok) for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  xSemaphoreGive(g_i2c);
  return ok;
}

// ---------- writer task (core 0) ----------
static File g_file;
static void writerTask(void*) {
  static uint8_t buf[8192];
  size_t fill = 0;
  uint32_t lastWrite = millis(), lastFlush = millis();
  Rec r;
  for (;;) {
    if (xQueueReceive(g_q, &r, pdMS_TO_TICKS(200)) == pdTRUE) {
      // safe: the flush condition below keeps fill <= sizeof(buf) - sizeof(Rec) before every append
      memcpy(buf + fill, r.data, r.len); fill += r.len;
    }
    bool due = fill >= 4096 || (fill > 0 && millis() - lastWrite > 1000);
    if (due || fill + sizeof(Rec) > sizeof(buf)) {
      size_t w = g_file.write(buf, fill);
      if (w != fill) { g_sdFail++; g_sdDead = true; }   // keep trying; LED blinks fast in loop()
      else g_sdDead = false;
      fill = 0; lastWrite = millis();
    }
    if (millis() - lastFlush > 5000) { g_file.flush(); lastFlush = millis(); }
  }
}

// ---------- MPU6050 task (core 1): one record per sample, 6 raw channels ax ay az gx gy gz ----------
static void mpuTask(void*) {
  uint32_t lastErr = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));   // 200 Hz
    uint64_t t = (uint64_t)esp_timer_get_time();
    uint8_t raw[14];   // ax ay az temp gx gy gz (big-endian from chip)
    if (!i2cReadReg(MPU_ADDR, 0x3B, raw, 14)) {
      if (millis() - lastErr > 1000) { logError(ERR_MPU_I2C, 0); lastErr = millis(); }
      continue;
    }
    static const uint8_t src[6] = {0, 2, 4, 8, 10, 12};   // skip chip temperature (raw[6..7])
    int16_t v[6];
    for (int i = 0; i < 6; i++) v[i] = (int16_t)((raw[src[i]] << 8) | raw[src[i] + 1]);
    logAt(LOGFMT_ORGAN_TOUCH, t, 0, v, 6);
  }
}

// ---------- analog task: light 100 Hz, battery 1 Hz, MCU temp 0.1 Hz (core 1) ----------
static void analogTask(void*) {
  TickType_t last = xTaskGetTickCount();
  uint32_t k = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    uint64_t t = (uint64_t)esp_timer_get_time();
    uint32_t acc = 0;
    for (int i = 0; i < LIGHT_OVERSAMPLE; i++) acc += analogRead(PIN_LDR);
    int16_t v = (int16_t)(acc / LIGHT_OVERSAMPLE);
    logAt(LOGFMT_ORGAN_LIGHT, t, 0, &v, 1);
    if (k % 100 == 0) { int16_t b = (int16_t)analogRead(PIN_BATT); logNow(LOGFMT_ORGAN_HUNGER, 0, &b, 1); }
    if (k % 1000 == 0) {
      int16_t c = (int16_t)(temperatureRead() * 100.0f);   // internal die temperature, centi-degC
      logNow(LOGFMT_ORGAN_MCU_TEMP, 0, &c, 1);
    }
    k++;
  }
}

// ---------- BH1750 (reference light) 10 Hz, core 1 ----------
static bool i2cWriteCmd(uint8_t addr, uint8_t cmd) {   // BH1750 takes single-byte commands (no register byte)
  xSemaphoreTake(g_i2c, portMAX_DELAY);
  Wire.beginTransmission(addr); Wire.write(cmd);
  bool ok = Wire.endTransmission() == 0;
  xSemaphoreGive(g_i2c);
  return ok;
}
static void bhTask(void*) {
  i2cWriteCmd(BH_ADDR, 0x01);   // power on
  i2cWriteCmd(BH_ADDR, 0x10);   // continuous high-resolution mode (1 lx, ~120 ms per measurement)
  TickType_t last = xTaskGetTickCount();
  uint32_t lastErr = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    uint8_t b[2];
    xSemaphoreTake(g_i2c, portMAX_DELAY);
    uint64_t t = (uint64_t)esp_timer_get_time();
    bool ok = Wire.requestFrom((int)BH_ADDR, 2) == 2;
    if (ok) { b[0] = Wire.read(); b[1] = Wire.read(); }   // chip sends MSB first
    xSemaphoreGive(g_i2c);
    if (ok) { int16_t v = (int16_t)((b[0] << 8) | b[1]); logAt(LOGFMT_ORGAN_LIGHT_REF, t, 0, &v, 1); }
    else if (millis() - lastErr > 1000) { logError(ERR_BH1750_I2C, 0); lastErr = millis(); }
  }
}

// ---------- DHT11: raw bit-bang, keeps timeouts/checksum errors (core 0) ----------
static portMUX_TYPE g_dhtMux = portMUX_INITIALIZER_UNLOCKED;
static int waitLevel(int level, uint32_t timeout_us) {
  uint32_t s = (uint32_t)esp_timer_get_time();
  while (digitalRead(PIN_DHT) == level) if ((uint32_t)esp_timer_get_time() - s > timeout_us) return -1;
  return (int)((uint32_t)esp_timer_get_time() - s);
}
static uint8_t readDht11(uint8_t out[5]) {   // returns status: 0 ok, 1 timeout, 2 checksum
  pinMode(PIN_DHT, OUTPUT); digitalWrite(PIN_DHT, LOW); vTaskDelay(pdMS_TO_TICKS(20));
  uint8_t status = 0;
  portENTER_CRITICAL(&g_dhtMux);
  digitalWrite(PIN_DHT, HIGH); delayMicroseconds(30); pinMode(PIN_DHT, INPUT_PULLUP);
  if (waitLevel(HIGH, 100) < 0 || waitLevel(LOW, 120) < 0 || waitLevel(HIGH, 120) < 0) status = 1;
  for (int i = 0; i < 40 && !status; i++) {
    if (waitLevel(LOW, 100) < 0) { status = 1; break; }       // low phase (~50us)
    int hi = waitLevel(HIGH, 120);                              // high phase: ~27us = 0, ~70us = 1
    if (hi < 0) { status = 1; break; }
    out[i / 8] = (out[i / 8] << 1) | (hi > 45 ? 1 : 0);
  }
  portEXIT_CRITICAL(&g_dhtMux);
  if (!status && ((uint8_t)(out[0] + out[1] + out[2] + out[3]) != out[4])) status = 2;
  return status;
}
static void dhtTask(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    uint8_t d[5] = {0};
    uint64_t t = (uint64_t)esp_timer_get_time();
    uint8_t st = readDht11(d);
    // values: temperature x10, humidity x10 (DHT11 decimal bytes are 0). On error flag READ_ERROR and
    // put the status code (1=timeout, 2=checksum) in v[0]; v[1] = 0.
    int16_t v[2];
    if (st == 0) { v[0] = d[2] * 10 + d[3]; v[1] = d[0] * 10 + d[1]; }
    else { v[0] = st; v[1] = 0; }
    logAt(LOGFMT_ORGAN_THERMO, t, st ? LOGFMT_FLAG_READ_ERROR : 0, v, 2);
  }
}

// ---------- DS18B20 (reference temperature), raw scratchpad, single device on bus (core 0) ----------
static void dsTask(void*) {
  OneWire ow(PIN_ONEWIRE);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    uint8_t status = 0; int16_t raw = 0;
    uint64_t t = (uint64_t)esp_timer_get_time();
    if (!ow.reset()) status = 1;              // no presence pulse
    else {
      ow.skip(); ow.write(0x44, 0);           // convert T (VDD powered, not parasite)
      vTaskDelay(pdMS_TO_TICKS(800));         // 12-bit conversion needs up to 750 ms
      if (!ow.reset()) status = 1;
      else {
        ow.skip(); ow.write(0xBE);
        uint8_t sp[9]; for (int i = 0; i < 9; i++) sp[i] = ow.read();
        if (OneWire::crc8(sp, 8) != sp[8]) status = 2;
        else raw = (int16_t)((sp[1] << 8) | sp[0]);   // 1/16 degC
      }
    }
    // v[0] = raw 1/16 degC (0 on error); flag READ_ERROR with status in v[1] (1=no presence, 2=crc)
    int16_t v[2] = {raw, status};
    logAt(LOGFMT_ORGAN_THERMO_REF, t, status ? LOGFMT_FLAG_READ_ERROR : 0, v, 2);
  }
}

// ---------- setup / loop ----------
static void fatalBlink(const char* msg) {
  for (;;) { Serial.println(msg); for (int i = 0; i < 10; i++) { digitalWrite(PIN_LED, i & 1); delay(100); } }
}

static void syncTimeNtp() {   // sets g_startUnix; Wi-Fi is off again before logging starts
#ifdef WIFI_SSID
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t s = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - s < 10000) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org");
    struct tm tm;
    if (getLocalTime(&tm, 5000)) {
      g_startUnix = (uint32_t)time(nullptr);
    }
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);   // Wi-Fi off while logging: it disturbs ADC and supply
#endif
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT); pinMode(PIN_MARK_BTN, INPUT_PULLUP);
  analogReadResolution(12); analogSetAttenuation(ADC_11db);

  Preferences prefs; prefs.begin("logger", false);
  uint32_t boot = prefs.getUInt("boot", 0) + 1; prefs.putUInt("boot", boot); prefs.end();

  g_i2c = xSemaphoreCreateMutex();
  g_logMux = xSemaphoreCreateMutex();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  syncTimeNtp();

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI, 20000000)) fatalBlink("SD init failed (check wiring, 3.3V/5V module, FAT32)");
  char name[32], mname[40];
  snprintf(name, sizeof(name), "/log_%05u.bin", (unsigned)boot);
  snprintf(mname, sizeof(mname), "/log_%05u.meta.txt", (unsigned)boot);
  g_file = SD.open(name, FILE_WRITE);
  g_meta = SD.open(mname, FILE_WRITE);
  if (!g_file) fatalBlink("cannot create log file");

  struct logfmt_header h;
  memset(&h, 0, sizeof(h));
  h.device_id = (uint32_t)ESP.getEfuseMac();
  memcpy(h.session, "RAW", 4);   // session codes S1..S7 are marked with EVENT markers, not in the header
  h.start_unix = g_startUnix; h.boot_count = boot;
  uint8_t hb[LOGFMT_HEADER_SIZE];
  logfmt_header_encode(&h, hb);
  g_file.write(hb, sizeof(hb));

  g_q = xQueueCreate(128, sizeof(Rec));

  // MPU6050: wake, DLPF 94 Hz (cfg 2), internal rate 1 kHz (div 0), gyro +-500 dps, accel +-8 g; host polls at 200 Hz
  uint8_t who = 0; bool haveWho = i2cReadReg(MPU_ADDR, 0x75, &who, 1);
  i2cWriteReg(MPU_ADDR, 0x6B, 0x01); delay(100);
  i2cWriteReg(MPU_ADDR, 0x1A, 0x02); i2cWriteReg(MPU_ADDR, 0x19, 0x00);
  i2cWriteReg(MPU_ADDR, 0x1B, 0x08); i2cWriteReg(MPU_ADDR, 0x1C, 0x10);

  metaLine(String("fw=") + FW_VERSION + " build=" + __DATE__ + " " + __TIME__);
  metaLine(String("boot=") + boot + " start_unix=" + g_startUnix + " (0 = no time source)");
  metaLine(String("mpu_who_am_i=") + (haveWho ? String(who, HEX) : String("NONE")) + " dlpf=94Hz host_rate=200Hz gyro=500dps accel=8g");
  metaLine(String("adc_atten=11dB bits=12 light_oversample=") + LIGHT_OVERSAMPLE + " board=esp32-s3");
  metaLine("records: light=ADC raw; thermo=[tempx10,humx10] (error: v0=status); touch=ax ay az gx gy gz raw;");
  metaLine("         hunger=battery ADC raw; light_ref=BH1750 raw; thermo_ref=[DS18B20 raw 1/16C, status]; mcu_temp=centi-degC;");
  metaLine("         error=[code,arg]; marker=[event,arg] (see docs/data-protocol.md)");

  xTaskCreatePinnedToCore(writerTask, "writer", 6144, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(mpuTask, "mpu", 4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(analogTask, "analog", 4096, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(bhTask, "bh1750", 3072, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(dhtTask, "dht", 3072, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(dsTask, "ds18", 3072, nullptr, 2, nullptr, 0);
  Serial.printf("logging to %s\nserial: 'ev <code> [arg]' event marker, 'meta <text>' note, 'status'\n", name);
}

void loop() {
  static uint32_t lastDropped = 0, lastBtn = 0, presses = 0, lastSdFail = 0;
  // serial commands
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if (line.startsWith("ev ")) {
      int code = 0, arg = 0; sscanf(line.c_str() + 3, "%d %d", &code, &arg);
      logMarker((uint8_t)code, (uint16_t)arg); Serial.printf("event %d %d\n", code, arg);
    } else if (line.startsWith("meta ")) {
      metaLine(String("note: ") + line.substring(5));
    } else if (line == "status") {
      Serial.printf("dropped=%u sd_fail=%u heap=%u\n", g_dropped, g_sdFail, ESP.getFreeHeap());
    }
  }
  // marker button (BOOT): EVENT 99
  if (digitalRead(PIN_MARK_BTN) == LOW && millis() - lastBtn > 300) {
    lastBtn = millis(); logMarker(99, ++presses);
  }
  // report queue overflows / SD failures
  if (g_dropped != lastDropped) { logError(ERR_QUEUE_FULL, (uint16_t)(g_dropped - lastDropped)); lastDropped = g_dropped; }
  if (g_sdFail != lastSdFail) { logError(ERR_SD_WRITE, (uint16_t)g_sdFail); lastSdFail = g_sdFail; }
  // LED: dark by default (it would pollute the light sensor); fast blink only when SD is failing
  digitalWrite(PIN_LED, g_sdDead ? ((millis() / 100) & 1) : 0);
  delay(10);
}
