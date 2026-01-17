# 스마트 엘리베이터 & 날씨 정보 디스플레이 (Smart Elevator & Weather Station)

이 프로젝트는 **ESP32**와 **Waveshare 4.2인치 E-Ink 디스플레이**를 활용하여, 평소에는 엘리베이터의 현재 층수를 표시하고, 엘리베이터 호출 대기 중일 때는 상세한 날씨 정보를 제공하는 스마트 대시보드입니다. **Home Assistant** API와 연동하여 실시간 데이터를 가져옵니다.

## 📋 주요 기능

### 1. 엘리베이터 상태 모니터링

- Home Assistant의 엘리베이터 센서(`sensor.elevator_...`) 값을 실시간으로 조회하여 현재 층수를 대형 폰트로 표시합니다.
- **빠른 갱신(Partial Refresh)** 기술을 사용하여 층수가 바뀔 때마다 화면 전체가 깜빡이는 현상을 방지했습니다.

### 2. 호출 대기 & 날씨 정보 대시보드

- 호출 대기 상태뿐만 아니라 층수가 표시될 때도 하단에 날씨 정보를 항상 표시합니다.
- 상단에는 현재 "호출대기중" 혹은 "15층"과 같은 상태 텍스트가 표시됩니다.
- **3x2 그리드 레이아웃**으로 총 6가지의 날씨 데이터를 시각적으로 제공합니다.
  - **온도**: 커스텀 온도계 아이콘 + 섭씨(C) 표시
  - **강수확률**: 비 내리는 구름 아이콘 + 퍼센트(%)
  - **미세먼지**: 마스크/먼지 아이콘 + 상태 텍스트
  - **습도**: 물방울 아이콘 + 퍼센트(%)
  - **풍속**: 바람 아이콘 + m/s 단위
  - **날씨 상태**: 구름 아이콘 + 텍스트 (맑음, 흐림 등)
- UI 디자인: 아이콘과 라벨을 상단에 함께 배치하고, 측정값을 하단에 배치하여 가독성을 높였습니다.

### 3. 스마트 절전 (Power Management)

- **Deep Sleep (Night Mode)**: 설정된 심야 시간(00:00 ~ 06:00)에는 "심야절전중" 화면을 띄우고 깊은 수면 모드로 진입합니다. 1시간마다 잠시 깨어나 날씨 정보를 갱신하고 다시 수면 모드로 들어갑니다.
- **CPU 클럭 조절**: 평상시 동작 주파수를 80MHz로 낮추어 전력 효율을 높였습니다.
- **디스플레이 수면**: 정보 갱신 후 E-Ink 디스플레이 컨트롤러를 즉시 Sleep 모드로 전환합니다.

### 4. 한글 폰트 지원

- 직접 변환한 **한글 비트맵 폰트(Font64KR, Font20KR)**를 내장하여 "호출대기중", "온도", "맑음" 등의 한글 텍스트를 깨짐 없이 출력합니다.

---

## 🛠 하드웨어 구성

- **MCU**: ESP32 Development Board
- **Display**: Waveshare 4.2inch E-Paper Module (V2)
- **Interface**: SPI

## ⚙️ 소프트웨어 설정

### 1. `secrets.h` 파일 생성

프로젝트 루트에 `secrets.h` 파일을 생성하고 다음 정보를 입력해야 합니다.

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* ha_base_url = "http://YOUR_HA_IP:8123";
const char* ha_token = "YOUR_LONG_LIVED_ACCESS_TOKEN";
```

### 2. Home Assistant 센서 연동

코드는 다음의 Home Assistant 엔티티 ID를 참조하도록 작성되었습니다. 필요시 `elevator_weather.ino` 및 `fetchWeatherInfo` 함수 내의 템플릿을 본인의 환경에 맞게 수정하세요.

- 엘리베이터: `sensor.elevator_0_0_6_floor`
- 날씨(기온): `sensor.wn_daeweondong_temperature`
- 습도: `sensor.wn_daeweondong_relative_humidity`
- 강수확률: `sensor.wn_daeweondong_precipitation_probability`
- 미세먼지: `sensor.wn_daeweondong_pm10_description`
- 현재상태: `sensor.wn_daeweondong_current_condition`
- 풍속: `sensor.wn_daeweondong_wind_speed`

## 📂 파일 구조

- **elevator_weather.ino**: 메인 로직, WiFi 연결, HA 통신, 화면 갱신 제어.
- **icons.h**: 날씨 아이콘(32x32) 비트맵 데이터 (직접 제작/수정).
- **fonts.h / font\*.c**: 한글 및 영문 폰트 데이터.
- **GUI*Paint, EPD*\***: 웨이브쉐어 E-Ink 드라이버 및 그래픽 라이브러리.

## 🔄 작동 로직

1. **부팅**: WiFi 연결 및 NTP 시간 동기화.
2. **루프**:
   - 심야 시간 체크 -> 1시간 단위 Deep Sleep 반복 (매 시간 날씨 갱신).
   - 엘리베이터 상태 API 조회.
     - 이동 중(층수 변경): **1초** 간격으로 빠른 상태 조회.
     - 대기 중: **3초** 간격으로 조회.
   - 상태 변경 시 화면 부분 갱신(Partial Refresh).
     - "23층" 도착 시: 즉시 갱신 후, 10초 뒤 잔상 제거를 위한 전체 갱신(Full Refresh) 수행.
   - 날씨 정보는 **10분** 간격으로 API 조회 및 갱신.

## 📝 버전 기록 (Version History)

### v1.0.1 (2026-01-17)

- **성능 최적화**: 호출 대기 상태에서 엘리베이터 이동 감지 시 즉시 층수를 표시하도록 개선 (불필요한 전체 리프레시 제거).
- **코드 정리**: 아두이노 프로젝트 구조 수정 (`elevator_weather.ino`로 변경) 및 불필요한 소스 파일 삭제.
- **도구 통합**: 폰트 생성 스크립트 기능 통합 (`gen_korean_font.py` - Full/Lite 모드 지원).
- **버그 수정**: 파일명 대소문자 표기 통일 (`Font20KR.c`).

### v1.0.0 (2026-01-16)

- **최초 릴리즈**: 스마트 엘리베이터 및 날씨 정보 디스플레이 기본 기능 구현.
- **엘리베이터 상태**: HA 연동 실시간 층수 표시, Partial Refresh 적용.
- **날씨 대시보드**: 6가지 날씨 정보(온도, 습도, 강수, 미세먼지, 풍속, 상태) 표시.
- **UI**: 3x2 그리드 레이아웃 및 커스텀 아이콘, 한글 폰트 지원.
- **전력 관리**: 심야 시간 Deep Sleep 및 CPU 클럭 조절 기능 포함.
