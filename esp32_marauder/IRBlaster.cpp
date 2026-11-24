#include "IRBlaster.h"
#include "WORLD_IR_CODES.h"
#include <Preferences.h>
#include <SPIFFS.h>

IRBlaster ir_blaster_obj;

static const uint8_t candidate_pins[] = {21, 16, 20, 1, 2, 3, 17, 18, 43, 44};
static const size_t num_candidate_pins = sizeof(candidate_pins) / sizeof(candidate_pins[0]);

extern const IrCode *const NApowerCodes[];
extern const uint8_t num_NAcodes;
extern const IrCode *const EUpowerCodes[];
extern const uint8_t num_EUcodes;
extern const IrCode *const UniversalParsedCodes[];
extern const uint8_t num_UniversalParsedCodes;
extern const RawIrCode *const UniversalRawCodes[];
extern const uint8_t num_UniversalRawCodes;

static uint32_t parseHexBytes(const String &str) {
  String s = str;
  s.trim();
  char buf[64];
  strncpy(buf, s.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char *token = strtok(buf, " ");
  uint32_t result = 0;
  int shift = 0;
  int count = 0;
  while (token != nullptr && count < 4) {
    uint32_t b = strtoul(token, nullptr, 16) & 0xFF;
    result |= (b << shift);
    shift += 8;
    count++;
    token = strtok(nullptr, " ");
  }
  if (count == 0) {
    result = strtoul(s.c_str(), nullptr, 16);
  }
  return result;
}

static String uint32ToHexStr(uint32_t val) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%02X %02X %02X %02X",
           (unsigned int)(val & 0xFF),
           (unsigned int)((val >> 8) & 0xFF),
           (unsigned int)((val >> 16) & 0xFF),
           (unsigned int)((val >> 24) & 0xFF));
  return String(buf);
}

IRBlaster::IRBlaster() {
  pin = IR_TX_PIN;
  rx_pin = IR_RX_PIN;
  channel = IR_LEDC_CHANNEL;
  tx_repeats = 1;
  jammer_mode = 2; // SWEEP default
  tx_inverted = false;
  initialized = false;
  irsend = nullptr;
  irrecv = nullptr;
  is_receiving = false;
  loop_active = false;
  loop_target = IR_LOOP_FAIRY_LIGHTS;
  loop_interval_ms = 3000;
  last_loop_tx_ms = 0;
  loop_cycle_count = 0;
  loop_sub_step = 0;
  last_tx_info = "Ready";
}

IRBlaster::~IRBlaster() {
  if (irrecv) {
    if (is_receiving) {
      irrecv->disableIRIn();
    }
    delete irrecv;
    irrecv = nullptr;
  }
  if (irsend) {
    delete irsend;
    irsend = nullptr;
  }
}

void IRBlaster::loadSettings() {
  Preferences prefs;
  bool needsSave = false;
  if (prefs.begin("ir_cfg", false)) {
    pin = prefs.getUChar("tx_pin", 21);
    rx_pin = prefs.getUChar("rx_pin", 16);
    // Auto-migrate old default (20) to user's hardware (21)
    if (pin == 20 || pin == 0) {
      pin = 21;
      needsSave = true;
    }
    // Ensure RX pin is GPIO 16
    if (rx_pin == 21 || rx_pin == 20 || rx_pin == 0) {
      rx_pin = 16;
      needsSave = true;
    }
    tx_repeats = prefs.getUChar("repeats", 1);
    jammer_mode = prefs.getUChar("jam_mode", 2);
    tx_inverted = prefs.getBool("tx_inv", true);
    if (needsSave) {
      prefs.putUChar("tx_pin", pin);
      prefs.putUChar("rx_pin", rx_pin);
    }
    prefs.end();
  } else {
    pin = 21;
    rx_pin = 16;
    tx_repeats = 1;
    jammer_mode = 2;
    tx_inverted = true;
    saveSettings();
  }
}

void IRBlaster::saveSettings() {
  Preferences prefs;
  if (prefs.begin("ir_cfg", false)) {
    prefs.putUChar("tx_pin", pin);
    prefs.putUChar("rx_pin", rx_pin);
    prefs.putUChar("repeats", tx_repeats);
    prefs.putUChar("jam_mode", jammer_mode);
    prefs.putBool("tx_inv", tx_inverted);
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
  digitalWrite(pin, tx_inverted ? HIGH : LOW);

  gpio_reset_pin((gpio_num_t)rx_pin);
  pinMode(rx_pin, INPUT_PULLUP);

  if (irsend) {
    delete irsend;
  }
  irsend = new IRsend(pin, tx_inverted);
  irsend->begin();
  digitalWrite(pin, tx_inverted ? HIGH : LOW);

  if (irrecv) {
    if (is_receiving) {
      irrecv->disableIRIn();
    }
    delete irrecv;
  }
  irrecv = new IRrecv(rx_pin, 1024, 50, false);
  is_receiving = false;

  initFilesystem();

  initialized = true;
  Serial.printf("[IR] Blaster initialized: TX GPIO %u, RX GPIO %u, Repeats %u\n",
                pin, rx_pin, tx_repeats);
}

void IRBlaster::setPin(uint8_t new_pin) {
  if (new_pin == pin && initialized) return;
  pin = new_pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, tx_inverted ? HIGH : LOW);

  if (irsend) {
    delete irsend;
  }
  irsend = new IRsend(pin, tx_inverted);
  irsend->begin();
  digitalWrite(pin, tx_inverted ? HIGH : LOW);

  saveSettings();
}

void IRBlaster::setInverted(bool inv) {
  tx_inverted = inv;
  if (irsend) {
    delete irsend;
  }
  irsend = new IRsend(pin, tx_inverted);
  irsend->begin();
  pinMode(pin, OUTPUT);
  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  saveSettings();
}

void IRBlaster::cycleInverted() {
  setInverted(!tx_inverted);
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
  if (new_rx == rx_pin && initialized) return;
  rx_pin = new_rx;
  gpio_reset_pin((gpio_num_t)rx_pin);
  pinMode(rx_pin, INPUT_PULLUP);

  if (irrecv) {
    if (is_receiving) {
      irrecv->disableIRIn();
    }
    delete irrecv;
  }
  irrecv = new IRrecv(rx_pin, 1024, 50, false);
  is_receiving = false;

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

void IRBlaster::startIRReceiver() {
  if (!initialized) begin();
  gpio_reset_pin((gpio_num_t)rx_pin);
  pinMode(rx_pin, INPUT_PULLUP);
  if (!irrecv) {
    irrecv = new IRrecv(rx_pin, 1024, 50, false);
  }
  if (!is_receiving) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

void IRBlaster::stopIRReceiver() {
  if (irrecv && is_receiving) {
    irrecv->disableIRIn();
    is_receiving = false;
  }
}

bool IRBlaster::decodeIR(decode_results *results) {
  if (!irrecv || !results) return false;
  return irrecv->decode(results);
}

void IRBlaster::resumeIR() {
  if (irrecv) {
    irrecv->resume();
  }
}

bool IRBlaster::readIRSignal(String &proto, uint32_t &addr, uint32_t &cmd, uint32_t &raw, uint16_t &bits) {
  if (!irrecv || !is_receiving) startIRReceiver();
  decode_results results;
  if (!irrecv->decode(&results)) {
    return false;
  }

  proto = typeToString(results.decode_type, results.repeat);
  if (results.decode_type == decode_type_t::UNKNOWN || proto.length() == 0) {
    proto = "RAW";
  }
  addr = results.address;
  cmd = results.command;
  raw = (uint32_t)(results.value & 0xFFFFFFFF);
  bits = results.bits;

  irrecv->resume();
  return true;
}

void IRBlaster::sendDecodedResult(const decode_results *result) {
  if (!result) return;
  if (!initialized) begin();

  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  for (uint8_t r = 0; r <= tx_repeats; r++) {
    if (result->decode_type == decode_type_t::UNKNOWN) {
      uint16_t *rawarray = resultToRawArray(result);
      uint16_t rawlen = getCorrectedRawLength(result);
      irsend->sendRaw(rawarray, rawlen, 38000);
      delete[] rawarray;
    } else {
      irsend->send(result->decode_type, result->value, result->bits);
    }
    if (r < tx_repeats) delay(40);
  }

  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

void IRBlaster::sendRaw(const uint16_t *buf, uint16_t len, uint32_t freq_hz) {
  if (!initialized) begin();
  if (!buf || len == 0 || !irsend) return;

  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  irsend->sendRaw(buf, len, freq_hz > 0 ? (uint16_t)(freq_hz / 1000 * 1000) : 38000);
  digitalWrite(pin, tx_inverted ? HIGH : LOW);

  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
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

  for (uint8_t i = 0; i < numpairs; i++) {
    rawData[i * 2] = code->times[i * 2] * 10;
    rawData[(i * 2) + 1] = code->times[(i * 2) + 1] * 10;
  }

  sendRaw(rawData, numpairs * 2, freq_hz);
}

void IRBlaster::sendNEC32(uint32_t data, uint8_t repeats) {
  if (!initialized) begin();
  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  uint8_t total_repeats = (repeats > 0) ? repeats : tx_repeats;
  for (uint8_t r = 0; r <= total_repeats; r++) {
    irsend->sendNEC(data, 32);
    if (r < total_repeats) delay(40);
  }

  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

void IRBlaster::sendNEC(uint16_t address, uint8_t command, uint8_t repeats) {
  if (!initialized) begin();
  uint64_t data = irsend->encodeNEC(address, command);
  sendNEC32((uint32_t)data, repeats);
}

void IRBlaster::sendPanasonic48(uint64_t data) {
  if (!initialized) begin();
  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  for (uint8_t r = 0; r <= tx_repeats; r++) {
    irsend->sendPanasonic64(data, 48);
    if (r < tx_repeats) delay(40);
  }

  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

void IRBlaster::sendPanasonic(uint16_t address, uint8_t device, uint8_t subdevice, uint8_t command) {
  uint64_t data = 0;
  data |= ((uint64_t)(address & 0xFF)) << 40;
  data |= ((uint64_t)((address >> 8) & 0xFF)) << 32;
  data |= ((uint64_t)device) << 24;
  data |= ((uint64_t)subdevice) << 16;
  data |= ((uint64_t)command) << 8;
  uint8_t checksum = (device ^ subdevice ^ command);
  data |= ((uint64_t)checksum);
  sendPanasonic48(data);
}

void IRBlaster::sendPanasonicTV(uint8_t cmd) {
  sendPanasonic(0x4004, 0x01, 0x00, cmd);
}

void IRBlaster::sendFairyLights(FairyLightCmd cmd) {
  uint8_t cmd_byte = 0;
  switch (cmd) {
    case FAIRY_CMD_ON:       cmd_byte = 0x05; break;
    case FAIRY_CMD_OFF:      cmd_byte = 0x06; break;
    case FAIRY_CMD_MODE_1:   cmd_byte = 0x09; break;
    case FAIRY_CMD_MODE_2:   cmd_byte = 0x0A; break;
    case FAIRY_CMD_MODE_3:   cmd_byte = 0x0B; break;
    case FAIRY_CMD_MODE_4:   cmd_byte = 0x0C; break;
    case FAIRY_CMD_MODE_5:   cmd_byte = 0x0D; break;
    case FAIRY_CMD_MODE_6:   cmd_byte = 0x0E; break;
    case FAIRY_CMD_MODE_7:   cmd_byte = 0x0F; break;
    case FAIRY_CMD_MODE_8:   cmd_byte = 0x10; break;
    case FAIRY_CMD_DIM_UP:   cmd_byte = 0x07; break;
    case FAIRY_CMD_DIM_DOWN: cmd_byte = 0x08; break;
    case FAIRY_CMD_TIMER:    cmd_byte = 0x11; break;
    default: return;
  }
  sendNEC(0x00FF, cmd_byte, tx_repeats);
}

void IRBlaster::sendLEDStrip(LedStripCmd cmd) {
  uint8_t cmd_byte = 0;
  switch (cmd) {
    case LED_CMD_POWER_ON:   cmd_byte = 0x02; break;
    case LED_CMD_POWER_OFF:  cmd_byte = 0x03; break;
    case LED_CMD_BRIGHT_UP:  cmd_byte = 0x00; break;
    case LED_CMD_BRIGHT_DOWN:cmd_byte = 0x01; break;
    case LED_CMD_SPEED_UP:   cmd_byte = 0x07; break;
    case LED_CMD_SPEED_DOWN: cmd_byte = 0x06; break;
    case LED_CMD_MODE_NEXT:  cmd_byte = 0x04; break;
    case LED_CMD_MUSIC:      cmd_byte = 0x05; break;
    case LED_CMD_AUTO:       cmd_byte = 0x0F; break;
    case LED_CMD_RED:        cmd_byte = 0x09; break;
    case LED_CMD_GREEN:      cmd_byte = 0x0A; break;
    case LED_CMD_BLUE:       cmd_byte = 0x0B; break;
    case LED_CMD_WHITE:      cmd_byte = 0x0C; break;
    case LED_CMD_FLASH:      cmd_byte = 0x10; break;
    case LED_CMD_STROBE:     cmd_byte = 0x11; break;
    case LED_CMD_FADE:       cmd_byte = 0x12; break;
    case LED_CMD_SMOOTH:     cmd_byte = 0x13; break;
    default: return;
  }
  sendNEC(0x00FF, cmd_byte, tx_repeats);
}

void IRBlaster::sendJamBurst(uint8_t mode, uint32_t freq_hz) {
  if (!initialized) begin();
  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  pinMode(pin, OUTPUT);
  uint32_t period_us = 1000000 / (freq_hz > 0 ? freq_hz : 38000);
  uint32_t half_period = period_us / 2;
  if (half_period < 5) half_period = 13;

  switch (mode) {
    case 0: // Basic 38k
      for (int i = 0; i < 70; i++) {
        digitalWrite(pin, tx_inverted ? LOW : HIGH);
        delayMicroseconds(half_period);
        digitalWrite(pin, tx_inverted ? HIGH : LOW);
        delayMicroseconds(half_period);
      }
      delayMicroseconds(200);
      break;
    case 1: // Random Jitter
      for (int i = 0; i < 50; i++) {
        uint32_t hp = random(8, 25);
        digitalWrite(pin, tx_inverted ? LOW : HIGH);
        delayMicroseconds(hp);
        digitalWrite(pin, tx_inverted ? HIGH : LOW);
        delayMicroseconds(hp);
      }
      break;
    case 2: // Sweep 36k - 56k
      const uint32_t freqs[] = {36000, 38000, 40000, 56000};
      for (int f = 0; f < 4; f++) {
        uint32_t hp = (1000000 / freqs[f]) / 2;
        for (int i = 0; i < 20; i++) {
          digitalWrite(pin, tx_inverted ? LOW : HIGH);
          delayMicroseconds(hp);
          digitalWrite(pin, tx_inverted ? HIGH : LOW);
          delayMicroseconds(hp);
        }
      }
      break;
  }
  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

bool IRBlaster::runTVBGoneBatch(uint8_t batch_type, std::function<bool(uint16_t current, uint16_t total, const char* name)> progressCb) {
  if (!initialized) begin();
  uint16_t total = num_UniversalParsedCodes + num_UniversalRawCodes;
  if (batch_type == TVB_BATCH_ALL_EU_THEN_NA || batch_type == TVB_BATCH_EU) total += num_EUcodes;
  if (batch_type == TVB_BATCH_ALL_EU_THEN_NA || batch_type == TVB_BATCH_NA) total += num_NAcodes;

  uint16_t current = 0;

  if (batch_type == TVB_BATCH_ALL_EU_THEN_NA || batch_type == TVB_BATCH_EU) {
    for (uint8_t i = 0; i < num_EUcodes; i++) {
      current++;
      if (progressCb && !progressCb(current, total, "EU Power")) return false;
      sendParsedCode(EUpowerCodes[i]);
      delay(25);
    }
  }

  if (batch_type == TVB_BATCH_ALL_EU_THEN_NA || batch_type == TVB_BATCH_NA) {
    for (uint8_t i = 0; i < num_NAcodes; i++) {
      current++;
      if (progressCb && !progressCb(current, total, "NA Power")) return false;
      sendParsedCode(NApowerCodes[i]);
      delay(25);
    }
  }

  for (uint8_t i = 0; i < num_UniversalParsedCodes; i++) {
    current++;
    if (progressCb && !progressCb(current, total, "Universal Parsed")) return false;
    sendParsedCode(UniversalParsedCodes[i]);
    delay(25);
  }

  for (uint8_t i = 0; i < num_UniversalRawCodes; i++) {
    current++;
    if (progressCb && !progressCb(current, total, "Universal Raw")) return false;
    sendRawCode(UniversalRawCodes[i]);
    delay(25);
  }

  return true;
}

// -------------------------------------------------------------
// Custom Remotes Management & File System
// -------------------------------------------------------------
bool IRBlaster::initFilesystem() {
  if (!SPIFFS.begin(false)) {
    // If not mounted yet, do not format in hot loops
    return SPIFFS.begin(true);
  }
  return true;
}

std::vector<String> IRBlaster::listCustomRemotes() {
  std::vector<String> list;
  if (!initFilesystem()) return list;

  File root = SPIFFS.open("/");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String fname = String(file.name());
        if (fname.startsWith("/")) fname = fname.substring(1);
        if (fname.startsWith("BruceIR/")) fname = fname.substring(8);
        if (fname.endsWith(".ir")) {
          bool dup = false;
          for (const auto &item : list) {
            if (item == fname) { dup = true; break; }
          }
          if (!dup) list.push_back(fname);
        }
      }
      file.close();
      file = root.openNextFile();
    }
    root.close();
  }

  return list;
}

bool IRBlaster::loadRemote(const String &nameOrPath, CustomRemote &remote) {
  initFilesystem();
  String path = nameOrPath;
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  if (!path.endsWith(".ir")) {
    path += ".ir";
  }

  File file;
  if (SPIFFS.exists(path)) {
    file = SPIFFS.open(path, "r");
  } else {
    String altPath = "/BruceIR" + path;
    if (SPIFFS.exists(altPath)) {
      file = SPIFFS.open(altPath, "r");
      path = altPath;
    }
  }

  if (!file) {
    return false;
  }

  remote.filename = path.substring(path.lastIndexOf('/') + 1);
  remote.name = remote.filename.substring(0, remote.filename.lastIndexOf('.'));
  remote.buttons.clear();

  CustomIRButton curBtn;
  bool inBtn = false;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line.startsWith("#")) {
      if (inBtn && curBtn.name.length() > 0) {
        remote.buttons.push_back(curBtn);
        curBtn = CustomIRButton();
        inBtn = false;
      }
      continue;
    }

    int colon = line.indexOf(':');
    if (colon < 0) continue;
    String key = line.substring(0, colon);
    String val = line.substring(colon + 1);
    key.trim();
    val.trim();

    if (key.equalsIgnoreCase("name")) {
      if (inBtn && curBtn.name.length() > 0) {
        remote.buttons.push_back(curBtn);
        curBtn = CustomIRButton();
      }
      curBtn.name = val;
      inBtn = true;
    } else if (key.equalsIgnoreCase("type")) {
      curBtn.type = val;
    } else if (key.equalsIgnoreCase("protocol")) {
      curBtn.protocol = val;
    } else if (key.equalsIgnoreCase("address")) {
      curBtn.address = parseHexBytes(val);
    } else if (key.equalsIgnoreCase("command")) {
      curBtn.command = parseHexBytes(val);
    } else if (key.equalsIgnoreCase("value")) {
      val.replace(" ", "");
      curBtn.value = strtoull(val.c_str(), nullptr, 16);
    } else if (key.equalsIgnoreCase("bits")) {
      curBtn.bits = val.toInt();
    } else if (key.equalsIgnoreCase("frequency")) {
      curBtn.frequency = val.toInt();
    } else if (key.equalsIgnoreCase("data")) {
      curBtn.type = "raw";
      char *token = strtok((char*)val.c_str(), " ");
      while (token != nullptr) {
        curBtn.rawData.push_back((uint16_t)atoi(token));
        token = strtok(nullptr, " ");
      }
    }
  }
  if (inBtn && curBtn.name.length() > 0) {
    remote.buttons.push_back(curBtn);
  }
  file.close();
  return true;
}

bool IRBlaster::saveRemote(const CustomRemote &remote) {
  initFilesystem();
  String fname = remote.name;
  if (!fname.endsWith(".ir")) fname += ".ir";
  String path = "/" + fname;

  File file = SPIFFS.open(path, "w");
  if (!file) return false;

  file.println("Filetype: Bruce IR File");
  file.println("Version: 1");
  file.println("#");
  file.println("# " + remote.name);

  for (const auto &btn : remote.buttons) {
    file.println("name: " + btn.name);
    if (btn.type.equalsIgnoreCase("raw")) {
      file.println("type: raw");
      file.println("frequency: " + String(btn.frequency > 0 ? btn.frequency : 38000));
      file.println("duty_cycle: 0.330000");
      file.print("data:");
      for (size_t i = 0; i < btn.rawData.size(); i++) {
        file.print(" ");
        file.print(btn.rawData[i]);
      }
      file.println();
    } else {
      file.println("type: parsed");
      file.println("protocol: " + btn.protocol);
      file.println("address: " + uint32ToHexStr(btn.address));
      file.println("command: " + uint32ToHexStr(btn.command));
      file.println("bits: " + String(btn.bits > 0 ? btn.bits : 32));
      char valHex[24];
      snprintf(valHex, sizeof(valHex), "%08llX", (unsigned long long)btn.value);
      file.println("value: " + String(valHex));
    }
    file.println("#");
  }
  file.close();
  return true;
}

bool IRBlaster::deleteRemote(const String &nameOrPath) {
  initFilesystem();
  String fname = nameOrPath;
  if (!fname.endsWith(".ir")) fname += ".ir";
  String path = "/" + fname;

  bool removed = false;
  if (SPIFFS.exists(path)) {
    removed |= SPIFFS.remove(path);
  }
  String altPath = "/BruceIR" + path;
  if (SPIFFS.exists(altPath)) {
    removed |= SPIFFS.remove(altPath);
  }
  return removed;
}

bool IRBlaster::appendButtonToRemote(const String &remoteName, const CustomIRButton &btn) {
  CustomRemote rem;
  loadRemote(remoteName, rem);
  String cleanName = remoteName;
  if (cleanName.startsWith("/")) cleanName = cleanName.substring(cleanName.lastIndexOf('/') + 1);
  if (cleanName.endsWith(".ir")) cleanName = cleanName.substring(0, cleanName.length() - 3);
  if (rem.name.length() == 0) rem.name = cleanName;
  rem.buttons.push_back(btn);
  return saveRemote(rem);
}

void IRBlaster::sendCustomButton(const CustomIRButton &btn) {
  if (!initialized) begin();
  bool wasListening = (irrecv != nullptr && is_receiving);
  if (wasListening) {
    irrecv->disableIRIn();
    is_receiving = false;
  }

  for (uint8_t r = 0; r <= tx_repeats; r++) {
    if (btn.type.equalsIgnoreCase("raw")) {
      if (!btn.rawData.empty()) {
        irsend->sendRaw(btn.rawData.data(), btn.rawData.size(), btn.frequency > 0 ? btn.frequency : 38000);
      }
    } else {
      String proto = btn.protocol;
      proto.toUpperCase();

      if (proto.startsWith("NEC")) {
        uint64_t data = 0;
        if (btn.value != 0) {
          data = btn.value;
        } else if (btn.address != 0 || btn.command != 0) {
          data = irsend->encodeNEC(btn.address, btn.command);
        }
        if (data != 0) {
          irsend->sendNEC(data, btn.bits > 0 ? btn.bits : 32);
        }
      } else if (proto.startsWith("SAMSUNG")) {
        uint64_t data = 0;
        if (btn.value != 0) {
          data = btn.value;
        } else if (btn.address != 0 || btn.command != 0) {
          data = irsend->encodeSAMSUNG(btn.address, btn.command);
        }
        if (data != 0) {
          irsend->sendSAMSUNG(data, btn.bits > 0 ? btn.bits : 32);
        }
      } else if (proto.startsWith("SONY") || proto.startsWith("SIRC")) {
        uint16_t nbits = btn.bits > 0 ? btn.bits : 12;
        if (proto.equals("SIRC15")) nbits = 15;
        else if (proto.equals("SIRC20")) nbits = 20;
        irsend->sendSony(btn.value != 0 ? btn.value : btn.command, nbits, 2);
      } else if (proto.startsWith("RC5")) {
        uint16_t data = (btn.value != 0) ? (uint16_t)btn.value : irsend->encodeRC5(btn.address, btn.command);
        irsend->sendRC5(data, btn.bits > 0 ? btn.bits : 13);
      } else if (proto.startsWith("RC6")) {
        uint64_t data = (btn.value != 0) ? btn.value : irsend->encodeRC6(btn.address, btn.command);
        irsend->sendRC6(data, btn.bits > 0 ? btn.bits : 20);
      } else if (proto.startsWith("PANASONIC")) {
        irsend->sendPanasonic64(btn.value, btn.bits > 0 ? btn.bits : 48);
      } else {
        decode_type_t dt = strToDecodeType(btn.protocol.c_str());
        if (dt != decode_type_t::UNKNOWN && dt != decode_type_t::UNUSED) {
          irsend->send(dt, btn.value != 0 ? btn.value : btn.command, btn.bits > 0 ? btn.bits : 32);
        } else if (btn.value != 0) {
          irsend->sendNEC(btn.value, btn.bits > 0 ? btn.bits : 32);
        }
      }
    }
    if (r < tx_repeats) delay(40);
  }

  digitalWrite(pin, tx_inverted ? HIGH : LOW);
  if (wasListening && irrecv) {
    irrecv->enableIRIn(true);
    is_receiving = true;
  }
}

// -------------------------------------------------------------
// Test Loop
// -------------------------------------------------------------
void IRBlaster::startLoop(IRLoopTarget target, uint32_t interval_ms) {
  if (!initialized) begin();
  loop_target = target;
  loop_interval_ms = interval_ms;
  loop_cycle_count = 0;
  loop_sub_step = 0;
  last_loop_tx_ms = millis() - interval_ms;
  loop_active = true;
  last_tx_info = "Loop Started";
}

void IRBlaster::stopLoop() {
  loop_active = false;
  last_tx_info = "Loop Stopped";
}

uint32_t IRBlaster::getNextTxRemainingMs(uint32_t now) const {
  if (!loop_active) return 0;
  uint32_t elapsed = now - last_loop_tx_ms;
  if (elapsed >= loop_interval_ms) return 0;
  return loop_interval_ms - elapsed;
}

void IRBlaster::main(uint32_t currentTime) {
  if (!loop_active) return;
  if (currentTime - last_loop_tx_ms < loop_interval_ms) return;

  last_loop_tx_ms = currentTime;
  loop_cycle_count++;

  switch (loop_target) {
    case IR_LOOP_FAIRY_LIGHTS: {
      uint8_t mode = loop_sub_step % 8;
      sendFairyLights((FairyLightCmd)(FAIRY_CMD_MODE_1 + mode));
      last_tx_info = "Fairy Mode " + String(mode + 1);
      loop_sub_step++;
      break;
    }
    case IR_LOOP_LED_STRIP: {
      const LedStripCmd cmds[] = {LED_CMD_RED, LED_CMD_GREEN, LED_CMD_BLUE, LED_CMD_WHITE, LED_CMD_FLASH};
      uint8_t idx = loop_sub_step % 5;
      sendLEDStrip(cmds[idx]);
      last_tx_info = "LED Step " + String(idx + 1);
      loop_sub_step++;
      break;
    }
    case IR_LOOP_PANASONIC_TV: {
      sendPanasonicTV(PANASONIC_CMD_POWER);
      last_tx_info = "TV Power (" + String(loop_cycle_count) + ")";
      break;
    }
    case IR_LOOP_UNIVERSAL_ALL: {
      sendJamBurst(2, 38000);
      last_tx_info = "Sweep Burst (" + String(loop_cycle_count) + ")";
      break;
    }
  }
}
