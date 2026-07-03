#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <time.h>
#include "secrets.h"

// Timezone: Turkey, UTC+3
const long GMT_OFFSET_SEC = 3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

// DHT11 configuration
#define DHT_PIN 14       // NodeMCU D5 = GPIO14
#define DHT_TYPE DHT11

// History buffer configuration
#define HISTORY_SIZE 30

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BMP085 bmp;
DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);

// Current sensor readings
float dhtTemperature = 0.0;
float humidity = 0.0;
float bmpTemperature = 0.0;
float pressure = 0.0;
bool dhtOk = false;

// Historical data buffers
float temperatureHistory[HISTORY_SIZE];
float humidityHistory[HISTORY_SIZE];
float pressureHistory[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

// Timing
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL_MS = 2000;

// Pressure trend
String pressureTrend = "Collecting";
String pressureTrendSymbol = "...";

String twoDigit(int value) {
  if (value < 10) return String("0") + String(value);
  return String(value);
}

String getTimeString() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) return "--:--:--";
  return twoDigit(timeInfo.tm_hour) + ":" + twoDigit(timeInfo.tm_min) + ":" + twoDigit(timeInfo.tm_sec);
}

String getDateString() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) return "--.--.----";
  return twoDigit(timeInfo.tm_mday) + "." + twoDigit(timeInfo.tm_mon + 1) + "." + String(timeInfo.tm_year + 1900);
}

int getCurrentHour() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) return 12;
  return timeInfo.tm_hour;
}

String getWeatherLabel() {
  if (!dhtOk) return "No Data";
  if (humidity < 40 && pressure > 905) return "Sunny";
  if (humidity >= 40 && humidity < 65) return "Partly Cloudy";
  return "Cloudy / Humid";
}

String getWeatherEmoji() {
  if (!dhtOk) return "?";
  if (humidity < 40 && pressure > 905) return "☀️";
  if (humidity >= 40 && humidity < 65) return "⛅";
  return "☁️";
}

String getWeatherClass() {
  if (!dhtOk) return "unknown";
  if (humidity < 40 && pressure > 905) return "sunny";
  if (humidity >= 40 && humidity < 65) return "partly";
  return "cloudy";
}

void addHistory(float temperatureValue, float humidityValue, float pressureValue) {
  temperatureHistory[historyIndex] = temperatureValue;
  humidityHistory[historyIndex] = humidityValue;
  pressureHistory[historyIndex] = pressureValue;

  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

float getMinValue(float values[]) {
  if (historyCount == 0) return 0;
  float minValue = values[0];
  for (int i = 1; i < historyCount; i++) {
    if (values[i] < minValue) minValue = values[i];
  }
  return minValue;
}

float getMaxValue(float values[]) {
  if (historyCount == 0) return 0;
  float maxValue = values[0];
  for (int i = 1; i < historyCount; i++) {
    if (values[i] > maxValue) maxValue = values[i];
  }
  return maxValue;
}

String historyToJson(float values[]) {
  String json = "[";
  int startIndex = 0;
  if (historyCount == HISTORY_SIZE) startIndex = historyIndex;

  for (int i = 0; i < historyCount; i++) {
    int idx = (startIndex + i) % HISTORY_SIZE;
    json += String(values[idx], 1);
    if (i < historyCount - 1) json += ",";
  }

  json += "]";
  return json;
}

void updatePressureTrend() {
  if (historyCount < 6) {
    pressureTrend = "Collecting";
    pressureTrendSymbol = "...";
    return;
  }

  int oldestIndex = (historyCount == HISTORY_SIZE) ? historyIndex : 0;
  float oldPressure = pressureHistory[oldestIndex];
  float pressureDifference = pressure - oldPressure;

  if (pressureDifference > 0.5) {
    pressureTrend = "Rising";
    pressureTrendSymbol = "+";
  } else if (pressureDifference < -0.5) {
    pressureTrend = "Falling";
    pressureTrendSymbol = "-";
  } else {
    pressureTrend = "Stable";
    pressureTrendSymbol = "=";
  }
}

void readSensors() {
  float newDhtTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  dhtOk = !(isnan(newDhtTemperature) || isnan(newHumidity));

  if (dhtOk) {
    dhtTemperature = newDhtTemperature;
    humidity = newHumidity;
  }

  bmpTemperature = bmp.readTemperature();
  pressure = bmp.readPressure() / 100.0;

  if (dhtOk) {
    addHistory(dhtTemperature, humidity, pressure);
    updatePressureTrend();
  }
}

void drawSunIcon(int x, int y) {
  display.drawCircle(x, y, 8, SSD1306_WHITE);
  display.fillCircle(x, y, 4, SSD1306_WHITE);
  display.drawLine(x, y - 12, x, y - 16, SSD1306_WHITE);
  display.drawLine(x, y + 12, x, y + 16, SSD1306_WHITE);
  display.drawLine(x - 12, y, x - 16, y, SSD1306_WHITE);
  display.drawLine(x + 12, y, x + 16, y, SSD1306_WHITE);
  display.drawLine(x - 9, y - 9, x - 13, y - 13, SSD1306_WHITE);
  display.drawLine(x + 9, y - 9, x + 13, y - 13, SSD1306_WHITE);
  display.drawLine(x - 9, y + 9, x - 13, y + 13, SSD1306_WHITE);
  display.drawLine(x + 9, y + 9, x + 13, y + 13, SSD1306_WHITE);
}

void drawCloudIcon(int x, int y) {
  display.fillCircle(x - 8, y, 6, SSD1306_WHITE);
  display.fillCircle(x, y - 4, 8, SSD1306_WHITE);
  display.fillCircle(x + 10, y, 6, SSD1306_WHITE);
  display.fillRect(x - 14, y, 28, 8, SSD1306_WHITE);
}

void drawPartlyCloudyIcon(int x, int y) {
  display.drawCircle(x - 8, y - 6, 6, SSD1306_WHITE);
  display.drawLine(x - 8, y - 16, x - 8, y - 12, SSD1306_WHITE);
  display.drawLine(x - 18, y - 6, x - 14, y - 6, SSD1306_WHITE);
  display.drawLine(x + 2, y - 6, x + 6, y - 6, SSD1306_WHITE);
  display.fillCircle(x - 4, y + 2, 5, SSD1306_WHITE);
  display.fillCircle(x + 4, y - 1, 7, SSD1306_WHITE);
  display.fillCircle(x + 12, y + 2, 5, SSD1306_WHITE);
  display.fillRect(x - 8, y + 2, 24, 7, SSD1306_WHITE);
}

void drawWeatherIcon() {
  if (!dhtOk) {
    display.setTextSize(2);
    display.setCursor(100, 6);
    display.print("?");
    return;
  }

  if (humidity < 40 && pressure > 905) {
    drawSunIcon(104, 15);
  } else if (humidity >= 40 && humidity < 65) {
    drawPartlyCloudyIcon(100, 14);
  } else {
    drawCloudIcon(104, 14);
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("WEATHER STATION");
  drawWeatherIcon();

  display.setCursor(0, 13);
  if (dhtOk) {
    display.print("Temp: ");
    display.print(dhtTemperature, 1);
    display.println(" C");
  } else {
    display.println("Temp: ERROR");
  }

  display.setCursor(0, 25);
  if (dhtOk) {
    display.print("Hum : ");
    display.print(humidity, 1);
    display.println(" %");
  } else {
    display.println("Hum : ERROR");
  }

  display.setCursor(0, 37);
  display.print("Pres: ");
  display.print(pressure, 1);
  display.print(" ");
  display.print(pressureTrendSymbol);

  display.setCursor(0, 50);
  display.print(getTimeString());
  display.print(" ");
  display.print(pressureTrend);

  display.display();
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Smart Weather Station</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0; min-height: 100vh; font-family: Arial, sans-serif;
      display: flex; justify-content: center; align-items: center;
      padding: 22px; color: #0f172a;
      background: linear-gradient(160deg, #fef3c7, #7dd3fc);
      transition: background 0.8s ease, color 0.8s ease;
    }
    body.night { color: #e5e7eb; background: linear-gradient(160deg, #020617, #1e293b, #334155); }
    .app { width: 100%; max-width: 520px; border-radius: 34px; padding: 26px; background: rgba(255,255,255,0.84); backdrop-filter: blur(12px); box-shadow: 0 20px 55px rgba(15,23,42,0.25); }
    body.night .app { background: rgba(15,23,42,0.78); box-shadow: 0 20px 55px rgba(0,0,0,0.45); }
    .top { text-align: center; padding: 12px 8px 22px; border-radius: 28px; transition: background 0.8s ease; }
    .app.sunny .top { background: linear-gradient(135deg, #fde68a, #fbbf24); }
    .app.partly .top { background: linear-gradient(135deg, #bae6fd, #38bdf8); }
    .app.cloudy .top { background: linear-gradient(135deg, #cbd5e1, #64748b); }
    .app.unknown .top { background: linear-gradient(135deg, #e5e7eb, #cbd5e1); }
    .weather-icon { font-size: 64px; line-height: 1; animation: floatIcon 2.4s ease-in-out infinite; }
    .app.sunny .weather-icon { animation: floatIcon 2.4s ease-in-out infinite, sunGlow 1.8s ease-in-out infinite; }
    .title { margin-top: 12px; font-size: 30px; font-weight: 800; }
    .subtitle { margin-top: 6px; font-size: 15px; opacity: 0.78; }
    .status { display: inline-block; margin-top: 14px; padding: 9px 16px; border-radius: 999px; background: rgba(255,255,255,0.7); font-weight: 700; }
    .time-box { margin-top: 14px; font-size: 15px; font-weight: 700; opacity: 0.88; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-top: 20px; }
    .card { background: rgba(255,255,255,0.88); border-radius: 22px; padding: 18px; box-shadow: 0 8px 24px rgba(15,23,42,0.10); transition: transform 0.25s ease, background 0.8s ease; }
    .card:hover { transform: translateY(-3px); }
    body.night .card { background: rgba(30,41,59,0.88); }
    .label { font-size: 14px; opacity: 0.72; font-weight: 700; }
    .value { margin-top: 10px; font-size: 27px; font-weight: 850; }
    .source { margin-top: 8px; font-size: 12px; opacity: 0.62; }
    .trend-box { margin-top: 18px; padding: 16px; border-radius: 22px; background: rgba(255,255,255,0.72); text-align: center; font-weight: 750; }
    body.night .trend-box { background: rgba(30,41,59,0.82); }
    .stats { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin-top: 14px; }
    .stat-mini { border-radius: 16px; padding: 12px; background: rgba(255,255,255,0.62); text-align: center; font-size: 12px; }
    body.night .stat-mini { background: rgba(30,41,59,0.82); }
    .stat-mini strong { display: block; margin-top: 5px; font-size: 14px; }
    .chart-box { margin-top: 18px; background: rgba(255,255,255,0.72); border-radius: 22px; padding: 16px; }
    body.night .chart-box { background: rgba(30,41,59,0.82); }
    .chart-title { font-weight: 800; margin-bottom: 8px; }
    canvas { width: 100%; height: 130px; display: block; }
    .bottom { margin-top: 18px; padding: 14px; border-radius: 20px; text-align: center; background: rgba(255,255,255,0.58); font-size: 14px; }
    body.night .bottom { background: rgba(30,41,59,0.72); }
    .pulse { display: inline-block; width: 9px; height: 9px; margin-right: 7px; border-radius: 50%; background: #22c55e; animation: pulseDot 1.5s ease-in-out infinite; }
    @keyframes floatIcon { 0%,100% { transform: translateY(0); } 50% { transform: translateY(-8px); } }
    @keyframes sunGlow { 0%,100% { filter: drop-shadow(0 0 4px rgba(251,191,36,0.6)); } 50% { filter: drop-shadow(0 0 18px rgba(251,191,36,1)); } }
    @keyframes pulseDot { 0%,100% { opacity: 0.4; transform: scale(0.85); } 50% { opacity: 1; transform: scale(1.2); } }
    @media (max-width: 460px) { .grid { grid-template-columns: 1fr; } .stats { grid-template-columns: 1fr; } .title { font-size: 26px; } }
  </style>
</head>
<body>
  <main id="app" class="app sunny">
    <section class="top">
      <div id="icon" class="weather-icon">☀️</div>
      <div class="title">Weather Station</div>
      <div class="subtitle">ESP8266 Live IoT Dashboard</div>
      <div id="status" class="status">Sunny</div>
      <div class="time-box"><div id="dateText">--.--.----</div><div id="timeText">--:--:--</div></div>
    </section>

    <section class="grid">
      <div class="card"><div class="label">Temperature</div><div id="temp" class="value">-- °C</div><div class="source">DHT11</div></div>
      <div class="card"><div class="label">Humidity</div><div id="hum" class="value">-- %</div><div class="source">DHT11</div></div>
      <div class="card"><div class="label">Pressure</div><div id="pres" class="value">-- hPa</div><div class="source">BMP180</div></div>
      <div class="card"><div class="label">BMP Temperature</div><div id="bmpTemp" class="value">-- °C</div><div class="source">BMP180</div></div>
    </section>

    <section class="trend-box">Pressure Trend: <span id="trendText">Collecting</span></section>

    <section class="stats">
      <div class="stat-mini">Temp Min/Max<strong id="tempMinMax">-- / --</strong></div>
      <div class="stat-mini">Hum Min/Max<strong id="humMinMax">-- / --</strong></div>
      <div class="stat-mini">Pres Min/Max<strong id="presMinMax">-- / --</strong></div>
    </section>

    <section class="chart-box"><div class="chart-title">Temperature Chart</div><canvas id="tempChart" width="460" height="130"></canvas></section>
    <section class="chart-box"><div class="chart-title">Humidity Chart</div><canvas id="humChart" width="460" height="130"></canvas></section>
    <section class="chart-box"><div class="chart-title">Pressure Chart</div><canvas id="presChart" width="460" height="130"></canvas></section>

    <div class="bottom"><span class="pulse"></span><span id="liveText">Waiting for live data...</span><br><span id="ipText">IP: --</span></div>
  </main>

  <script>
    function applyDayNightTheme(hour) {
      if (hour >= 20 || hour < 6) document.body.classList.add("night");
      else document.body.classList.remove("night");
    }

    function drawChart(canvasId, values, unit) {
      const canvas = document.getElementById(canvasId);
      const ctx = canvas.getContext("2d");
      const w = canvas.width;
      const h = canvas.height;
      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = "rgba(255,255,255,0.12)";
      ctx.fillRect(0, 0, w, h);

      if (!values || values.length < 2) {
        ctx.fillStyle = "#64748b";
        ctx.font = "16px Arial";
        ctx.fillText("Collecting data...", 18, 70);
        return;
      }

      let min = Math.min(...values);
      let max = Math.max(...values);
      if (Math.abs(max - min) < 0.1) { max += 0.5; min -= 0.5; }

      const pad = 24;
      const chartW = w - pad * 2;
      const chartH = h - pad * 2;

      ctx.strokeStyle = "rgba(100,116,139,0.35)";
      ctx.lineWidth = 1;
      for (let i = 0; i <= 3; i++) {
        const y = pad + (chartH / 3) * i;
        ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w - pad, y); ctx.stroke();
      }

      ctx.beginPath();
      values.forEach((val, i) => {
        const x = pad + (chartW * i) / (values.length - 1);
        const y = h - pad - ((val - min) / (max - min)) * chartH;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.strokeStyle = "#0f172a";
      ctx.lineWidth = 3;
      ctx.stroke();

      ctx.fillStyle = "#0f172a";
      values.forEach((val, i) => {
        const x = pad + (chartW * i) / (values.length - 1);
        const y = h - pad - ((val - min) / (max - min)) * chartH;
        ctx.beginPath(); ctx.arc(x, y, 3, 0, Math.PI * 2); ctx.fill();
      });

      ctx.font = "12px Arial";
      ctx.fillText(max.toFixed(1) + " " + unit, 8, 14);
      ctx.fillText(min.toFixed(1) + " " + unit, 8, h - 8);
    }

    async function updateData() {
      try {
        const response = await fetch("/data?t=" + Date.now());
        const data = await response.json();
        const app = document.getElementById("app");
        app.className = "app " + data.theme;
        applyDayNightTheme(data.hour);

        document.getElementById("icon").textContent = data.emoji;
        document.getElementById("status").textContent = data.label;
        document.getElementById("dateText").textContent = data.date;
        document.getElementById("timeText").textContent = data.time;

        if (data.dhtOk) {
          document.getElementById("temp").innerHTML = data.dhtTemperature.toFixed(1) + " °C";
          document.getElementById("hum").innerHTML = data.humidity.toFixed(1) + " %";
        } else {
          document.getElementById("temp").textContent = "Error";
          document.getElementById("hum").textContent = "Error";
        }

        document.getElementById("pres").innerHTML = data.pressure.toFixed(1) + " hPa";
        document.getElementById("bmpTemp").innerHTML = data.bmpTemperature.toFixed(1) + " °C";
        document.getElementById("trendText").textContent = data.trend;
        document.getElementById("tempMinMax").textContent = data.temperatureMin.toFixed(1) + " / " + data.temperatureMax.toFixed(1) + " °C";
        document.getElementById("humMinMax").textContent = data.humidityMin.toFixed(1) + " / " + data.humidityMax.toFixed(1) + " %";
        document.getElementById("presMinMax").textContent = data.pressureMin.toFixed(1) + " / " + data.pressureMax.toFixed(1) + " hPa";

        drawChart("tempChart", data.temperatureHistory, "C");
        drawChart("humChart", data.humidityHistory, "%");
        drawChart("presChart", data.pressureHistory, "hPa");

        document.getElementById("liveText").textContent = "Last update: " + data.time;
        document.getElementById("ipText").textContent = "IP: " + data.ip;
      } catch (error) {
        document.getElementById("liveText").textContent = "Data fetch failed";
      }
    }

    updateData();
    setInterval(updateData, 2000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleData() {
  float temperatureMin = getMinValue(temperatureHistory);
  float temperatureMax = getMaxValue(temperatureHistory);
  float humidityMin = getMinValue(humidityHistory);
  float humidityMax = getMaxValue(humidityHistory);
  float pressureMin = getMinValue(pressureHistory);
  float pressureMax = getMaxValue(pressureHistory);

  String json = "{";
  json += "\"dhtOk\":" + String(dhtOk ? "true" : "false") + ",";
  json += "\"dhtTemperature\":" + String(dhtTemperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"bmpTemperature\":" + String(bmpTemperature, 1) + ",";
  json += "\"pressure\":" + String(pressure, 1) + ",";
  json += "\"label\":\"" + getWeatherLabel() + "\",";
  json += "\"emoji\":\"" + getWeatherEmoji() + "\",";
  json += "\"theme\":\"" + getWeatherClass() + "\",";
  json += "\"trend\":\"" + pressureTrend + "\",";
  json += "\"date\":\"" + getDateString() + "\",";
  json += "\"time\":\"" + getTimeString() + "\",";
  json += "\"hour\":" + String(getCurrentHour()) + ",";
  json += "\"temperatureMin\":" + String(temperatureMin, 1) + ",";
  json += "\"temperatureMax\":" + String(temperatureMax, 1) + ",";
  json += "\"humidityMin\":" + String(humidityMin, 1) + ",";
  json += "\"humidityMax\":" + String(humidityMax, 1) + ",";
  json += "\"pressureMin\":" + String(pressureMin, 1) + ",";
  json += "\"pressureMax\":" + String(pressureMax, 1) + ",";
  json += "\"temperatureHistory\":" + historyToJson(temperatureHistory) + ",";
  json += "\"humidityHistory\":" + historyToJson(humidityHistory) + ",";
  json += "\"pressureHistory\":" + historyToJson(pressureHistory) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  server.send(200, "application/json; charset=utf-8", json);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(4, 5); // SDA = D2(GPIO4), SCL = D1(GPIO5)

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed.");
    while (true) delay(100);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Starting system...");
  display.display();

  if (!bmp.begin()) {
    Serial.println("BMP180 not found.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("BMP180 missing!");
    display.display();
    while (true) delay(100);
  }

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.google.com");

  Serial.println("Waiting for NTP time...");
  struct tm timeInfo;
  while (!getLocalTime(&timeInfo)) {
    Serial.println("Time unavailable, retrying...");
    delay(1000);
  }
  Serial.println("NTP time synchronized.");

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started.");

  readSensors();
  updateOLED();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    readSensors();
    updateOLED();

    Serial.println("--------------------");
    Serial.print("Date: ");
    Serial.println(getDateString());
    Serial.print("Time: ");
    Serial.println(getTimeString());

    if (dhtOk) {
      Serial.print("DHT temperature: ");
      Serial.print(dhtTemperature);
      Serial.println(" C");
      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
    } else {
      Serial.println("DHT11 read failed.");
    }

    Serial.print("BMP temperature: ");
    Serial.print(bmpTemperature);
    Serial.println(" C");
    Serial.print("Pressure: ");
    Serial.print(pressure);
    Serial.println(" hPa");
    Serial.print("Pressure trend: ");
    Serial.println(pressureTrend);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
}
