#include <SPI.h>
#include <stdint.h>
#include <string.h>

static SPIClass &TEST_SPI = SPI1;

static const uint32_t TEST_MAGIC = 0x54455354UL;
static const uint32_t RAW_FRAME_BYTES = 38400UL;
static const uint32_t DEFAULT_SPI_CLOCK_HZ = 10000000UL;
static const uint32_t DEFAULT_GAP_US = 20UL;
static const uint16_t TEST_PKT_SIZE = 1024U;
static const uint8_t PIN_CS = 0U;
static const uint8_t PIN_MISO = 1U;
static const uint8_t PIN_MOSI = 26U;
static const uint8_t PIN_SCK = 27U;

struct __attribute__((packed)) TestHeader {
  uint32_t magic;
  uint32_t seq;
  uint16_t payload_len;
  uint16_t checksum;
};

static const uint16_t TEST_PAYLOAD_SIZE = (uint16_t)(TEST_PKT_SIZE - sizeof(TestHeader));

struct WindowStats {
  uint32_t packets;
  uint32_t bytes;
  uint32_t errors;
  uint64_t write_us_sum;
  uint32_t write_us_min;
  uint32_t write_us_max;
};

static uint8_t s_tx_buf[TEST_PKT_SIZE];
static uint8_t s_rx_buf[TEST_PKT_SIZE];
static char s_cmd_buf[64];
static size_t s_cmd_len = 0U;

static bool s_running = true;
static uint32_t s_spi_clock_hz = DEFAULT_SPI_CLOCK_HZ;
static uint32_t s_gap_us = DEFAULT_GAP_US;
static uint32_t s_seq = 0U;
static uint32_t s_total_packets = 0U;
static uint32_t s_total_bytes = 0U;
static uint32_t s_total_errors = 0U;
static uint32_t s_window_start_ms = 0U;
static WindowStats s_window;

static uint16_t calc_checksum(const uint8_t *data, uint16_t len) {
  uint32_t sum = 0U;
  for (uint16_t i = 0U; i < len; i++) {
    sum += data[i];
  }
  return (uint16_t)(sum & 0xFFFFU);
}

static void fill_packet(uint32_t seq) {
  TestHeader *hdr = reinterpret_cast<TestHeader *>(s_tx_buf);
  uint8_t *payload = &s_tx_buf[sizeof(TestHeader)];

  for (uint16_t i = 0U; i < TEST_PAYLOAD_SIZE; i++) {
    payload[i] = (uint8_t)((seq + i) & 0xFFU);
  }

  hdr->magic = TEST_MAGIC;
  hdr->seq = seq;
  hdr->payload_len = TEST_PAYLOAD_SIZE;
  hdr->checksum = calc_checksum(payload, TEST_PAYLOAD_SIZE);
}

static void reset_window_stats(void) {
  memset(&s_window, 0, sizeof(s_window));
  s_window.write_us_min = 0xFFFFFFFFUL;
}

static void print_help(void) {
  Serial.println("HELP commands:");
  Serial.println("HELP start");
  Serial.println("HELP stop");
  Serial.println("HELP status");
  Serial.println("HELP speed <hz>");
  Serial.println("HELP gap <us>");
  Serial.println("HELP reset");
  Serial.println("HELP help");
}

static void print_status_line(void) {
  if (s_window.packets == 0U) {
    Serial.printf(
      "STAT mode=spi fps=0.00 mbps=0.00 packets=0 bytes=0 clock_hz=%lu gap_us=%lu total_packets=%lu total_bytes=%lu total_errors=%lu\r\n",
      s_spi_clock_hz,
      s_gap_us,
      s_total_packets,
      s_total_bytes,
      s_total_errors
    );
    return;
  }

  const uint32_t elapsed_ms = millis() - s_window_start_ms;
  const uint32_t fps_x100 = (uint32_t)(((uint64_t)s_window.bytes * 100000ULL) /
                                       ((uint64_t)elapsed_ms * (uint64_t)RAW_FRAME_BYTES));
  const uint32_t mbps_x100 = (uint32_t)(((uint64_t)s_window.bytes * 8ULL * 100ULL) /
                                        ((uint64_t)elapsed_ms * 1000ULL));
  const uint32_t avg_write_us_x100 = (uint32_t)((s_window.write_us_sum * 100ULL) /
                                                (uint64_t)s_window.packets);

  Serial.printf(
    "STAT mode=spi fps=%lu.%02lu mbps=%lu.%02lu packets=%lu bytes=%lu avg_write_us=%lu.%02lu min_write_us=%lu max_write_us=%lu clock_hz=%lu gap_us=%lu total_packets=%lu total_bytes=%lu total_errors=%lu\r\n",
    fps_x100 / 100U,
    fps_x100 % 100U,
    mbps_x100 / 100U,
    mbps_x100 % 100U,
    s_window.packets,
    s_window.bytes,
    avg_write_us_x100 / 100U,
    avg_write_us_x100 % 100U,
    (s_window.write_us_max == 0U) ? 0U : s_window.write_us_min,
    s_window.write_us_max,
    s_spi_clock_hz,
    s_gap_us,
    s_total_packets,
    s_total_bytes,
    s_total_errors
  );
}

static void print_current_config(void) {
  Serial.printf(
    "INFO running=%d clock_hz=%lu gap_us=%lu packet_size=%u total_packets=%lu total_bytes=%lu total_errors=%lu\r\n",
    s_running ? 1 : 0,
    s_spi_clock_hz,
    s_gap_us,
    TEST_PKT_SIZE,
    s_total_packets,
    s_total_bytes,
    s_total_errors
  );
}

static void handle_command(const char *cmd) {
  if ((strcmp(cmd, "help") == 0) || (strcmp(cmd, "?") == 0)) {
    print_help();
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
    print_current_config();
    return;
  }

  if (strncmp(cmd, "speed ", 6) == 0) {
    s_spi_clock_hz = strtoul(&cmd[6], NULL, 10);
    Serial.printf("ACK speed %lu\r\n", s_spi_clock_hz);
    return;
  }

  if (strncmp(cmd, "gap ", 4) == 0) {
    s_gap_us = strtoul(&cmd[4], NULL, 10);
    Serial.printf("ACK gap %lu\r\n", s_gap_us);
    return;
  }

  if (strcmp(cmd, "reset") == 0) {
    s_seq = 0U;
    s_total_packets = 0U;
    s_total_bytes = 0U;
    s_total_errors = 0U;
    reset_window_stats();
    s_window_start_ms = millis();
    Serial.println("ACK reset");
    return;
  }

  Serial.printf("ERR unknown_command=%s\r\n", cmd);
}

static void poll_usb_commands(void) {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }

    if ((ch == '\r') || (ch == '\n')) {
      if (s_cmd_len > 0U) {
        s_cmd_buf[s_cmd_len] = '\0';
        handle_command(s_cmd_buf);
        s_cmd_len = 0U;
      }
      continue;
    }

    if (s_cmd_len < (sizeof(s_cmd_buf) - 1U)) {
      s_cmd_buf[s_cmd_len++] = (char)ch;
    }
  }
}

static void send_one_packet(void) {
  fill_packet(s_seq);

  const uint32_t started_us = micros();
  TEST_SPI.beginTransaction(SPISettings(s_spi_clock_hz, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  TEST_SPI.transfer(s_tx_buf, s_rx_buf, TEST_PKT_SIZE);
  digitalWrite(PIN_CS, HIGH);
  TEST_SPI.endTransaction();
  const uint32_t elapsed_us = micros() - started_us;

  if (s_gap_us > 0U) {
    delayMicroseconds(s_gap_us);
  }

  s_window.packets++;
  s_window.bytes += TEST_PKT_SIZE;
  s_window.write_us_sum += elapsed_us;
  if (elapsed_us < s_window.write_us_min) {
    s_window.write_us_min = elapsed_us;
  }
  if (elapsed_us > s_window.write_us_max) {
    s_window.write_us_max = elapsed_us;
  }

  s_total_packets++;
  s_total_bytes += TEST_PKT_SIZE;
  s_seq++;
}

void setup() {
  Serial.begin(2000000);
  delay(1500U);

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);

  TEST_SPI.setMISO(PIN_MISO);
  TEST_SPI.setMOSI(PIN_MOSI);
  TEST_SPI.setSCK(PIN_SCK);
  TEST_SPI.begin();

  reset_window_stats();
  s_window_start_ms = millis();

  Serial.println("BOOT teensy_spi1_test");
  Serial.println("INFO wiring_teensy_spi1 pins 0(CS) 1(MISO) 26(MOSI) 27(SCK)");
  print_help();
  print_current_config();
}

void loop() {
  poll_usb_commands();

  if (s_running) {
    send_one_packet();
  } else {
    delay(1U);
  }

  const uint32_t now_ms = millis();
  if ((now_ms - s_window_start_ms) >= 1000U) {
    print_status_line();
    reset_window_stats();
    s_window_start_ms = now_ms;
  }
}
