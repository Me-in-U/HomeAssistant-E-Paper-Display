#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "DEV_Config.h"
#include "EPD_7in5_V2.h"
#include "GUI_Paint.h"
#include "secrets.h"

// 디스플레이 해상도
#define EPD_WIDTH 800
#define EPD_HEIGHT 480

// NTP 서버 설정
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 9 * 3600; // 한국 표준시 (UTC+9)
const int   daylightOffset_sec = 0;

// 이미지 버퍼
UBYTE *BlackImage;

// RTC 메모리 변수 (Deep Sleep 유지)
RTC_DATA_ATTR uint32_t lastEventHash = 0;
RTC_DATA_ATTR int lastDay = -1;

// 함수 원형 선언
void drawCalendar(struct tm *timeinfo, DynamicJsonDocument *doc);
void fetchEvents(int year, int month, int daysInMonth, DynamicJsonDocument *doc);
void drawEventsForDay(int year, int month, int day, int x, int y, int w, int h, DynamicJsonDocument *doc);
uint32_t computeEventsHash(DynamicJsonDocument *doc);

void setup() {
  Serial.begin(115200);
  
  // GPIO 초기화
  DEV_Module_Init();

  // WiFi 연결
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  // 시간 초기화 및 동기화
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  // 데이터 조회를 위한 연/월 계산
  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int daysInMonth;
  if (month == 2) {
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) daysInMonth = 29;
    else daysInMonth = 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    daysInMonth = 30;
  } else {
    daysInMonth = 31;
  }

  // 이벤트(일정) 조회
  DynamicJsonDocument *doc = new DynamicJsonDocument(65536);
  fetchEvents(year, month, daysInMonth, doc);
  
  // 데이터 해시 계산 (변경 감지용)
  uint32_t currentHash = computeEventsHash(doc);
  Serial.printf("Last Hash: %u, Current Hash: %u\n", lastEventHash, currentHash);
  Serial.printf("Last Day: %d, Current Day: %d\n", lastDay, timeinfo.tm_mday);

  // 업데이트 필요 여부 확인
  // 업데이트 조건:
  // 1. 날짜가 변경되었을 때 (자정 또는 첫 실행)
  // 2. 이벤트(일정)가 변경되었을 때
  bool needUpdate = (timeinfo.tm_mday != lastDay) || (currentHash != lastEventHash);

  if (needUpdate) {
      printf("Update required. Initializing Display...\r\n");
      
      // 디스플레이 초기화
      EPD_7IN5_V2_Init();
      EPD_7IN5_V2_Clear();
      DEV_Delay_ms(500);

      // 새 이미지 캐시 생성
      UWORD Imagesize = ((EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
      if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
          printf("Failed to apply for black memory...\r\n");
          while(1);
      }
      Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
      Paint_SelectImage(BlackImage);
      Paint_Clear(WHITE);

      // 달력 그리기
      drawCalendar(&timeinfo, doc);

      // 화면 출력
      EPD_7IN5_V2_Display(BlackImage);
      DEV_Delay_ms(2000);

      // 디스플레이 절전
      printf("Goto Sleep...\r\n");
      EPD_7IN5_V2_Sleep();
      free(BlackImage);
      BlackImage = NULL;

      // 상태 업데이트
      lastEventHash = currentHash;
      lastDay = timeinfo.tm_mday;
  } else {
      printf("No changes and not a new day. Skipping display update.\r\n");
  }

  delete doc;

  // 다음 정각까지 Deep Sleep
  // 깨어난 후 ESP32가 재시작됩니다
  long sleepSeconds = (60 - timeinfo.tm_min) * 60 - timeinfo.tm_sec;
  if (sleepSeconds <= 0) sleepSeconds = 3600; // Safety fallback

  uint64_t sleepTime = (uint64_t)sleepSeconds * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTime);
  Serial.printf("Entering Deep Sleep for %ld seconds...\r\n", sleepSeconds);
  esp_deep_sleep_start();
}

void loop() {
  // This will not be reached due to deep sleep
}

void fetchEvents(int year, int month, int daysInMonth, DynamicJsonDocument *doc) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // SSL 검증 건너뛰기
  HTTPClient http;

  // 1. 캘린더 목록 조회
  String listUrl = String(ha_base_url) + "/api/calendars";
  Serial.println("Fetching Calendars List: " + listUrl);
  
  http.begin(client, listUrl);
  http.addHeader("Authorization", String("Bearer ") + ha_token);
  http.addHeader("Content-Type", "application/json");
  
  int code = http.GET();
  if (code != 200) {
      Serial.printf("Failed to fetch calendar list: %d\n", code);
      if (code > 0) Serial.println(http.getString());
      http.end();
      return;
  }
  
  String listPayload = http.getString();
  http.end();

  // 목록 파싱
  DynamicJsonDocument listDoc(8192);
  DeserializationError listError = deserializeJson(listDoc, listPayload);
  if (listError) {
    Serial.print("List deserializeJson() failed: ");
    Serial.println(listError.f_str());
    return;
  }
  
  JsonArray calendars = listDoc.as<JsonArray>();
  JsonArray allEvents = doc->to<JsonArray>(); // 메인 문서를 배열로 초기화

  // 2. 각 캘린더별 이벤트 순회 및 조회
  for (JsonObject cal : calendars) {
      const char* entity_id = cal["entity_id"];
      if (!entity_id) continue;

      String eventsUrl = String(ha_base_url) + "/api/calendars/" + entity_id;
      char query[128];
      sprintf(query, "?start=%04d-%02d-01T00:00:00&end=%04d-%02d-%02dT23:59:59", year, month, year, month, daysInMonth);
      eventsUrl += query;

      Serial.print("Fetching events for: ");
      Serial.println(entity_id);
      Serial.println(eventsUrl);
      
      http.begin(client, eventsUrl);
      http.addHeader("Authorization", String("Bearer ") + ha_token);
      http.addHeader("Content-Type", "application/json");
      
      int eventCode = http.GET();
      if (eventCode == 200) {
          String eventPayload = http.getString();
          // 각 캘린더 이벤트 파싱을 위한 임시 문서 사용
          DynamicJsonDocument tempDoc(16384); 
          DeserializationError err = deserializeJson(tempDoc, eventPayload);
          if (!err) {
              JsonArray events = tempDoc.as<JsonArray>();
              for (JsonVariant v : events) {
                  // 메인 문서로 이벤트 깊은 복사
                  allEvents.add(v);
              }
              Serial.printf("  Found %d events\n", events.size());
          } else {
              Serial.print("  JSON Error: ");
              Serial.println(err.f_str());
          }
      } else {
          Serial.printf("  Failed: %d\n", eventCode);
      }
      http.end();
  }
  Serial.printf("Total events fetched: %d\n", allEvents.size());
}

// "YYYY-MM-DD" 문자열을 정수형 YYYYMMDD로 변환하는 헬퍼 함수
long getDateValue(String dateStr) {
  if (dateStr.length() < 10) return 0;
  return dateStr.substring(0, 4).toInt() * 10000 + 
         dateStr.substring(5, 7).toInt() * 100 + 
         dateStr.substring(8, 10).toInt();
}

void drawTruncatedString(int x, int y, int w, String text) {
  // Font12KR: ASCII 약 9px, 한글 12px
  int asciiW = 9;
  int krW = 12;
  int availableW = w - 5; 
  
  char buffer[128]; 
  int currentW = 0;
  int srcIdx = 0;
  int dstIdx = 0;
  
  const char* summary = text.c_str();

  while (summary[srcIdx] != '\0') {
      unsigned char c = (unsigned char)summary[srcIdx];
      int len = 1;
      int charW = asciiW;
      
      if (c == ' ' || c == '[' || c == ']' || c == ',') charW = asciiW / 2; 
      
      if (c >= 0xC0) { 
         charW = krW;
         if (c >= 0xF0) len = 4;
         else if (c >= 0xE0) len = 3;
         else len = 2;
      }
      
      if (currentW + charW > availableW) break;
      if (dstIdx + len >= 127) break; 
      
      for(int k=0; k<len; k++) {
          buffer[dstIdx++] = summary[srcIdx++];
      }
      currentW += charW;
  }
  buffer[dstIdx] = '\0';
  
  Paint_DrawString_CN(x + 4, y, buffer, &Font12KR, BLACK, WHITE);
}

int drawWrappedString(int x, int y, int w, int h, int startY, String text) {
  int asciiW = 9;
  int krW = 12;
  int availableW = w - 5; 
  
  const char* str = text.c_str();
  int srcIdx = 0;
  int currentY = startY;
  
  while (str[srcIdx] != '\0') {
      if (currentY > y + h - 12) break; // No more vertical space

      char buffer[128]; 
      int currentW = 0;
      int dstIdx = 0;
      
      while (str[srcIdx] != '\0') {
          unsigned char c = (unsigned char)str[srcIdx];
          int len = 1;
          int charW = asciiW;
          
          if (c == ' ' || c == '[' || c == ']' || c == ',') charW = asciiW / 2; 
          
          if (c >= 0xC0) { 
             charW = krW;
             if (c >= 0xF0) len = 4;
             else if (c >= 0xE0) len = 3;
             else len = 2;
          }
          
          if (currentW + charW > availableW) break; 
          if (dstIdx + len >= 127) break; 
          
          for(int k=0; k<len; k++) {
              buffer[dstIdx++] = str[srcIdx++];
          }
          currentW += charW;
      }
      buffer[dstIdx] = '\0';
      
      Paint_DrawString_CN(x + 4, currentY, buffer, &Font12KR, BLACK, WHITE);
      currentY += 12;
  }
  return currentY;
}

void drawEventsForDay(int year, int month, int day, int x, int y, int w, int h, DynamicJsonDocument *doc) {
  int eventY = y + 25;
  JsonArray events = doc->as<JsonArray>();
  long currentVal = year * 10000L + month * 100L + day;
  
  String birthdaySummary = "";
  
  // 1단계: 생일 정보 수집
  for (JsonObject event : events) {
    const char* summaryStr = event["summary"];
    String summary = String(summaryStr);
    
    String startStr, endStr;
    bool isAllDay = false;
    if (event["start"]["date"]) {
      startStr = String((const char*)event["start"]["date"]);
      endStr = String((const char*)event["end"]["date"]);
      isAllDay = true;
    } else if (event["start"]["dateTime"]) {
      startStr = String((const char*)event["start"]["dateTime"]).substring(0, 10);
      endStr = String((const char*)event["end"]["dateTime"]).substring(0, 10);
    } else { continue; }

    long sVal = getDateValue(startStr);
    long eVal = getDateValue(endStr);
    bool shouldDraw = false;
    if (isAllDay) { if (currentVal >= sVal && currentVal < eVal) shouldDraw = true; }
    else { if (currentVal >= sVal && currentVal <= eVal) shouldDraw = true; }

    if (shouldDraw && summary.endsWith("님의 생일")) {
        String name = summary.substring(0, summary.lastIndexOf("님의 생일"));
        if (birthdaySummary.length() > 0) birthdaySummary += ", ";
        birthdaySummary += name;
    }
  }

  // 생일 라인 그리기
  if (birthdaySummary.length() > 0) {
      eventY = drawWrappedString(x, y, w, h, eventY, "[생일] " + birthdaySummary);
  }

  // 2단계: 기타 일정 그리기
  for (JsonObject event : events) {
    const char* summaryStr = event["summary"];
    String summary = String(summaryStr);
    
    if (summary.endsWith("님의 생일")) continue; // 생일 제외

    String startStr, endStr;
    bool isAllDay = false;
    if (event["start"]["date"]) {
      startStr = String((const char*)event["start"]["date"]);
      endStr = String((const char*)event["end"]["date"]);
      isAllDay = true;
    } else if (event["start"]["dateTime"]) {
      startStr = String((const char*)event["start"]["dateTime"]).substring(0, 10);
      endStr = String((const char*)event["end"]["dateTime"]).substring(0, 10);
    } else { continue; }

    long sVal = getDateValue(startStr);
    long eVal = getDateValue(endStr);
    bool shouldDraw = false;
    if (isAllDay) { if (currentVal >= sVal && currentVal < eVal) shouldDraw = true; }
    else { if (currentVal >= sVal && currentVal <= eVal) shouldDraw = true; }

    if (shouldDraw) {
      if (eventY > y + h - 12) break;
      drawTruncatedString(x, eventY, w, summary);
      eventY += 12;
    }
  }
}

void drawCalendar(struct tm *timeinfo, DynamicJsonDocument *doc) {
  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1; // 1-12
  int today = timeinfo->tm_mday; // 오늘 날짜
  
  // 요일 표시 (한글)
  const char* days[] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
  int cellWidth = EPD_WIDTH / 7;
  int headerHeight = 30; // 헤더 높이 축소 (요일 표시용)
  
  for (int i = 0; i < 7; i++) {
    // 텍스트 중앙 정렬
    // Font20KR 너비 20. "일요일" 3글자 -> 60px 너비.
    int textWidth = 20 * 3;
    int x = i * cellWidth + (cellWidth - textWidth) / 2;
    Paint_DrawString_CN(x, 5, days[i], &Font20KR, BLACK, WHITE);
  }

  // 해당 월의 첫 번째 날 요일 계산
  struct tm firstDay = *timeinfo;
  firstDay.tm_mday = 1;
  mktime(&firstDay);
  int startDayOfWeek = firstDay.tm_wday; // 0=일요일, 6=토요일

  // 해당 월의 총 일수 계산
  int daysInMonth;
  if (month == 2) {
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
      daysInMonth = 29;
    else
      daysInMonth = 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    daysInMonth = 30;
  } else {
    daysInMonth = 31;
  }

  // 필요한 주(Week) 수 계산
  // (startDayOfWeek + daysInMonth + 6) / 7 로 행 개수 계산
  int numWeeks = (startDayOfWeek + daysInMonth + 6) / 7;
  int cellHeight = (EPD_HEIGHT - headerHeight) / numWeeks;

  // 그리드 및 날짜 그리기
  int currentDay = 1;
  for (int row = 0; row < numWeeks; row++) {
    for (int col = 0; col < 7; col++) {
      if (row == 0 && col < startDayOfWeek) {
        continue;
      }
      if (currentDay > daysInMonth) {
        break;
      }

      int x = col * cellWidth;
      int y = headerHeight + row * cellHeight;

      // 가로선 그리기 (상단/하단)
      Paint_DrawLine(x, y, x + cellWidth, y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawLine(x, y + cellHeight, x + cellWidth, y + cellHeight, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

      // 세로선 그리기
      // 왼쪽 테두리: 일요일(col 0)은 제외
      if (col > 0) {
        Paint_DrawLine(x, y, x, y + cellHeight, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      }
      // 오른쪽 테두리: 토요일(col 6)은 제외
      if (col < 6) {
        Paint_DrawLine(x + cellWidth, y, x + cellWidth, y + cellHeight, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      }

      // 날짜 숫자 그리기 (스타일을 위해 한글 폰트 사용)
      char dayStr[4];
      sprintf(dayStr, "%d", currentDay);
      
      if (currentDay == today) {
        // 오늘 날짜 강조: 굵은 테두리
        Paint_DrawRectangle(x + 1, y + 1, x + cellWidth - 1, y + cellHeight - 1, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
        
        // 오늘 날짜 강조: 숫자 배경 반전
        // Font20KR ASCII 너비 약 15px. 높이 20px.
        int numW = (currentDay < 10) ? 15 : 30; 
        Paint_DrawRectangle(x + 3, y + 3, x + 3 + numW, y + 3 + 20, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_CN(x + 3, y + 3, dayStr, &Font20KR, WHITE, BLACK);
      } else {
        Paint_DrawString_CN(x + 3, y + 3, dayStr, &Font20KR, BLACK, WHITE);
      }

      // 해당 날짜의 일정 그리기
      drawEventsForDay(year, month, currentDay, x, y, cellWidth, cellHeight, doc);

      currentDay++;
    }
  }
}

uint32_t computeEventsHash(DynamicJsonDocument *doc) {
  uint32_t hash = 5381;
  JsonArray events = doc->as<JsonArray>();
  for (JsonObject event : events) {
    const char* summary = event["summary"];
    if (summary) {
        for (const char* p = summary; *p; p++) hash = ((hash << 5) + hash) + *p;
    }
    // 시작 시간 해싱
    const char* start = event["start"]["dateTime"];
    if (!start) start = event["start"]["date"];
    if (start) {
        for (const char* p = start; *p; p++) hash = ((hash << 5) + hash) + *p;
    }
  }
  return hash;
}
