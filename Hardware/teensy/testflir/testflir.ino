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
  Serial1.begin(921600);
  Wire.begin();
  Wire.setClock(400000);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(18000000, MSBFIRST, SPI_MODE3));
  delay(1000);

  Wire.beginTransmission(LEP_CCI_ADDRESS);
  if (Wire.endTransmission() != 0) { Serial.println("Error: Lepton not found"); return; }

  cciSet(LEP_CMD_AGC_ENABLE, 0x0000);
  cciSet(LEP_CMD_VID_OUTPUT, 0x0007);
  Serial.println("Lepton init complete");
}

void loop() {
  uint32_t startTime = millis();
  int currentSeg = 0;
  bool frameComplete = false;

  while (!frameComplete) {
    if (millis() - startTime > 3000) { resetLepton(); return; }

    digitalWrite(CS_PIN, LOW);
    SPI.transfer(rawPacket, PACKET_SIZE);
    digitalWrite(CS_PIN, HIGH);

    if ((rawPacket[0] & 0x0F) == 0x0F) {
        delayMicroseconds(30);
        continue;
    }

    uint8_t packetId = rawPacket[1];
    if (packetId >= PACKETS_PER_SEG) continue;

    // 20번 패킷에서 세그먼트 번호 확인 및 동기화
    if (packetId == 20) {
      int segBits = (rawPacket[0] >> 4) & 0x07;
      if (segBits > 0 && segBits <= 4) {
        currentSeg = segBits;
      }
    }

    if (currentSeg > 0) {
      int baseIdx = ((currentSeg - 1) * PACKETS_PER_SEG * PIXELS_PER_PKT) + (packetId * PIXELS_PER_PKT);

      if (baseIdx + PIXELS_PER_PKT <= FRAME_SIZE) {
        for (int i = 0; i < PIXELS_PER_PKT; i++) {
          // Python(Big-Endian) 수신을 위해 바이트 순서를 뒤집어서 저장
          // Lepton: MSB(raw[4]), LSB(raw[5]) -> Teensy RAM에 LSB, MSB 순으로 배치되도록 함
          uint8_t msb = rawPacket[4 + i * 2];
          uint8_t lsb = rawPacket[5 + i * 2];
          frameBuffer[baseIdx + i] = (lsb << 8) | msb; 
        }
      }

      // 마지막 패킷 도달 시
      if (packetId == 59) {
        if (currentSeg == 4) {
          frameComplete = true;
        } else {
          // 다음 세그먼트 번호를 미리 예측하여 0번 패킷 수집 준비
          currentSeg++;
        }
      }
    }
  }

  // 데이터 전송 (ESP32로 분할 전송)
  Serial.println("Frame complete - sending");
  Serial1.write("FSTART", 6);
  Serial1.write((uint8_t)0);
  
  // 1KB씩나눠서 전송하여 UART 버퍼 오버플로우 방지
  uint8_t* ptr = (uint8_t*)frameBuffer;
  size_t totalBytes = FRAME_SIZE * 2;
  size_t sentBytes = 0;
  
  while(sentBytes < totalBytes) {
    size_t len = (totalBytes - sentBytes > 1024) ? 1024 : (totalBytes - sentBytes);
    Serial1.write(ptr + sentBytes, len);
    sentBytes += len;
    delayMicroseconds(200); // 휴식 시간을 200us로 늘림
  }
  Serial1.flush();
  
  Serial.println("Sent OK");
  delay(400); // 프레임 간 딜레이를 400ms로 늘려 안정성 확보
}
