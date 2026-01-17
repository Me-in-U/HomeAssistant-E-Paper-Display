# 스마트 캘린더 & 일정 디스플레이 (Smart Calendar & Schedule Display)

이 프로젝트는 **ESP32**와 **Waveshare 7.5인치 E-Ink 디스플레이(V2)**를 활용하여, Google Calendar 등 **Home Assistant**에 연동된 캘린더 정보를 가져와 월간 달력과 당일의 주요 일정을 표시합니다.

## 📋 주요 기능

### 1. 월간 달력 (Monthly View)

- 현재 월의 달력을 생성하여 표시합니다.
- 오늘의 날짜를 반전된 배경과 굵은 테두리로 강조하여 쉽게 식별할 수 있습니다.
- 요일 헤더를 한글("일요일", "월요일" 등)로 표시합니다.

### 2. 일정 동기화 (Event Sync)

- **Home Assistant API**를 통해 등록된 모든 캘린더 엔티티(`calendar.*`)의 이벤트를 조회합니다.
- 달력의 각 날짜 칸 안에 해당 날짜의 일정을 요약하여 표시합니다.
- 내용이 길 경우 자동으로 말줄임표(...) 처리를 하거나, 가능한 공간 내에서 줄바꿈하여 표시합니다.

### 3. 특별 일정 강조 (Birthday Highlight)

- 이벤트 제목에 "님의 생일"이 포함된 경우, 별도의 생일 섹션으로 추출하여 상단에 강조 표시합니다.
- 일반 일정과 구분하여 시각적인 우선순위를 부여했습니다.

### 4. 스마트 업데이트 및 절전 (Smart Refresh & Deep Sleep)

- **변경 감지 (Hash Check)**: 매번 화면을 갱신하지 않고, 이전 데이터와 비교하여 이벤트 내용이나 날짜가 변경되었을 때만 E-Paper를 갱신합니다.
- **Deep Sleep**: 화면 갱신이 필요 없거나 갱신을 마친 후에는 다음 시간 정각까지 Deep Sleep 모드로 진입하여 배터리 소모를 최소화합니다.

### 5. 한글 폰트 지원

- **Font20KR, Font12KR**: 직접 변환한 한글 비트맵 폰트를 사용하여 달력의 숫자, 요일, 일정 내용을 한글로 깨짐 없이 출력합니다.

---

## 🛠 하드웨어 구성

- **MCU**: ESP32 Development Board
- **Display**: Waveshare 7.5inch E-Paper Module (V2)
- **Interface**: SPI

## ⚙️ 소프트웨어 설정

### 1. `secrets.h` 파일 생성

`calender.ino`와 같은 디렉토리에 `secrets.h` 파일을 생성하고 다음 정보를 입력해야 합니다.

```cpp
#ifndef SECRETS_H
#define SECRETS_H

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Home Assistant 설정
const char* ha_base_url = "http://your-homeassistant-ip:8123";
const char* ha_token = "YOUR_LONG_LIVED_ACCESS_TOKEN";

#endif
```

### 2. 라이브러리 의존성

- `ArduinoJson`: JSON 데이터 파싱
- ESP32 Board Support Package

## 📂 프로젝트 구조

- `calender.ino`: 메인 펌웨어 소스 코드
- `DEV_Config.*`, `EPD_7in5_V2.*`: E-Paper 드라이버 및 SPI 설정
- `GUI_Paint.*`: 그래픽 그리기 라이브러리 (선, 사각형, 문자열 등)
- `Font*KR.c`: 한글/영문 비트맵 폰트 데이터
