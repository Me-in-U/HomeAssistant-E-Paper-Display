/* ESP32 + Waveshare 4.2" V2: 시계 + 오늘의 일정 */
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include "DEV_Config.h"
#include "EPD_4in2_V2.h"
#include "GUI_Paint.h"
#include "secrets.h" // Provides ssid, password, ha_base_url, ha_token, calendar_entity_id

// ===== 설정 =====
static const long   GMT_OFFSET_SEC = 9 * 3600; // KST
static const int    DST_OFFSET_SEC = 0;
static const char*  NTP_SERVER     = "pool.ntp.org";

// ===== 디스플레이 레이아웃 (400x300) =====
static const UWORD EPD_W = EPD_4IN2_V2_WIDTH;   // 400
static const UWORD EPD_H = EPD_4IN2_V2_HEIGHT;  // 300

// 부분 갱신 잔상 및 영역 소실 방지를 위한 전체 화면 버퍼
static UBYTE* gCanvas = nullptr;

// 시계 로직
static const UWORD CLOCK_Y = 15;
static const UWORD SCH_Y = 130;  // 일정 시작 Y좌표

// 폰트 설정
#define DATE_FONT   Font20KR
#define TIME_FONT   Maple44
#define HEADER_FONT Font20KR
#define ITEM_FONT   Font16KR

// 상태 변수
static int8_t lastFullRefreshHour = -1;
static int8_t lastFullRefreshMin  = -1;
static long   lastScheduleUpdate  = 0;
String        todayScheduleCache = ""; 

// 유틸리티
static bool getNowKST(struct tm& t) {
  return getLocalTime(&t, 2000);
}

// 오늘의 일정 데이터 수집
void fetchTodaySchedule(int year, int month, int day, String &output) {
  if (WiFi.status() != WL_CONNECTED) {
    output = "WiFi Disconnected";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // 1. 캘린더 목록 조회
  String listUrl = String(ha_base_url) + "/api/calendars";
  
  http.begin(client, listUrl);
  http.addHeader("Authorization", String("Bearer ") + ha_token);
  http.addHeader("Content-Type", "application/json");
  
  int code = http.GET();
  if (code != 200) {
      http.end();
      return;
  }
  
  String listPayload = http.getString();
  http.end();

  DynamicJsonDocument listDoc(8192);
  deserializeJson(listDoc, listPayload);
  
  JsonArray calendars = listDoc.as<JsonArray>();
  DynamicJsonDocument eventsDoc(16384);
  JsonArray allEvents = eventsDoc.to<JsonArray>();

  // 2. 이벤트 조회
  for (JsonObject cal : calendars) {
      const char* entity_id = cal["entity_id"];
      if (!entity_id) continue;

      // 필터: secrets.h에 calendar_entity_id가 설정된 경우 해당 캘린더만 처리
      if (calendar_entity_id && *calendar_entity_id != '\0') {
          if (String(entity_id) != String(calendar_entity_id)) continue;
      }

      String eventsUrl = String(ha_base_url) + "/api/calendars/" + entity_id;
      char query[128];
      sprintf(query, "?start=%04d-%02d-%02dT00:00:00&end=%04d-%02d-%02dT23:59:59", year, month, day, year, month, day);
      eventsUrl += query;

      http.begin(client, eventsUrl);
      http.addHeader("Authorization", String("Bearer ") + ha_token);
      
      if (http.GET() == 200) {
          DynamicJsonDocument tempDoc(4096); 
          deserializeJson(tempDoc, http.getString());
          JsonArray events = tempDoc.as<JsonArray>();
          for (JsonVariant v : events) {
              allEvents.add(v);
          }
      }
      http.end();
  }
  
  // 3. 결과 문자열 생성
  output = "";
  if (allEvents.size() == 0) {
      output = "일정이 없습니다.";
  } else {
      for (JsonObject event : allEvents) {
          const char* summary = event["summary"];
          if (!summary) continue;
          String sumStr = String(summary);
          if (sumStr.endsWith("님의 생일")) continue;

          String timePrefix = ""; 
          if (event["start"]["dateTime"]) {
             String sDt = event["start"]["dateTime"];
             String eDt = event["end"]["dateTime"]; // usually exists if start has dateTime
             
             String sTime = (sDt.length() >= 16) ? sDt.substring(11, 16) : "00:00";
             String eTime = (eDt.length() >= 16) ? eDt.substring(11, 16) : "00:00";
             
             timePrefix = "[" + sTime + " ~ " + eTime + "] ";
          } else {
             timePrefix = "[종일] ";
          }
          output += timePrefix + sumStr + "\n";
      }
      if (output.length() == 0) output = "일정이 없습니다.";
  }
}

// 캔버스에 일정 그리기
void drawScheduleToCanvas(String text) {
  // 일정 영역 초기화 (흰색 사각형)
  // X: 0-400, Y: SCH_Y onwards
  Paint_DrawRectangle(0, SCH_Y, EPD_W, EPD_H, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  // 헤더 그리기
  Paint_DrawString_CN(10, SCH_Y, "오늘의 일정", &HEADER_FONT, BLACK, WHITE);
  Paint_DrawLine(10, SCH_Y + 25, EPD_W - 10, SCH_Y + 25, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

  // 항목 그리기
  int x = 10;
  int y = SCH_Y + 40; 
  int lineH = 28; // 16px font, 28px spacing

  int sIdx = 0;
  const char* str = text.c_str();
  
  while (str[sIdx] != '\0' && y < EPD_H - lineH) {
      int eIdx = sIdx;
      while(str[eIdx] != '\n' && str[eIdx] != '\0') eIdx++;
      String line = text.substring(sIdx, eIdx);
      
      Paint_DrawString_CN(x, y, line.c_str(), &ITEM_FONT, BLACK, WHITE);
      
      y += lineH;
      sIdx = eIdx;
      if (str[sIdx] == '\n') sIdx++;
  }
}

// 수동 시간 그리기 (Maple44 폰트)
void drawTimeManual(int x, int y, const char* str) {
  for (int i=0; str[i] != '\0'; i++) {
     char c = str[i];
     if (c == ':') {
        Paint_DrawChar(x, y, ':', (sFONT*)&Maple44, BLACK, WHITE);
        x += 20; // 콜론 간격
     } else {
        Paint_DrawChar(x, y, c, (sFONT*)&Maple44, BLACK, WHITE);
        x += 35; // 숫자 간격
     }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // EPD 초기화
  DEV_Module_Init();
  EPD_4IN2_V2_Init();
  EPD_4IN2_V2_Clear();
  DEV_Delay_ms(500);

  // 전체 화면 버퍼 할당
  UWORD imagesize = ((EPD_W % 8 == 0)? (EPD_W / 8 ): (EPD_W / 8 + 1)) * EPD_H;
  gCanvas = (UBYTE*)malloc(imagesize);
  if (!gCanvas) {
      Serial.println("Buffer Alloc Failed");
      return;
  }

  // 버퍼 초기화
  Paint_NewImage(gCanvas, EPD_W, EPD_H, 0, WHITE);
  Paint_SelectImage(gCanvas);
  Paint_Clear(WHITE);

  // WiFi 연결
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500); Serial.print("."); retry++;
  }
  
  // NTP 동기화
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);

  // 부분 갱신 초기화
  EPD_4IN2_V2_Init_Fast(Seconds_1S);
}

void loop() {
  static uint32_t lastTick = 0;
  if (millis() - lastTick < 1000) return;
  lastTick = millis();

  struct tm ti;
  if (!getNowKST(ti)) return;

  // 1. 일정 데이터 갱신
  bool scheduleDataChanged = false;
  // 시작 시 혹은 매 30분마다 갱신 (전체 리프레시와 동기화)
  if ((todayScheduleCache == "") || ((ti.tm_min == 0 || ti.tm_min == 30) && ti.tm_sec == 0)) {
      String newSchedule;
      fetchTodaySchedule(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, newSchedule);
      if (newSchedule != todayScheduleCache) {
          todayScheduleCache = newSchedule;
          scheduleDataChanged = true;
          lastScheduleUpdate = millis();
      }
  }

  // 2. 전체 리프레시 체크 (매 30분 - 잔상 제거)
  bool didFullRefresh = false;
  if ((ti.tm_min == 0 || ti.tm_min == 30) && ti.tm_sec == 0) {
      if (lastFullRefreshHour != ti.tm_hour || lastFullRefreshMin != ti.tm_min) {
          EPD_4IN2_V2_Init();
          EPD_4IN2_V2_Clear();
          EPD_4IN2_V2_Init_Fast(Seconds_1S); // 고속 모드 복귀
          
          lastFullRefreshHour = ti.tm_hour;
          lastFullRefreshMin = ti.tm_min;
          didFullRefresh = true;
      }
  }

  // 3. 글로벌 캔버스에 그리기
  Paint_SelectImage(gCanvas);
  
  // 시계 영역만 지우기
  Paint_DrawRectangle(0, 0, EPD_W, SCH_Y - 1, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);

  // 날짜 그리기
  char dateStr[64];
  snprintf(dateStr, sizeof(dateStr), "%04d년 %02d월 %02d일", 1900 + ti.tm_year, 1 + ti.tm_mon, ti.tm_mday);
  
  // 날짜 중앙 정렬
  // Font20KR, 대략적 너비 계산에 기반한 좌표
  Paint_DrawString_CN(95, 5, dateStr, &DATE_FONT, BLACK, WHITE);

  // 시간 그리기
  char timeStr[20];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
  
  // 수동 중앙 정렬 (커스텀 간격)
  int timeX = 50;
  for (int i=0; timeStr[i] != '\0'; i++) {
     if (timeStr[i] == ':') {
        Paint_DrawChar(timeX, 40, ':', (sFONT*)&Maple44, BLACK, WHITE);
        timeX += 30; // 간격 조정
     } else {
        Paint_DrawChar(timeX, 40, timeStr[i], (sFONT*)&Maple44, BLACK, WHITE);
        timeX += 40; // 간격 조정
     }
  }

  // 일정이 변경되었거나 전체 리프레시가 수행된 경우 버퍼 갱신
  if (scheduleDataChanged || didFullRefresh || todayScheduleCache != "") {
      if (scheduleDataChanged) {
         drawScheduleToCanvas(todayScheduleCache);
      }
  }

  // 4. 화면 갱신 (전체 프레임의 부분 갱신 전송)
  EPD_4IN2_V2_PartialDisplay(gCanvas, 0, 0, EPD_W, EPD_H);
}

