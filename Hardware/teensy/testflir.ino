#include <SPI.h>
#include <Wire.h>

#define LEP_CCI_ADDRESS  0x2A
#define PACKET_SIZE      164
#define PACKETS_PER_SEG  60
#define PIXELS_PER_PKT   80
#define NUM_SEGMENTS     4
#define FRAME_SIZE       (160 * 120)

#define LEP_REG_STATUS   0x0002
#define LEP_REG_COMMAND  0x0004
#define LEP_REG_LENGTH   0x0006
#define LEP_REG_DATA0    0x0008
#define LEP_CMD_AGC_ENABLE  0x0101
#define LEP_CMD_VID_OUTPUT  0x0204

uint16_t frameBuffer[FRAME_SIZE];
uint8_t  rawPacket[PACKET_SIZE];
const int CS_PIN = 10;

// ── CCI 헬퍼 ────────────────────────────────────────

void cciWriteReg(uint16_t reg, uint16_t val) {
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  Wire.write(val >> 8); Wire.write(val & 0xFF);
  Wire.endTransmission();
  delay(5);
}

uint16_t cciReadReg(uint16_t reg) {
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)LEP_CCI_ADDRESS, (uint8_t)2);
  uint16_t val = 0;
  if (Wire.available() >= 2)
    val = ((uint16_t)Wire.read() << 8) | Wire.read();
  return val;
}

bool cciWaitBusy(uint32_t ms = 2000) {
  uint32_t t = millis();
  while (millis() - t < ms) {
    if ((cciReadReg(LEP_REG_STATUS) & 0x0001) == 0) return true;
    delay(10);
  }
  return false;
}

bool cciSet(uint16_t cmdId, uint16_t value) {
  if (!cciWaitBusy()) return false;
  cciWriteReg(LEP_REG_DATA0, value);
  cciWriteReg(LEP_REG_LENGTH, 1);
  cciWriteReg(LEP_REG_COMMAND, cmdId | 0x02);
  return cciWaitBusy();
}

void resetLepton() {
  SPI.endTransaction();
  digitalWrite(CS_PIN, HIGH);
  delay(200);
  SPI.beginTransaction(SPISettings(18000000, MSBFIRST, SPI_MODE3));
}

void setup() {
  Serial.begin(2000000);
  Wire.begin();
  Wire.setClock(400000);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(18000000, MSBFIRST, SPI_MODE3));
  delay(1000);

  Wire.beginTransmission(LEP_CCI_ADDRESS);
  if (Wire.endTransmission() != 0) { Serial.println("Error: Lepton not found"); return; }
  cciSet(LEP_CMD_AGC_ENABLE, 0x0000) ? Serial.println("OK: AGC disabled") : Serial.println("FAIL: AGC");
  cciSet(LEP_CMD_VID_OUTPUT, 0x0007) ? Serial.println("OK: RAW14 mode")   : Serial.println("FAIL: VID");
  Serial.println("Lepton init complete");
}

void loop() {
  memset(frameBuffer, 0, sizeof(frameBuffer));
  uint32_t startTime  = millis();
  int  currentSeg     = -1;   // -1 = 동기화 전
  bool frameComplete  = false;

  while (!frameComplete) {
    if (millis() - startTime > 3000) { resetLepton(); return; }

    digitalWrite(CS_PIN, LOW);
    SPI.transfer(rawPacket, PACKET_SIZE);
    digitalWrite(CS_PIN, HIGH);

    // 버림 패킷
    if ((rawPacket[0] & 0x0F) == 0x0F) { delayMicroseconds(30); continue; }

    uint8_t packetId = rawPacket[1];
    int     segBits  = (rawPacket[0] >> 4) & 0x07;

    // ── 동기화: packet 20에서 seg=1 발견 시 즉시 시작 ──
    if (currentSeg == -1) {
      if (packetId == 20 && segBits == 1) {
        currentSeg = 1;
        // packet 20도 지금 바로 저장 (packet 0~19는 0으로 유지됨 → Python이 보정)
      } else {
        continue;  // 아직 동기화 전, 버림
      }
    }

    // ── 세그먼트 전환 검증 (packet 20에서만) ──
    if (packetId == 20 && segBits != 0) {
      if (segBits != currentSeg) {
        // 예상과 다른 세그먼트 → 리셋
        resetLepton();
        return;
      }
    }

    // ── 데이터 저장 ──
    if (packetId < PACKETS_PER_SEG) {
      int baseIdx = ((currentSeg - 1) * PACKETS_PER_SEG * PIXELS_PER_PKT)
                    + (packetId * PIXELS_PER_PKT);

      if (baseIdx + PIXELS_PER_PKT <= FRAME_SIZE) {
        for (int i = 0; i < PIXELS_PER_PKT; i++) {
          frameBuffer[baseIdx + i] =
              ((uint16_t)rawPacket[4 + i * 2] << 8) | rawPacket[5 + i * 2];
        }
      }

      if (packetId == 59) {
        if (currentSeg == NUM_SEGMENTS) {
          frameComplete = true;
        } else {
          currentSeg++;
        }
      }
    }
  }

  // ── 전송: 헤더에 seg1 시작 packet 번호(20)도 함께 보냄 ──
  // Python이 어디서 시작했는지 알 수 있도록
  // 프로토콜: "FSTART" + 1바이트(seg1 첫 저장 packetId=20) + 프레임 데이터
  Serial.write("FSTART", 6);
  Serial.write((uint8_t)20);           // seg1 데이터가 packet 20부터 시작됨을 알림
  Serial.write((uint8_t*)frameBuffer, FRAME_SIZE * 2);
  Serial.flush();
}
