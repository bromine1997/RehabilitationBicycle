# 🚲 RehabilitationBicycle

> **재활 자전거용 실시간 답력 측정 시스템**  
> ESP32 Feather V2 기반 4채널 동기화 로드셀 데이터 수집 장치

<br>

![Platform](https://img.shields.io/badge/Platform-ESP32%20Feather%20V2-blue?logo=espressif)
![IDE](https://img.shields.io/badge/IDE-Arduino-teal?logo=arduino)
![Storage](https://img.shields.io/badge/Storage-SPIFFS-orange)
![Sensor](https://img.shields.io/badge/Sensor-ADS1232%20Load%20Cell-green)

---

## 📌 프로젝트 개요

재활 운동 자전거 페달에 가해지는 힘을 정밀하게 측정하기 위해 개발한 임베디드 시스템입니다.  
4채널 로드셀을 **동기화 샘플링**으로 수집하고, SPIFFS에 저장한 뒤 IoT Monitor를 통해 실시간으로 시각화합니다.

| 항목 | 내용 |
|------|------|
| MCU | Adafruit ESP32 Feather V2 (PSRAM 내장) |
| 개발 환경 | Arduino IDE |
| 핵심 기능 | 4채널 로드셀 동기화 수집, SPIFFS 저장, 실시간 모니터링 |
| 데이터 용량 | 최대 1.7MB (PSRAM 활용) |

---

## 🔧 하드웨어 구성

### 부품 목록

| 부품 | 모델 | 역할 |
|------|------|------|
| MCU | ESP32 Feather V2 | 메인 제어 및 Wi-Fi 통신 |
| ADC | ADS1232 × 4 | 24bit 로드셀 신호 변환 |
| 로드셀 | - | 페달 답력 측정 |
| 외부 클럭 | 외부 오실레이터 | ADS1232 채널 간 동기화 |

### 회로도

> 📷 **회로 사진을 아래에 추가하세요**
```
![회로도](./data/schematic.png)
```

### 실제 하드웨어

> 📷 **실제 장착 사진을 아래에 추가하세요**
```
![하드웨어](./data/hardware.jpg)
```

---

## ⚙️ 핵심 구현

### 1. 4채널 ADS1232 동기화 샘플링

일반적인 순차 polling 방식은 채널 간 시간 오차가 발생합니다.  
이 프로젝트에서는 **외부 공통 클럭 + 인터럽트 기반 SPI**를 사용해 4채널을 동시에 래칭합니다.

```cpp
// DRDY 핀 인터럽트 → 모든 채널 동시 읽기
void IRAM_ATTR onDataReady() {
    for (int i = 0; i < 4; i++) {
        rawData[i] = readADS1232(csPin[i]);
    }
    dataReady = true;
}
```

### 2. 2의 보수 부호 확장 (24bit → 32bit)

ADS1232는 24bit 2의 보수 값을 반환합니다.  
Arduino/ESP32에서 `long`(32bit)으로 올바르게 변환하려면 **산술 우시프트**가 필요합니다.

```cpp
// 잘못된 방법 (논리 시프트 → 부호 손실)
long value = (long)(uDATA >> 8);  // ❌

// 올바른 방법 (부호 비트 확장)
long value = (long)(uDATA << 8) >> 8;  // ✅
```

> ESP32는 `long`에 대해 산술 우시프트를 보장하므로,  
> `<< 8` 후 `>> 8`로 MSB(부호 비트)를 32bit 전체로 확장합니다.

### 3. PSRAM 활용 대용량 버퍼

ESP32 Feather V2의 내장 PSRAM을 활용해 약 **1.7MB** 분량의 측정 데이터를 RAM에 적재한 뒤 SPIFFS에 일괄 저장합니다. 실시간 Flash 쓰기로 인한 샘플링 지연을 방지합니다.

```cpp
// PSRAM 할당
float* dataBuffer = (float*)ps_malloc(BUFFER_SIZE * sizeof(float));
```

---

## 📊 데이터 수집 흐름

```
[로드셀 × 4]
     │
[ADS1232 × 4] ←── 외부 공통 클럭 (동기화)
     │
[ESP32 Feather V2]
  ├─ PSRAM 버퍼링 (1.7MB)
  ├─ SPIFFS 저장
  └─ IoT Monitor 실시간 전송
```

---

## 🚀 실행 방법

1. **의존성 설치** — Arduino IDE에서 아래 라이브러리 설치
   - `Adafruit ESP32 Feather V2` 보드 패키지
   - SPIFFS 업로더 플러그인

2. **SPIFFS 파티션 설정** — `tools > Partition Scheme > Huge APP (3MB No OTA)`

3. **코드 업로드**
   ```
   RehabilitationBicycle/RehabilitationBicycle.ino
   ```

4. **IoT Monitor 연결** — 시리얼 또는 Wi-Fi를 통해 실시간 데이터 확인

---

## 📁 레포지토리 구조

```
RehabilitationBicycle/
├── RehabilitationBicycle.ino   # 메인 펌웨어
└── ...
data/
├── (측정 데이터 샘플)
README.md
```

---

## 👤 개발자

| | |
|---|---|
| **이름** | 서보민 (Bromine) |
| **GitHub** | [@bromine1997](https://github.com/bromine1997) |
| **포트폴리오** | [bromine1997.github.io/web-porfolio](https://bromine1997.github.io/web-porfolio) |
