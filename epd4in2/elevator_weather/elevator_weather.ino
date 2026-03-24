#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "DEV_Config.h"
#include "EPD_4in2_V2.h"
#include "GUI_Paint.h"
#include "icons.h"
#include "secrets.h"

#ifndef HA_USE_INTERNAL_BASE_URL
#define HA_USE_INTERNAL_BASE_URL 0
#endif

#ifndef HA_INTERNAL_BASE_URL
#define HA_INTERNAL_BASE_URL ""
#endif

#ifndef WIFI_USE_STATIC_IP
#define WIFI_USE_STATIC_IP 0
#endif

#ifndef WIFI_STATIC_IP_ADDR
#define WIFI_STATIC_IP_ADDR 192, 168, 0, 50
#endif

#ifndef WIFI_GATEWAY_ADDR
#define WIFI_GATEWAY_ADDR 192, 168, 0, 1
#endif

#ifndef WIFI_SUBNET_MASK
#define WIFI_SUBNET_MASK 255, 255, 255, 0
#endif

#ifndef WIFI_PRIMARY_DNS_ADDR
#define WIFI_PRIMARY_DNS_ADDR 192, 168, 0, 1
#endif

#ifndef WIFI_SECONDARY_DNS_ADDR
#define WIFI_SECONDARY_DNS_ADDR 8, 8, 8, 8
#endif

// 4.2인치 E-Ink 해상도
#define EPD_WIDTH 400
#define EPD_HEIGHT 300

// 날씨 정보 구조체
struct WeatherInfo {
  String temp;
  String humi;
  String rain;
  String dust;
  String cond;
  String wind;
};

WeatherInfo gWeather;
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 10 * 60 * 1000; // 10분
unsigned long lastFullRefreshAt = 0;
const unsigned long WAITING_FULL_REFRESH_INTERVAL = 30 * 60 * 1000; // 30분
const unsigned long STATUS_POLL_WAITING_INTERVAL = 5000;
const unsigned long STATUS_POLL_ACTIVE_INTERVAL = 1000;
const unsigned long STATUS_POLL_DEFAULT_INTERVAL = 3000;
const unsigned long WIFI_RECONNECT_INTERVAL = 5000;
const unsigned long NIGHT_MODE_CHECK_INTERVAL = 1000;
const unsigned long HOME_REFRESH_DELAY = 10000;
const unsigned long LOOP_IDLE_DELAY = 20;

// 심야 모드를 위한 시간 설정
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 9 * 3600; // 한국 시간 (UTC+9)
const int   daylightOffset_sec = 0;

// 심야 모드: 00:00 부터 06:00 까지 절전
#define SLEEP_START_HOUR 0
#define SLEEP_END_HOUR 6

#define WEATHER_REGION_Y 130
UBYTE *BlackImage;
String lastStatus = "";
bool isFirstRun = true;
unsigned long arrivalHomeTime = 0; // 23층 도착 시간 기록
bool homeRefreshed = false;        // 23층 도착 후 10초 뒤 리프레시 여부
unsigned long lastStatusPollAt = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastNightModeCheckAt = 0;

// 함수 선언
String getElevatorStatus();
bool fetchWeatherInfo();
void updateDisplay(const String &statusText, bool redrawStatus, bool redrawWeather, bool forceFullRefresh);
void checkNightMode();
const char* getHaBaseUrl();
bool beginHaRequest(HTTPClient &http, WiFiClient &plainClient, WiFiClientSecure &secureClient, const String &url);
void drawStatusRegion(const String &statusText);
void drawWeatherRegion();
void drawFrameTop();
unsigned long getStatusPollInterval();

void setup() {
  // 1. 절전을 위해 CPU 주파수를 80MHz로 낮춤
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  
  // GPIO 초기화
  DEV_Module_Init();
  
  // 초기화 및 화면 클리어 (시작 시)
  EPD_4IN2_V2_Init();
  EPD_4IN2_V2_Clear();
  EPD_4IN2_V2_Init_Fast(Seconds_1S); // 이후 동작을 위해 Fast 모드 유지

  // WiFi 연결
  // 실시간 업데이트 수신을 위해 연결 유지 (delay() 중 Modem Sleep 자동 활성화)
  WiFi.mode(WIFI_STA);
#if WIFI_USE_STATIC_IP
  IPAddress localIP(WIFI_STATIC_IP_ADDR);
  IPAddress gateway(WIFI_GATEWAY_ADDR);
  IPAddress subnet(WIFI_SUBNET_MASK);
  IPAddress primaryDNS(WIFI_PRIMARY_DNS_ADDR);
  IPAddress secondaryDNS(WIFI_SECONDARY_DNS_ADDR);

  if (WiFi.config(localIP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.print("Static IP configured: ");
    Serial.println(localIP);
  } else {
    Serial.println("Failed to configure static IP. Falling back to DHCP.");
  }
#endif
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");
  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("HA Base URL: ");
  Serial.println(getHaBaseUrl());

  // 심야 모드 로직을 위한 시간 동기화
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 디스플레이 버퍼 메모리 할당
  UWORD Imagesize = ((EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
  if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
      Serial.println("Failed to apply for black memory...");
      while(1);
  }

  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);

  fetchWeatherInfo();
}

void loop() {
  unsigned long now = millis();

  if (lastNightModeCheckAt == 0 || (now - lastNightModeCheckAt) >= NIGHT_MODE_CHECK_INTERVAL) {
    lastNightModeCheckAt = now;
    checkNightMode();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (lastReconnectAttempt == 0 || (now - lastReconnectAttempt) >= WIFI_RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      Serial.println("WiFi Disconnected. Reconnecting...");
      WiFi.reconnect();
    }
    delay(LOOP_IDLE_DELAY);
    return;
  }

  unsigned long statusPollInterval = getStatusPollInterval();
  if (lastStatusPollAt == 0 || (now - lastStatusPollAt) >= statusPollInterval) {
    lastStatusPollAt = now;

    String currentStatus = getElevatorStatus();
    if (currentStatus != lastStatus) {
      Serial.println("Status Changed: " + currentStatus);

      bool weatherChanged = false;
      if (lastWeatherUpdate == 0) {
        weatherChanged = fetchWeatherInfo();
      }

      if (currentStatus == "23층") {
        arrivalHomeTime = now;
        homeRefreshed = false;
      } else {
        arrivalHomeTime = 0;
        homeRefreshed = false;
      }

      bool isEnteringWaiting = (currentStatus == "호출대기중" && lastStatus != "호출대기중");
      bool waitingMaintenanceRefresh =
          (currentStatus == "호출대기중") &&
          !isEnteringWaiting &&
          (lastFullRefreshAt == 0 || (now - lastFullRefreshAt) >= WAITING_FULL_REFRESH_INTERVAL);
      bool forceFullRefresh = isFirstRun || isEnteringWaiting || waitingMaintenanceRefresh;

      updateDisplay(currentStatus, true, weatherChanged || isFirstRun, forceFullRefresh);
      lastStatus = currentStatus;
    }
  }

  if (lastStatus == "23층" && !homeRefreshed && arrivalHomeTime != 0 && (now - arrivalHomeTime) >= HOME_REFRESH_DELAY) {
    Serial.println("Home Refresh Triggered (10s delay)");
    homeRefreshed = true;
    updateDisplay(lastStatus, true, false, true);
  }

  if (lastStatus == "호출대기중") {
    bool waitingMaintenanceRefresh =
        lastFullRefreshAt == 0 || (now - lastFullRefreshAt) >= WAITING_FULL_REFRESH_INTERVAL;

    if (lastWeatherUpdate == 0 || (now - lastWeatherUpdate) >= WEATHER_UPDATE_INTERVAL) {
      Serial.println("Updating Weather Info in Waiting Mode...");
      bool weatherChanged = fetchWeatherInfo();
      if (weatherChanged || waitingMaintenanceRefresh) {
        updateDisplay(lastStatus, false, true, waitingMaintenanceRefresh);
      }
    } else if (waitingMaintenanceRefresh) {
      updateDisplay(lastStatus, false, false, true);
    }
  }

  delay(LOOP_IDLE_DELAY);
}

const char* getHaBaseUrl() {
#if HA_USE_INTERNAL_BASE_URL
  static const char* internalBaseUrl = HA_INTERNAL_BASE_URL;
  if (internalBaseUrl[0] != '\0') {
    return internalBaseUrl;
  }
#endif
  return ha_base_url;
}

bool beginHaRequest(HTTPClient &http, WiFiClient &plainClient, WiFiClientSecure &secureClient, const String &url) {
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    return http.begin(secureClient, url);
  }
  return http.begin(plainClient, url);
}

unsigned long getStatusPollInterval() {
  if (lastStatus == "호출대기중") {
    return STATUS_POLL_WAITING_INTERVAL;
  }
  if (lastStatus != "" && lastStatus != "심야절전중") {
    return STATUS_POLL_ACTIVE_INTERVAL;
  }
  return STATUS_POLL_DEFAULT_INTERVAL;
}

void checkNightMode() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // 시간 동기화 전이면 리턴
    return;
  }

  int currentHour = timeinfo.tm_hour;

  // 현재 시간이 설정된 심야 시간대(예: 00:00 - 06:00)인지 확인
  if (currentHour >= SLEEP_START_HOUR && currentHour < SLEEP_END_HOUR) {
    Serial.printf("Night mode activated (%d:00 ~ %d:00). Going to Deep Sleep.\n", SLEEP_START_HOUR, SLEEP_END_HOUR);
    
    // 심야에도 날씨 업데이트 수행 (1시간 주기)
    fetchWeatherInfo();
    updateDisplay("심야절전중", true, true, true);

     // 다음 정각 혹은 기상 시간(6시)까지 남은 초 계산
    long secondsToMorning = ((SLEEP_END_HOUR - currentHour) * 3600) - (timeinfo.tm_min * 60) - timeinfo.tm_sec;
    long secondsToNextHour = 3600 - (timeinfo.tm_min * 60) - timeinfo.tm_sec;
    
    // 1시간마다 깨어나도록 설정하되, 아침 6시를 넘기지 않도록 함
    long secondsUntilWakeup = (secondsToNextHour < secondsToMorning) ? secondsToNextHour : secondsToMorning;
    
    // 최소 1분 안전시간 (혹시 계산이 0 이하일 경우 방지)
    if (secondsUntilWakeup <= 0) secondsUntilWakeup = 60;

    // 메모리 해제
    free(BlackImage);
    
    // 디스플레이 전원 차단
    EPD_4IN2_V2_Sleep();
    
    // 웨이크업 타이머 설정
    esp_sleep_enable_timer_wakeup((uint64_t)secondsUntilWakeup * 1000000ULL);
    
    // 딥 슬립 진입 (재부팅됨)
    esp_deep_sleep_start();
  }
}

bool fetchWeatherInfo() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient plainClient;
  WiFiClientSecure client;
  HTTPClient http;

  String url = String(getHaBaseUrl()) + "/api/template";
  if (!beginHaRequest(http, plainClient, client, url)) {
    Serial.println("Failed to initialize weather request");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + ha_token);
  http.addHeader("Content-Type", "application/json");

  // 6개의 센서값을 한 번에 가져오기 위한 템플릿
  String templateBody = "{\"template\": \"{\\\"temp\\\": \\\"{{ states('sensor.wn_daeweondong_temperature') }}\\\", \\\"humi\\\": \\\"{{ states('sensor.wn_daeweondong_relative_humidity') }}\\\", \\\"rain\\\": \\\"{{ states('sensor.wn_daeweondong_precipitation_probability') }}\\\", \\\"dust\\\": \\\"{{ states('sensor.wn_daeweondong_pm10_description') }}\\\", \\\"cond\\\": \\\"{{ states('sensor.wn_daeweondong_current_condition') }}\\\", \\\"wind\\\": \\\"{{ states('sensor.wn_daeweondong_wind_speed') }}\\\"}\"}";

  int httpCode = http.POST(templateBody);
  bool weatherChanged = false;
  
  if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload); // template API는 렌더링된 문자열을 반환함
      
      WeatherInfo newWeather;
      newWeather.temp = doc["temp"].as<String>();
      newWeather.humi = doc["humi"].as<String>();
      newWeather.rain = doc["rain"].as<String>();
      newWeather.dust = doc["dust"].as<String>();
      newWeather.cond = doc["cond"].as<String>();
      newWeather.wind = doc["wind"].as<String>();
      
      // 단위가 없으면 추가하고 포맷팅
      if (newWeather.humi != "unavailable") newWeather.humi += "%";
      if (newWeather.rain != "unavailable") newWeather.rain += "%";
      if (newWeather.wind != "unavailable") newWeather.wind += "m/s";

      weatherChanged =
          newWeather.temp != gWeather.temp ||
          newWeather.humi != gWeather.humi ||
          newWeather.rain != gWeather.rain ||
          newWeather.dust != gWeather.dust ||
          newWeather.cond != gWeather.cond ||
          newWeather.wind != gWeather.wind;

      gWeather = newWeather;
      
      lastWeatherUpdate = millis();
      Serial.println("Weather Updated: " + gWeather.temp + ", " + gWeather.cond);
  } else {
      Serial.print("Weather Fetch Failed: ");
      Serial.println(httpCode);
  }
  http.end();
  return weatherChanged;
}

String getElevatorStatus() {
  WiFiClient plainClient;
  WiFiClientSecure client;
  HTTPClient http;

  // secrets에 정의된 URL 생성
  String url = String(getHaBaseUrl()) + "/api/states/sensor.elevator_0_0_6_floor";
//   String url = String(ha_base_url) + "/api/states/sensor.airdata_2_eco2";
  
  if (!beginHaRequest(http, plainClient, client, url)) {
    Serial.println("Failed to initialize elevator request");
    return lastStatus;
  }
  http.addHeader("Authorization", String("Bearer ") + ha_token);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.GET();
  String result = lastStatus; // 오류 발생 시 이전 상태 유지

  if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
          const char* state = doc["state"];
          if (state) {
            String s = String(state);
            // 값이 없거나, 알수 없음, 사용 불가 상태 체크
            if (s == "unavailable" || s == "unknown" || s == "null" || s == "") {
               result = "호출대기중";
            } else {
               // 정수 부분만 사용 (예: "30.0" -> "30")
               float f = s.toFloat();
               result = String((int)f) + "층";
            }
          }
      } else {
        Serial.println("JSON Parse Error");
      }
  } else {
      Serial.print("Elevator Fetch Failed: ");
      Serial.println(httpCode);
  }
// 참고: HTTP 실패 시 깜빡임이나 에러 메시지를 피하기 위해 lastStatus 유지
  
  http.end();
  return result;
}

void drawFrameTop() {
  Paint_DrawLine(2, 2, EPD_WIDTH - 3, 2, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(2, 2, 2, WEATHER_REGION_Y - 1, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(EPD_WIDTH - 3, 2, EPD_WIDTH - 3, WEATHER_REGION_Y - 1, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
}

void drawStatusRegion(const String &statusText) {
  Paint_DrawRectangle(0, 0, EPD_WIDTH, WEATHER_REGION_Y - 1, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  drawFrameTop();

  if (statusText == "심야절전중") {
    Paint_DrawString_CN(40, 33, "심야절전중", &Font64KR, BLACK, WHITE);
    return;
  }

  if (statusText == "호출대기중") {
    Paint_DrawString_CN(40, 33, "호출대기중", &Font64KR, BLACK, WHITE);
    return;
  }

  String floorNum = "";
  String suffix = "층";
  int idx = statusText.indexOf("층");
  if (idx > 0) {
    floorNum = statusText.substring(0, idx);
  } else {
    floorNum = statusText;
    suffix = "";
  }

  int digitW = 64;
  int suffixW = (suffix.length() > 0) ? 64 : 0;
  int textW = (floorNum.length() * digitW) + suffixW;

  int startX = (EPD_WIDTH - textW) / 2;
  int startY = 33;

  Paint_DrawString_CN(startX, startY, floorNum.c_str(), &Font64KR, BLACK, WHITE);
  if (suffix != "") {
    Paint_DrawString_CN(startX + (floorNum.length() * digitW), startY, suffix.c_str(), &Font64KR, BLACK, WHITE);
  }
}

void drawWeatherRegion() {
  Paint_DrawRectangle(0, WEATHER_REGION_Y, EPD_WIDTH, EPD_HEIGHT, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  int tableY = WEATHER_REGION_Y;
  int rowH = 80;
  int colW = EPD_WIDTH / 3;

  Paint_DrawLine(2, tableY, EPD_WIDTH - 3, tableY, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(2, tableY, 2, EPD_HEIGHT - 3, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(EPD_WIDTH - 3, tableY, EPD_WIDTH - 3, EPD_HEIGHT - 3, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(2, EPD_HEIGHT - 3, EPD_WIDTH - 3, EPD_HEIGHT - 3, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(2, tableY + rowH, EPD_WIDTH - 3, tableY + rowH, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(2, tableY + rowH * 2, EPD_WIDTH - 3, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

  Paint_DrawLine(colW, tableY, colW, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(colW * 2, tableY, colW * 2, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

  int c1 = 0, c2 = colW, c3 = colW * 2;
  int center1 = c1 + colW / 2, center2 = c2 + colW / 2, center3 = c3 + colW / 2;

  int w1 = 77;
  int startX1 = center1 - w1 / 2;
  Paint_DrawBitmap(startX1, tableY + 10, Icon_Thermometer, 32, 32, BLACK);
  Paint_DrawString_CN(startX1 + 37, tableY + 14, "온도", &Font20KR, BLACK, WHITE);

  if (gWeather.temp != "" && gWeather.temp != "unavailable") {
    int charW = Maple20.Width;
    int tLen = gWeather.temp.length();
    int valW = tLen * charW;
    int degreeGap = 10;
    int totalW = valW + degreeGap + charW;
    int startX = center1 - totalW / 2;
    int startY = tableY + 50;
    Paint_DrawString_EN(startX, startY, gWeather.temp.c_str(), &Maple20, WHITE, BLACK);
    Paint_DrawRectangle(startX + valW, startY, startX + valW + degreeGap, startY + Maple20.Height, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawCircle(startX + valW + 4, startY + 6, 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(startX + valW + degreeGap, startY, "C", &Maple20, WHITE, BLACK);
  } else {
    Paint_DrawString_EN(center1 - Maple20.Width, tableY + 50, "--", &Maple20, WHITE, BLACK);
  }

  int w2 = 117;
  int startX2 = center2 - w2 / 2;
  Paint_DrawBitmap(startX2, tableY + 10, Icon_Rain, 32, 32, BLACK);
  Paint_DrawString_CN(startX2 + 37, tableY + 14, "강수확률", &Font20KR, BLACK, WHITE);
  if (gWeather.rain != "" && gWeather.rain != "unavailable") {
    int vLen = gWeather.rain.length() * Maple20.Width;
    Paint_DrawString_EN(center2 - vLen / 2, tableY + 50, gWeather.rain.c_str(), &Maple20, WHITE, BLACK);
  }

  int w3 = 117;
  int startX3 = center3 - w3 / 2;
  Paint_DrawBitmap(startX3, tableY + 10, Icon_Dust, 32, 32, BLACK);
  Paint_DrawString_CN(startX3 + 37, tableY + 14, "미세먼지", &Font20KR, BLACK, WHITE);
  if (gWeather.dust != "" && gWeather.dust != "unavailable") {
    int w = (gWeather.dust.length() / 3) * 22;
    Paint_DrawString_CN(center3 - w / 2, tableY + 50, gWeather.dust.c_str(), &Font20KR, BLACK, WHITE);
  }

  int startX4 = center1 - 77 / 2;
  Paint_DrawBitmap(startX4, tableY + rowH + 10, Icon_Drop, 32, 32, BLACK);
  Paint_DrawString_CN(startX4 + 37, tableY + rowH + 13, "습도", &Font20KR, BLACK, WHITE);
  if (gWeather.humi != "" && gWeather.humi != "unavailable") {
    int vLen = gWeather.humi.length() * Maple20.Width;
    Paint_DrawString_EN(center1 - vLen / 2, tableY + rowH + 50, gWeather.humi.c_str(), &Maple20, WHITE, BLACK);
  }

  int startX5 = center2 - 77 / 2;
  Paint_DrawBitmap(startX5, tableY + rowH + 10, Icon_Wind, 32, 32, BLACK);
  Paint_DrawString_CN(startX5 + 37, tableY + rowH + 14, "풍속", &Font20KR, BLACK, WHITE);
  if (gWeather.wind != "" && gWeather.wind != "unavailable") {
    int vLen = gWeather.wind.length() * Maple20.Width;
    Paint_DrawString_EN(center2 - vLen / 2, tableY + rowH + 50, gWeather.wind.c_str(), &Maple20, WHITE, BLACK);
  }

  int startX6 = center3 - 77 / 2;
  Paint_DrawBitmap(startX6, tableY + rowH + 10, Icon_Cloud, 32, 32, BLACK);
  Paint_DrawString_CN(startX6 + 37, tableY + rowH + 14, "날씨", &Font20KR, BLACK, WHITE);
  if (gWeather.cond != "" && gWeather.cond != "unavailable") {
    int w = (gWeather.cond.length() / 3) * 22;
    Paint_DrawString_CN(center3 - w / 2, tableY + rowH + 50, gWeather.cond.c_str(), &Font20KR, BLACK, WHITE);
  }
}

void updateDisplay(const String &statusText, bool redrawStatus, bool redrawWeather, bool forceFullRefresh) {
  Serial.println("Drawing UI: " + statusText);

  Paint_SelectImage(BlackImage);

  if (!forceFullRefresh && !redrawStatus && !redrawWeather) {
    return;
  }

  if (forceFullRefresh) {
    Serial.println("Performing Full Refresh...");
    EPD_4IN2_V2_Init();
    EPD_4IN2_V2_Clear();
    EPD_4IN2_V2_Init_Fast(Seconds_1S);
    lastFullRefreshAt = millis();
  } else {
    Serial.println("Updating Whole Screen...");
  }

  drawStatusRegion(statusText);
  drawWeatherRegion();

  EPD_4IN2_V2_PartialDisplay(BlackImage, 0, 0, EPD_WIDTH, EPD_HEIGHT);

  isFirstRun = false;

  if (statusText == "심야절전중") {
    EPD_4IN2_V2_Sleep();
  }
}
