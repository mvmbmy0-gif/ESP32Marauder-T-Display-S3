#include "IRBlaster.h"
#include "WORLD_IR_CODES.h"
#include <Preferences.h>

IRBlaster ir_blaster_obj;

static const uint8_t candidate_pins[] = {20, 16, 21, 1, 2, 3, 17, 18, 43, 44};
static const size_t num_candidate_pins = sizeof(candidate_pins) / sizeof(candidate_pins[0]);

extern const IrCode *const NApowerCodes[];
extern const uint8_t num_NAcodes;
extern const IrCode *const EUpowerCodes[];
extern const uint8_t num_EUcodes;
extern const IrCode *const UniversalParsedCodes[];
extern const uint8_t num_UniversalParsedCodes;
extern const RawIrCode *const UniversalRawCodes[];
extern const uint8_t num_UniversalRawCodes;

IRBlaster::IRBlaster() {
  pin = IR_TX_PIN;
  rx_pin = IR_RX_PIN;
  channel = IR_LEDC_CHANNEL;
  current_freq = 38000;
  tx_repeats = 1;
  jammer_mode = 2; // SWEEP default
  initialized = false;
  loop_active = false;
  loop_target = IR_LOOP_FAIRY_LIGHTS;
  loop_interval_ms = 3000;
  last_loop_tx_ms = 0;
  loop_cycle_count = 0;
  loop_sub_step = 0;
  last_tx_info = "Ready";
}

void IRBlaster::loadSettings() {
  Preferences prefs;
  if (prefs.begin("ir_cfg", true)) {
    pin = prefs.getUChar("tx_pin", pin);
    rx_pin = prefs.getUChar("rx_pin", rx_pin);
    // Ensure RX pin points to user's hardware connection (GPIO 16)
    if (rx_pin == 21) {
      rx_pin = 16;
    }
    tx_repeats = prefs.getUChar("repeats", tx_repeats);
    current_freq = prefs.getUInt("freq", current_freq);
    jammer_mode = prefs.getUChar("jam_mode", jammer_mode);
    prefs.end();
  }
}

void IRBlaster::saveSettings() {
  Preferences prefs;
  if (prefs.begin("ir_cfg", false)) {
    prefs.putUChar("tx_pin", pin);
    prefs.putUChar("rx_pin", rx_pin);
    prefs.putUChar("repeats", tx_repeats);
    prefs.putUInt("freq", current_freq);
    prefs.putUChar("jam_mode", jammer_mode);
    prefs.end();
  }
}

void IRBlaster::begin(uint8_t tx_pin, uint8_t ledc_ch) {
  loadSettings();
  if (tx_pin != IR_TX_PIN && tx_pin != pin) {
    pin = tx_pin;
  }
  channel = ledc_ch;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  // Setup hardware LEDC timer for carrier PWM
  ledcSetup(channel, current_freq > 0 ? current_freq : 38000, 8);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0); // Start with carrier OFF

  pinMode(rx_pin, INPUT_PULLUP);

  initialized = true;
  Serial.printf("[IR] Blaster initialized on TX GPIO %u (LEDC CH %u, Carrier: %u Hz), RX GPIO %u\n",
                pin, channel, current_freq, rx_pin);
}

void IRBlaster::setFrequency(uint32_t freq_hz) {
  if (current_freq != freq_hz) {
    current_freq = freq_hz;
    ledcSetup(channel, current_freq, 8);
  }
}

void IRBlaster::mark(uint32_t duration_us) {
  // 50% duty cycle on 8-bit timer = 128
  ledcWrite(channel, 128);
  delayMicroseconds(duration_us);
}

void IRBlaster::space(uint32_t duration_us) {
  // Carrier OFF
  ledcWrite(channel, 0);
  if (duration_us > 0) {
    delayMicroseconds(duration_us);
  }
}

void IRBlaster::setPin(uint8_t new_pin) {
  if (new_pin == pin && initialized) return;
  if (initialized) {
    ledcDetachPin(pin);
    pinMode(pin, INPUT);
  }
  pin = new_pin;
  saveSettings();
  begin(pin, channel);
}

void IRBlaster::cycleTxPin() {
  int idx = -1;
  for (size_t i = 0; i < num_candidate_pins; i++) {
    if (candidate_pins[i] == pin) {
      idx = i;
      break;
    }
  }
  uint8_t next_pin = (idx >= 0) ? candidate_pins[(idx + 1) % num_candidate_pins] : candidate_pins[0];
  setPin(next_pin);
}

void IRBlaster::setRxPin(uint8_t new_rx) {
  rx_pin = new_rx;
  pinMode(rx_pin, INPUT_PULLUP);
  saveSettings();
}

void IRBlaster::cycleRxPin() {
  int idx = -1;
  for (size_t i = 0; i < num_candidate_pins; i++) {
    if (candidate_pins[i] == rx_pin) {
      idx = i;
      break;
    }
  }
  uint8_t next_pin = (idx >= 0) ? candidate_pins[(idx + 1) % num_candidate_pins] : candidate_pins[0];
  setRxPin(next_pin);
}

void IRBlaster::setRepeats(uint8_t r) {
  tx_repeats = r;
  saveSettings();
}

void IRBlaster::cycleRepeats() {
  const uint8_t r_opts[] = {0, 1, 2, 3, 5, 10};
  int idx = -1;
  for (int i = 0; i < 6; i++) {
    if (r_opts[i] == tx_repeats) { idx = i; break; }
  }
  tx_repeats = (idx >= 0) ? r_opts[(idx + 1) % 6] : 1;
  saveSettings();
}

void IRBlaster::setConfigFrequency(uint32_t f) {
  setFrequency(f);
  saveSettings();
}

void IRBlaster::cycleFrequency() {
  const uint32_t f_opts[] = {38000, 36000, 40000, 56000};
  int idx = -1;
  for (int i = 0; i < 4; i++) {
    if (f_opts[i] == current_freq) { idx = i; break; }
  }
  uint32_t next_f = (idx >= 0) ? f_opts[(idx + 1) % 4] : 38000;
  setConfigFrequency(next_f);
}

void IRBlaster::setJammerMode(uint8_t m) {
  jammer_mode = m % 3;
  saveSettings();
}

void IRBlaster::cycleJammerMode() {
  jammer_mode = (jammer_mode + 1) % 3;
  saveSettings();
}

String IRBlaster::getJammerModeName() const {
  switch (jammer_mode) {
    case 0: return "BASIC (38k)";
    case 1: return "RANDOM";
    case 2: return "SWEEP";
    default: return "SWEEP";
  }
}

void IRBlaster::sendRaw(const uint16_t *buf, uint16_t len, uint32_t freq_hz) {
  if (!initialized) begin();
  if (!buf || len == 0) return;
  setFrequency(freq_hz > 0 ? freq_hz : 38000);
  for (uint16_t i = 0; i < len; i++) {
    if ((i % 2) == 0) {
      mark(buf[i]);
    } else {
      space(buf[i]);
    }
  }
  space(0);
}

void IRBlaster::sendParsedCode(const struct IrCode *code) {
  if (!initialized) begin();
  if (!code) return;
  uint16_t rawData[300];
  const uint8_t freq_khz = code->timer_val;
  const uint32_t freq_hz = (freq_khz > 0) ? (uint32_t)freq_khz * 1000 : 38000;
  const uint8_t numpairs = code->numpairs;
  const uint8_t bitcompression = code->bitcompression;

  uint16_t code_ptr = 0;
  uint8_t bits_r = 0;
  uint8_t bitsleft_r = 0;

  auto read_bits = [&](uint8_t count) -> uint8_t {
    uint8_t tmp = 0;
    while (count) {
      if (bitsleft_r == 0) {
        bits_r = code->codes[code_ptr++];
        bitsleft_r = 8;
      }
      bitsleft_r--;
      tmp = (tmp << 1) | ((bits_r >> bitsleft_r) & 1);
      count--;
    }
    return tmp;
  };

  for (uint8_t k = 0; k < numpairs; k++) {
    uint16_t ti = (read_bits(bitcompression)) * 2;
    rawData[k * 2] = code->times[ti] * 10;
    rawData[(k * 2) + 1] = code->times[ti + 1] * 10;
  }

  sendRaw(rawData, numpairs * 2, freq_hz);
}

void IRBlaster::sendRawCode(const struct RawIrCode *code) {
  if (!initialized) begin();
  if (!code) return;
  uint16_t rawData[300];
  const uint8_t freq_khz = code->timer_val;
  const uint32_t freq_hz = (freq_khz > 0) ? (uint32_t)freq_khz * 1000 : 38000;
  const uint8_t numpairs = code->numpairs;

  for (uint8_t k = 0; k < numpairs; k++) {
    rawData[k * 2] = (uint16_t)(code->times[k * 2]);
    rawData[(k * 2) + 1] = (uint16_t)(code->times[(k * 2) + 1]);
  }

  sendRaw(rawData, numpairs * 2, freq_hz);
}

bool IRBlaster::runTVBGoneBatch(uint8_t batch_type, std::function<bool(uint16_t current, uint16_t total, const char* name)> progressCb) {
  if (!initialized) begin();

  uint16_t total = 0;
  if (batch_type == 0) {
    // Merged: Europe first, then North America, then Universal Parsed & Raw
    total = num_EUcodes + num_NAcodes + num_UniversalParsedCodes + num_UniversalRawCodes;
  } else if (batch_type == 1) {
    // Europe + Universal
    total = num_EUcodes + num_UniversalParsedCodes + num_UniversalRawCodes;
  } else {
    // North America + Universal
    total = num_NAcodes + num_UniversalParsedCodes + num_UniversalRawCodes;
  }

  uint16_t current = 0;

  // 1. EU Codes
  if (batch_type == 0 || batch_type == 1) {
    for (uint8_t i = 0; i < num_EUcodes; i++) {
      current++;
      if (progressCb && !progressCb(current, total, "EU Region")) return false;
      sendParsedCode(EUpowerCodes[i]);
      delay(25);
    }
  }

  // 2. NA Codes
  if (batch_type == 0 || batch_type == 2) {
    for (uint8_t i = 0; i < num_NAcodes; i++) {
      current++;
      if (progressCb && !progressCb(current, total, "NA Region")) return false;
      sendParsedCode(NApowerCodes[i]);
      delay(25);
    }
  }

  // 3. Universal Parsed
  for (uint8_t i = 0; i < num_UniversalParsedCodes; i++) {
    current++;
    if (progressCb && !progressCb(current, total, "Universal Parsed")) return false;
    sendParsedCode(UniversalParsedCodes[i]);
    delay(25);
  }

  // 4. Universal Raw
  for (uint8_t i = 0; i < num_UniversalRawCodes; i++) {
    current++;
    if (progressCb && !progressCb(current, total, "Universal Raw")) return false;
    sendRawCode(UniversalRawCodes[i]);
    delay(25);
  }

  return true;
}

bool IRBlaster::readIRSignal(String &proto, uint32_t &addr, uint32_t &cmd, uint32_t &raw, uint16_t &bits) {
  pinMode(rx_pin, INPUT_PULLUP);

  // Quick poll: if line is idle (HIGH), wait up to 30ms for a falling edge
  uint32_t t0 = micros();
  while (digitalRead(rx_pin) == HIGH) {
    if (micros() - t0 > 30000) return false;
  }

  // Active carrier turns receiver output LOW
  unsigned long startLow = pulseIn(rx_pin, LOW, 20000); // expect ~9000us for NEC
  if (startLow < 1500) return false; // glitch / noise

  unsigned long startHigh = pulseIn(rx_pin, HIGH, 10000); // expect ~4500us

  // Check NEC Header (~9000us LOW, ~4500us HIGH)
  if (startLow >= 7000 && startLow <= 11000 && startHigh >= 3000 && startHigh <= 6000) {
    uint8_t bytes[4] = {0, 0, 0, 0};
    for (int b = 0; b < 4; b++) {
      for (int bit = 0; bit < 8; bit++) {
        unsigned long bitLow = pulseIn(rx_pin, LOW, 2500);
        unsigned long bitHigh = pulseIn(rx_pin, HIGH, 3500);
        if (bitLow == 0 || bitHigh == 0) return false;
        if (bitHigh > 1000) {
          bytes[b] |= (1 << bit);
        }
      }
    }
    proto = "NEC";
    raw = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    bits = 32;
    addr = ((uint32_t)bytes[0] << 8) | bytes[1];
    cmd = bytes[2];
    return true;
  }

  // Sony SIRC Header (~2400us LOW)
  if (startLow >= 1800 && startLow <= 3000 && startHigh >= 400 && startHigh <= 1000) {
    uint32_t data = 0;
    int bCount = 0;
    for (int i = 0; i < 20; i++) {
      unsigned long bitLow = pulseIn(rx_pin, LOW, 2000);
      unsigned long bitHigh = pulseIn(rx_pin, HIGH, 2000);
      if (bitLow == 0) break;
      bCount++;
      if (bitLow > 900) {
        data |= ((uint32_t)1 << i);
      }
    }
    proto = "SONY SIRC";
    raw = data;
    bits = bCount;
    cmd = data & 0x7F;
    addr = (data >> 7);
    return true;
  }

  // Generic / RAW pulse
  proto = "RAW PULSE";
  raw = startLow;
  addr = startHigh;
  cmd = 0;
  bits = 1;
  return true;
}

void IRBlaster::sendJamBurst(uint8_t mode, uint32_t freq_hz) {
  if (!initialized) begin();
  setFrequency(freq_hz > 0 ? freq_hz : 38000);
  switch (mode) {
    case 0:
      for (int i = 0; i < 60; i++) {
        mark(12);
        space(12);
      }
      break;
    case 1:
      for (int i = 0; i < 40; i++) {
        mark(random(10, 30));
        space(random(10, 50));
      }
      break;
    case 2:
      setFrequency(36000);
      for (int i = 0; i < 15; i++) { mark(14); space(14); }
      setFrequency(38000);
      for (int i = 0; i < 15; i++) { mark(13); space(13); }
      setFrequency(40000);
      for (int i = 0; i < 15; i++) { mark(12); space(12); }
      setFrequency(56000);
      for (int i = 0; i < 15; i++) { mark(9); space(9); }
      break;
  }
  space(0);
}

// NEC Protocol (32-bit MSB-to-LSB grouped into bytes, each byte sent LSB-first)
void IRBlaster::sendNEC32(uint32_t data, uint8_t repeats) {
  if (!initialized) begin();
  setFrequency(38000);

  // NEC Leader
  mark(9000);
  space(4500);

  // 4 Bytes: (data>>24), (data>>16), (data>>8), (data & 0xFF)
  for (int b = 3; b >= 0; b--) {
    uint8_t byte_val = (data >> (b * 8)) & 0xFF;
    for (int bit = 0; bit < 8; bit++) {
      mark(560);
      if ((byte_val >> bit) & 1) {
        space(1690); // Logical 1
      } else {
        space(560);  // Logical 0
      }
    }
  }

  // Stop bit
  mark(560);
  space(0);

  // Optional repeats
  for (uint8_t r = 0; r < repeats; r++) {
    delay(40);
    mark(9000);
    space(2250);
    mark(560);
    space(0);
  }
}

void IRBlaster::sendNEC(uint16_t address, uint8_t command, uint8_t repeats) {
  uint32_t raw_data = 0;
  if ((address & 0xFF00) == 0) {
    // Standard 8-bit address: Byte0 = Addr, Byte1 = ~Addr
    raw_data = ((uint32_t)address << 24) | (((uint32_t)(~address & 0xFF)) << 16);
  } else {
    // Extended 16-bit address
    raw_data = ((uint32_t)(address & 0xFF) << 24) | (((uint32_t)((address >> 8) & 0xFF)) << 16);
  }
  raw_data |= ((uint32_t)command << 8) | ((uint32_t)(~command & 0xFF));

  sendNEC32(raw_data, repeats);
}

// Panasonic / Kaseikyo Protocol (48-bit, 36.7kHz carrier)
void IRBlaster::sendPanasonic48(uint64_t data) {
  if (!initialized) begin();
  setFrequency(36700);

  // Panasonic Header
  mark(3500);
  space(1750);

  // 6 Bytes transmitted from high to low byte, each byte LSB first
  for (int b = 5; b >= 0; b--) {
    uint8_t byte_val = (data >> (b * 8)) & 0xFF;
    for (int bit = 0; bit < 8; bit++) {
      mark(432);
      if ((byte_val >> bit) & 1) {
        space(1296); // Logical 1
      } else {
        space(432);  // Logical 0
      }
    }
  }

  // Stop bit
  mark(432);
  space(0);

  // Return to standard 38kHz
  setFrequency(38000);
}

void IRBlaster::sendPanasonic(uint16_t address, uint8_t device, uint8_t subdevice, uint8_t command) {
  uint8_t checksum = device ^ subdevice ^ command;
  uint64_t data = ((uint64_t)(address >> 8) << 40) |
                  ((uint64_t)(address & 0xFF) << 32) |
                  ((uint64_t)device << 24) |
                  ((uint64_t)subdevice << 16) |
                  ((uint64_t)command << 8) |
                  ((uint64_t)checksum);
  sendPanasonic48(data);
}

void IRBlaster::sendPanasonicTV(uint8_t cmd) {
  // Address 0x4004, Device 0x01, Subdevice 0x00
  uint8_t checksum = 0x01 ^ 0x00 ^ cmd;
  uint64_t packet = ((uint64_t)0x4004 << 32) |
                    ((uint64_t)0x0100 << 16) |
                    ((uint64_t)cmd << 8) |
                    checksum;
  sendPanasonic48(packet);
  last_tx_info = "Panasonic TV (0x" + String(cmd, HEX) + ")";
  Serial.printf("[IR TX] Panasonic TV Command 0x%02X (48-bit: 0x%04X%04X%04X)\n",
                cmd, (uint16_t)(packet >> 32), (uint16_t)(packet >> 16), (uint16_t)packet);
}

void IRBlaster::sendFairyLights(FairyLightCmd cmd) {
  uint32_t code_var1 = 0;
  uint32_t code_var2 = 0;
  String name = "";

  switch (cmd) {
    case FAIRY_CMD_ON:
      code_var1 = 0x00FF02FD; code_var2 = 0x00FF40BF; name = "ON"; break;
    case FAIRY_CMD_OFF:
      code_var1 = 0x00FF827D; code_var2 = 0x00FF19E6; name = "OFF"; break;
    case FAIRY_CMD_MODE_1:
      code_var1 = 0x00FF04FB; code_var2 = 0x00FF0CF3; name = "Mode 1 (Comb)"; break;
    case FAIRY_CMD_MODE_2:
      code_var1 = 0x00FF05FA; code_var2 = 0x00FF18E7; name = "Mode 2 (Waves)"; break;
    case FAIRY_CMD_MODE_3:
      code_var1 = 0x00FF06F9; code_var2 = 0x00FF5EA1; name = "Mode 3 (Seq)"; break;
    case FAIRY_CMD_MODE_4:
      code_var1 = 0x00FF07F8; code_var2 = 0x00FF08F7; name = "Mode 4 (Slo-Glo)"; break;
    case FAIRY_CMD_MODE_5:
      code_var1 = 0x00FF08F7; code_var2 = 0x00FF1CE3; name = "Mode 5 (Chase)"; break;
    case FAIRY_CMD_MODE_6:
      code_var1 = 0x00FF09F6; code_var2 = 0x00FF5AA5; name = "Mode 6 (Fade)"; break;
    case FAIRY_CMD_MODE_7:
      code_var1 = 0x00FF0AF5; code_var2 = 0x00FF42BD; name = "Mode 7 (Twinkle)"; break;
    case FAIRY_CMD_MODE_8:
      code_var1 = 0x00FF0BF4; code_var2 = 0x00FF52AD; name = "Mode 8 (Steady)"; break;
    case FAIRY_CMD_DIM_UP:
      code_var1 = 0x00FF10EF; code_var2 = 0x00FF15EA; name = "DIM +"; break;
    case FAIRY_CMD_DIM_DOWN:
      code_var1 = 0x00FF11EE; code_var2 = 0x00FF09F6; name = "DIM -"; break;
    case FAIRY_CMD_TIMER:
      code_var1 = 0x00FF03FC; code_var2 = 0x00FF08F7; name = "TIMER"; break;
  }

  // Transmit primary variant
  sendNEC32(code_var1);
  // Wait 45ms and transmit alternative variant (guarantees compatibility with all Chinese window fairy light batches)
  delay(45);
  sendNEC32(code_var2);

  last_tx_info = "Fairy Lights: " + name;
  Serial.printf("[IR TX] Window Fairy Lights: %s (0x%08X / 0x%08X)\n", name.c_str(), code_var1, code_var2);
}

void IRBlaster::sendLEDStrip(LedStripCmd cmd) {
  uint32_t code_primary = 0;
  uint32_t code_alt = 0;
  String name = "";

  switch (cmd) {
    case LED_CMD_POWER_ON:
      code_primary = 0x00FFA25D; code_alt = 0x00FF02FD; name = "Power ON"; break;
    case LED_CMD_POWER_OFF:
      code_primary = 0x00FFE21D; code_alt = 0x00FF827D; name = "Power OFF"; break;
    case LED_CMD_BRIGHT_UP:
      code_primary = 0x00FF5AA5; code_alt = 0x00FF00FF; name = "Bright +"; break;
    case LED_CMD_BRIGHT_DOWN:
      code_primary = 0x00FF10EF; code_alt = 0x00FF807F; name = "Bright -"; break;
    case LED_CMD_SPEED_UP:
      code_primary = 0x00FF22DD; code_alt = 0x00F7E817; name = "Speed +"; break;
    case LED_CMD_SPEED_DOWN:
      code_primary = 0x00FF32CD; code_alt = 0x00F7C837; name = "Speed -"; break;
    case LED_CMD_MODE_NEXT:
      code_primary = 0x00FF12ED; code_alt = 0x00F7D02F; name = "Mode +"; break;
    case LED_CMD_MUSIC:
      code_primary = 0x00FF08F7; code_alt = 0x00FF52AD; name = "Music Mode"; break;
    case LED_CMD_AUTO:
      code_primary = 0x00FF30CF; code_alt = 0x00FF32CD; name = "Auto Effect"; break;
    case LED_CMD_RED:
      code_primary = 0x00FF1AE5; code_alt = 0x00F720DF; name = "Red"; break;
    case LED_CMD_GREEN:
      code_primary = 0x00FF9A65; code_alt = 0x00F7A05F; name = "Green"; break;
    case LED_CMD_BLUE:
      code_primary = 0x00FFA25D; code_alt = 0x00F7609F; name = "Blue"; break;
    case LED_CMD_WHITE:
      code_primary = 0x00FF22DD; code_alt = 0x00F7E01F; name = "White"; break;
    case LED_CMD_FLASH:
      code_primary = 0x00FF5EA1; code_alt = 0x00FFD02F; name = "Flash"; break;
    case LED_CMD_STROBE:
      code_primary = 0x00FF0CF3; code_alt = 0x00FFF00F; name = "Strobe"; break;
    case LED_CMD_FADE:
      code_primary = 0x00FF16E9; code_alt = 0x00FFC837; name = "Fade"; break;
    case LED_CMD_SMOOTH:
      code_primary = 0x00FF0DF2; code_alt = 0x00FFE817; name = "Smooth"; break;
  }

  // Transmit primary code from user's learned remote
  if (code_primary != 0) {
    sendNEC32(code_primary);
  }
  // If alternative exists and differs, send it as fallback after 35ms
  if (code_alt != 0 && code_alt != code_primary) {
    delay(35);
    sendNEC32(code_alt);
  }

  last_tx_info = "SP611E: " + name;
  Serial.printf("[IR TX] SP611E LED: %s (Primary: 0x%08X, Alt: 0x%08X)\n", name.c_str(), code_primary, code_alt);
}

void IRBlaster::startLoop(IRLoopTarget target, uint32_t interval_ms) {
  loop_active = true;
  loop_target = target;
  loop_interval_ms = interval_ms;
  last_loop_tx_ms = millis() - interval_ms; // Trigger immediate first burst
  loop_cycle_count = 0;
  loop_sub_step = 0;
  last_tx_info = "Starting 3s Loop...";
  Serial.printf("[IR Loop] Started target %d with %u ms interval\n", (int)target, interval_ms);
}

void IRBlaster::stopLoop() {
  loop_active = false;
  last_tx_info = "Loop Stopped";
  Serial.println("[IR Loop] Stopped");
}

uint32_t IRBlaster::getNextTxRemainingMs(uint32_t now) const {
  if (!loop_active) return 0;
  uint32_t elapsed = now - last_loop_tx_ms;
  if (elapsed >= loop_interval_ms) return 0;
  return loop_interval_ms - elapsed;
}

void IRBlaster::main(uint32_t currentTime) {
  if (!loop_active) return;

  if (currentTime - last_loop_tx_ms >= loop_interval_ms) {
    last_loop_tx_ms = currentTime;
    loop_cycle_count++;

    switch (loop_target) {
      case IR_LOOP_FAIRY_LIGHTS: {
        // Cycle through all 8 modes: Mode 1 -> 2 -> ... -> 8 -> 1...
        FairyLightCmd modes[8] = {
          FAIRY_CMD_MODE_1, FAIRY_CMD_MODE_2, FAIRY_CMD_MODE_3, FAIRY_CMD_MODE_4,
          FAIRY_CMD_MODE_5, FAIRY_CMD_MODE_6, FAIRY_CMD_MODE_7, FAIRY_CMD_MODE_8
        };
        uint8_t mode_idx = loop_sub_step % 8;
        sendFairyLights(modes[mode_idx]);
        last_tx_info = "Fairy Mode " + String(mode_idx + 1);
        loop_sub_step++;
        break;
      }

      case IR_LOOP_LED_STRIP: {
        // Cycle colors: Red -> Green -> Blue -> White -> Fade -> Flash
        LedStripCmd colors[6] = {
          LED_CMD_RED, LED_CMD_GREEN, LED_CMD_BLUE,
          LED_CMD_WHITE, LED_CMD_FADE, LED_CMD_FLASH
        };
        const char* color_names[6] = {"Red", "Green", "Blue", "White", "Fade", "Flash"};
        uint8_t c_idx = loop_sub_step % 6;
        sendLEDStrip(colors[c_idx]);
        last_tx_info = String("LED: ") + color_names[c_idx];
        loop_sub_step++;
        break;
      }

      case IR_LOOP_PANASONIC_TV: {
        // Panasonic TV Power Toggle
        sendPanasonicTV(PANASONIC_CMD_POWER);
        last_tx_info = "Panasonic TV Power";
        break;
      }

      case IR_LOOP_UNIVERSAL_ALL: {
        // Burst for all 3 targets
        FairyLightCmd modes[8] = {
          FAIRY_CMD_MODE_1, FAIRY_CMD_MODE_2, FAIRY_CMD_MODE_3, FAIRY_CMD_MODE_4,
          FAIRY_CMD_MODE_5, FAIRY_CMD_MODE_6, FAIRY_CMD_MODE_7, FAIRY_CMD_MODE_8
        };
        sendFairyLights(modes[loop_sub_step % 8]);
        delay(60);
        sendLEDStrip(LED_CMD_FLASH);
        delay(60);
        sendPanasonicTV(PANASONIC_CMD_POWER);
        last_tx_info = "Universal Burst #" + String(loop_cycle_count);
        loop_sub_step++;
        break;
      }
    }

    Serial.printf("[IR 3s Loop #%u] Transmitted: %s\n", loop_cycle_count, last_tx_info.c_str());
  }
}
