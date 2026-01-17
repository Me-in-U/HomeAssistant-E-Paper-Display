# 폰트 변환 도구 (Font Converter)

이 디렉토리는 TTF 폰트 파일을 Waveshare E-Paper 디스플레이에서 사용할 수 있는 C/C++ 소스 코드로 변환하는 파이썬 스크립트들을 포함하고 있습니다.

## 스크립트 목록 및 용도

다음 3가지 스크립트가 제공되며, 목적에 맞게 선택하여 사용하세요.

| 파일명                     | 용도 및 특징                  | 주요 대상                | 출력 포맷                          |
| :------------------------- | :---------------------------- | :----------------------- | :--------------------------------- |
| **`font2waveshare.py`**    | **영문/숫자 전용** (ASCII)    | 숫자, 알파벳             | `sFONT` 구조체 (ASCII 인덱싱)      |
| **`gen_korean_font.py`**   | **한글 전체** (KS X 1001)     | 한글 2350자 전체         | `cFONT` 구조체 (UTF-8 검색 테이블) |
| **`gen_required_font.py`** | **특정 한글/기호만** (경량화) | 프로젝트에 쓰이는 글자만 | `cFONT` 구조체 (UTF-8 검색 테이블) |

---

## 1. `font2waveshare.py` (영문/숫자용)

ASCII 문자(0x20~0x7E)를 변환합니다. 주로 **숫자와 영문**을 표시하는 기본 폰트(`Maple64` 등)를 생성할 때 사용합니다.

### 사용 예시 (숫자 전용 변환)

```cmd
python font2waveshare.py ^
  --ttf "C:\Path\To\Font.ttf" ^
  --size 64 ^
  --name Maple64 ^
  --out .\output ^
  --digits-only ^
  --force-width 48 ^
  --force-height 72
```

- `--digits-only`: 숫자와 일부 기호만 포함하여 용량을 줄입니다.
- `--force-width/height`: 고정폭 폰트가 필요할 때 크기를 강제합니다.

---

## 2. `gen_korean_font.py` (한글 전체 변환)

KS X 1001 표준에 정의된 **한글 2,350자 전체**를 변환합니다. 모든 상황에 대응할 수 있지만, 플래시 메모리 용량을 많이 차지합니다.

### 사용 예시

```cmd
python gen_korean_font.py --ttf "C:\Path\To\Font.ttf" --size 20 --out .\output
```

- `--size`: 폰트 크기 (기본값: 24)
- 결과물: `Font{size}KR.c` 파일 생성 (예: `Font20KR.c`)

---

## 3. `gen_required_font.py` (프로젝트 전용 경량화)

프로젝트에서 실제로 사용되는 **필수 한글 및 기호**만 선별하여 변환합니다. 메모리를 가장 효율적으로 사용합니다.
(현재 `REQUIRED_HANGUL` 변수에 "엘리베이터", "온도", "날씨" 관련 단어들이 정의되어 있습니다.)

### 사용 예시

```cmd
python gen_required_font.py --ttf "C:\Path\To\Font.ttf" --size 64 --out .\output
```

- 스크립트 내부의 `REQUIRED_HANGUL` 변수를 수정하여 필요한 글자만 추가/삭제할 수 있습니다.
- 결과물: `Font{size}KR.c` 생성 (테이블 크기가 매우 작음)
