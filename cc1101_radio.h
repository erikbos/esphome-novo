#pragma once

// Novo D33100 protocol layer. Implements one of three frame formats.
//
// Radio bring-up (868 MHz, 2-FSK, 9.6 kbps, sync word, preamble, packet mode)
// is configured declaratively via the native ESPHome `cc1101:` component in the
// YAML. This header only owns the Novo-specific frame building (rolling code +
// checksum) and the multi-frame command burst, handing finished packets to the
// radio via CC1101Component::transmit_packet().

#include "esphome/core/log.h"
#include "esphome/components/cc1101/cc1101.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using esphome::cc1101::CC1101Component;
using esphome::cc1101::CC1101Error;

static const char *const NOVO_TAG = "novo_cc1101";

// Preamble length
static constexpr uint8_t NUM_PREAMBLE = 2;  // index 2 => 4 preamble bytes
// Sync word
static constexpr uint8_t SYNC1 = 0xA9;
static constexpr uint8_t SYNC0 = 0x2F;
// Sync tail byte (first byte of the 37-byte packet)
static constexpr uint8_t SYNC_TAIL = 0x50;
// Payload length
static constexpr uint8_t PAYLOAD_LEN = 11;
// Trailing zeros after payload (to fill out the 37-byte packet)
static constexpr uint8_t TRAILING_ZEROS = 25;
// Total packet length (preamble + sync + payload + trailing zeros)
static constexpr uint8_t PACKET_LEN = 1 + PAYLOAD_LEN + TRAILING_ZEROS;
// Number of frames to send per press
static constexpr uint8_t FRAMES_PER_BURST = 4;
// Gap between frames in milliseconds
static constexpr uint16_t FRAME_GAP_MS = 25;

struct RemoteInfo {
  const char *name;
  uint8_t dev1;
  uint8_t dev2;
  uint8_t dev3;
  uint8_t phase;
};

// Remotes are identified by their device IDs and phase, which are used to build the rolling code in the frame.
static const RemoteInfo REMOTES[3] = {
    {"Remote 1", 0xAC, 0xC5, 0xC4, 0x02},
    {"Remote 2", 0x6A, 0x1A, 0x58, 0x03},
    {"Remote 3", 0x98, 0xDE, 0x74, 0x01},
};

static uint8_t remote_idx = 2;   // default Remote 3
static uint8_t channel_idx = 0;  // default channel 1

// Start up counter values, 0x04 is observed after power on of remote
static uint8_t counters[3] = {0x04, 0x04, 0x04};

static std::string last_tx_status = "not initialized";

// Delay wrapper for FreeRTOS task delay
static void sleep_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
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

// Apply the Novo packet framing to the radio. RF/modem parameters (frequency,
// FSK, symbol rate, deviation, bandwidth, output power) are set declaratively in
// the YAML `cc1101:` block; everything about the packet structure lives here.
static void novo_configure_radio(CC1101Component *radio) {
  radio->set_sync_mode(esphome::cc1101::SyncMode::SYNC_MODE_16_16);
  radio->set_sync1(SYNC1);
  radio->set_sync0(SYNC0);
  radio->set_num_preamble(NUM_PREAMBLE);  // 4 preamble bytes
  radio->set_packet_length(PACKET_LEN);   // fixed-length 37-byte packets
  radio->set_crc_enable(false);
  radio->set_whitening(false);
  radio->set_packet_mode(true);           // FIFO packet mode (uses GDO0)
  ESP_LOGI(NOVO_TAG, "Radio framing: sync=%02X%02X preamble_idx=%u len=%u no-crc no-whitening",
           SYNC1, SYNC0, NUM_PREAMBLE, PACKET_LEN);
}

// Configure the radio framing once, on first use.
static bool radio_configured = false;
static void novo_ensure_configured(CC1101Component *radio) {
  if (radio_configured) {
    return;
  }
  novo_configure_radio(radio);
  radio_configured = true;
}

// Transmit single Novo frame: [sync tail][11 payload][25 zeros] via the radio.
static bool sendFrame(CC1101Component *radio, const uint8_t payload[11]) {
  std::vector<uint8_t> packet(PACKET_LEN, 0x00);
  packet[0] = SYNC_TAIL;
  for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
    packet[1 + i] = payload[i];
  }
  // Bytes [1 + PAYLOAD_LEN, PACKET_LEN) stay 0 (trailing zeros).

  CC1101Error err = radio->transmit_packet(packet);
  if (err != CC1101Error::NONE) {
    last_tx_status = "TX FIFO write failed";
    ESP_LOGE(NOVO_TAG, "transmit_packet failed (err=%d)", static_cast<int>(err));
    return false;
  }
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
static bool sendCommand(CC1101Component *radio, uint8_t cmd1, uint8_t cmd2) {
  novo_ensure_configured(radio);

  uint8_t &c = counters[remote_idx];
  uint8_t r = 0;
  uint8_t frame[11];

  ESP_LOGI(NOVO_TAG, "TX %s remote=%s channel_idx=%u counter=0x%02X",
           command_name(cmd1, cmd2), REMOTES[remote_idx].name, channel_idx, c);

  // First frame, r=0
  buildFrame(cmd1, cmd2, c, 0, frame);
  if (!sendFrame(radio, frame)) {
    return false;
  }
  c += 4;
  sleep_ms(FRAME_GAP_MS);

  // Second frame has counter r at 0 as well
  for (uint8_t i = 0; i < FRAMES_PER_BURST; i++) {
    buildFrame(cmd1, cmd2, c, r, frame);
    if (!sendFrame(radio, frame)) {
      return false;
    }
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
  if (idx > 2) {
    return;
  }

  remote_idx = idx;
  ESP_LOGI(NOVO_TAG, "Selected %s counter=0x%02X", REMOTES[remote_idx].name, counters[remote_idx]);
}

// Set blind channel number (1-6)
static void novo_set_channel_number(uint8_t ch) {
  if (ch < 1 || ch > 6) {
    return;
  }

  // ch1->0, ch2->1, ch3->2, ch4->3, ch5->4, ch6->7
  channel_idx = (ch <= 5) ? static_cast<uint8_t>(ch - 1) : static_cast<uint8_t>(7);
  ESP_LOGI(NOVO_TAG, "Selected channel %u channel_idx=%u", ch, channel_idx);
}

// Get current channel number (1-6)
static uint8_t novo_channel_number() {
  if (channel_idx == 7) {
    return 6;
  }
  if (channel_idx == 5) {
    return 0;
  }
  return channel_idx + 1;
}

// Send OPEN command to blind
static void novo_open(CC1101Component *radio) {
  sendCommand(radio, 0x0A, 0xBC);
}

// Send STOP command to blind
static void novo_stop(CC1101Component *radio) {
  sendCommand(radio, 0x07, 0xAC);
}

// Send CLOSE command to blind
static void novo_close(CC1101Component *radio) {
  sendCommand(radio, 0x13, 0xE8);
}

// Send PAIR command to blind (for programming/pairing)
static void novo_pair(CC1101Component *radio) {
  sendCommand(radio, 0x22, 0xB4);
}
