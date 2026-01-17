# 폰트 변환 도구 (Font Converter)

이 디렉토리는 TTF 폰트 파일을 Waveshare E-Paper 디스플레이에서 사용할 수 있는 C/C++ 소스 코드로 변환하는 파이썬 스크립트들을 포함하고 있습니다.

## 스크립트 목록 및 용도

다음 2가지 스크립트가 제공되며, 목적에 맞게 선택하여 사용하세요.

| 파일명                   | 용도 및 특징                  | 주요 대상                | 출력 포맷                          |
| :----------------------- | :---------------------------- | :----------------------- | :--------------------------------- |
| **`font2waveshare.py`**  | **영문/숫자 전용** (ASCII)    | 숫자, 알파벳             | `sFONT` 구조체 (ASCII 인덱싱)      |
| **`gen_korean_font.py`** | **한글/다국어 변환** (통합본) | 한글, 특수기호, 필수문자 | `cFONT` 구조체 (UTF-8 검색 테이블) |

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

## 2. `gen_korean_font.py` (한글 통합 변환)

한글을 포함한 다국어 폰트를 생성합니다. 용도에 따라 **전체 모드**와 **경량 모드**를 선택할 수 있습니다.

### 생성 모드 (`--mode`)

1.  **`full` (기본)**: ASCII + 완성형 한글(2350자) + 사용자 정의 특수문자를 모두 포함하여 생성합니다. 범용적으로 사용 가능하지만 용량이 큽니다.
2.  **`lite`**: 엘리베이터/날씨 표시에 **꼭 필요한 문자만** 선별하여 생성합니다. 메모리를 매우 적게 사용합니다.

### 사용 예시

1.  **전체 한글 폰트 생성 (기본값):**

    ```cmd
    python gen_korean_font.py --ttf "C:\Path\To\Font.ttf" --size 20 --mode full
    ```

2.  **필수 문자만 생성 (경량화):**
    ```cmd
    python gen_korean_font.py --ttf "C:\Path\To\Font.ttf" --size 64 --mode lite
    ```

- `--size`: 폰트 크기 (기본값: 24)
- 결과물: `Font{size}KR.c` 파일 생성 (예: `Font20KR.c`)
