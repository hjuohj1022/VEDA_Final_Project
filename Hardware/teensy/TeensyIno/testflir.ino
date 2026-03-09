#include <SPI.h>
#include <Wire.h>
#include <stdint.h>

#define LEP_CCI_ADDRESS  (0x2AU)
#define PACKET_SIZE      (164U)
#define PACKETS_PER_SEG  (60U)
#define PIXELS_PER_PKT   (80U)
#define NUM_SEGMENTS     (4U)
#define FRAME_SIZE       (160U * 120U)

#define LEP_REG_STATUS   (0x0002U)
#define LEP_REG_COMMAND  (0x0004U)
#define LEP_REG_LENGTH   (0x0006U)
#define LEP_REG_DATA0    (0x0008U)
#define LEP_CMD_AGC_ENABLE  (0x0101U)
#define LEP_CMD_VID_OUTPUT  (0x0204U)

static uint16_t frameBuffer[FRAME_SIZE];
static uint8_t  rawPacket[PACKET_SIZE];
static const uint8_t CS_PIN = 10U;

static void cciWriteReg(uint16_t reg, uint16_t val) {
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  (void)Wire.write((uint8_t)(reg >> 8U)); 
  (void)Wire.write((uint8_t)(reg & 0xFFU));
  (void)Wire.write((uint8_t)(val >> 8U)); 
  (void)Wire.write((uint8_t)(val & 0xFFU));
  (void)Wire.endTransmission();
  delay(5U);
}

static uint16_t cciReadReg(uint16_t reg) {
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  (void)Wire.write((uint8_t)(reg >> 8U)); 
  (void)Wire.write((uint8_t)(reg & 0xFFU));
  (void)Wire.endTransmission(false);
  (void)Wire.requestFrom((uint8_t)LEP_CCI_ADDRESS, (uint8_t)2U);
  
  uint16_t val = 0U;
  if (Wire.available() >= 2) {
    uint16_t msb = (uint16_t)Wire.read();
    uint16_t lsb = (uint16_t)Wire.read();
    val = (uint16_t)((msb << 8U) | lsb);
  }
  return val;
}

static bool cciWaitBusy(uint32_t ms) {
  bool is_ready = false;
  uint32_t t = millis();
  while ((millis() - t) < ms) {
    if ((cciReadReg(LEP_REG_STATUS) & 0x0001U) == 0U) {
      is_ready = true;
      break;
    }
    delay(10U);
  }
  return is_ready;
}

static bool cciSet(uint16_t cmdId, uint16_t value) {
  bool success = false;
  if (cciWaitBusy(2000U)) {
    cciWriteReg(LEP_REG_DATA0, value);
    cciWriteReg(LEP_REG_LENGTH, 1U);
    cciWriteReg(LEP_REG_COMMAND, (uint16_t)(cmdId | 0x02U));
    success = cciWaitBusy(2000U);
  }
  return success;
}

static void resetLepton(void) {
  SPI.endTransaction();
  digitalWrite(CS_PIN, HIGH);
  delay(200U);
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
  delay(1000U);

  Wire.beginTransmission(LEP_CCI_ADDRESS);
  if (Wire.endTransmission() != 0U) { 
    Serial.println("Error: Lepton not found"); 
  } else {
    (void)cciSet(LEP_CMD_AGC_ENABLE, 0x0000U);
    (void)cciSet(LEP_CMD_VID_OUTPUT, 0x0007U);
    Serial.println("Lepton init complete");
  }
}

void loop() {
  uint32_t startTime = millis();
  uint8_t currentSeg = 0U;
  bool frameComplete = false;

  while (!frameComplete) {
    if ((millis() - startTime) > 3000U) { 
      resetLepton(); 
      return; 
    }

    digitalWrite(CS_PIN, LOW);
    SPI.transfer(rawPacket, PACKET_SIZE);
    digitalWrite(CS_PIN, HIGH);

    if ((rawPacket[0] & 0x0FU) == 0x0FU) {
        delayMicroseconds(30U);
        continue;
    }

    uint8_t packetId = rawPacket[1];
    if (packetId >= PACKETS_PER_SEG) {
      continue;
    }

    if (packetId == 20U) {
      uint8_t segBits = (uint8_t)((rawPacket[0] >> 4U) & 0x07U);
      if ((segBits > 0U) && (segBits <= 4U)) {
        currentSeg = segBits;
      }
    }

    if (currentSeg > 0U) {
      uint32_t baseIdx = (uint32_t)(((uint32_t)(currentSeg - 1U) * (uint32_t)PACKETS_PER_SEG * (uint32_t)PIXELS_PER_PKT) + ((uint32_t)packetId * (uint32_t)PIXELS_PER_PKT));

      if ((baseIdx + PIXELS_PER_PKT) <= FRAME_SIZE) {
        for (uint32_t i = 0U; i < PIXELS_PER_PKT; i++) {
          uint8_t msb = rawPacket[4U + (i * 2U)];
          uint8_t lsb = rawPacket[5U + (i * 2U)];
          frameBuffer[baseIdx + i] = (uint16_t)(((uint16_t)lsb << 8U) | (uint16_t)msb); 
        }
      }

      if (packetId == 59U) {
        if (currentSeg == 4U) {
          frameComplete = true;
        } else {
          currentSeg++;
        }
      }
    }
  }

  Serial.println("Frame complete - sending");
  (void)Serial1.write("FSTART", 6U);
  (void)Serial1.write((uint8_t)0U);
  
  const uint8_t* ptr = (const uint8_t*)frameBuffer;
  size_t totalBytes = (size_t)FRAME_SIZE * 2U;
  size_t sentBytes = 0U;
  
  while(sentBytes < totalBytes) {
    size_t remaining = totalBytes - sentBytes;
    size_t len = (remaining > 1024U) ? 1024U : remaining;
    (void)Serial1.write(&ptr[sentBytes], len);
    sentBytes += len;
    delayMicroseconds(200U);
  }
  Serial1.flush();
  
  Serial.println("Sent OK");
  delay(400U);
}
