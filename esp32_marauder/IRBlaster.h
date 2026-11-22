#pragma once

#ifndef IRBlaster_h
#define IRBlaster_h

#include <Arduino.h>
#include <functional>
#include "configs.h"

struct IrCode;
struct RawIrCode;

#ifndef IR_TX_PIN
  #define IR_TX_PIN 20
#endif

#ifndef IR_LEDC_CHANNEL
  #define IR_LEDC_CHANNEL 2
#endif

// Panasonic TV Commands (48-bit Kaseikyo format)
#define PANASONIC_CMD_POWER    0xBC  // 0x40040100BCBD
#define PANASONIC_CMD_MUTE     0x4C  // 0x400401004C4D
#define PANASONIC_CMD_VOL_UP   0x04  // 0x400401000405
#define PANASONIC_CMD_VOL_DOWN 0x84  // 0x400401008485
#define PANASONIC_CMD_CH_UP    0x2C  // 0x400401002C2D
#define PANASONIC_CMD_CH_DOWN  0xAC  // 0x40040100ACAD
#define PANASONIC_CMD_INPUT    0xA0  // 0x40040100A0A1
#define PANASONIC_CMD_OK       0x92  // 0x400401009293
#define PANASONIC_CMD_MENU     0x72  // 0x400401007273

// Window Fairy Lights (8-Mode remote)
enum FairyLightCmd {
  FAIRY_CMD_ON,
  FAIRY_CMD_OFF,
  FAIRY_CMD_MODE_1,
  FAIRY_CMD_MODE_2,
  FAIRY_CMD_MODE_3,
  FAIRY_CMD_MODE_4,
  FAIRY_CMD_MODE_5,
  FAIRY_CMD_MODE_6,
  FAIRY_CMD_MODE_7,
  FAIRY_CMD_MODE_8,
  FAIRY_CMD_DIM_UP,
  FAIRY_CMD_DIM_DOWN,
  FAIRY_CMD_TIMER
};

// SP611E / Smart Pixel LED Controller (38-key remote)
enum LedStripCmd {
  LED_CMD_POWER_ON,
  LED_CMD_POWER_OFF,
  LED_CMD_BRIGHT_UP,
  LED_CMD_BRIGHT_DOWN,
  LED_CMD_SPEED_UP,
  LED_CMD_SPEED_DOWN,
  LED_CMD_MODE_NEXT,
  LED_CMD_MUSIC,
  LED_CMD_AUTO,
  LED_CMD_RED,
  LED_CMD_GREEN,
  LED_CMD_BLUE,
  LED_CMD_WHITE,
  LED_CMD_FLASH,
  LED_CMD_STROBE,
  LED_CMD_FADE,
  LED_CMD_SMOOTH
};

// Loop Test Targets
enum IRLoopTarget {
  IR_LOOP_FAIRY_LIGHTS = 0,
  IR_LOOP_LED_STRIP,
  IR_LOOP_PANASONIC_TV,
  IR_LOOP_UNIVERSAL_ALL
};

class IRBlaster {
  private:
    uint8_t pin;
    uint8_t channel;
    uint32_t current_freq;
    bool initialized;

    // Loop state
    bool loop_active;
    IRLoopTarget loop_target;
    uint32_t loop_interval_ms;
    uint32_t last_loop_tx_ms;
    uint32_t loop_cycle_count;
    uint8_t loop_sub_step;
    String last_tx_info;

    void setFrequency(uint32_t freq_hz);
    void mark(uint32_t duration_us);
    void space(uint32_t duration_us);

  public:
    IRBlaster();
    void begin(uint8_t tx_pin = IR_TX_PIN, uint8_t ledc_ch = IR_LEDC_CHANNEL);

    // Raw protocol transmitters
    void sendRaw(const uint16_t *buf, uint16_t len, uint32_t freq_hz = 38000);
    void sendParsedCode(const struct IrCode *code);
    void sendRawCode(const struct RawIrCode *code);
    void sendNEC32(uint32_t data, uint8_t repeats = 0);
    void sendNEC(uint16_t address, uint8_t command, uint8_t repeats = 0);
    void sendPanasonic48(uint64_t data);
    void sendPanasonic(uint16_t address, uint8_t device, uint8_t subdevice, uint8_t command);

    // Bruce-ported Tools: TV-B-Gone & IR Jammer
    bool runTVBGoneBatch(uint8_t batch_type = 0, std::function<bool(uint16_t current, uint16_t total, const char* name)> progressCb = nullptr);
    void sendJamBurst(uint8_t mode = 0, uint32_t freq_hz = 38000);

    // Dynamic Pin configuration
    void setPin(uint8_t new_pin);
    uint8_t getPin() const { return pin; }

    // Device helpers
    void sendPanasonicTV(uint8_t cmd);
    void sendFairyLights(FairyLightCmd cmd);
    void sendLEDStrip(LedStripCmd cmd);

    // Test Loop (Every 3 seconds)
    void startLoop(IRLoopTarget target = IR_LOOP_FAIRY_LIGHTS, uint32_t interval_ms = 3000);
    void stopLoop();
    bool isLoopRunning() const { return loop_active; }
    uint32_t getCycleCount() const { return loop_cycle_count; }
    String getLastTxInfo() const { return last_tx_info; }
    uint32_t getNextTxRemainingMs(uint32_t now) const;

    // Main tick called from loop()
    void main(uint32_t currentTime);
};

extern IRBlaster ir_blaster_obj;

#endif
