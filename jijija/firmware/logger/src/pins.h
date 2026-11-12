#pragma once
// PLACEHOLDER pins (placeholder, chosen by Claude without seeing your board). Hardware is owned by the user:
// change every pin below to match your actual wiring before building.
// Avoided on purpose: GPIO 3, 45, 46 (strapping), 19/20 (USB), 26-32 (flash), 33-37 (octal PSRAM on N8R8/N16R8 boards).
// Analog inputs use ADC1 only (GPIO 1-10), because ADC2 conflicts with Wi-Fi.

#define PIN_LDR        1   // ADC1_CH0  light (LDR divider)
#define PIN_BATT       2   // ADC1_CH1  battery divider ("hunger")
#define PIN_DHT        4   // DHT11 data (needs 4.7k-10k pull-up to 3V3 if the module has none)
#define PIN_LED        5   // LED (via resistor)
#define PIN_BUZZER     6   // passive piezo (unused by logger firmware; reserved)
#define PIN_ONEWIRE    7   // DS18B20 data, 4.7k pull-up to 3V3
#define PIN_I2C_SDA    8   // MPU6050, BH1750, (DS3231 later)
#define PIN_I2C_SCL    9
#define PIN_SD_CS     10   // SD module over SPI (FSPI default pins on S3)
#define PIN_SD_MOSI   11
#define PIN_SD_SCK    12
#define PIN_SD_MISO   13
#define PIN_MARK_BTN   0   // BOOT button used as event marker after boot
