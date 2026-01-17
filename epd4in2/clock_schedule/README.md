# E-Paper 시계 및 일정 대시보드 (Smart Clock & Schedule)

이 프로젝트는 **ESP32**와 **Waveshare 4.2인치 E-Ink 디스플레이(V2)**를 사용하여 현재 시간과 **Home Assistant**의 캘린더 일정을 표시하는 스마트 시계입니다.

## 📋 주요 기능

### 1. 디지털 시계 및 날짜

- **실시간 시계**: NTP 서버와 동기화하여 정확한 날짜(년/월/일)와 시간(시:분:초)을 표시합니다.
- **1초 단위 갱신**: E-Ink의 **Partial Refresh(부분 갱신)** 기능을 활용하여 매초 초침이 바뀌는 디지털 시계를 구현했습니다.
- **디자인**: 굵고 가독성 좋은 숫자 폰트(`Maple44`)를 사용하여 시인성을 높였습니다.

### 2. 오늘의 일정 (Home Assistant 연동)

- Home Assistant의 Calendar API(`api/calendars`)를 통해 등록된 모든 캘린더의 **오늘 일정**을 가져옵니다.
- **일정 표시 형식**:
  - 시간 지정 일정: `[시작시간 ~ 종료시간] 일정 내용`
  - 종일 일정: `[종일] 일정 내용`
- **자동 갱신**: 일정 데이터는 부팅 시, 그리고 매 30분(정각, 30분)마다 자동으로 동기화됩니다.

### 3. 디스플레이 관리

- **고스팅 방지**: E-Ink 특유의 잔상을 제거하기 위해 **매 30분마다 전체 화면 리프레시(Full Refresh)**를 수행합니다.
- **한글 지원**: 직접 변환한 비트맵 폰트(`Font20KR`, `Font16KR`)를 사용하여 한글 일정을 깨짐 없이 표시합니다.

---

## 🛠 하드웨어 구성

- **MCU**: ESP32 Development Board
- **Display**: Waveshare 4.2inch E-Paper Module (V2)
- **Interface**: SPI

## ⚙️ 소프트웨어 설정

### 1. `secrets.h` 파일 생성

프로젝트 루트에 `secrets.h` 파일을 생성하고 WiFi 및 Home Assistant 접속 정보를 입력해야 합니다.

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* ha_base_url = "http://YOUR_HA_IP:8123";
const char* ha_token = "YOUR_LONG_LIVED_ACCESS_TOKEN";

// 특정 캘린더만 표시하고 싶은 경우 (선택 사항, 코드 수정 필요)
// const char* calendar_entity_id = "calendar.google_calendar";
```

### 2. Home Assistant 권한 설정

- Home Assistant에서 "장기 액세스 토큰(Long-Lived Access Token)"을 생성하여 `ha_token`에 입력해야 합니다.
- 이 토큰을 사용하는 사용자는 캘린더에 접근할 수 있는 권한이 있어야 합니다.

## 📂 파일 구조

- **clock_schedule.ino**: 메인 펌웨어 소스. 시계 로직, WiFi/HTTP 통신, 화면 갱신 제어.
- **fonts.h / Font\*.c**: 한글(KR) 및 영문/숫자 폰트 데이터.
- **GUI_Paint, EPD_4in2_V2**: 웨이브쉐어 드라이버 및 그래픽 라이브러리.
- **DEV_Config**: SPI 통신 및 GPIO 설정.

## 🔄 작동 로직

1. **부팅**: WiFi 연결 및 NTP 시간 동기화.
2. **메인 루프 (1초 주기)**:
   - 현재 시간 확인.
   - **매초**: 화면의 시계 영역(상단)만 부분 갱신하여 시간 업데이트.
   - **매 30분 (xx:00, xx:30)**:
     - Home Assistant에서 최신 일정 데이터 조회.
     - 화면 전체 리프레시 (잔상 제거) 후 일정 정보 업데이트.

## 📝 버전 기록 (Version History)

### v1.0.1 (2026-01-17)

- **기능 추가**: 특정 캘린더만 표시하는 필터링 기능 추가 (`secrets.h`의 `calendar_entity_id`).
- **코드 정리**: 불필요한 라이브러리/폰트 파일 삭제 및 프로젝트 구조 최적화.
- **주석 한글화**: 소스 코드 내 주요 로직에 대한 한글 주석 적용.

### v1.0.0 (2026-01-17)

- **최초 릴리즈**: 스마트 시계 및 오늘의 일정 뷰어 기능 구현.
- **시계 모드**: 1초 단위 초침 갱신 (Partial Refresh) 및 대형 디지털 시계 UI.
- **일정 연동**: Home Assistant Calendar API 연동, 오늘의 일정 자동 동기화 (30분 주기).
- **디스플레이 관리**: 30분 주기 전체 리프레시를 통한 잔상 제거 및 한글 폰트 지원.
