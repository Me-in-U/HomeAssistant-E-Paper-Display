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

// 심야 모드를 위한 시간 설정
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 9 * 3600; // 한국 시간 (UTC+9)
const int   daylightOffset_sec = 0;

// 심야 모드: 00:00 부터 06:00 까지 절전
#define SLEEP_START_HOUR 0
#define SLEEP_END_HOUR 6

UBYTE *BlackImage;
String lastStatus = "";
bool isFirstRun = true;
unsigned long arrivalHomeTime = 0; // 23층 도착 시간 기록
bool homeRefreshed = false;        // 23층 도착 후 10초 뒤 리프레시 여부

// 함수 선언
String getElevatorStatus();
void fetchWeatherInfo();
void updateDisplay(String statusText);
void checkNightMode();

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
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  // 심야 모드 로직을 위한 시간 동기화
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 디스플레이 버퍼 메모리 할당
  UWORD Imagesize = ((EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
  if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
      Serial.println("Failed to apply for black memory...");
      while(1);
  }
}

void loop() {
  // 2. 심야 모드 확인
  checkNightMode();

  if (WiFi.status() == WL_CONNECTED) {
    String currentStatus = getElevatorStatus();

    // 상태가 변했을 때만 업데이트 수행
    if (currentStatus != lastStatus) {
      Serial.println("Status Changed: " + currentStatus);
      
      // 23층 도착 시 타이머 초기화
      if (currentStatus == "23층") {
          arrivalHomeTime = millis();
          homeRefreshed = false;
      }

      updateDisplay(currentStatus);
      lastStatus = currentStatus;
    } else if (currentStatus == "23층") {
      // 23층 상태 유지 중: 10초 경과 체크
      if (!homeRefreshed && (millis() - arrivalHomeTime > 10000)) {
          Serial.println("Home Refresh Triggered (10s delay)");
          homeRefreshed = true;
          updateDisplay(currentStatus); // 이 호출 시 isMyHomeRefresh 로직 발동
      }
    } else if (currentStatus == "호출대기중") {
      // 대기중일 때는 설정된 시간(10분)마다 날씨 정보 갱신하고 화면 업데이트
      if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
         Serial.println("Updating Weather Info in Waiting Mode...");
         fetchWeatherInfo();
         // 화면 갱신
         updateDisplay(currentStatus);
      }
    }
  } else {
    Serial.println("WiFi Disconnected. Reconnecting...");
    WiFi.reconnect();
  }

  // 3. 루프 지연 (절전 모드)
  if (lastStatus == "호출대기중") {
      // 호출 대기 중일 때는 5초마다 상태 확인
      delay(5000);
  } else if (lastStatus != "심야절전중" && lastStatus != "") {
      // 엘리베이터 이동 중일 때는 1초마다 갱신하여 반응 속도 높임
      delay(1000); 
  } else {
      // 그 외 상태
      delay(3000); 
  }
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
    updateDisplay("심야절전중");

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

void fetchWeatherInfo() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = String(ha_base_url) + "/api/template";
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + ha_token);
  http.addHeader("Content-Type", "application/json");

  // 6개의 센서값을 한 번에 가져오기 위한 템플릿
  String templateBody = "{\"template\": \"{\\\"temp\\\": \\\"{{ states('sensor.wn_daeweondong_temperature') }}\\\", \\\"humi\\\": \\\"{{ states('sensor.wn_daeweondong_relative_humidity') }}\\\", \\\"rain\\\": \\\"{{ states('sensor.wn_daeweondong_precipitation_probability') }}\\\", \\\"dust\\\": \\\"{{ states('sensor.wn_daeweondong_pm10_description') }}\\\", \\\"cond\\\": \\\"{{ states('sensor.wn_daeweondong_current_condition') }}\\\", \\\"wind\\\": \\\"{{ states('sensor.wn_daeweondong_wind_speed') }}\\\"}\"}";

  int httpCode = http.POST(templateBody);
  
  if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload); // template API는 렌더링된 문자열을 반환함
      
      // JSON 파싱 (doc 객체에 바로 매핑됨)
      
      gWeather.temp = doc["temp"].as<String>();
      gWeather.humi = doc["humi"].as<String>();
      gWeather.rain = doc["rain"].as<String>();
      gWeather.dust = doc["dust"].as<String>();
      gWeather.cond = doc["cond"].as<String>();
      gWeather.wind = doc["wind"].as<String>();
      
      // 단위가 없으면 추가하고 포맷팅
      // if (gWeather.temp != "unavailable") gWeather.temp += "C"; // 온도 기호는 그리기 단계에서 수동 처리
      if (gWeather.humi != "unavailable") gWeather.humi += "%";
      if (gWeather.rain != "unavailable") gWeather.rain += "%";
      if (gWeather.wind != "unavailable") gWeather.wind += "m/s";
      
      lastWeatherUpdate = millis();
      Serial.println("Weather Updated: " + gWeather.temp + ", " + gWeather.cond);
  } else {
      Serial.print("Weather Fetch Failed: ");
      Serial.println(httpCode);
  }
  http.end();
}

String getElevatorStatus() {
  WiFiClientSecure client;
  client.setInsecure(); // SSL 검증 건너뛰기
  HTTPClient http;

  // secrets에 정의된 URL 생성
  String url = String(ha_base_url) + "/api/states/sensor.elevator_0_0_6_floor";
//   String url = String(ha_base_url) + "/api/states/sensor.airdata_2_eco2";
  
  http.begin(client, url);
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
  } 
// 참고: HTTP 실패 시 깜빡임이나 에러 메시지를 피하기 위해 lastStatus 유지
  
  http.end();
  return result;
}

void updateDisplay(String statusText) {
  Serial.println("Drawing UI: " + statusText);

  // 로직: 
  // 1. 23층(Home)인데 10초가 지났으면(homeRefreshed==true) 전체 리프레시
  // 2. 호출대기중 상태
  // 3. 이전 상태가 "호출대기중"이었으면 깨우기 위해 전체 리프레시
  bool isMyHomeRefresh = (statusText == "23층" && homeRefreshed);
  bool isWaiting = (statusText == "호출대기중");
  bool wasWaiting = (lastStatus == "호출대기중");
  bool isSleeping = (statusText == "심야절전중");
  
  bool fullRefresh = isFirstRun || isMyHomeRefresh || isWaiting || wasWaiting || isSleeping;

  // 1. 디스플레이 드라이버 초기화
  if (fullRefresh) {
    Serial.println("Performing Full Refresh...");
    EPD_4IN2_V2_Init();
    EPD_4IN2_V2_Clear(); 
    EPD_4IN2_V2_Init_Fast(Seconds_1S); 
  } else {
    Serial.println("Performing Partial Refresh...");
  }

  // 2. 버퍼에 그리기
  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);

  // --- 정적 UI ---
  Paint_DrawRectangle(2, 2, EPD_WIDTH - 3, EPD_HEIGHT - 3, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

  // --- 동적 콘텐츠 ---
      // 0. 날씨 데이터 확인
      if (gWeather.temp == "") {
          fetchWeatherInfo();
      }

      // 1. 상단 텍스트 그리기 (Y=33)
      if (statusText == "심야절전중") {
           Paint_DrawString_CN(40, 33, "심야절전중", &Font64KR, BLACK, WHITE);
      } else if (statusText == "호출대기중") {
           Paint_DrawString_CN(40, 33, "호출대기중", &Font64KR, BLACK, WHITE);
      } else {
          // "XX층" 그리기
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
          int startY = 33; // 상단으로 이동

          Paint_DrawString_CN(startX, startY, floorNum.c_str(), &Font64KR, BLACK, WHITE);
          if (suffix != "") {
            Paint_DrawString_CN(startX + (floorNum.length() * digitW), startY, suffix.c_str(), &Font64KR, BLACK, WHITE);
          }
      }
      
      // 2. 날씨 정보 테이블 그리기 (공통)
      int tableY = 130;
      int rowH = 80;
      int colW = EPD_WIDTH / 3;
      
      // 가로선
      Paint_DrawLine(2, tableY, EPD_WIDTH - 3, tableY, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawLine(2, tableY + rowH, EPD_WIDTH - 3, tableY + rowH, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawLine(2, tableY + rowH * 2, EPD_WIDTH - 3, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      
      // 세로선
      Paint_DrawLine(colW, tableY, colW, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawLine(colW * 2, tableY, colW * 2, tableY + rowH * 2, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

      int c1 = 0, c2 = colW, c3 = colW * 2;
      int center1 = c1 + colW/2, center2 = c2 + colW/2, center3 = c3 + colW/2;

      // 1행 1열: 온도
      int w1 = 77;
      int startX1 = center1 - w1/2;
      Paint_DrawBitmap(startX1, tableY + 10, Icon_Thermometer, 32, 32, BLACK);
      Paint_DrawString_CN(startX1 + 37, tableY + 14, "온도", &Font20KR, BLACK, WHITE);
      
      if (gWeather.temp != "" && gWeather.temp != "unavailable") {
          int charW = Maple20.Width;
          int tLen = gWeather.temp.length();
          int valW = tLen * charW;
          int degreeGap = 10;
          int totalW = valW + degreeGap + charW;
          int startX = center1 - totalW/2;
          int startY = tableY + 50;
          Paint_DrawString_EN(startX, startY, gWeather.temp.c_str(), &Maple20, WHITE, BLACK);
          // Draw degree symbol background and circle
          Paint_DrawRectangle(startX + valW, startY, startX + valW + degreeGap, startY + Maple20.Height, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
          Paint_DrawCircle(startX + valW + 4, startY + 6, 2, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
          Paint_DrawString_EN(startX + valW + degreeGap, startY, "C", &Maple20, WHITE, BLACK);
      } else {
           Paint_DrawString_EN(center1 - Maple20.Width, tableY + 50, "--", &Maple20, WHITE, BLACK);
      }

      // 1행 2열: 강수확률
      int w2 = 117;
      int startX2 = center2 - w2/2;
      Paint_DrawBitmap(startX2, tableY + 10, Icon_Rain, 32, 32, BLACK);
      Paint_DrawString_CN(startX2 + 37, tableY + 14, "강수확률", &Font20KR, BLACK, WHITE);
      if (gWeather.rain != "" && gWeather.rain != "unavailable") {
          int vLen = gWeather.rain.length() * Maple20.Width;
          Paint_DrawString_EN(center2 - vLen/2, tableY + 50, gWeather.rain.c_str(), &Maple20, WHITE, BLACK);
      }

      // 1행 3열: 미세먼지
      int w3 = 117;
      int startX3 = center3 - w3/2;
      Paint_DrawBitmap(startX3, tableY + 10, Icon_Dust, 32, 32, BLACK);
      Paint_DrawString_CN(startX3 + 37, tableY + 14, "미세먼지", &Font20KR, BLACK, WHITE);
      if (gWeather.dust != "" && gWeather.dust != "unavailable") {
          // 한글 3바이트 가정, 글자당 약 22픽셀 계산 (기존 21은 바이트당 계산되어 너무 넓었음)
          int w = (gWeather.dust.length() / 3) * 22; 
          // 영문/숫자가 포함될 경우 오차가 있을 수 있으나 날씨 상태는 주로 한글임
          Paint_DrawString_CN(center3 - w/2, tableY + 50, gWeather.dust.c_str(), &Font20KR, BLACK, WHITE);
      }

      // 2행 1열: 습도
      int startX4 = center1 - 77/2;
      Paint_DrawBitmap(startX4, tableY + rowH + 10, Icon_Drop, 32, 32, BLACK); 
      Paint_DrawString_CN(startX4 + 37, tableY + rowH + 13, "습도", &Font20KR, BLACK, WHITE);
       if (gWeather.humi != "" && gWeather.humi != "unavailable") {
          int vLen = gWeather.humi.length() * Maple20.Width;
          Paint_DrawString_EN(center1 - vLen/2, tableY + rowH + 50, gWeather.humi.c_str(), &Maple20, WHITE, BLACK);
      }

      // 2행 2열: 풍속
      int startX5 = center2 - 77/2;
      Paint_DrawBitmap(startX5, tableY + rowH + 10, Icon_Wind, 32, 32, BLACK);
      Paint_DrawString_CN(startX5 + 37, tableY + rowH + 14, "풍속", &Font20KR, BLACK, WHITE);
      if (gWeather.wind != "" && gWeather.wind != "unavailable") {
          int vLen = gWeather.wind.length() * Maple20.Width;
          Paint_DrawString_EN(center2 - vLen/2, tableY + rowH + 50, gWeather.wind.c_str(), &Maple20, WHITE, BLACK);
      }

      // 2행 3열: 날씨 상태
      int startX6 = center3 - 77/2;
      Paint_DrawBitmap(startX6, tableY + rowH + 10, Icon_Cloud, 32, 32, BLACK);
      Paint_DrawString_CN(startX6 + 37, tableY + rowH + 14, "날씨", &Font20KR, BLACK, WHITE);
      if (gWeather.cond != "" && gWeather.cond != "unavailable") {
          // 한글 3바이트 기준 너비 계산 수정
          int w = (gWeather.cond.length() / 3) * 22;
          Paint_DrawString_CN(center3 - w/2, tableY + rowH + 50, gWeather.cond.c_str(), &Font20KR, BLACK, WHITE); 
      } 

  // 3. 디스플레이 전송
  if (fullRefresh) {
     // 빠른 갱신 모드에서 전체 화면 그리기
      EPD_4IN2_V2_PartialDisplay(BlackImage, 0, 0, EPD_WIDTH, EPD_HEIGHT);
      isFirstRun = false; 
  } else {
      // 부분 갱신
      EPD_4IN2_V2_PartialDisplay(BlackImage, 0, 0, EPD_WIDTH, EPD_HEIGHT);
  }
  
  // 4. 절전
  if (isWaiting || isSleeping) {
      EPD_4IN2_V2_Sleep();
  }
}
