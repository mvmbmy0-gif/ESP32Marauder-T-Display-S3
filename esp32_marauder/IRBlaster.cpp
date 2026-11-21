#include "IRBlaster.h"

IRBlaster ir_blaster_obj;

IRBlaster::IRBlaster() {
  pin = IR_TX_PIN;
  channel = IR_LEDC_CHANNEL;
  current_freq = 38000;
  initialized = false;
  loop_active = false;
  loop_target = IR_LOOP_FAIRY_LIGHTS;
  loop_interval_ms = 3000;
  last_loop_tx_ms = 0;
  loop_cycle_count = 0;
  loop_sub_step = 0;
  last_tx_info = "Ready";
}

void IRBlaster::begin(uint8_t tx_pin, uint8_t ledc_ch) {
  pin = tx_pin;
  channel = ledc_ch;
  current_freq = 38000;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  // Setup hardware LEDC timer for 38kHz PWM
  ledcSetup(channel, current_freq, 8);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0); // Start with carrier OFF

  initialized = true;
  Serial.printf("[IR] Blaster initialized on GPIO %u (LEDC CH %u, Carrier: %u Hz)\n", pin, channel, current_freq);
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
  uint32_t code = 0;
  String name = "";

  switch (cmd) {
    case LED_CMD_POWER_ON:
      code = 0x00F7C03F; name = "Power ON"; break;
    case LED_CMD_POWER_OFF:
      code = 0x00F740BF; name = "Power OFF"; break;
    case LED_CMD_BRIGHT_UP:
      code = 0x00F700FF; name = "Bright +"; break;
    case LED_CMD_BRIGHT_DOWN:
      code = 0x00F7807F; name = "Bright -"; break;
    case LED_CMD_RED:
      code = 0x00F720DF; name = "Red"; break;
    case LED_CMD_GREEN:
      code = 0x00F7A05F; name = "Green"; break;
    case LED_CMD_BLUE:
      code = 0x00F7609F; name = "Blue"; break;
    case LED_CMD_WHITE:
      code = 0x00F7E01F; name = "White"; break;
    case LED_CMD_FLASH:
      code = 0x00F7D02F; name = "Flash"; break;
    case LED_CMD_STROBE:
      code = 0x00F7F00F; name = "Strobe"; break;
    case LED_CMD_FADE:
      code = 0x00F7C837; name = "Fade"; break;
    case LED_CMD_SMOOTH:
      code = 0x00F7E817; name = "Smooth"; break;
  }

  // Send primary 0x00F7 code
  sendNEC32(code);
  // Also send 0x00FF alternate if power command
  if (cmd == LED_CMD_POWER_ON) {
    delay(40);
    sendNEC32(0x00FF02FD);
  } else if (cmd == LED_CMD_POWER_OFF) {
    delay(40);
    sendNEC32(0x00FF827D);
  }

  last_tx_info = "WS611I LED: " + name;
  Serial.printf("[IR TX] WS611I LED Strip: %s (0x%08X)\n", name.c_str(), code);
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
