/*
 * Novo blind remote emulator — Format A, all remotes
 * ---------------------------------------------------
 * Emulates any of the three Novo D33100 remotes over CC1101/E07-900M10S at 868 MHz / 9.6 kbps / 2-FSK.
 *
 * Serial commands (115200 baud):
 *   r<1|2|3>  select remote   e.g. r2
 *   c<1-6>    select channel  e.g. c1
 *   u         broadcast button 1 (OPEN)
 *   s         broadcast button 2 (STOP)
 *   d         broadcast button 3 (CLOSE)
 *   p         broadcast P1 (PAIR) — use with care
 *   x         enter / exit receiver mode
 *
 * Wiring Arduino Nano Matter -> CC1101 / Ebyte E07-900M10S
 *  3V3 - VCC
 *  GND - GND
 *  D10 - CSN
 *  D13 - SCK
 *  D11 - MOSI
 *  D12 - MISO
 *  D2  - GDO0
 *  D3  - GDO2
 */

#include <RadioLib.h>

// CC1101 pin map
#define PIN_CSN   10
#define PIN_GDO0   2
#define PIN_GDO2   3
CC1101 radio = new Module(PIN_CSN, PIN_GDO0, RADIOLIB_NC, PIN_GDO2);

// Remotes we can emulate
struct RemoteInfo {
  const char *name;
  uint8_t dev1, dev2, dev3, phase;
};
static const RemoteInfo REMOTES[3] = {
  { "Remote 1", 0xAC, 0xC5, 0xC4, 0x02 },
  { "Remote 2", 0x6A, 0x1A, 0x58, 0x03 },
  { "Remote 3", 0x98, 0xDE, 0x74, 0x01 },
};

// Our default is Remote 2
static uint8_t remote_idx = 1;  

// Default channel 1 (ch1→0, ch2→1, ch3→2, ch4→3, ch5→4, all→5, ch6→7)
static uint8_t channel_idx = 0;

// Per-remote counters to preserves rolling counter when switching remotes.
// R1 starts at 0x00 (c&3=0 → phase=2 → a0=0x2A)
// R2 starts at 0x01 (c&3=1 → phase=3 → a0=0x2A)
// R3 starts at 0x03 (c&3=3 → phase=1 → a0=0x2A)
static uint8_t counters[3] = { 0x00, 0x01, 0x03 };

// Radio & packet timing 
static const float    CARRIER_MHZ      = 868.0;
static const float    DEVIATION_KHZ    = 52.0;
static const uint32_t BITRATE_BPS      = 9600;

static const uint8_t  FRAMES_PER_BURST = 4;
static const uint16_t FRAME_GAP_MS     = 17;

static const uint16_t PREAMBLE_BITS    = 32;    // 4 × 0xAA
static const uint16_t PREAMBLE_BYTES   = PREAMBLE_BITS / 8;

static const uint8_t  SYNC_WORD[2]     = { 0xA9, 0x2F };
static const uint8_t  SYNC_TAIL        = 0x50; // constant byte after A9 2F sync word

static const uint8_t  PAYLOAD_LEN      = 11; // computed payload
static const uint8_t  TRAILING_ZEROS   = 25; // 200 bits, matching real remote
static const uint8_t  PACKET_LEN       = 1 + PAYLOAD_LEN + TRAILING_ZEROS;  // 37 bytes

// buildFrame builds a frame with remote id, channel id and rolling codes
void buildFrame(uint8_t cmd1, uint8_t cmd2, uint8_t c, uint8_t r, uint8_t out[11]) {
  uint8_t a0     = 0x28 | ((c & 0x03) ^ REMOTES[remote_idx].phase);
  uint8_t a1     = c ^ REMOTES[remote_idx].dev1;
  uint8_t a2     = c ^ REMOTES[remote_idx].dev2;
  uint8_t a3     = c ^ REMOTES[remote_idx].dev3;
  uint8_t a4     = c;
  uint8_t a5     = c ^ (channel_idx << 2);
  uint8_t a6     = c ^ cmd1;
  uint8_t a7     = c ^ (uint8_t)((cmd2 + ((r >> 6) & 0x03)) & 0xFF);
  uint8_t a8     = c ^ (uint8_t)((r & 0x3F) << 2);
  uint8_t a9_hi6 = (uint8_t)(((c >> 2) + 7) & 0x3F);
  uint8_t a9     = (uint8_t)((a9_hi6 << 2) | (c & 0x03));
  uint16_t s  = (a1>>2)+(a2>>2)+(a3>>2)+(a4>>2)+(a5>>2)+(a6>>2)+(a7>>2)+(a8>>2)+(a9>>2);
  uint8_t a10 = (uint8_t)((s & 0x3F) << 2);

  out[0]=a0; out[1]=a1; out[2]=a2; out[3]=a3; out[4]=a4; out[5]=a5;
  out[6]=a6; out[7]=a7; out[8]=a8; out[9]=a9; out[10]=a10;
}

// sendFrame sends a frames
void sendFrame(const uint8_t payload[11]) {
  uint8_t packet[PACKET_LEN];
  packet[0] = SYNC_TAIL;
  for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
    packet[1 + i] = payload[i];
  }
  for (uint8_t i = 1 + PAYLOAD_LEN; i < PACKET_LEN; i++) {
    packet[i] = 0x00;
  }

  int state = radio.startTransmit(packet, PACKET_LEN);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("startTransmit err "));
    Serial.println(state);
    return;
  }

  uint32_t bits    = (uint32_t)(PREAMBLE_BYTES + 2 + PACKET_LEN) * 8;
  uint32_t wait_us = (bits * 1000000UL) / BITRATE_BPS;
  delayMicroseconds(wait_us + 300);
  radio.finishTransmit();
}

// sendCommands sends multiple frames
void sendCommand(uint8_t cmd1, uint8_t cmd2) {
  uint8_t &c = counters[remote_idx];
  uint8_t r = 0;

  uint8_t frame[11];
  buildFrame(cmd1, cmd2, c, 0, frame);
  sendFrame(frame);
  c += 4;
  delay(FRAME_GAP_MS);
  
  // Second frame has counter r at 0 as well
  for (uint8_t i = 0; i < FRAMES_PER_BURST; i++) {
    buildFrame(cmd1, cmd2, c, r, frame);
    sendFrame(frame);
    c += 4;
    r++;
    delay(FRAME_GAP_MS);
  }
}

// printStatus prints active remote
void printStatus() {
  Serial.print(F("Active="));
  Serial.print(REMOTES[remote_idx].name);
  Serial.print(F(" channel="));
  Serial.print(channel_idx + 1);
  Serial.print(F(" counter=0x"));
  Serial.println(counters[remote_idx], HEX);
}

static bool   rx_mode      = false;
volatile bool packet_ready = false;

void onPacketReceived() {
  packet_ready = true;
}

static const char *cmdName(uint8_t cm1, uint8_t cm2) {
  if (cm1 == 0x0A && cm2 == 0xBC) {
    return "OPEN";
  }
  if (cm1 == 0x07 && cm2 == 0xAC) {
    return "STOP";
  }
  if (cm1 == 0x13 && cm2 == 0xE8) {
    return "CLOSE";
  }
  if (cm1 == 0x22 && cm2 == 0xB4) {
    return "PAIR";
  }
  return "Uknown command";
}

static uint8_t chFromIndex(uint8_t idx) {
  if (idx == 5) {
    return 0;  // all channels
  }
  if (idx == 7) {
    return 6;  // ch6
  }
  return idx + 1;
}

void printReceivedPacket(const uint8_t *buf, size_t len) {
  float   rssi = radio.getRSSI();
  uint8_t lqi  = radio.getLQI();

  // Line 1: full hex of every received byte + RSSI/LQI
  Serial.print(F("hex  RSSI="));
  Serial.print(rssi, 1);
  Serial.print(F(" LQI="));
  Serial.print(lqi);
  Serial.print(F(" : "));
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // Line 2: decoded Format A fields
  const uint8_t *p    = buf;
  size_t         avail = len;
  if (avail >= 1 && p[0] == SYNC_TAIL) {
    p++;
    avail--;
  }
  if (avail < PAYLOAD_LEN) {
    Serial.println(F("dec  (too short)"));
    return;
  }

  uint8_t a[PAYLOAD_LEN];
  for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
    a[i] = p[i];
  }

  uint8_t  c   = a[4];
  uint8_t  dev = a[1] ^ c;
  uint8_t  chi = ((a[5] ^ c) & 0xFF) >> 2;
  uint8_t  cm1 = a[6] ^ c;
  uint8_t  cm2 = a[7] ^ c;
  uint8_t  r   = ((a[8] ^ c) >> 2) & 0x3F;
  uint16_t cs  = (((uint16_t)(a[1]>>2)+(a[2]>>2)+(a[3]>>2)+(a[4]>>2)+(a[5]>>2)
                             +(a[6]>>2)+(a[7]>>2)+(a[8]>>2)+(a[9]>>2)) & 0x3F) << 2;

  const char *remName = (dev == 0xAC) ? "R1" : (dev == 0x6A) ? "R2" : (dev == 0x98) ? "R3" : nullptr;
  const char *btn     = cmdName(cm1, cm2);
  uint8_t     chNum   = chFromIndex(chi);

  Serial.print(F("dec  remote="));
  if (remName) {
    Serial.print(remName);
  } else {
    Serial.print(F("?(0x"));
    Serial.print(dev, HEX);
    Serial.print(')');
  }

  Serial.print(F("  ch="));
  if (chNum == 0) {
    Serial.print(F("all"));
  } else {
    Serial.print(chNum);
  }

  Serial.print(F("  btn="));
  if (btn) {
    Serial.print(btn);
  } else {
    Serial.print(F("?(0x"));
    Serial.print(cm1, HEX);
    Serial.print(',');
    Serial.print(cm2, HEX);
    Serial.print(')');
  }

  Serial.print(F("  c=0x"));
  Serial.print(c, HEX);
  Serial.print(F("  r="));
  Serial.print(r);
  Serial.print(F("  a10="));
  Serial.println(cs == a[10] ? F("OK") : F("BAD"));
}

void enterReceiveMode() {
  radio.setBitRate(9.6);
  radio.setFrequencyDeviation(DEVIATION_KHZ);
  radio.setRxBandwidth(203.0);
  radio.setFrequency(CARRIER_MHZ);
  radio.setSyncWord(SYNC_WORD, 2);
  packet_ready = false;
  radio.setGdo0Action(onPacketReceived, RISING);
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("startReceive err "));
    Serial.println(state);
    rx_mode = false;
    return;
  }
  rx_mode = true;
  Serial.println(F("RX ON  (x to exit)"));
}

void exitReceiveMode() {
  radio.clearGdo0Action();
  radio.standby();

  rx_mode      = false;
  packet_ready = false;
  Serial.println(F("RX OFF"));
  printStatus();
}

void pollReceive() {
  if (!packet_ready) {
    return;
  }
  packet_ready = false;
  uint8_t buf[PACKET_LEN + 4];
  int state = radio.readData(buf, PACKET_LEN);
  if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH) {
    printReceivedPacket(buf, PACKET_LEN);
  } else {
    Serial.print(F("readData err "));
    Serial.println(state);
  }
  radio.startReceive();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Serial.println(F("Novo remote emulator (Format A / 9.6 kbps / 868 MHz)"));
  Serial.print(F("Initialising CC1101 ..."));

  int state = radio.begin(CARRIER_MHZ, 9.6, DEVIATION_KHZ, 100.0, 10, PREAMBLE_BITS);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("FAILED code "));
    Serial.println(state);
    while (true) {
      delay(1000);
    }
  }

  int16_t ver = radio.getChipVersion();
  Serial.print(F("CC1101 version 0x"));
  Serial.print(ver, HEX);
  if (ver == 0x00 || ver == 0xFF || ver < 0) {
    Serial.println(F("  WARNING: SPI problem?"));
  } else {
    Serial.println(F("  OK"));
  }

  radio.setSyncWord(SYNC_WORD, 2);
  pinMode(PIN_GDO0, INPUT);
  radio.fixedPacketLengthMode(PACKET_LEN);
  radio.setCrcFiltering(false);

  Serial.println(F("ready."));
  printStatus();
  Serial.println(F("r<1-3>=remote  c<1-6>=channel  u=OPEN  s=STOP  d=CLOSE  p=PAIR  x=RX"));
}

// Read next serial byte with a short timeout (used for two-char commands r/c).
static int readNextChar(uint16_t timeout_ms = 500) {
  unsigned long t = millis();
  while (!Serial.available()) {
    if (millis() - t >= timeout_ms) {
      return -1;
    }
  }
  return Serial.read();
}

void loop() {
  if (rx_mode) {
    pollReceive();
  }
  if (!Serial.available()) {
    return;
  }
  char ch = Serial.read();

  // x — toggle RX mode from anywhere
  if (ch == 'x' || ch == 'X') {
    if (rx_mode) {
      exitReceiveMode();
    } else {
      enterReceiveMode();
    }
    return;
  }

  // In RX mode, ignore all other keys
  if (rx_mode) {
    if (ch != '\n' && ch != '\r') {
      Serial.println(F("(RX mode — x to exit)"));
    }
    return;
  }

  // r<digit> — select remote
  if (ch == 'r') {
    int d = readNextChar();
    if (d >= '1' && d <= '3') {
      remote_idx = (uint8_t)(d - '1');
      printStatus();
    } else {
      Serial.println(F("r: expected 1, 2 or 3"));
    }
    return;
  }

  // c<digit> — select channel
  if (ch == 'c') {
    int d = readNextChar();
    if (d >= '1' && d <= '6') {
      uint8_t ch_num = (uint8_t)(d - '0');
      channel_idx  = (ch_num <= 5) ? (ch_num - 1) : 7;  // ch6 → index 7
      printStatus();
    } else {
      Serial.println(F("c: expected 1-6"));
    }
    return;
  }

  // TX commands
  switch (ch) {
    case 'u':
    case 'U':
      Serial.print(REMOTES[remote_idx].name);
      Serial.print(F(" channel "));
      Serial.print(channel_idx + 1);
      Serial.println(F(" Open"));
      sendCommand(0x0A, 0xBC);
      Serial.println(F("Sent."));
      break;

    case 's':
    case 'S':
      Serial.print(REMOTES[remote_idx].name);
      Serial.print(F(" channel "));
      Serial.print(channel_idx + 1);
      Serial.println(F(" Stop"));
      sendCommand(0x07, 0xAC);
      Serial.println(F("Sent."));
      break;

    case 'd':
    case 'D':
      Serial.print(REMOTES[remote_idx].name);
      Serial.print(F(" channel "));
      Serial.print(channel_idx + 1);
      Serial.println(F(" Close"));
      sendCommand(0x13, 0xE8);
      Serial.println(F("Sent."));
      break;

    case 'p':
    case 'P':
      Serial.print(REMOTES[remote_idx].name);
      Serial.print(F(" channel "));
      Serial.print(channel_idx + 1);
      Serial.println(F(" Pair"));
      sendCommand(0x22, 0xB4);
      Serial.println(F("Sent."));
      break;

    case '\n':
    case '\r':
      break;

    default:
      Serial.println(F("Usage: "));
      Serial.println(ch);
      Serial.println(F("r<1-3>=remote  c<1-6>=channel  u=OPEN  s=STOP  d=CLOSE  p=PAIR  x=RX"));
      Serial.println(ch);
      printStatus();
      break;
  }
}
