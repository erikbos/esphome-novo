#pragma once

#include "esphome/core/log.h"

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <string>

static const char *const NOVO_TAG = "novo_cc1101";

// ESP32-C6 pin map
static constexpr gpio_num_t PIN_CC1101_SCK  = GPIO_NUM_6;
static constexpr gpio_num_t PIN_CC1101_MOSI = GPIO_NUM_7;
static constexpr gpio_num_t PIN_CC1101_MISO = GPIO_NUM_2;
static constexpr gpio_num_t PIN_CC1101_CSN  = GPIO_NUM_18;
static constexpr gpio_num_t PIN_CC1101_GDO0 = GPIO_NUM_15;
static constexpr gpio_num_t PIN_CC1101_GDO2 = GPIO_NUM_14;

static spi_device_handle_t cc1101_spi = nullptr;
static bool cc1101_ready = false;

// CC1101 command bits
static constexpr uint8_t CC1101_READ_SINGLE = 0x80;
static constexpr uint8_t CC1101_READ_BURST  = 0xC0;
static constexpr uint8_t CC1101_WRITE_BURST = 0x40;

// CC1101 strobe commands
static constexpr uint8_t CC1101_SRES = 0x30;
static constexpr uint8_t CC1101_SIDLE = 0x36;
static constexpr uint8_t CC1101_STX = 0x35;
static constexpr uint8_t CC1101_SFTX = 0x3B;

// CC1101 registers
static constexpr uint8_t REG_IOCFG2 = 0x00;
static constexpr uint8_t REG_IOCFG0 = 0x02;
static constexpr uint8_t REG_FIFOTHR = 0x03;
static constexpr uint8_t REG_PKTLEN = 0x06;
static constexpr uint8_t REG_PKTCTRL1 = 0x07;
static constexpr uint8_t REG_PKTCTRL0 = 0x08;
static constexpr uint8_t REG_ADDR = 0x09;
static constexpr uint8_t REG_CHANNR = 0x0A;
static constexpr uint8_t REG_FSCTRL1 = 0x0B;
static constexpr uint8_t REG_FSCTRL0 = 0x0C;
static constexpr uint8_t REG_FREQ2 = 0x0D;
static constexpr uint8_t REG_FREQ1 = 0x0E;
static constexpr uint8_t REG_FREQ0 = 0x0F;
static constexpr uint8_t REG_MDMCFG4 = 0x10;
static constexpr uint8_t REG_MDMCFG3 = 0x11;
static constexpr uint8_t REG_MDMCFG2 = 0x12;
static constexpr uint8_t REG_MDMCFG1 = 0x13;
static constexpr uint8_t REG_MDMCFG0 = 0x14;
static constexpr uint8_t REG_DEVIATN = 0x15;
static constexpr uint8_t REG_MCSM1 = 0x17;
static constexpr uint8_t REG_MCSM0 = 0x18;
static constexpr uint8_t REG_FOCCFG = 0x19;
static constexpr uint8_t REG_BSCFG = 0x1A;
static constexpr uint8_t REG_AGCCTRL2 = 0x1B;
static constexpr uint8_t REG_AGCCTRL1 = 0x1C;
static constexpr uint8_t REG_AGCCTRL0 = 0x1D;
static constexpr uint8_t REG_FREND1 = 0x21;
static constexpr uint8_t REG_FREND0 = 0x22;
static constexpr uint8_t REG_FSCAL3 = 0x23;
static constexpr uint8_t REG_FSCAL2 = 0x24;
static constexpr uint8_t REG_FSCAL1 = 0x25;
static constexpr uint8_t REG_FSCAL0 = 0x26;
static constexpr uint8_t REG_TEST2 = 0x2C;
static constexpr uint8_t REG_TEST1 = 0x2D;
static constexpr uint8_t REG_TEST0 = 0x2E;
static constexpr uint8_t REG_PATABLE = 0x3E;
static constexpr uint8_t REG_TXFIFO = 0x3F;

// Status registers
static constexpr uint8_t REG_PARTNUM = 0x30;
static constexpr uint8_t REG_VERSION = 0x31;
static constexpr uint8_t REG_MARCSTATE = 0x35;

// Novo frame settings from your Arduino sketch
static constexpr uint8_t SYNC1 = 0xA9;
static constexpr uint8_t SYNC0 = 0x2F;
static constexpr uint8_t SYNC_TAIL = 0x50;

static constexpr uint8_t PAYLOAD_LEN = 11;
static constexpr uint8_t TRAILING_ZEROS = 25;
static constexpr uint8_t PACKET_LEN = 1 + PAYLOAD_LEN + TRAILING_ZEROS;  // 37

static constexpr uint8_t FRAMES_PER_BURST = 4;
static constexpr uint16_t FRAME_GAP_MS = 17;

// Radio timing: 4 preamble bytes + 2 sync bytes + 37 packet bytes at 9600 bps.
static constexpr uint32_t TX_WAIT_MS = 45;

struct RemoteInfo {
  const char *name;
  uint8_t dev1;
  uint8_t dev2;
  uint8_t dev3;
  uint8_t phase;
};

static const RemoteInfo REMOTES[3] = {
    {"Remote 1", 0xAC, 0xC5, 0xC4, 0x02},
    {"Remote 2", 0x6A, 0x1A, 0x58, 0x03},
    {"Remote 3", 0x98, 0xDE, 0x74, 0x01},
};

static uint8_t remote_idx = 1;   // default Remote 2
static uint8_t channel_idx = 0;  // default channel 1

// Same starts as your Arduino sketch
static uint8_t counters[3] = {0x00, 0x01, 0x03};

static std::string last_tx_status = "not initialized";

// Delay wrapper for FreeRTOS task delay
static void sleep_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

// Initialize SPI bus and attach CC1101 device
static bool cc1101_spi_setup() {
  if (cc1101_spi != nullptr) {
    return true;
  }

  ESP_LOGI(NOVO_TAG, "Initializing SPI: SCK=%d MOSI=%d MISO=%d CSN=%d",
           PIN_CC1101_SCK, PIN_CC1101_MOSI, PIN_CC1101_MISO, PIN_CC1101_CSN);

  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = PIN_CC1101_MISO;
  buscfg.mosi_io_num = PIN_CC1101_MOSI;
  buscfg.sclk_io_num = PIN_CC1101_SCK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 80;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(NOVO_TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return false;
  }

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = 1000000;  // 1 MHz for reliable first tests
  devcfg.mode = 0;
  devcfg.spics_io_num = PIN_CC1101_CSN;
  devcfg.queue_size = 1;
  devcfg.command_bits = 0;
  devcfg.address_bits = 0;
  devcfg.dummy_bits = 0;

  err = spi_bus_add_device(SPI2_HOST, &devcfg, &cc1101_spi);
  if (err != ESP_OK) {
    ESP_LOGE(NOVO_TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

// Perform bidirectional SPI transfer with CC1101
static bool cc1101_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (!cc1101_spi_setup()) {
    return false;
  }

  spi_transaction_t t = {};
  t.length = len * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;

  esp_err_t err = spi_device_transmit(cc1101_spi, &t);
  if (err != ESP_OK) {
    ESP_LOGE(NOVO_TAG, "SPI transmit failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

// Send single-byte strobe command to CC1101
static bool cc1101_strobe(uint8_t command) {
  uint8_t tx[1] = {command};
  uint8_t rx[1] = {0x00};
  return cc1101_transfer(tx, rx, sizeof(tx));
}

// Write single value to CC1101 configuration register
static bool cc1101_write_reg(uint8_t addr, uint8_t value) {
  uint8_t tx[2] = {addr, value};
  uint8_t rx[2] = {0x00, 0x00};
  return cc1101_transfer(tx, rx, sizeof(tx));
}

// Write multiple bytes to CC1101 register or FIFO
static bool cc1101_write_burst(uint8_t addr, const uint8_t *data, size_t len) {
  if (len > 70) {
    return false;
  }

  uint8_t tx[80] = {};
  uint8_t rx[80] = {};
  tx[0] = addr | CC1101_WRITE_BURST;
  memcpy(&tx[1], data, len);

  return cc1101_transfer(tx, rx, len + 1);
}

// Read single CC1101 configuration register
static uint8_t cc1101_read_config_reg(uint8_t addr) {
  uint8_t tx[2] = {static_cast<uint8_t>(addr | CC1101_READ_SINGLE), 0x00};
  uint8_t rx[2] = {0x00, 0x00};
  if (!cc1101_transfer(tx, rx, sizeof(tx))) {
    return 0xFF;
  }
  return rx[1];
}

// Read single CC1101 status register
static uint8_t cc1101_read_status_reg(uint8_t addr) {
  uint8_t tx[2] = {static_cast<uint8_t>(addr | CC1101_READ_BURST), 0x00};
  uint8_t rx[2] = {0x00, 0x00};
  if (!cc1101_transfer(tx, rx, sizeof(tx))) {
    return 0xFF;
  }
  return rx[1];
}

// Reset CC1101 radio to default state
static void cc1101_reset() {
  cc1101_strobe(CC1101_SRES);
  sleep_ms(10);
}

// Configure CC1101 for Novo D33100 protocol (868 MHz, 9.6 kbps, 2-FSK)
static bool cc1101_configure_novo() {
  if (!cc1101_spi_setup()) {
    last_tx_status = "SPI setup failed";
    return false;
  }

  cc1101_reset();

  uint8_t partnum = cc1101_read_status_reg(REG_PARTNUM);
  uint8_t version = cc1101_read_status_reg(REG_VERSION);

  ESP_LOGI(NOVO_TAG, "CC1101 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);

  if (version == 0x00 || version == 0xFF) {
    last_tx_status = "CC1101 version invalid; check wiring";
    ESP_LOGE(NOVO_TAG, "%s", last_tx_status.c_str());
    return false;
  }

  // GPIOs: keep them quiet/useful but not required for TX wait.
  cc1101_write_reg(REG_IOCFG2, 0x0D);
  cc1101_write_reg(REG_IOCFG0, 0x06);

  // FIFO threshold
  cc1101_write_reg(REG_FIFOTHR, 0x47);

  // Packet format:
  // fixed length, no CRC, no whitening, no address check, no status append
  cc1101_write_reg(REG_PKTLEN, PACKET_LEN);
  cc1101_write_reg(REG_PKTCTRL1, 0x00);
  cc1101_write_reg(REG_PKTCTRL0, 0x00);
  cc1101_write_reg(REG_ADDR, 0x00);
  cc1101_write_reg(REG_CHANNR, 0x00);

  // Frequency: 868.000 MHz with 26 MHz crystal.
  // FREQ = round(868e6 * 65536 / 26e6) = 0x216276
  cc1101_write_reg(REG_FREQ2, 0x21);
  cc1101_write_reg(REG_FREQ1, 0x62);
  cc1101_write_reg(REG_FREQ0, 0x76);

  // Modem settings:
  // RX bandwidth approx 101.6 kHz, data rate approx 9596 bps.
  cc1101_write_reg(REG_FSCTRL1, 0x06);
  cc1101_write_reg(REG_FSCTRL0, 0x00);
  cc1101_write_reg(REG_MDMCFG4, 0xC8);
  cc1101_write_reg(REG_MDMCFG3, 0x83);

  // 2-FSK, no Manchester, 16/16 sync word detection.
  cc1101_write_reg(REG_MDMCFG2, 0x02);

  // 4-byte preamble.
  cc1101_write_reg(REG_MDMCFG1, 0x22);
  cc1101_write_reg(REG_MDMCFG0, 0xF8);

  // Frequency deviation approx 50.8 kHz, closest CC1101 setting to 52 kHz.
  cc1101_write_reg(REG_DEVIATN, 0x50);

  // Radio state handling / calibration
  cc1101_write_reg(REG_MCSM1, 0x30);
  cc1101_write_reg(REG_MCSM0, 0x18);

  // Common 868/915 MHz calibration/filter defaults.
  cc1101_write_reg(REG_FOCCFG, 0x16);
  cc1101_write_reg(REG_BSCFG, 0x6C);
  cc1101_write_reg(REG_AGCCTRL2, 0x43);
  cc1101_write_reg(REG_AGCCTRL1, 0x40);
  cc1101_write_reg(REG_AGCCTRL0, 0x91);
  cc1101_write_reg(REG_FREND1, 0x56);
  cc1101_write_reg(REG_FREND0, 0x10);
  cc1101_write_reg(REG_FSCAL3, 0xE9);
  cc1101_write_reg(REG_FSCAL2, 0x2A);
  cc1101_write_reg(REG_FSCAL1, 0x00);
  cc1101_write_reg(REG_FSCAL0, 0x1F);
  cc1101_write_reg(REG_TEST2, 0x81);
  cc1101_write_reg(REG_TEST1, 0x35);
  cc1101_write_reg(REG_TEST0, 0x09);

  // Output power. 0xC0 is commonly used for high/+10 dBm-ish PA table on 868/915 modules.
  cc1101_write_reg(REG_PATABLE, 0xC0);

  // Sync word A9 2F. The 0x50 sync tail is sent as first payload byte.
  cc1101_write_reg(0x04, SYNC1);
  cc1101_write_reg(0x05, SYNC0);

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SFTX);

  cc1101_ready = true;
  last_tx_status = "CC1101 configured for Novo TX";
  ESP_LOGI(NOVO_TAG, "%s", last_tx_status.c_str());
  return true;
}

// Return diagnostic string with CC1101 register values
static std::string cc1101_probe_string() {
  if (!cc1101_spi_setup()) {
    return "SPI setup failed";
  }

  uint8_t iocfg2 = cc1101_read_config_reg(REG_IOCFG2);
  uint8_t iocfg0 = cc1101_read_config_reg(REG_IOCFG0);
  uint8_t partnum = cc1101_read_status_reg(REG_PARTNUM);
  uint8_t version = cc1101_read_status_reg(REG_VERSION);
  uint8_t marcstate = cc1101_read_status_reg(REG_MARCSTATE);

  char buf[180];
  snprintf(buf, sizeof(buf),
           "IOCFG2=0x%02X IOCFG0=0x%02X PARTNUM=0x%02X VERSION=0x%02X MARCSTATE=0x%02X",
           iocfg2, iocfg0, partnum, version, marcstate);
  return std::string(buf);
}

// Build 11-byte Novo protocol frame with rolling code and checksum
static void buildFrame(uint8_t cmd1, uint8_t cmd2, uint8_t c, uint8_t r, uint8_t out[11]) {
  const RemoteInfo &remote = REMOTES[remote_idx];

  uint8_t a0 = 0x28 | ((c & 0x03) ^ remote.phase);
  uint8_t a1 = c ^ remote.dev1;
  uint8_t a2 = c ^ remote.dev2;
  uint8_t a3 = c ^ remote.dev3;
  uint8_t a4 = c;
  uint8_t a5 = c ^ (channel_idx << 2);
  uint8_t a6 = c ^ cmd1;
  uint8_t a7 = c ^ static_cast<uint8_t>((cmd2 + ((r >> 6) & 0x03)) & 0xFF);
  uint8_t a8 = c ^ static_cast<uint8_t>((r & 0x3F) << 2);
  uint8_t a9_hi6 = static_cast<uint8_t>(((c >> 2) + 7) & 0x3F);
  uint8_t a9 = static_cast<uint8_t>((a9_hi6 << 2) | (c & 0x03));

  uint16_t s = (a1 >> 2) + (a2 >> 2) + (a3 >> 2) + (a4 >> 2) + (a5 >> 2) +
               (a6 >> 2) + (a7 >> 2) + (a8 >> 2) + (a9 >> 2);
  uint8_t a10 = static_cast<uint8_t>((s & 0x3F) << 2);

  out[0] = a0;
  out[1] = a1;
  out[2] = a2;
  out[3] = a3;
  out[4] = a4;
  out[5] = a5;
  out[6] = a6;
  out[7] = a7;
  out[8] = a8;
  out[9] = a9;
  out[10] = a10;
}

// Transmit single 11-byte Novo frame over the air
static bool sendFrame(const uint8_t payload[11]) {
  if (!cc1101_ready && !cc1101_configure_novo()) {
    return false;
  }

  uint8_t packet[PACKET_LEN] = {};
  packet[0] = SYNC_TAIL;

  for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
    packet[1 + i] = payload[i];
  }

  for (uint8_t i = 1 + PAYLOAD_LEN; i < PACKET_LEN; i++) {
    packet[i] = 0x00;
  }

  cc1101_strobe(CC1101_SIDLE);
  sleep_ms(1);
  cc1101_strobe(CC1101_SFTX);
  sleep_ms(1);

  if (!cc1101_write_burst(REG_TXFIFO, packet, PACKET_LEN)) {
    last_tx_status = "TX FIFO write failed";
    ESP_LOGE(NOVO_TAG, "%s", last_tx_status.c_str());
    return false;
  }

  cc1101_strobe(CC1101_STX);

  // Wait for full over-air frame to finish.
  sleep_ms(TX_WAIT_MS);

  cc1101_strobe(CC1101_SIDLE);
  sleep_ms(1);
  cc1101_strobe(CC1101_SFTX);

  return true;
}

// Convert command bytes to human-readable name
static const char *command_name(uint8_t cmd1, uint8_t cmd2) {
  if (cmd1 == 0x0A && cmd2 == 0xBC) return "OPEN";
  if (cmd1 == 0x07 && cmd2 == 0xAC) return "STOP";
  if (cmd1 == 0x13 && cmd2 == 0xE8) return "CLOSE";
  if (cmd1 == 0x22 && cmd2 == 0xB4) return "PAIR";
  return "UNKNOWN";
}

// Send complete command burst (5 frames with gaps) to blind
static bool sendCommand(uint8_t cmd1, uint8_t cmd2) {
  if (!cc1101_ready && !cc1101_configure_novo()) {
    return false;
  }

  uint8_t &c = counters[remote_idx];
  uint8_t r = 0;
  uint8_t frame[11];

  ESP_LOGI(NOVO_TAG, "TX %s remote=%s channel_idx=%u counter=0x%02X",
           command_name(cmd1, cmd2), REMOTES[remote_idx].name, channel_idx, c);

  // First frame, r=0
  buildFrame(cmd1, cmd2, c, 0, frame);
  if (!sendFrame(frame)) return false;
  c += 4;
  sleep_ms(FRAME_GAP_MS);

  // Then four more frames, matching your Arduino sketch.
  for (uint8_t i = 0; i < FRAMES_PER_BURST; i++) {
    buildFrame(cmd1, cmd2, c, r, frame);
    if (!sendFrame(frame)) return false;
    c += 4;
    r++;
    sleep_ms(FRAME_GAP_MS);
  }

  char buf[160];
  snprintf(buf, sizeof(buf), "sent %s using %s channel_idx=%u next_counter=0x%02X",
           command_name(cmd1, cmd2), REMOTES[remote_idx].name, channel_idx, c);
  last_tx_status = std::string(buf);
  ESP_LOGI(NOVO_TAG, "%s", last_tx_status.c_str());
  return true;
}

// Select which remote ID to emulate (0-2)
static void novo_set_remote(uint8_t idx) {
  if (idx > 2) return;
  remote_idx = idx;
  ESP_LOGI(NOVO_TAG, "Selected %s counter=0x%02X", REMOTES[remote_idx].name, counters[remote_idx]);
}

// Set blind channel number (1-6)
static void novo_set_channel_number(uint8_t ch) {
  if (ch < 1 || ch > 6) return;

  // Your Arduino mapping:
  // ch1->0, ch2->1, ch3->2, ch4->3, ch5->4, ch6->7
  channel_idx = (ch <= 5) ? static_cast<uint8_t>(ch - 1) : static_cast<uint8_t>(7);
  ESP_LOGI(NOVO_TAG, "Selected channel %u channel_idx=%u", ch, channel_idx);
}

// Get current channel number (1-6)
static uint8_t novo_channel_number() {
  if (channel_idx == 7) return 6;
  if (channel_idx == 5) return 0;
  return channel_idx + 1;
}

// Return formatted status string with remote, channel, and counters
static std::string novo_status_string() {
  char buf[180];
  snprintf(buf, sizeof(buf), "%s ch=%u channel_idx=%u counters=[0x%02X,0x%02X,0x%02X] last=%s",
           REMOTES[remote_idx].name,
           novo_channel_number(),
           channel_idx,
           counters[0],
           counters[1],
           counters[2],
           last_tx_status.c_str());
  return std::string(buf);
}

// Send OPEN command to blind
static void novo_open() {
  sendCommand(0x0A, 0xBC);
}

// Send STOP command to blind
static void novo_stop() {
  sendCommand(0x07, 0xAC);
}

// Send CLOSE command to blind
static void novo_close() {
  sendCommand(0x13, 0xE8);
}

// Send PAIR command to blind (for programming/pairing)
static void novo_pair() {
  sendCommand(0x22, 0xB4);
}
