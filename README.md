# HAKU — Air Quality Monitoring and Improvement System (Residential)

This project is firmware for an ESP32-based air quality monitor that reads multiple sensors (PM2.5, CO2, O2, temperature/humidity), controls a fan via relays, sends alerts through Telegram, reports data to ThingsSpeak, and estimates filter usage based on accumulated dust.

## Key Features
- PM2.5 readings from a PMS-series particulate sensor
- CO2 readings from MH-Z19 with a moving average buffer
- O2 measurement via an analog oxygen sensor
- Temperature and humidity from a DHT22
- Fan control (Auto / Manual) using relay outputs based on sensor thresholds
- Telegram notifications for status and alerts
- Data upload to ThingsSpeak for dashboards and charts
- Estimated filter usage (grams and %), stored in EEPROM across reboots
- Telegram commands for status, fan control, filter reset, and graphs

## Hardware (minimum)
- ESP32 development board
- PMS-series particulate matter sensor (UART)
- MH-Z19 CO2 sensor (UART)
- DHT22 temperature/humidity sensor
- Analog oxygen sensor
- 2-channel relay module for fan control
- 4 indicator LEDs for air quality
- Power supply and wiring

## Typical Wiring (as used in the code)
- PMS serial (HardwareSerial(2)): RX = 32, TX = 33
- MH-Z19 (HardwareSerial(1)): RX = 16, TX = 17
- DHT22: GPIO 4
- Oxygen analog input: GPIO 35 (ADC)
- Relay 1: GPIO 25
- Relay 2: GPIO 26
- LEDs:
  - LED_GREEN: GPIO 12
  - LED_YELLOW: GPIO 13
  - LED_ORANGE: GPIO 14
  - LED_RED: GPIO 27

Note: Verify pin mapping for your specific ESP32 board variant before wiring.

## Configuration (edit in source before upload)
Open `main (1).cpp` and set the following values correctly:
- WiFi SSID and password (`ssid[]`, `pass[]`)
- Blynk auth token (`BLYNK_AUTH_TOKEN`)
- Telegram bot token and chat ID (`BOTtoken`, `CHAT_ID`)
- ThingsSpeak API key (`thingspeakApiKey`) and channel ID used in Telegram links

Security note: Do not commit tokens or secrets to public repositories. Consider using a separate untracked config file or other secret management.

## Libraries (Arduino IDE / PlatformIO)
- Blynk (BlynkSimpleEsp32)
- DHT sensor library
- UniversalTelegramBot
- ArduinoJson
- MHZ19
- EEPROM (built-in)
- HTTPClient (built-in)
- WiFi (built-in)

Install required libraries from the Arduino Library Manager or add them to your PlatformIO configuration.

## Build & Upload (Arduino IDE)
1. Install ESP32 board support for Arduino if not already installed.
2. Open `main (1).cpp` in Arduino IDE.
3. Install the libraries listed above via Library Manager.
4. Select your ESP32 board (e.g., "ESP32 Dev Module") and the correct COM port.
5. Edit credentials and tokens in the source file.
6. Upload to the board.

PlatformIO: create an ESP32 project, add the source file, ensure libraries are declared in `platformio.ini`, then build/upload with PlatformIO commands.

## Telegram Commands (implemented in the firmware)
- /status — Show current readings (Temp, Humidity, PM2.5, CO2, O2, filter status, fan state)
- /help — List available commands
- /graph — Show a menu of graph commands
- /graph_pm25, /graph_co2, /graph_4_values, /graph_all — Graph links (ThingsSpeak)
- /fan_on — Turn fan ON (Manual mode)
- /fan_off — Turn fan OFF (switch to Auto)
- /reset_filter — Reset filter usage calculation and EEPROM values
- /co2_test — Run CO2 sensor self-test and report readings
- /co2_reset — Clear CO2 moving average buffer and reset baseline
- /co2_calibrate — Send zero calibration command (requires fresh air ~20 minutes)

## Important Behaviors & Notes
- Filter usage is estimated from airflow rate (`AIRFLOW_RATE_M3_PER_HR`), sample interval, PM2.5 concentration, and filter efficiency (`FILTER_EFFICIENCY`).
- Dust accumulation and sample counters are stored in EEPROM using a magic number check to persist across reboots.
- Alert flags (e.g., `tempAlertSent`, `co2AlertSent`, `filterAlertSent`) prevent repeated identical notifications.
- Data is pushed to ThingsSpeak every `thingspeakInterval` (default 20 seconds in the code). ThingsSpeak rate limits apply — do not set interval lower than the service minimum (typically 15 seconds).

## Troubleshooting
- WiFi fails to connect: confirm SSID/password and WiFi coverage.
- Telegram messages not delivered: confirm BOT token and CHAT_ID are correct.
- Strange sensor values: verify wiring, baud rates for UART devices, and sensor health.
- EEPROM not saving: confirm `EEPROM.begin(EEPROM_SIZE)` succeeds and EEPROM size fits data for your board.

## Files of Interest
- `main (1).cpp` — Main firmware source (sensors, control logic, Telegram/ThingsSpeak integration)

## Future Enhancements
- Add a web interface or REST API for local monitoring and control.
- Persist historical data into a time-series database (e.g., InfluxDB) instead of/in addition to ThingsSpeak.
- Use a proper flow sensor to measure actual airflow for more accurate filter life estimation.
- Move credentials out of source code and use a secure configuration/storage method.

---

Author: Jessadakron2003

If you want, I can commit this README to the repository (English version), or provide a bilingual README (English + Thai), or generate a sample `platformio.ini` / library list. Reply with which option you prefer.