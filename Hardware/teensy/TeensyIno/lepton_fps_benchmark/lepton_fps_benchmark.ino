#include <SPI.h>
#include <Wire.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define LEP_CCI_ADDRESS    ((uint8_t)0x2AU)
#define PACKET_SIZE        (164U)
#define PACKETS_PER_SEG    (60U)
#define PIXELS_PER_PKT     (80U)
#define FRAME_WIDTH        (160U)
#define FRAME_HEIGHT       (120U)
#define FRAME_SIZE         (FRAME_WIDTH * FRAME_HEIGHT)
#define UART_FRAME_BYTES   (FRAME_SIZE * 2U)

#define LEP_REG_STATUS     (0x0002U)
#define LEP_REG_COMMAND    (0x0004U)
#define LEP_REG_LENGTH     (0x0006U)
#define LEP_REG_DATA0      (0x0008U)
#define LEP_CMD_AGC_ENABLE (0x0101U)
#define LEP_CMD_VID_OUTPUT (0x0204U)

enum BenchMode {
  MODE_CAPTURE_ONLY = 0,
  MODE_CAPTURE_AND_UART = 1,
};

struct WindowStats {
  uint32_t frames;
  uint32_t timeouts;
  uint32_t resets;
  uint32_t discards;
  uint64_t capture_us_sum;
  uint64_t tx_us_sum;
  uint64_t frame_us_sum;
  uint32_t capture_us_min;
  uint32_t capture_us_max;
  uint32_t tx_us_min;
  uint32_t tx_us_max;
  uint32_t frame_us_min;
  uint32_t frame_us_max;
};

static uint16_t s_frame_buffer[FRAME_SIZE];
static uint8_t s_raw_packet[PACKET_SIZE];
static char s_cmd_buf[64];
static size_t s_cmd_len = 0U;

static const uint8_t CS_PIN = 10U;

static BenchMode s_mode = MODE_CAPTURE_ONLY;
static bool s_running = true;
static uint32_t s_post_frame_sleep_ms = 0U;
static uint32_t s_total_frames = 0U;
static uint32_t s_total_timeouts = 0U;
static uint32_t s_total_resets = 0U;
static uint32_t s_total_discards = 0U;
static uint32_t s_window_start_ms = 0U;
static WindowStats s_window = {0U};

static void resetWindowStats(void)
{
  memset(&s_window, 0, sizeof(s_window));
  s_window.capture_us_min = 0xFFFFFFFFUL;
  s_window.tx_us_min = 0xFFFFFFFFUL;
  s_window.frame_us_min = 0xFFFFFFFFUL;
}

static const char *modeName(BenchMode mode)
{
  return (mode == MODE_CAPTURE_AND_UART) ? "uart" : "capture";
}

static void updateMinMax(uint32_t value, uint32_t *min_val, uint32_t *max_val)
{
  if (value < *min_val) {
    *min_val = value;
  }
  if (value > *max_val) {
    *max_val = value;
  }
}

static void cciWriteReg(uint16_t reg, uint16_t val)
{
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  (void)Wire.write((uint8_t)(reg >> 8U));
  (void)Wire.write((uint8_t)(reg & 0xFFU));
  (void)Wire.write((uint8_t)(val >> 8U));
  (void)Wire.write((uint8_t)(val & 0xFFU));
  (void)Wire.endTransmission();
  delay(5U);
}

static uint16_t cciReadReg(uint16_t reg)
{
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  (void)Wire.write((uint8_t)(reg >> 8U));
  (void)Wire.write((uint8_t)(reg & 0xFFU));
  (void)Wire.endTransmission(false);
  (void)Wire.requestFrom((uint8_t)LEP_CCI_ADDRESS, (uint8_t)2U);

  uint16_t val = 0U;
  if (Wire.available() >= 2) {
    const uint16_t msb = (uint16_t)Wire.read();
    const uint16_t lsb = (uint16_t)Wire.read();
    val = (uint16_t)((msb << 8U) | lsb);
  }
  return val;
}

static bool cciWaitBusy(uint32_t timeout_ms)
{
  const uint32_t start_ms = millis();
  while ((millis() - start_ms) < timeout_ms) {
    if ((cciReadReg(LEP_REG_STATUS) & 0x0001U) == 0U) {
      return true;
    }
    delay(10U);
  }
  return false;
}

static bool cciSet(uint16_t cmd_id, uint16_t value)
{
  if (!cciWaitBusy(2000U)) {
    return false;
  }

  cciWriteReg(LEP_REG_DATA0, value);
  cciWriteReg(LEP_REG_LENGTH, 1U);
  cciWriteReg(LEP_REG_COMMAND, (uint16_t)(cmd_id | 0x02U));
  return cciWaitBusy(2000U);
}

static void resetLepton(void)
{
  SPI.endTransaction();
  digitalWrite(CS_PIN, HIGH);
  delay(200U);
  SPI.beginTransaction(SPISettings(18000000U, MSBFIRST, SPI_MODE3));
}

static bool initLepton(void)
{
  Wire.beginTransmission(LEP_CCI_ADDRESS);
  if (Wire.endTransmission() != 0U) {
    return false;
  }

  if (!cciSet(LEP_CMD_AGC_ENABLE, 0x0000U)) {
    return false;
  }
  if (!cciSet(LEP_CMD_VID_OUTPUT, 0x0007U)) {
    return false;
  }
  return true;
}

static bool captureFrame(uint32_t *discard_count)
{
  const uint32_t start_ms = millis();
  uint8_t current_seg = 0U;
  bool frame_complete = false;
  uint32_t local_discards = 0U;

  while (!frame_complete) {
    if ((millis() - start_ms) > 3000U) {
      *discard_count = local_discards;
      return false;
    }

    digitalWrite(CS_PIN, LOW);
    SPI.transfer(s_raw_packet, PACKET_SIZE);
    digitalWrite(CS_PIN, HIGH);

    if ((s_raw_packet[0] & 0x0FU) == 0x0FU) {
      local_discards++;
      delayMicroseconds(30U);
      continue;
    }

    const uint8_t packet_id = s_raw_packet[1];
    if (packet_id >= PACKETS_PER_SEG) {
      local_discards++;
      continue;
    }

    if (packet_id == 20U) {
      const uint8_t seg_bits = (uint8_t)((s_raw_packet[0] >> 4U) & 0x07U);
      if ((seg_bits > 0U) && (seg_bits <= 4U)) {
        current_seg = seg_bits;
      }
    }

    if (current_seg > 0U) {
      const uint32_t base_idx =
          (((uint32_t)(current_seg - 1U) * (uint32_t)PACKETS_PER_SEG * (uint32_t)PIXELS_PER_PKT) +
           ((uint32_t)packet_id * (uint32_t)PIXELS_PER_PKT));

      if ((base_idx + PIXELS_PER_PKT) <= FRAME_SIZE) {
        for (uint32_t i = 0U; i < PIXELS_PER_PKT; i++) {
          const uint8_t msb = s_raw_packet[4U + (i * 2U)];
          const uint8_t lsb = s_raw_packet[5U + (i * 2U)];
          s_frame_buffer[base_idx + i] = (uint16_t)(((uint16_t)lsb << 8U) | (uint16_t)msb);
        }
      }

      if (packet_id == 59U) {
        if (current_seg == 4U) {
          frame_complete = true;
        } else {
          current_seg++;
        }
      }
    }
  }

  *discard_count = local_discards;
  return true;
}

static void sendFrameToSerial1(void)
{
  const uint8_t *ptr = (const uint8_t *)s_frame_buffer;
  size_t sent_bytes = 0U;

  (void)Serial1.write("FSTART", 6U);
  (void)Serial1.write((uint8_t)0U);

  while (sent_bytes < UART_FRAME_BYTES) {
    const size_t remaining = UART_FRAME_BYTES - sent_bytes;
    const size_t chunk_len = (remaining > 1024U) ? 1024U : remaining;
    (void)Serial1.write(&ptr[sent_bytes], chunk_len);
    sent_bytes += chunk_len;
    delayMicroseconds(200U);
  }

  Serial1.flush();
}

static void printStatusLine(void)
{
  if (s_window.frames == 0U) {
    Serial.print("STAT mode=");
    Serial.print(modeName(s_mode));
    Serial.print(" fps=0.00 frames=0 sleep_ms=");
    Serial.print(s_post_frame_sleep_ms);
    Serial.print(" total_frames=");
    Serial.print(s_total_frames);
    Serial.print(" total_timeouts=");
    Serial.print(s_total_timeouts);
    Serial.print(" total_resets=");
    Serial.print(s_total_resets);
    Serial.print(" total_discards=");
    Serial.println(s_total_discards);
    return;
  }

  const uint32_t elapsed_ms = millis() - s_window_start_ms;
  const float fps = ((float)s_window.frames * 1000.0f) / (float)elapsed_ms;
  const float avg_capture_ms = (float)s_window.capture_us_sum / (1000.0f * (float)s_window.frames);
  const float avg_tx_ms = (float)s_window.tx_us_sum / (1000.0f * (float)s_window.frames);
  const float avg_frame_ms = (float)s_window.frame_us_sum / (1000.0f * (float)s_window.frames);
  const float min_tx_ms = (s_window.tx_us_max == 0U) ? 0.0f : ((float)s_window.tx_us_min / 1000.0f);
  const float max_tx_ms = (float)s_window.tx_us_max / 1000.0f;

  Serial.print("STAT mode=");
  Serial.print(modeName(s_mode));
  Serial.print(" fps=");
  Serial.print(fps, 2);
  Serial.print(" frames=");
  Serial.print(s_window.frames);
  Serial.print(" elapsed_ms=");
  Serial.print(elapsed_ms);
  Serial.print(" avg_capture_ms=");
  Serial.print(avg_capture_ms, 2);
  Serial.print(" min_capture_ms=");
  Serial.print((float)s_window.capture_us_min / 1000.0f, 2);
  Serial.print(" max_capture_ms=");
  Serial.print((float)s_window.capture_us_max / 1000.0f, 2);
  Serial.print(" avg_tx_ms=");
  Serial.print(avg_tx_ms, 2);
  Serial.print(" min_tx_ms=");
  Serial.print(min_tx_ms, 2);
  Serial.print(" max_tx_ms=");
  Serial.print(max_tx_ms, 2);
  Serial.print(" avg_frame_ms=");
  Serial.print(avg_frame_ms, 2);
  Serial.print(" discards=");
  Serial.print(s_window.discards);
  Serial.print(" timeouts=");
  Serial.print(s_window.timeouts);
  Serial.print(" resets=");
  Serial.print(s_window.resets);
  Serial.print(" sleep_ms=");
  Serial.print(s_post_frame_sleep_ms);
  Serial.print(" total_frames=");
  Serial.print(s_total_frames);
  Serial.print(" total_timeouts=");
  Serial.print(s_total_timeouts);
  Serial.print(" total_resets=");
  Serial.print(s_total_resets);
  Serial.print(" total_discards=");
  Serial.println(s_total_discards);
}

static void printHelp(void)
{
  Serial.println("HELP commands:");
  Serial.println("HELP start");
  Serial.println("HELP stop");
  Serial.println("HELP status");
  Serial.println("HELP mode capture");
  Serial.println("HELP mode uart");
  Serial.println("HELP sleep <ms>");
  Serial.println("HELP reset");
  Serial.println("HELP help");
}

static void printCurrentConfig(void)
{
  Serial.print("INFO running=");
  Serial.print(s_running ? "1" : "0");
  Serial.print(" mode=");
  Serial.print(modeName(s_mode));
  Serial.print(" sleep_ms=");
  Serial.print(s_post_frame_sleep_ms);
  Serial.print(" total_frames=");
  Serial.print(s_total_frames);
  Serial.print(" total_timeouts=");
  Serial.print(s_total_timeouts);
  Serial.print(" total_resets=");
  Serial.print(s_total_resets);
  Serial.print(" total_discards=");
  Serial.println(s_total_discards);
}

static void handleCommand(const char *cmd)
{
  if ((strcmp(cmd, "help") == 0) || (strcmp(cmd, "?") == 0)) {
    printHelp();
    return;
  }

  if (strcmp(cmd, "start") == 0) {
    s_running = true;
    Serial.println("ACK start");
    return;
  }

  if (strcmp(cmd, "stop") == 0) {
    s_running = false;
    Serial.println("ACK stop");
    return;
  }

  if (strcmp(cmd, "status") == 0) {
    printCurrentConfig();
    return;
  }

  if (strcmp(cmd, "mode capture") == 0) {
    s_mode = MODE_CAPTURE_ONLY;
    Serial.println("ACK mode capture");
    return;
  }

  if (strcmp(cmd, "mode uart") == 0) {
    s_mode = MODE_CAPTURE_AND_UART;
    Serial.println("ACK mode uart");
    return;
  }

  if (strncmp(cmd, "sleep ", 6) == 0) {
    s_post_frame_sleep_ms = (uint32_t)strtoul(&cmd[6], NULL, 10);
    Serial.print("ACK sleep ");
    Serial.println(s_post_frame_sleep_ms);
    return;
  }

  if (strcmp(cmd, "reset") == 0) {
    s_total_frames = 0U;
    s_total_timeouts = 0U;
    s_total_resets = 0U;
    s_total_discards = 0U;
    resetWindowStats();
    s_window_start_ms = millis();
    Serial.println("ACK reset");
    return;
  }

  Serial.print("ERR unknown_command=");
  Serial.println(cmd);
}

static void pollUsbCommands(void)
{
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }

    if ((ch == '\r') || (ch == '\n')) {
      if (s_cmd_len > 0U) {
        s_cmd_buf[s_cmd_len] = '\0';
        handleCommand(s_cmd_buf);
        s_cmd_len = 0U;
      }
      continue;
    }

    if (s_cmd_len < (sizeof(s_cmd_buf) - 1U)) {
      s_cmd_buf[s_cmd_len++] = (char)ch;
    }
  }
}

void setup(void)
{
  Serial.begin(2000000U);
  Serial1.begin(4000000U);
  Wire.begin();
  Wire.setClock(400000U);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(18000000U, MSBFIRST, SPI_MODE3));
  delay(1000U);

  resetWindowStats();
  s_window_start_ms = millis();

  Serial.println("BOOT lepton_fps_benchmark");
  if (!initLepton()) {
    Serial.println("ERR lepton_init_failed");
    return;
  }

  Serial.println("READY lepton_init_complete");
  printHelp();
  printCurrentConfig();
}

void loop(void)
{
  pollUsbCommands();

  if ((millis() - s_window_start_ms) >= 1000U) {
    printStatusLine();
    resetWindowStats();
    s_window_start_ms = millis();
  }

  if (!s_running) {
    delay(10U);
    return;
  }

  const uint32_t frame_start_us = micros();
  uint32_t discard_count = 0U;

  if (!captureFrame(&discard_count)) {
    s_total_timeouts++;
    s_total_resets++;
    s_total_discards += discard_count;
    s_window.timeouts++;
    s_window.resets++;
    s_window.discards += discard_count;
    resetLepton();
    Serial.println("WARN capture_timeout_reset");
    return;
  }

  const uint32_t capture_us = micros() - frame_start_us;
  uint32_t tx_us = 0U;

  if (s_mode == MODE_CAPTURE_AND_UART) {
    const uint32_t tx_start_us = micros();
    sendFrameToSerial1();
    tx_us = micros() - tx_start_us;
  }

  if (s_post_frame_sleep_ms > 0U) {
    delay(s_post_frame_sleep_ms);
  }

  const uint32_t frame_us = micros() - frame_start_us;

  s_total_frames++;
  s_total_discards += discard_count;
  s_window.frames++;
  s_window.discards += discard_count;
  s_window.capture_us_sum += capture_us;
  s_window.tx_us_sum += tx_us;
  s_window.frame_us_sum += frame_us;

  updateMinMax(capture_us, &s_window.capture_us_min, &s_window.capture_us_max);
  updateMinMax(tx_us, &s_window.tx_us_min, &s_window.tx_us_max);
  updateMinMax(frame_us, &s_window.frame_us_min, &s_window.frame_us_max);
}
