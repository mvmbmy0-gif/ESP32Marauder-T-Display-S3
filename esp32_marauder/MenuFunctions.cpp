#include "MenuFunctions.h"
#include "MenuMarquee.h"
#include "CommandLine.h"
#include "OwnedListLifecycle.h"
#include "CensorText.h"
#include "lang_var.h"
#include "IRBlaster.h"
#if defined(HAS_CSI_MOTION)
  #include "CsiMotionMonitor.h"
#endif

#ifdef HAS_SCREEN

extern const unsigned char menu_icons[][66];
extern LinkedList<AccessPoint>* access_points;
extern LinkedList<Station>* stations;
extern LinkedList<AirTag>* airtags;
extern LinkedList<Flipper>* flippers;
extern LinkedList<BleDevice>* ble_devices;
extern CommandLine cli_obj;

namespace {
String screenName(String value) {
  if (!settings_obj.loadSetting<bool>("Censor")) return value;
  for (size_t i = 3; i < value.length(); ++i) value.setCharAt(i, 'X');
  return value;
}

String screenMac(String value) {
  if (!settings_obj.loadSetting<bool>("Censor")) return value;
  size_t digits = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(value[i]))) continue;
    if (digits >= 4) value.setCharAt(i, 'X');
    ++digits;
  }
  return value;
}

String screenIdentifier(const String& value) {
  return CensorText::looksLikeMac(value.c_str()) ? screenMac(value) : screenName(value);
}

bool parseGeofenceNumber(const String& value, double& parsed) {
  char* end = nullptr;
  parsed = strtod(value.c_str(), &end);
  return end != value.c_str() && *end == '\0' && isfinite(parsed);
}
}

#ifdef HAS_MINI_SCREEN
void MenuFunctions::drawMiniMenuButton(int b, int x, bool selected, uint16_t text_offset) {
  if (!current_menu || !current_menu->list || x < 0 || x >= current_menu->list->size())
    return;

  MenuNode mini_node = current_menu->list->get(x);
  bool is_setting_node = (mini_node.icon == SETTINGS && mini_node.color == TFTLIGHTGREY);
  uint16_t color = is_setting_node ? (mini_node.selected ? TFT_GREEN : TFT_RED) : this->getColor(mini_node.color);
  int16_t button_x = KEY_X - (KEY_W / 2);
  int16_t button_y = (KEY_Y + (b * (KEY_H + KEY_SPACING_Y))) - (KEY_H / 2);

  uint16_t background = selected ? (is_setting_node ? TFT_LIGHTGREY : color) : TFT_BLACK;
  uint16_t text_color = (selected && !is_setting_node) ? TFT_BLACK : color;

  display_obj.tft.setFreeFont(NULL);
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextWrap(false);
  #ifdef MARAUDER_POOM
    // The POOM status bar owns rows 0-7. Mini-screen's historical -4
    // adjustment moves the first menu item into that reserved area.
    display_obj.tft.fillRect(button_x, button_y, KEY_W, KEY_H, background);
  #else
    display_obj.tft.fillRect(button_x, button_y - 4, KEY_W, KEY_H, background);
  #endif
  display_obj.tft.setTextColor(text_color, background);
  #ifdef MARAUDER_POOM
    display_obj.tft.setCursor(button_x + BUTTON_PADDING, button_y + 1);
  #else
    display_obj.tft.setCursor(button_x + BUTTON_PADDING, button_y + (KEY_H / 2) - 8);
  #endif
  display_obj.tft.print(this->menuLabelWindow(current_menu->list->get(x).name, text_offset));
}
#endif

String MenuFunctions::menuLabelWindow(const String& name, uint16_t offset) {
  const int16_t available_width = max(1, KEY_W - (2 * BUTTON_PADDING));

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
    display_obj.tft.setTextSize(1);
  #else
    display_obj.tft.setFreeFont(MENU_FONT);
    display_obj.tft.setTextSize(KEY_TEXTSIZE);
  #endif

  if (offset >= name.length())
    offset = 0;

  String visible = name.substring(offset);
  while (visible.length() > 1 && display_obj.tft.textWidth(visible) > available_width)
    visible.remove(visible.length() - 1);

  return visible;
}

uint16_t MenuFunctions::menuLabelMaxOffset(const String& name) {
  const int16_t available_width = max(1, KEY_W - (2 * BUTTON_PADDING));
  uint16_t max_offset = 0;

  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
    display_obj.tft.setTextSize(1);
  #else
    display_obj.tft.setFreeFont(MENU_FONT);
    display_obj.tft.setTextSize(KEY_TEXTSIZE);
  #endif

  if (display_obj.tft.textWidth(name) > available_width) {
    max_offset = name.length() > 0 ? name.length() - 1 : 0;
    for (uint16_t offset = 1; offset < name.length(); offset++) {
      if (display_obj.tft.textWidth(name.substring(offset)) <= available_width) {
        max_offset = offset;
        break;
      }
    }
  }

  #ifdef HAS_FULL_SCREEN
    display_obj.tft.setFreeFont(NULL);
  #endif

  return max_offset;
}

void MenuFunctions::updateMenuMarquee(uint32_t current_time) {
  const bool menu_visible = (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
                            (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
                            (wifi_scan_obj.currentScanMode == OTA_UPDATE);

  if (!menu_visible || !current_menu || !current_menu->list ||
      current_menu->list->size() == 0 || current_menu->selected >= current_menu->list->size()) {
    marquee_menu = nullptr;
    marquee_selected = 0xFFFF;
    marquee_rendered_offset = 0;
    marquee_max_offset = 0;
    return;
  }

  if (marquee_menu != current_menu || marquee_selected != current_menu->selected) {
    marquee_menu = current_menu;
    marquee_selected = current_menu->selected;
    marquee_selected_since = current_time;
    marquee_rendered_offset = 0;
    marquee_max_offset = this->menuLabelMaxOffset(
      current_menu->list->get(current_menu->selected).name
    );
    return;
  }

  const uint16_t next_offset = MenuMarquee::offsetForElapsed(
    current_time - marquee_selected_since,
    marquee_max_offset
  );

  if (next_offset == marquee_rendered_offset)
    return;

  marquee_rendered_offset = next_offset;
  this->buttonSelected(
    current_menu->selected - menu_start_index,
    current_menu->selected,
    marquee_rendered_offset
  );
}

void MenuFunctions::buttonNotSelected(int b, int x) {
  if (x == -1)
    x = b;

  // Ensure b is within valid button index range
  b = (x - menu_start_index) % BUTTON_SCREEN_LIMIT;

  #ifdef HAS_MINI_SCREEN
    this->drawMiniMenuButton(b, x, false);
  #endif

  uint16_t color = (current_menu->list->get(x).icon == SETTINGS && current_menu->list->get(x).color == TFTLIGHTGREY) ? (current_menu->list->get(x).selected ? TFT_GREEN : TFT_RED) : this->getColor(current_menu->list->get(x).color);
  uint16_t icon_color = (current_menu->list->get(x).icon == SETTINGS && current_menu->list->get(x).color == TFTLIGHTGREY) ? TFT_LIGHTGREY : color;

  #ifdef HAS_FULL_SCREEN
    display_obj.tft.setFreeFont(MENU_FONT);
    display_obj.key[b].initButton(&display_obj.tft, KEY_X, KEY_Y + b * (KEY_H + KEY_SPACING_Y), KEY_W, KEY_H, TFT_BLACK, TFT_BLACK, color, (char*)"", KEY_TEXTSIZE);
    display_obj.key[b].drawButton(false, this->menuLabelWindow(current_menu->list->get(x).name));
    if ((current_menu->list->get(x).name != text09) && (current_menu->list->get(x).icon != 255))
          display_obj.tft.drawXBitmap(0,
                                      KEY_Y + (b * (KEY_H + KEY_SPACING_Y)) - (ICON_H / 2),
                                      menu_icons[current_menu->list->get(x).icon],
                                      ICON_W,
                                      ICON_H,
                                      TFT_BLACK,
                                      icon_color);
    display_obj.tft.setFreeFont(NULL);
  #endif
}

void MenuFunctions::buttonSelected(int b, int x, uint16_t text_offset) {
  if (x == -1)
    x = b;

  // Ensure b is within valid button index range
  b = (x - menu_start_index) % BUTTON_SCREEN_LIMIT;

  uint16_t color = this->getColor(current_menu->list->get(x).color);

  #ifdef HAS_MINI_SCREEN
    this->drawMiniMenuButton(b, x, true, text_offset);
  #endif

  #ifdef HAS_FULL_SCREEN
    display_obj.tft.setFreeFont(MENU_FONT);
    if (current_menu->list->get(x).icon == SETTINGS && current_menu->list->get(x).color == TFTLIGHTGREY) {
      uint16_t setting_color = current_menu->list->get(x).selected ? TFT_GREEN : TFT_RED;
      display_obj.key[b].initButton(&display_obj.tft, KEY_X, KEY_Y + b * (KEY_H + KEY_SPACING_Y), KEY_W, KEY_H, TFT_BLACK, TFT_LIGHTGREY, setting_color, (char*)"", KEY_TEXTSIZE);
      display_obj.key[b].drawButton(false, this->menuLabelWindow(current_menu->list->get(x).name, text_offset));
      display_obj.tft.drawXBitmap(0,
                                      KEY_Y + (b * (KEY_H + KEY_SPACING_Y)) - (ICON_H / 2),
                                      menu_icons[current_menu->list->get(x).icon],
                                      ICON_W,
                                      ICON_H,
                                      TFT_BLACK,
                                      TFT_LIGHTGREY);
    } else {
      display_obj.key[b].drawButton(true, this->menuLabelWindow(current_menu->list->get(x).name, text_offset));
      if ((current_menu->list->get(x).name != text09) && (current_menu->list->get(x).icon != 255))
            display_obj.tft.drawXBitmap(0,
                                        KEY_Y + (b * (KEY_H + KEY_SPACING_Y)) - (ICON_H / 2),
                                        menu_icons[current_menu->list->get(x).icon],
                                        ICON_W,
                                        ICON_H,
                                        TFT_BLACK,
                                        color);
    }
    display_obj.tft.setFreeFont(NULL);
  #endif
}

void MenuFunctions::displayMenuButtons() {
  #if defined(HAS_TOUCH) && !defined(MARAUDER_M5STACK_CORE2_AWS)
    // Draw lines to show each menu button
    for (int i = 0; i < 3; i++) {

      // Draw horizontal line on left
      display_obj.tft.drawLine(0,
                              TFT_HEIGHT / 3 * (i),
                              (TFT_WIDTH / 12) / 2,
                              TFT_HEIGHT / 3 * (i),
                              TFT_FARTGRAY);

      // Draw horizontal line on right
      display_obj.tft.drawLine(TFT_WIDTH - 1 - ((TFT_WIDTH / 12) / 2),
                              TFT_HEIGHT / 3 * (i),
                              TFT_WIDTH,
                              TFT_HEIGHT / 3 * (i),
                              TFT_FARTGRAY);

      // Draw vertical line on left
      display_obj.tft.drawLine(0,
                              (TFT_HEIGHT / 3 * (i)) - ((TFT_WIDTH / 12) / 2),
                              0,
                              (TFT_HEIGHT / 3 * (i)) + ((TFT_WIDTH / 12) / 2),
                              TFT_FARTGRAY);

      // Draw vertical line on right
      display_obj.tft.drawLine(TFT_WIDTH - 1,
                              (TFT_HEIGHT / 3 * (i)) - ((TFT_WIDTH / 12) / 2),
                              TFT_WIDTH - 1,
                              (TFT_HEIGHT / 3 * (i)) + ((TFT_WIDTH / 12) / 2),
                              TFT_FARTGRAY);
    }
  #endif
}

// Function to check menu input
void MenuFunctions::main(uint32_t currentTime)
{
  if (lazy_menu_release_pending && lazy_menu_release_pending != current_menu) {
    this->releaseLazyMenu(lazy_menu_release_pending);
    lazy_menu_release_pending = nullptr;
  }

  #ifdef HAS_MINI_SCREEN
    if (geofenceMenuRefreshPending) {
      geofenceMenuRefreshPending = false;
      this->buildGeofenceMenu();
    }
  #endif
  #ifdef HAS_SD
    if (sd_browser_release_pending && current_menu != &sdDeleteMenu)
      this->releaseSDDeleteBrowserResources();
  #endif
  if (saved_wifi_release_pending && current_menu != &savedWifiMenu) {
    this->releaseSavedWifiMenu();
    saved_wifi_release_pending = false;
  }

  #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD)
    this->updateKeyboard();
  #endif

  // Some function exited and we need to go back to normal
  if (display_obj.exit_draw) {
    if (wifi_scan_obj.currentScanMode != WIFI_CONNECTED)
      wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    display_obj.exit_draw = false;
    this->orientDisplay();
    if (post_scan_callback) {
      auto cb = post_scan_callback;
      post_scan_callback = nullptr;
      cb();
    }
  }
  if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
      (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
      (wifi_scan_obj.currentScanMode == OTA_UPDATE) ||
      (wifi_scan_obj.currentScanMode == ESP_UPDATE) ||
      (wifi_scan_obj.currentScanMode == SHOW_INFO) ||
      (wifi_scan_obj.currentScanMode == WIFI_SCAN_GPS_DATA) ||
      (wifi_scan_obj.currentScanMode == GPS_POI) ||
      (wifi_scan_obj.currentScanMode == GPS_TRACKER) ||
      (wifi_scan_obj.currentScanMode == WIFI_SCAN_GPS_NMEA)) {
    if (wifi_scan_obj.orient_display) {
      this->orientDisplay();
      wifi_scan_obj.orient_display = false;
    }
  }

  if (currentTime != 0) {
    if (currentTime - initTime >= BANNER_TIME) {
      this->initTime = millis();
      if ((wifi_scan_obj.currentScanMode != LV_JOIN_WIFI) &&
          (wifi_scan_obj.currentScanMode != LV_ADD_SSID) && (wifi_scan_obj.currentScanMode != WIFI_PACKET_MONITOR)) // GCOVR_EXCL_LINE -- Packet Monitor owns its full hardware display.
        this->updateStatusBar();

      // Do channel analyzer stuff
      if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER)){
        #ifdef HAS_SCREEN
          this->setGraphScale(this->graphScaleCheck(wifi_scan_obj._analyzer_values));

          this->drawGraph(wifi_scan_obj._analyzer_values);
        #endif
      }

      if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
        #ifdef HAS_SCREEN
          this->setGraphScale(this->graphScaleCheckSmall(wifi_scan_obj.channel_activity));

          this->drawGraphSmall(wifi_scan_obj.channel_activity);

        #endif
      }
    }
  }

  this->updateMenuMarquee(currentTime);

  if (wifi_scan_obj.background_scan) {
    static int last_bg_ap_count = -1;
    static int last_bg_sta_count = -1;
    static int last_bg_probe_count = -1;
    static uint32_t last_bg_refresh_ms = 0;
    extern LinkedList<AccessPoint>* access_points;
    extern LinkedList<Station>* stations;
    extern LinkedList<ProbeReqSsid>* probe_req_ssids;

    int cur_ap = access_points ? access_points->size() : 0;
    int cur_sta = stations ? stations->size() : 0;
    int cur_probe = probe_req_ssids ? probe_req_ssids->size() : 0;

    if (millis() - last_bg_refresh_ms >= 350) {
      if (current_menu == &attackTargetMenu) {
        if (cur_ap != last_bg_ap_count || cur_sta != last_bg_sta_count) {
          last_bg_ap_count = cur_ap;
          last_bg_sta_count = cur_sta;
          last_bg_refresh_ms = millis();
          if (attack_target_kind == AttackTargetKind::ACCESS_POINTS) {
            refreshAttackApPickerLive();
          } else if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE ||
                     attack_target_kind == AttackTargetKind::STATION_SCOPE) {
            refreshAttackStationApPickerLive();
          }
        }
      } else if (current_menu == &selectProbeSSIDsMenu) {
        if (cur_probe != last_bg_probe_count) {
          last_bg_probe_count = cur_probe;
          last_bg_refresh_ms = millis();
          refreshKarmaProbeLive();
        }
      } else if (current_menu == &wifiAPMenu) {
        if (cur_ap != last_bg_ap_count) {
          last_bg_ap_count = cur_ap;
          last_bg_refresh_ms = millis();
          refreshSelectAPsLive();
        }
      }
    }
  }

  boolean pressed = false;
  // This is code from bodmer's keypad example
  uint16_t t_x = 0, t_y = 0; // To store the touch coordinates

  // Get the display buffer out of the way
  if (!wifi_scan_obj.background_scan &&
      (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF ) &&
      (wifi_scan_obj.currentScanMode != WIFI_CONNECTED) &&
      (wifi_scan_obj.currentScanMode != WIFI_PACKET_MONITOR) &&
      (wifi_scan_obj.currentScanMode != WIFI_SCAN_CHAN_ANALYZER) &&
      (wifi_scan_obj.currentScanMode != WIFI_SCAN_CHAN_ACT) &&
      (wifi_scan_obj.currentScanMode != WIFI_SCAN_PACKET_RATE) &&
      (wifi_scan_obj.currentScanMode != BT_SCAN_ANALYZER) &&
      (wifi_scan_obj.currentScanMode != WIFI_SCAN_CSI_MOTION) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BEACON_SPAM) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AP_SPAM) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_CSA) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_QUIET) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AUTH) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_MANUAL) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_TARGETED) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BAD_MSG_TARGETED) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BAD_MSG) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_SLEEP) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_SLEEP_TARGETED) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_MIMIC) &&
	  (wifi_scan_obj.currentScanMode != WIFI_ATTACK_FUNNY_BEACON) &&
      (wifi_scan_obj.currentScanMode != WIFI_ATTACK_RICK_ROLL))
    display_obj.displayBuffer();


  int pre_getTouch = millis();

  #ifdef HAS_ILI9341
    if (!this->disable_touch)
      pressed = display_obj.updateTouch(&t_x, &t_y);
  #endif

  #ifdef MARAUDER_M5STACK_CORE2_AWS
    const bool front_control_touch = pressed && display_obj.isFrontButtonTouch();
    const bool core2_center_pressed = front_control_touch &&
        display_obj.menuButton(&t_x, &t_y, true, true) == SELECT_BUTTON;
    const bool core2_return_pressed = this->isKeyPressed(KEY_ENTER);
  #else
    const bool front_control_touch = false;
  #endif

  #ifdef MARAUDER_M5STACK_CORE2_AWS
    // Translate the controls that normally mean "tap/center to exit" into a
    // single Core2-wide stop action. This runs before feature-specific touch
    // handling so every non-blocking scan and recon view gets the same exit.
    if ((core2_center_pressed || core2_return_pressed) &&
        wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF &&
        wifi_scan_obj.currentScanMode != WIFI_CONNECTED &&
        wifi_scan_obj.currentScanMode != OTA_UPDATE &&
        wifi_scan_obj.currentScanMode != ESP_UPDATE) {
      recon_obj.stop();
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
      display_obj.init();
      changeMenu(current_menu, true);
      x = -1;
      y = -1;
      return;
    }
  #endif


  // Brightness gesture: hold top or bottom zone 1.5s to enter brightness mode
  #ifdef HAS_ILI9341
    bool touching_menu_control = false;
    if (pressed) {
      for (uint8_t b = BUTTON_ARRAY_LEN; b < BUTTON_ARRAY_LEN + 3; b++) {
        if (display_obj.key[b].contains(t_x, t_y)) {
          touching_menu_control = true;
          break;
        }
      }
    }
    if (pressed && !touching_menu_control &&
        (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF ||
                    wifi_scan_obj.currentScanMode == WIFI_CONNECTED)) {
      uint16_t zoneUp = TFT_HEIGHT * 25 / 100;
      uint16_t zoneDown = TFT_HEIGHT * 75 / 100;
      if (t_y < zoneUp || t_y >= zoneDown) {
        uint32_t hold_start = millis();
        uint16_t hx, hy;
        bool held = false;
        while (display_obj.updateTouch(&hx, &hy)) {
          if (millis() - hold_start >= 1500) {
            held = true;
            break;
          }
          delay(10);
        }
        if (held) {
          // Wait for release before entering brightness mode
          while (display_obj.updateTouch(&hx, &hy)) delay(10);
          this->brightnessMode();
          return;
        }
      }
    }
  #endif

  // POI button interception during wardrive — full width bottom bar
  #ifdef HAS_ILI9341
    if (pressed &&
        (wifi_scan_obj.currentScanMode == WIFI_SCAN_WAR_DRIVE ||
         wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION_WAR_DRIVE)) {
      if (t_y >= (SCREEN_HEIGHT - 50)) {
        wifi_scan_obj.tagPOI(nullptr);
        // Brief green flash
        display_obj.tft.fillRect(0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 50, TFT_GREEN);
        display_obj.tft.setTextSize(2);
        #ifdef HAS_GPS
        if (gps_obj.getFixStatus())
          display_obj.tft.setTextColor(TFT_BLACK, TFT_GREEN);
        else
        #endif
          display_obj.tft.setTextColor(TFT_BLACK, TFT_RED);
        String poiFlash = "POI (" + String(wifi_scan_obj.poiCount) + ")";
        int16_t flashWidth = poiFlash.length() * 12;
        display_obj.tft.setCursor((SCREEN_WIDTH - flashWidth) / 2, SCREEN_HEIGHT - 33);
        display_obj.tft.print(poiFlash);
        delay(200);
        x = -1;
        y = -1;
        return;
      }
    }
  #endif

  // This is if there are scans/attacks going on
  #ifdef HAS_ILI9341
    if ((wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) &&
        (pressed) &&
        (!front_control_touch) &&
        (wifi_scan_obj.currentScanMode != WIFI_CONNECTED) &&
        (wifi_scan_obj.currentScanMode != OTA_UPDATE) &&
        (wifi_scan_obj.currentScanMode != ESP_UPDATE) &&
        (wifi_scan_obj.currentScanMode != SHOW_INFO) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_DATA) &&
        (wifi_scan_obj.currentScanMode != GPS_POI) &&
        (wifi_scan_obj.currentScanMode != GPS_TRACKER) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_NMEA))
    {
      #if defined(HAS_CSI_MOTION)
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CSI_MOTION) {
          if (t_y < SCREEN_HEIGHT / 2) {
            csi_motion_obj.recalibrate();
          } else {
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            display_obj.init();
            changeMenu(current_menu, true);
          }
          x = -1;
          y = -1;
          return;
        }
      #endif
      // Stop the current scan
      if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_SAE_COMMIT) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_DETECT_FOLLOW) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION_WAR_DRIVE) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_WAR_DRIVE) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_DISPLAY_AP_INFO) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP_STA) ||
          (wifi_scan_obj.currentScanMode == WIFI_PING_SCAN) ||
          (wifi_scan_obj.currentScanMode == WIFI_ARP_SCAN) ||
          (wifi_scan_obj.currentScanMode == WIFI_PORT_SCAN_ALL) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_SSH) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_TELNET) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_DNS) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_SMTP) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_HTTP) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_HTTPS) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_RDP) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_PWN) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_PINESCAN) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_MULTISSID) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_ESPRESSIF) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_ALL) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_SPAM) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AP_SPAM) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_CSA) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_QUIET) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AUTH) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_MANUAL) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_TARGETED) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BAD_MSG_TARGETED) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BAD_MSG) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SLEEP) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SLEEP_TARGETED) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SAE_COMMIT) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_MIMIC) ||
		      (wifi_scan_obj.currentScanMode == WIFI_ATTACK_FUNNY_BEACON) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_RICK_ROLL) ||
          (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_LIST) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_ALL) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_IBEACON) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_FOX_HUNT) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_FINDMY_LIVE) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_RAYBAN) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_AIRTAG) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_AIRTAG_MON) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_FLIPPER) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_SIMPLE) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_SIMPLE_TWO) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_SOUR_APPLE) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_APPLE_JUICE) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_SWIFTPAIR_SPAM) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_SPAM_ALL) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_SAMSUNG_SPAM) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_GOOGLE_SPAM) ||
          (wifi_scan_obj.currentScanMode == BT_ATTACK_FLIPPER_SPAM) ||
          (wifi_scan_obj.currentScanMode == BT_SPOOF_AIRTAG) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE_CONT) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_SKIMMERS) ||
          (wifi_scan_obj.currentScanMode == REMOTE_ID_SCAN_ALL) ||
          (wifi_scan_obj.currentScanMode == REMOTE_ID_SCAN_TARGET) ||
          (wifi_scan_obj.currentScanMode == WIFI_SCAN_CSI_MOTION) ||
          (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER))
      {
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);

        // If we don't do this, the text and button coordinates will be off
        display_obj.init();

        // Take us back to the menu
        changeMenu(current_menu, true);
      }

      x = -1;
      y = -1;

      return;
    }
  #endif

  #ifdef HAS_BUTTONS

    #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
      bool c_btn_press = false;
    #elif (C_BTN >= 0) && !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV)
      bool c_btn_press = c_btn.justPressed();
    #elif defined(HAS_FACES_KEYBOARD)
      bool c_btn_press = ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
                          (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
                          (wifi_scan_obj.currentScanMode == OTA_UPDATE)) &&
                         (this->isKeyPressed('s') || this->isKeyPressed('S') || this->isKeyPressed(KEY_ENTER));
    #elif defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      bool c_btn_press = this->isKeyPressed('(') || this->isKeyPressed(KEY_ENTER);
    #endif

    #ifndef HAS_ILI9341

      if (!wifi_scan_obj.background_scan &&
          (c_btn_press) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF) &&
          (wifi_scan_obj.currentScanMode != WIFI_CONNECTED) &&
          (wifi_scan_obj.currentScanMode != OTA_UPDATE) &&
          (wifi_scan_obj.currentScanMode != ESP_UPDATE) &&
          (wifi_scan_obj.currentScanMode != SHOW_INFO) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_DATA) &&
          (wifi_scan_obj.currentScanMode != GPS_POI) &&
          (wifi_scan_obj.currentScanMode != GPS_TRACKER) &&
          (wifi_scan_obj.currentScanMode != WIFI_SCAN_GPS_NMEA))
      {
        // Stop the current scan
        if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_SAE_COMMIT) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DETECT_FOLLOW) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_STATION) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DISPLAY_AP_INFO) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_FINDMY_LIVE) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP_STA) ||
            (wifi_scan_obj.currentScanMode == WIFI_PING_SCAN) ||
            (wifi_scan_obj.currentScanMode == WIFI_ARP_SCAN) ||
            (wifi_scan_obj.currentScanMode == WIFI_PORT_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_SSH) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_TELNET) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DNS) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_SMTP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_HTTP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_HTTPS) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_RDP) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PWN) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PINESCAN) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_MULTISSID) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ESPRESSIF) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AP_SPAM) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_CSA) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_QUIET) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_AUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_MANUAL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_DEAUTH_TARGETED) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BAD_MSG_TARGETED) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BAD_MSG) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SLEEP) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SLEEP_TARGETED) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_SAE_COMMIT) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_MIMIC) ||
			      (wifi_scan_obj.currentScanMode == WIFI_ATTACK_FUNNY_BEACON) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_RICK_ROLL) ||
            (wifi_scan_obj.currentScanMode == WIFI_ATTACK_BEACON_LIST) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_IBEACON) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_FOX_HUNT) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_RAYBAN) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_AIRTAG) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_AIRTAG_MON) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_FLIPPER) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_FLOCK) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_SIMPLE) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_SIMPLE_TWO) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SOUR_APPLE) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_APPLE_JUICE) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SWIFTPAIR_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SPAM_ALL) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_SAMSUNG_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_GOOGLE_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_ATTACK_FLIPPER_SPAM) ||
            (wifi_scan_obj.currentScanMode == BT_SPOOF_AIRTAG) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_WAR_DRIVE_CONT) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_SKIMMERS) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ACTIVE_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_ACTIVE_LIST_EAPOL) ||
            (wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
            (wifi_scan_obj.currentScanMode == REMOTE_ID_SCAN_ALL) ||
            (wifi_scan_obj.currentScanMode == REMOTE_ID_SCAN_TARGET) ||
            (wifi_scan_obj.currentScanMode == WIFI_SCAN_CSI_MOTION) ||
            (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER))
        {
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF);

          // Restore display state without full reinit to avoid screen flash
          #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
            display_obj.tft.setRotation(SCREEN_ORIENTATION);
            display_obj.clearScreen();
          #else
            display_obj.init();
          #endif

          // Take us back to the menu
          if (post_scan_callback) {
            auto cb = post_scan_callback;
            post_scan_callback = nullptr;
            cb();
          } else {
            changeMenu(current_menu, true);
          }
        }

        x = -1;
        y = -1;

        return;
      }
    #endif

  #endif


  // Check if any key coordinate boxes contain the touch coordinates
  // This is for when on a menu
  // Make sure to add certain scanning functions here or else
  // menu items will be selected while scans and attacks are running
  #ifdef HAS_ILI9341
    if ((wifi_scan_obj.currentScanMode != WIFI_ATTACK_BEACON_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AP_SPAM) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_CSA) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_QUIET) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_AUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_MANUAL) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_DEAUTH_TARGETED) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BAD_MSG_TARGETED) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_BAD_MSG) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_SLEEP) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_SLEEP_TARGETED) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_SAE_COMMIT) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_MIMIC) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_PACKET_RATE) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_RAW_CAPTURE) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_CHAN_ANALYZER) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_CHAN_ACT) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_SIG_STREN) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_AP) &&
        (wifi_scan_obj.currentScanMode != BT_SCAN_FLOCK) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_PROBE) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_DEAUTH) &&
		    (wifi_scan_obj.currentScanMode != WIFI_ATTACK_FUNNY_BEACON) &&
        (wifi_scan_obj.currentScanMode != WIFI_SCAN_EAPOL) &&
        (wifi_scan_obj.currentScanMode != WIFI_ATTACK_RICK_ROLL))
    {
      // Need this to set all keys to false
      /*for (uint8_t b = 0; b < BUTTON_ARRAY_LEN; b++) {
        if (pressed && display_obj.key[b].contains(t_x, t_y)) {
          display_obj.key[b].press(true);  // tell the button it is pressed
        } else {
          display_obj.key[b].press(false);  // tell the button it is NOT pressed
        }
      }*/

      // Detect up, down, select
      int8_t menu_button;
      const bool menu_navigation_active =
          wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF ||
          wifi_scan_obj.currentScanMode == WIFI_CONNECTED ||
          wifi_scan_obj.currentScanMode == OTA_UPDATE ||
          wifi_scan_obj.background_scan;
      if (menu_navigation_active) {
        const int8_t held_button =
            display_obj.menuButton(&t_x, &t_y, pressed, true);
        const int8_t released_button = menuTouchReleasedButton(
            menu_touch_button, held_button, pressed);
        menu_touch_button = held_button;
        #ifdef MARAUDER_M5STACK_CORE2_AWS
          // The FT6336 occasionally reports a brief empty sample while a
          // front control is held. Do not turn that gap into another tap.
          constexpr uint32_t core2_release_debounce_ms = 200;
          const bool up_event = menu_touch_up_repeat.updateDebounced(
              held_button == UP_BUTTON, currentTime,
              core2_release_debounce_ms);
          const bool down_event = menu_touch_down_repeat.updateDebounced(
              held_button == DOWN_BUTTON, currentTime,
              core2_release_debounce_ms);
        #else
          const bool up_event = menu_touch_up_repeat.update(
              held_button == UP_BUTTON, currentTime);
          const bool down_event = menu_touch_down_repeat.update(
              held_button == DOWN_BUTTON, currentTime);
        #endif
        menu_button = up_event ? UP_BUTTON :
                      down_event ? DOWN_BUTTON :
                      released_button == SELECT_BUTTON ? SELECT_BUTTON : -1;
      } else {
        menu_touch_up_repeat.reset();
        menu_touch_down_repeat.reset();
        menu_touch_button = -1;
        menu_button = display_obj.menuButton(&t_x, &t_y, pressed);
      }

      if (menu_button >= 0) {
        if (menu_button == UP_BUTTON) {
          if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
              (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
              (wifi_scan_obj.currentScanMode == OTA_UPDATE)) {
            if (current_menu->selected > 0) {
              current_menu->selected--;
              // Page up
              if (current_menu->selected < this->menu_start_index) {
                this->buildButtons(current_menu, current_menu->selected);
                this->displayCurrentMenu(current_menu->selected);
              }
              this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
              if (!current_menu->list->get(current_menu->selected + 1).selected || (current_menu->list->get(current_menu->selected + 1).icon == SETTINGS && current_menu->list->get(current_menu->selected + 1).color == TFTLIGHTGREY))
                this->buttonNotSelected(current_menu->selected + 1 - this->menu_start_index, current_menu->selected + 1);
            }
            // Loop to end
            else {
              current_menu->selected = current_menu->list->size() - 1;
              if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
                this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
              }
              this->buttonSelected(current_menu->selected, current_menu->selected);
              if (!current_menu->list->get(0).selected || (current_menu->list->get(0).icon == SETTINGS && current_menu->list->get(0).color == TFTLIGHTGREY))
                this->buttonNotSelected(0, this->menu_start_index);
            }
          }
          else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
                  (wifi_scan_obj.currentScanMode == BT_SCAN_FLOCK) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
            #ifndef HAS_DUAL_BAND
              if (wifi_scan_obj.set_channel < 14)
                wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel + 1);
              else
                wifi_scan_obj.changeChannel(1);
            #else
              if (wifi_scan_obj.dual_band_channel_index < DUAL_BAND_CHANNELS - 1)
                wifi_scan_obj.dual_band_channel_index++;
              else
                wifi_scan_obj.dual_band_channel_index = 0;

              wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
            #endif
          }
          else if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
            #ifndef HAS_DUAL_BAND
              if (wifi_scan_obj.activity_page < MAX_CHANNEL / CHAN_PER_PAGE) {
                wifi_scan_obj.activity_page++;
              }
            #else
              if (wifi_scan_obj.activity_page < DUAL_BAND_CHANNELS / CHAN_PER_PAGE) {
                wifi_scan_obj.activity_page++;
              }
            #endif
            wifi_scan_obj.drawChannelLine();
          }
        }
        if (menu_button == DOWN_BUTTON) {
          if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
              (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
              (wifi_scan_obj.currentScanMode == OTA_UPDATE)) {
            if (current_menu->selected < current_menu->list->size() - 1) {
              current_menu->selected++;
              // Page down
              if (current_menu->selected - this->menu_start_index >= BUTTON_SCREEN_LIMIT) {
                this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
              }
              else
                this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
              if (!current_menu->list->get(current_menu->selected - 1).selected || (current_menu->list->get(current_menu->selected - 1).icon == SETTINGS && current_menu->list->get(current_menu->selected - 1).color == TFTLIGHTGREY))
                this->buttonNotSelected(current_menu->selected - 1 - this->menu_start_index, current_menu->selected - 1);
            }
            // Loop to beginning
            else {
              if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
                current_menu->selected = 0;
                this->buildButtons(current_menu);
                this->displayCurrentMenu();
                this->buttonSelected(current_menu->selected);
              }
              else {
                current_menu->selected = 0;
                this->buttonSelected(current_menu->selected);
                if (!current_menu->list->get(current_menu->list->size() - 1).selected || (current_menu->list->get(current_menu->list->size() - 1).icon == SETTINGS && current_menu->list->get(current_menu->list->size() - 1).color == TFTLIGHTGREY))
                  this->buttonNotSelected(current_menu->list->size() - 1);
              }
            }
          }
          else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
                  (wifi_scan_obj.currentScanMode == BT_SCAN_FLOCK) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
                  (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
            #ifndef HAS_DUAL_BAND
              if (wifi_scan_obj.set_channel > 1)
                wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel - 1);
              else
                wifi_scan_obj.changeChannel(14);
            #else
              if (wifi_scan_obj.dual_band_channel_index > 0)
                wifi_scan_obj.dual_band_channel_index--;
              else
                wifi_scan_obj.dual_band_channel_index = DUAL_BAND_CHANNELS - 1;

              wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
            #endif
          }
          else if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
            #ifndef HAS_DUAL_BAND
              if (wifi_scan_obj.activity_page > 1) {
                wifi_scan_obj.activity_page--;
              }
            #else
              if (wifi_scan_obj.activity_page > 0) {
                wifi_scan_obj.activity_page--;
              }
            #endif
            wifi_scan_obj.drawChannelLine();
          }
        }
        if(menu_button == SELECT_BUTTON) {
          current_menu->list->get(current_menu->selected).callable();
        }
        else {
          if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
              (wifi_scan_obj.currentScanMode == WIFI_CONNECTED))
            this->displayMenuButtons();
        }
      }
    }
    x = -1;
    y = -1;
  #endif

  // Menu navigation and paging
  #ifdef HAS_BUTTONS
    #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
      const bool menu_nav_active = (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
                                   (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
                                   (wifi_scan_obj.currentScanMode == OTA_UPDATE) ||
                                   wifi_scan_obj.background_scan;
      if (menu_nav_active && current_menu && current_menu->list && current_menu->list->size() > 0) {
        // TOP BUTTON (GPIO 14 / u_btn): Click = UP, Hold (>= 400ms) = BACK
        static uint32_t u_btn_down_ms = 0;
        static bool u_btn_held_done = false;

        if (u_btn.isPressedNow()) {
          if (u_btn_down_ms == 0) {
            u_btn_down_ms = millis();
            u_btn_held_done = false;
          } else if (!u_btn_held_done && (millis() - u_btn_down_ms >= 400)) {
            // Long press Top -> Back
            u_btn_held_done = true;
            this->navBack();
          }
        } else {
          if (u_btn_down_ms != 0) {
            if (!u_btn_held_done && (millis() - u_btn_down_ms < 400)) {
              // Short click Top -> Move UP
              this->navUp();
            }
            u_btn_down_ms = 0;
            u_btn_held_done = false;
          }
        }

        // BOTTOM BUTTON (GPIO 0 / d_btn): Click = DOWN, Hold (>= 400ms) = OK / SELECT
        static uint32_t d_btn_down_ms = 0;
        static bool d_btn_held_done = false;

        if (d_btn.isPressedNow()) {
          if (d_btn_down_ms == 0) {
            d_btn_down_ms = millis();
            d_btn_held_done = false;
          } else if (!d_btn_held_done && (millis() - d_btn_down_ms >= 400)) {
            // Long press Bottom -> OK / RUN
            d_btn_held_done = true;
            this->navOk();
          }
        } else {
          if (d_btn_down_ms != 0) {
            if (!d_btn_held_done && (millis() - d_btn_down_ms < 400)) {
              // Short click Bottom -> Move DOWN
              this->navDown();
            }
            d_btn_down_ms = 0;
            d_btn_held_done = false;
          }
        }
      }
      else if (!menu_nav_active) {
        // TOP BUTTON (GPIO 14 / u_btn)
        static uint32_t scan_u_down_ms = 0;
        static bool scan_u_held_done = false;

        if (u_btn.isPressedNow()) {
          if (scan_u_down_ms == 0) {
            scan_u_down_ms = millis();
            scan_u_held_done = false;
          } else if (!scan_u_held_done && (millis() - scan_u_down_ms >= 350)) {
            // Long press Top Button (Hold UP) -> EXIT SCAN TO MENU!
            scan_u_held_done = true;
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            recon_obj.stop();
            display_obj.init();
            if (this->post_scan_callback) {
              auto cb = this->post_scan_callback;
              this->post_scan_callback = nullptr;
              cb();
            } else {
              this->changeMenu(this->current_menu, true);
            }
            return;
          }
        } else {
          if (scan_u_down_ms != 0) {
            if (!scan_u_held_done && (millis() - scan_u_down_ms < 350)) {
              // Short click Top Button -> Channel UP (+1)
              if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
                if (wifi_scan_obj.activity_page < MAX_CHANNEL / CHAN_PER_PAGE) wifi_scan_obj.activity_page++;
                else wifi_scan_obj.activity_page = 1;
                wifi_scan_obj.drawChannelLine();
              }
              else {
                int ch = wifi_scan_obj.set_channel + 1;
                if (ch > 14) ch = 1;
                wifi_scan_obj.changeChannel(ch);
              }
            }
            scan_u_down_ms = 0;
            scan_u_held_done = false;
          }
        }

        // BOTTOM BUTTON (GPIO 0 / d_btn)
        static uint32_t scan_d_down_ms = 0;
        static bool scan_d_held_done = false;

        if (d_btn.isPressedNow()) {
          if (scan_d_down_ms == 0) {
            scan_d_down_ms = millis();
            scan_d_held_done = false;
          } else if (!scan_d_held_done && (millis() - scan_d_down_ms >= 350)) {
            // Long press Bottom Button (Hold DOWN) -> EXIT SCAN TO MENU!
            scan_d_held_done = true;
            wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
            recon_obj.stop();
            display_obj.init();
            if (this->post_scan_callback) {
              auto cb = this->post_scan_callback;
              this->post_scan_callback = nullptr;
              cb();
            } else {
              this->changeMenu(this->current_menu, true);
            }
            return;
          }
        } else {
          if (scan_d_down_ms != 0) {
            if (!scan_d_held_done && (millis() - scan_d_down_ms < 350)) {
              // Short click Bottom Button -> Channel DOWN (-1)
              if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
                if (wifi_scan_obj.activity_page > 1) wifi_scan_obj.activity_page--;
                else wifi_scan_obj.activity_page = MAX_CHANNEL / CHAN_PER_PAGE;
                wifi_scan_obj.drawChannelLine();
              }
              else {
                int ch = wifi_scan_obj.set_channel - 1;
                if (ch < 1) ch = 14;
                wifi_scan_obj.changeChannel(ch);
              }
            }
            scan_d_down_ms = 0;
            scan_d_held_done = false;
          }
        }
      }
    #else
    // Don't do this for touch screens
    #if !(defined(MARAUDER_V6) || defined(MARAUDER_V6_1) || defined(MARAUDER_CYD_MICRO) || defined(MARAUDER_CYD_GUITION) || defined(MARAUDER_CYD_2USB) || defined(MARAUDER_CYD_3_5_INCH))
      #if !defined(MARAUDER_M5STICKC) || defined(MARAUDER_M5STICKCP2)
        #if (U_BTN >= 0 || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD))
          #if (U_BTN >= 0)
            const bool up_just_pressed = u_btn.justPressed();
            const bool up_is_pressed = u_btn.isPressedNow();
          #elif defined(HAS_FACES_KEYBOARD)
            const bool up_just_pressed = this->isKeyPressed('k') || this->isKeyPressed('K');
            const bool up_is_pressed = up_just_pressed;
          #elif defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
            const bool up_just_pressed = this->isKeyPressed(';') || this->isKeyPressed('w') || this->isKeyPressed('W');
            const bool up_is_pressed = up_just_pressed;
          #endif
            const bool menu_navigation_active =
                wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF ||
                wifi_scan_obj.currentScanMode == WIFI_CONNECTED ||
                wifi_scan_obj.currentScanMode == OTA_UPDATE;
            const bool up_event = menu_navigation_active
                ? menu_up_repeat.update(up_is_pressed, currentTime)
                : up_just_pressed;
            if (!menu_navigation_active) menu_up_repeat.reset();
            if (up_event) {
              if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
                  (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
                  (wifi_scan_obj.currentScanMode == OTA_UPDATE)) {
                if (current_menu->selected > 0) {
                  current_menu->selected--;
                  // Page up
                  if (current_menu->selected < this->menu_start_index) {
                    this->buildButtons(current_menu, current_menu->selected);
                    this->displayCurrentMenu(current_menu->selected);
                  }
                  this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
                  if (!current_menu->list->get(current_menu->selected + 1).selected || (current_menu->list->get(current_menu->selected + 1).icon == SETTINGS && current_menu->list->get(current_menu->selected + 1).color == TFTLIGHTGREY))
                    this->buttonNotSelected(current_menu->selected + 1 - this->menu_start_index, current_menu->selected + 1);
                }
                // Loop to end
                else {
                  current_menu->selected = current_menu->list->size() - 1;
                  if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
                    this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                    this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
                  }
                  this->buttonSelected(current_menu->selected, current_menu->selected);
                  if (!current_menu->list->get(0).selected || (current_menu->list->get(0).icon == SETTINGS && current_menu->list->get(0).color == TFTLIGHTGREY))
                    this->buttonNotSelected(0, this->menu_start_index);
                }
              }
              else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
                      (wifi_scan_obj.currentScanMode == BT_SCAN_FLOCK) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
                      (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
                #ifndef HAS_DUAL_BAND
                  if (wifi_scan_obj.set_channel < 14)
                    wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel + 1);
                  else
                    wifi_scan_obj.changeChannel(1);
                #else
                  if (wifi_scan_obj.dual_band_channel_index < DUAL_BAND_CHANNELS - 1)
                    wifi_scan_obj.dual_band_channel_index++;
                  else
                    wifi_scan_obj.dual_band_channel_index = 0;

                  wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
                #endif
              }
              #if defined(HAS_CSI_BUTTON_CONTROLS) && defined(HAS_CSI_MOTION)
                else if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CSI_MOTION) {
                  csi_motion_obj.recalibrate();
                }
              #endif
              else if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
                #ifndef HAS_DUAL_BAND
                  if (wifi_scan_obj.activity_page < MAX_CHANNEL / CHAN_PER_PAGE) {
                    wifi_scan_obj.activity_page++;
                  }
                #else
                  if (wifi_scan_obj.activity_page < DUAL_BAND_CHANNELS / CHAN_PER_PAGE) {
                    wifi_scan_obj.activity_page++;
                  }
                #endif
                wifi_scan_obj.drawChannelLine();
              }
            }
        #endif
      #endif

      #if (D_BTN >= 0 || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD))
      #if (D_BTN >= 0)
      const bool down_just_pressed = d_btn.justPressed();
      const bool down_is_pressed = d_btn.isPressedNow();
      #elif defined(HAS_FACES_KEYBOARD)
      const bool down_just_pressed = this->isKeyPressed('m') || this->isKeyPressed('M');
      const bool down_is_pressed = down_just_pressed;
      #elif defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      const bool down_just_pressed = this->isKeyPressed('.') || this->isKeyPressed('s') || this->isKeyPressed('S');
      const bool down_is_pressed = down_just_pressed;
      #endif
      const bool down_menu_navigation_active =
          wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF ||
          wifi_scan_obj.currentScanMode == WIFI_CONNECTED ||
          wifi_scan_obj.currentScanMode == OTA_UPDATE;
      const bool down_event = down_menu_navigation_active
          ? menu_down_repeat.update(down_is_pressed, currentTime)
          : down_just_pressed;
      if (!down_menu_navigation_active) menu_down_repeat.reset();
      if (down_event) {
        if ((wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
            (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
            (wifi_scan_obj.currentScanMode == OTA_UPDATE)) {
          if (current_menu->selected < current_menu->list->size() - 1) {
            current_menu->selected++;
            // Page down
            if (current_menu->selected - this->menu_start_index >= BUTTON_SCREEN_LIMIT) {
              this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
              this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
            }
            else
              this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
            if (!current_menu->list->get(current_menu->selected - 1).selected || (current_menu->list->get(current_menu->selected - 1).icon == SETTINGS && current_menu->list->get(current_menu->selected - 1).color == TFTLIGHTGREY))
              this->buttonNotSelected(current_menu->selected - 1 - this->menu_start_index, current_menu->selected - 1);
          }
          // Loop to beginning
          else {
            if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
              current_menu->selected = 0;
              this->buildButtons(current_menu);
              this->displayCurrentMenu();
              this->buttonSelected(current_menu->selected);
            }
            else {
              current_menu->selected = 0;
              this->buttonSelected(current_menu->selected);
              if (!current_menu->list->get(current_menu->list->size() - 1).selected || (current_menu->list->get(current_menu->list->size() - 1).icon == SETTINGS && current_menu->list->get(current_menu->list->size() - 1).color == TFTLIGHTGREY))
                this->buttonNotSelected(current_menu->list->size() - 1);
            }
          }
        }
        else if ((wifi_scan_obj.currentScanMode == WIFI_PACKET_MONITOR) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_EAPOL) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_PACKET_RATE) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_RAW_CAPTURE) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_AP) ||
                (wifi_scan_obj.currentScanMode == BT_SCAN_FLOCK) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_PROBE) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_DEAUTH) ||
                (wifi_scan_obj.currentScanMode == WIFI_SCAN_SIG_STREN)) {
          #ifndef HAS_DUAL_BAND
            if (wifi_scan_obj.set_channel > 1)
              wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel - 1);
            else
              wifi_scan_obj.changeChannel(14);
          #else
            if (wifi_scan_obj.dual_band_channel_index > 0)
              wifi_scan_obj.dual_band_channel_index--;
            else
              wifi_scan_obj.dual_band_channel_index = DUAL_BAND_CHANNELS - 1;

            wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
          #endif
        }
        else if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ACT) {
          #ifndef HAS_DUAL_BAND
            if (wifi_scan_obj.activity_page > 1) {
              wifi_scan_obj.activity_page--;
            }
          #else
            if (wifi_scan_obj.activity_page > 0) {
              wifi_scan_obj.activity_page--;
            }
          #endif
          wifi_scan_obj.drawChannelLine();
        }
      }
      #endif

      #if (R_BTN >= 0 || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD))
      #if (R_BTN >= 0)
      if (r_btn.justPressed()) {
      #elif defined(HAS_FACES_KEYBOARD)
      if (this->isKeyPressed('s') || this->isKeyPressed('S')) {
      #elif defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      if (this->isKeyPressed('/') || this->isKeyPressed('d') || this->isKeyPressed('D')) {
      #endif
        #ifdef HAS_FACES_KEYBOARD
        if (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF &&
            wifi_scan_obj.currentScanMode != WIFI_CONNECTED) {
        #else
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) {
        #endif
          #ifndef HAS_DUAL_BAND
            if (wifi_scan_obj.set_channel < 14)
              wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel + 1);
            else
              wifi_scan_obj.changeChannel(1);
          #else
            if (wifi_scan_obj.dual_band_channel_index < DUAL_BAND_CHANNELS - 1)
              wifi_scan_obj.dual_band_channel_index++;
            else
              wifi_scan_obj.dual_band_channel_index = 0;

            wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
          #endif
        }
      }
      #endif

      #if (L_BTN >= 0 || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD))
      #if (L_BTN >= 0)
      if (l_btn.justPressed()) {
      #elif defined(HAS_FACES_KEYBOARD)
      if (this->isKeyPressed('n') || this->isKeyPressed('N')) {
      #elif defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      if (this->isKeyPressed(',') || this->isKeyPressed('a') || this->isKeyPressed('A')) {
      #endif
        #ifdef HAS_FACES_KEYBOARD
        if (wifi_scan_obj.currentScanMode != WIFI_SCAN_OFF &&
            wifi_scan_obj.currentScanMode != WIFI_CONNECTED) {
        #else
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) {
        #endif
          #ifndef HAS_DUAL_BAND
            if (wifi_scan_obj.set_channel > 1)
              wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel - 1);
            else
              wifi_scan_obj.changeChannel(14);
          #else
            if (wifi_scan_obj.dual_band_channel_index > 0)
              wifi_scan_obj.dual_band_channel_index--;
            else
              wifi_scan_obj.dual_band_channel_index = DUAL_BAND_CHANNELS - 1;

            wifi_scan_obj.changeChannel(wifi_scan_obj.dual_band_channels[wifi_scan_obj.dual_band_channel_index]);
          #endif
        }
      }
      #endif

      #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD)
      if (this->isKeyPressed('`') || this->isKeyPressed(KEY_BACKSPACE)
          #ifdef HAS_FACES_KEYBOARD
            || this->isKeyPressed('n') || this->isKeyPressed('N')
          #endif
      ) {
        if (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) {
          if (current_menu->parentMenu != NULL) {
            this->changeMenu(current_menu->parentMenu, true);
          }
        }
      }
      #endif

      #ifdef MARAUDER_POOM
      if (b_btn.justPressed() && wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF &&
          current_menu->parentMenu != NULL) {
        this->changeMenu(current_menu->parentMenu, true);
      }
      #endif

      if(c_btn_press){
        current_menu->list->get(current_menu->selected).callable();
      }

    #endif // !MARAUDER_LILYGO_T_DISPLAY_S3
    #endif // !touch screens
  #endif // HAS_BUTTONS
}

#if BATTERY_ANALOG_ON == 1
byte battery_analog_array[10];
byte battery_count = 0;
byte battery_analog_last = 101;
#define BATTERY_CHECK 50
uint16_t battery_analog = 0;
void MenuFunctions::battery(bool initial)
{
  if (BATTERY_ANALOG_ON) {
    uint8_t n = 0;
    byte battery_analog_sample[10];
    byte deviation;
    if (battery_count == BATTERY_CHECK - 5)  digitalWrite(BATTERY_PIN, HIGH);
    else if (battery_count == 5) digitalWrite(BATTERY_PIN, LOW);
    if (battery_count == 0) {
      battery_analog = 0;
      for (n = 9; n > 0; n--)battery_analog_array[n] = battery_analog_array[n - 1];
      for (n = 0; n < 10; n++) {
        battery_analog_sample[n] = map((analogRead(ANALOG_PIN) * 5), 2400, 4200, 0, 100);
        if (battery_analog_sample[n] > 100) battery_analog_sample[n] = 100;
        else if (battery_analog_sample[n] < 0) battery_analog_sample[n] = 0;
        battery_analog += battery_analog_sample[n];
      }
      battery_analog = battery_analog / 10;
      for (n = 0; n < 10; n++) {
        deviation = abs(battery_analog - battery_analog_sample[n]);
        if (deviation >= 10) battery_analog_sample[n] = battery_analog;
      }
      battery_analog = 0;
      for (n = 0; n < 10; n++) battery_analog += battery_analog_sample[n];
      battery_analog = battery_analog / 10;
      battery_analog_array[0] = battery_analog;
      if (battery_analog_array[9] > 0 ) {
        battery_analog = 0;
        for (n = 0; n < 10; n++) battery_analog += battery_analog_array[n];
        battery_analog = battery_analog / 10;
      }
      battery_count ++;
    }
    else if (battery_count < BATTERY_CHECK) battery_count++;
    else if (battery_count >= BATTERY_CHECK) battery_count = 0;

    if (battery_analog_last != battery_analog) {
      battery_analog_last = battery_analog;
      MenuFunctions::battery2();
    }
  }
}
void MenuFunctions::battery2(bool initial)
{
  uint16_t the_color;
  if ( digitalRead(CHARGING_PIN) == 1) the_color = TFT_BLUE;
  else if (battery_analog < 20) the_color = TFT_RED;
  else if (battery_analog < 40)  the_color = TFT_YELLOW;
  else the_color = TFT_GREEN;

  display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
  display_obj.tft.fillRect(SB_TOUCH_X, 0, 50, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  display_obj.tft.drawXBitmap(SB_TOUCH_X,
                              0,
                              menu_icons[STATUS_BAT],
                              16,
                              16,
                              STATUSBAR_COLOR,
                              the_color);
  display_obj.tft.drawString((String) battery_analog + "%", SB_BAT_X, 0, 2);
}
#else
void MenuFunctions::battery(bool initial)
{
  #ifdef HAS_BATTERY
    uint16_t the_color;
    if (battery_obj.i2c_supported)
    {
      // Could use int compare maybe idk
      if (((String)battery_obj.battery_level != "25") && ((String)battery_obj.battery_level != "0"))
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;

      if ((battery_obj.battery_level != battery_obj.old_level) || (initial)) {
        battery_obj.old_level = battery_obj.battery_level;
        display_obj.tft.fillRect(204, 0, SCREEN_WIDTH, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
      }

      display_obj.tft.setCursor(0, 1);
      /*if (!this->disable_touch) {
        display_obj.tft.drawXBitmap(SB_TOUCH_X,
                                    0,
                                    menu_icons[STATUS_BAT],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    the_color);
      }*/
      #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
        display_obj.tft.drawString((String)battery_obj.battery_level + "%", SB_BAT_X, 0, 1);
      #else
        display_obj.tft.drawString((String)battery_obj.battery_level + "%", SB_BAT_X, 0, 2);
      #endif
    }
  #endif
}
void MenuFunctions::battery2(bool initial)
{
  MenuFunctions::battery(initial);
}
#endif

void MenuFunctions::updateStatusBar()
{
  display_obj.tft.setTextSize(1);

  bool status_changed = false;

  #if defined(MARAUDER_MINI) || defined(MARAUDER_M5STICKC) || defined(MARAUDER_REV_FEATHER) || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(MARAUDER_MINI_V3)
    display_obj.tft.setFreeFont(NULL);
  #endif

  uint16_t the_color;

  #ifdef HAS_GPS
    if (this->old_gps_sat_count != gps_obj.getNumSats()) {
      this->old_gps_sat_count = gps_obj.getNumSats();
      display_obj.tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
      status_changed = true;
    }
  #endif

  // GPS Stuff
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      if (gps_obj.getFixStatus())
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;

      #ifdef HAS_FULL_SCREEN
        display_obj.tft.drawXBitmap(4,
                                    0,
                                    menu_icons[STATUS_GPS],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    the_color);
        display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR, true);

        display_obj.tft.drawString(gps_obj.getNumSatsString(), 22, 0, 2);
      #elif defined(HAS_SCREEN)
        display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR, true);
        display_obj.tft.drawString("GPS", 0, 0, 1);
      #endif
    }
  #endif

  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR, true);

  // WiFi Channel Stuff
  uint8_t primaryChannel;
  wifi_second_chan_t secondChannel;
  esp_err_t err = esp_wifi_get_channel(&primaryChannel, &secondChannel);

  uint8_t current_channel = wifi_scan_obj.set_channel;

  if (err == ESP_OK)
    current_channel = primaryChannel;

  if ((current_channel != wifi_scan_obj.old_channel) || (status_changed)) {
    wifi_scan_obj.old_channel = current_channel;
    #if defined(MARAUDER_POOM)
      display_obj.tft.fillRect(0, 0, 44, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #elif defined(MARAUDER_MINI) || defined(MARAUDER_M5STICKC) || defined(MARAUDER_REV_FEATHER) || defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(MARAUDER_MINI_V3)
      display_obj.tft.fillRect(TFT_WIDTH/4, 0, CHAR_WIDTH * 6, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #elif defined(HAS_DUAL_BAND)
      display_obj.tft.fillRect(50, 0, (CHAR_WIDTH / 2) * 8, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #else
      display_obj.tft.fillRect(50, 0, (CHAR_WIDTH / 2) * 7, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #endif
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.old_channel, 50, 0, 2);
    #endif

    #if defined(MARAUDER_POOM)
      display_obj.tft.drawString("C" + (String)wifi_scan_obj.old_channel, 0, 0, 1);
    #elif defined(HAS_MINI_SCREEN)
      display_obj.tft.drawString("CH:" + (String)wifi_scan_obj.old_channel, TFT_WIDTH/4, 0, 1);
    #endif
  }

  // RAM Stuff
  wifi_scan_obj.free_ram = String(esp_get_free_heap_size());
  if ((wifi_scan_obj.free_ram != wifi_scan_obj.old_free_ram) || (status_changed)) {
    wifi_scan_obj.old_free_ram = wifi_scan_obj.free_ram;
    //display_obj.tft.fillRect(SB_MEM_X, 0, 60, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    #ifdef HAS_FULL_SCREEN
    #ifndef HAS_PSRAM
      display_obj.tft.drawString("D:" + String(getDRAMUsagePercent()) + "%", SB_MEM_X, 0, 2);
    #else
      display_obj.tft.drawString("D:" + String(getDRAMUsagePercent()) + "%", SB_MEM_X, 0, 1);
      display_obj.tft.drawString("P:" + String(getPSRAMUsagePercent()) + "%", SB_MEM_X, 8, 1);
    #endif
  #endif

  #if defined(MARAUDER_POOM)
    display_obj.tft.fillRect(46, 0, 34, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    display_obj.tft.drawString(String(getDRAMUsagePercent()) + "%", 46, 0, 1);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.drawString(String(getDRAMUsagePercent()) + "%", TFT_WIDTH/1.75, 0, 1);
  #endif
  }

  // Draw battery info
  MenuFunctions::battery(false);
  display_obj.tft.fillRect(186, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);

  // Disable touch stuff
  #ifdef HAS_ILI9341
    #ifdef HAS_BUTTONS
      if (this->disable_touch) {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(SB_TOUCH_X,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_RED);
      }
      else {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(SB_TOUCH_X,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_DARKGREY);
      }
    #endif
  #endif

  // Draw SD info
  #ifdef HAS_SD
    if (sd_obj.supported)
      the_color = TFT_GREEN;
    else
      the_color = TFT_RED;

    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_SD_X,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif
  #endif

  #if defined(MARAUDER_POOM)
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR, true);
    display_obj.tft.drawString("SD", 116, 0, 1);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR, true);
    display_obj.tft.drawString("SD", TFT_WIDTH - 12, 0, 1);
  #endif

  // WiFi connection status stuff
  if (wifi_scan_obj.wifi_connected) {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_WIFI_X,
                                  0,
                                  menu_icons[JOINED],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_GREEN);
    #endif
  } else {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_WIFI_X,
                                  0,
                                  menu_icons[JOINED],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_DARKGREY);
    #endif
  }

  // Force PMKID stuff
  if ((wifi_scan_obj.force_pmkid) || (wifi_scan_obj.ep_deauth)) {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_FORCE_X,
                                  0,
                                  menu_icons[FORCE],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_GREEN);
    #endif
  } else {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_FORCE_X,
                                  0,
                                  menu_icons[FORCE],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_DARKGREY);
    #endif
  }
}

void MenuFunctions::drawStatusBar()
{
  display_obj.tft.setTextSize(1);
  #ifdef HAS_MINI_SCREEN
    display_obj.tft.setFreeFont(NULL);
  #endif
  display_obj.tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);

  uint16_t the_color;

  // GPS Stuff
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      if (gps_obj.getFixStatus())
        the_color = TFT_GREEN;
      else
        the_color = TFT_RED;

      #ifdef HAS_FULL_SCREEN
        display_obj.tft.drawXBitmap(4,
                                    0,
                                    menu_icons[STATUS_GPS],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    the_color);
        display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);

        display_obj.tft.drawString(gps_obj.getNumSatsString(), 22, 0, 2);
      #endif
    }
  #endif

  display_obj.tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);


  // WiFi Channel Stuff
  uint8_t primaryChannel;
  wifi_second_chan_t secondChannel;
  esp_err_t err = esp_wifi_get_channel(&primaryChannel, &secondChannel);

  if (err == ESP_OK)
    wifi_scan_obj.old_channel = primaryChannel;
  else
    wifi_scan_obj.old_channel = wifi_scan_obj.set_channel;

  #if defined(MARAUDER_POOM)
    display_obj.tft.fillRect(0, 0, 44, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.fillRect(43, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #else
    display_obj.tft.fillRect(50, 0, TFT_WIDTH * 0.21, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #endif
  #ifdef HAS_FULL_SCREEN
    display_obj.tft.drawString("CH: " + (String)wifi_scan_obj.old_channel, 50, 0, 2);
  #endif

  #if defined(MARAUDER_POOM)
    display_obj.tft.drawString("C" + (String)wifi_scan_obj.old_channel, 0, 0, 1);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.drawString("CH:" + (String)wifi_scan_obj.old_channel, TFT_WIDTH/4, 0, 1);
  #endif

  // RAM Stuff
  wifi_scan_obj.free_ram = String(esp_get_free_heap_size());
  wifi_scan_obj.old_free_ram = wifi_scan_obj.free_ram;
  display_obj.tft.fillRect(100, 0, 60, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  #ifdef HAS_FULL_SCREEN
    #ifndef HAS_PSRAM
      display_obj.tft.drawString("D:" + String(getDRAMUsagePercent()) + "%", SB_MEM_X, 0, 2);
    #else
      display_obj.tft.drawString("D:" + String(getDRAMUsagePercent()) + "%", SB_MEM_X, 0, 1);
      display_obj.tft.drawString("P:" + String(getPSRAMUsagePercent()) + "%", SB_MEM_X, 8, 1);
    #endif
  #endif

  #if defined(MARAUDER_POOM)
    display_obj.tft.fillRect(46, 0, 34, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
    display_obj.tft.drawString(String(getDRAMUsagePercent()) + "%", 46, 0, 1);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.drawString(String(getDRAMUsagePercent()) + "%", TFT_WIDTH/1.75, 0, 1);
  #endif


  MenuFunctions::battery(true);
  display_obj.tft.fillRect(186, 0, 16, STATUS_BAR_WIDTH, STATUSBAR_COLOR);


  // Disable touch stuff
  #ifdef HAS_ILI9341
    #ifdef HAS_BUTTONS
      if (this->disable_touch) {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(SB_TOUCH_X,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_RED);
      }
      else {
        display_obj.tft.setCursor(0, 1);
        display_obj.tft.drawXBitmap(SB_TOUCH_X,
                                    0,
                                    menu_icons[DISABLE_TOUCH],
                                    16,
                                    16,
                                    STATUSBAR_COLOR,
                                    TFT_DARKGREY);
      }
    #endif
  #endif

  // Draw SD info
  #ifdef HAS_SD
    if (sd_obj.supported)
      the_color = TFT_GREEN;
    else
      the_color = TFT_RED;


    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_SD_X,
                                  0,
                                  menu_icons[STATUS_SD],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  the_color);
    #endif
  #endif

  #if defined(MARAUDER_POOM)
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
    display_obj.tft.drawString("SD", 116, 0, 1);
  #elif defined(HAS_MINI_SCREEN)
    display_obj.tft.setTextColor(the_color, STATUSBAR_COLOR);
    display_obj.tft.drawString("SD", TFT_WIDTH - 12, 0, 1);
  #endif

  // WiFi connection status stuff
  if (wifi_scan_obj.wifi_connected) {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_WIFI_X,
                                  0,
                                  menu_icons[JOINED],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_GREEN);
    #endif
  } else {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_WIFI_X,
                                  0,
                                  menu_icons[JOINED],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_DARKGREY);
    #endif
  }

  // Force PMKID stuff
  if ((wifi_scan_obj.force_pmkid) || (wifi_scan_obj.ep_deauth)) {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_FORCE_X,
                                  0,
                                  menu_icons[FORCE],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_GREEN);
    #endif
  } else {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.drawXBitmap(SB_FORCE_X,
                                  0,
                                  menu_icons[FORCE],
                                  16,
                                  16,
                                  STATUSBAR_COLOR,
                                  TFT_DARKGREY);
    #endif
  }
}

void MenuFunctions::orientDisplay() {
  display_obj.init();

  display_obj.tft.setRotation(SCREEN_ORIENTATION); // Portrait

  display_obj.tft.setCursor(0, 0);

  #ifdef HAS_ILI9341
    #ifndef HAS_CYD_TOUCH
      display_obj.setCalData();
    #else
      display_obj.touchscreen.setRotation(0);
    #endif
  #endif

  changeMenu(current_menu, true);
}

const char* MenuFunctions::callSetting(const char* key) {
  specSettingMenu.name = key;

  const char* setting_type = settings_obj.getSettingType(key);

  if (setting_type && strcmp(setting_type, "bool") == 0) {
    return "bool";
  }

  return "";
}

/*void MenuFunctions::displaySetting(String key, Menu* menu, int index) {
  specSettingMenu.name = key;

  bool setting_value = settings_obj.loadSetting<bool>(key);

  // Make a local copy of menu node
  MenuNode node = menu->list->get(index);

  display_obj.tft.setTextWrap(false);
  display_obj.tft.setFreeFont(NULL);
  display_obj.tft.setCursor(0, 100);
  display_obj.tft.setTextSize(1);

  // Set local copy value
  if (!setting_value) {
    display_obj.tft.setTextColor(TFT_RED);
    display_obj.tft.println(F(text_table1[4]));
    node.selected = false;
  }
  else {
    display_obj.tft.setTextColor(TFT_GREEN);
    display_obj.tft.println(F(text_table1[5]));
    node.selected = true;
  }

  // Put local copy back into menu
  menu->list->set(index, node);

}*/

void MenuFunctions::displaySetting(const char* key, Menu* menu, int index) {
  specSettingMenu.name = String(key);

  bool setting_value = settings_obj.loadSetting<bool>(key);

  // Make a local copy of menu node
  MenuNode node = menu->list->get(index);

  display_obj.tft.setTextWrap(false);
  display_obj.tft.setFreeFont(NULL);
  display_obj.tft.setCursor(0, 100);
  display_obj.tft.setTextSize(1);

  // Set local copy value
  if (!setting_value) {
    display_obj.tft.setTextColor(TFT_RED);
    display_obj.tft.println(F(text_table1[4]));
    node.selected = false;
  } else {
    display_obj.tft.setTextColor(TFT_GREEN);
    display_obj.tft.println(F(text_table1[5]));
    node.selected = true;
  }

  // Put local copy back into menu
  menu->list->set(index, node);
}

#if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD)
void MenuFunctions::updateKeyboard()
{
  marauder_keyboard.updateKeyList();
  marauder_keyboard.updateKeysState();
}

bool MenuFunctions::isKeyPressed(char c)
{
  bool pressed = marauder_keyboard.isKeyPressed(c);
  #ifdef HAS_FACES_KEYBOARD
    return pressed && marauder_keyboard.isChange();
  #else
    if (pressed) delay(200);
    return pressed;
  #endif
}
#endif

bool MenuFunctions::textInput(char *buffer, size_t bufLen, const char *title)
{
  #ifdef HAS_FACES_KEYBOARD
    if (marauder_keyboard.available()) {
      display_obj.clearScreen();
      display_obj.tft.setTextSize(1);
      display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
      display_obj.tft.drawString(title ? title : "Enter text", 4, 6, 2);
      display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      display_obj.tft.drawString(buffer, 4, 34, 2);
      // Do not carry the menu-select key into the text field.
      do {
        this->updateKeyboard();
        delay(5);
      } while (marauder_keyboard.isPressed());
      while (true) {
        this->updateKeyboard();
        if (marauder_keyboard.isChange()) {
          uint8_t key = marauder_keyboard.currentKey();
          if (key == KEY_ENTER) return true;
          if (key == KEY_BACKSPACE) {
            size_t len = strlen(buffer);
            if (len) buffer[len - 1] = '\0';
          } else if (key >= 32 && key <= 126) {
            size_t len = strlen(buffer);
            if (len + 1 < bufLen) {
              buffer[len] = (char)key;
              buffer[len + 1] = '\0';
            }
          } else {
            delay(5);
            continue;
          }
          display_obj.tft.fillRect(0, 30, display_obj.tft.width(), 28, TFT_BLACK);
          display_obj.tft.drawString(buffer, 4, 34, 2);
        }
        delay(5);
        yield();
      }
    }
  #endif
  #ifdef HAS_TOUCH
    return keyboardInput(buffer, bufLen, title);
  #elif defined(HAS_BUTTONS)
    static const char* const CHAR_SET[] = {
      "A","B","C","D","E","F","G","H","I","J","K","L","M",
      "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
      "a","b","c","d","e","f","g","h","i","j","k","l","m",
      "n","o","p","q","r","s","t","u","v","w","x","y","z",
      "0","1","2","3","4","5","6","7","8","9",
      " ","_","-",".","!","@","#","$","%","&","*",
      "<BS>", "<DONE>", "<CANCEL>"
    };
    const int NUM_CHARS = sizeof(CHAR_SET) / sizeof(CHAR_SET[0]);
    int char_idx = 0;

    while (u_btn.isPressedNow() || d_btn.isPressedNow()) delay(10);

    display_obj.clearScreen();
    // Header
    display_obj.tft.fillRect(0, 0, SCREEN_WIDTH, 24, 0x18E3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18E3);
    display_obj.tft.setTextDatum(MC_DATUM);
    display_obj.tft.drawString(title ? title : "Enter Text", SCREEN_WIDTH / 2, 12, 2);

    // Buffer box
    display_obj.tft.drawRoundRect(6, 28, SCREEN_WIDTH - 12, 34, 4, TFT_CYAN);
    display_obj.tft.fillRect(8, 30, SCREEN_WIDTH - 16, 30, TFT_BLACK);

    // Help banner
    display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    display_obj.tft.setTextDatum(MC_DATUM);
    display_obj.tft.drawString("DN: Next | UP: Prev | Hold DN: Add | Hold UP: Done", SCREEN_WIDTH / 2, 70, 1);

    auto drawBuffer = [&]() {
      display_obj.tft.fillRect(10, 32, SCREEN_WIDTH - 20, 26, TFT_BLACK);
      display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
      display_obj.tft.setTextDatum(ML_DATUM);
      String disp = String(buffer) + "_";
      display_obj.tft.drawString(disp, 16, 45, 2);
    };

    auto drawCarousel = [&]() {
      display_obj.tft.fillRect(0, 85, SCREEN_WIDTH, 80, TFT_BLACK);

      const int cx = SCREEN_WIDTH / 2;
      const int cy = 120;

      display_obj.tft.fillRoundRect(cx - 36, cy - 18, 72, 36, 6, TFT_NAVY);
      display_obj.tft.drawRoundRect(cx - 36, cy - 18, 72, 36, 6, TFT_YELLOW);
      display_obj.tft.setTextColor(TFT_YELLOW, TFT_NAVY);
      display_obj.tft.setTextDatum(MC_DATUM);
      display_obj.tft.drawString(CHAR_SET[char_idx], cx, cy, (strlen(CHAR_SET[char_idx]) > 2) ? 2 : 4);

      display_obj.tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      int offsets[] = {-64, -118};
      for (int i = 0; i < 2; i++) {
        int idx = (char_idx - (i + 1) + NUM_CHARS * 5) % NUM_CHARS;
        display_obj.tft.drawString(CHAR_SET[idx], cx + offsets[i], cy, (strlen(CHAR_SET[idx]) > 2) ? 1 : 2);
      }

      int offsets_r[] = {64, 118};
      for (int i = 0; i < 2; i++) {
        int idx = (char_idx + (i + 1)) % NUM_CHARS;
        display_obj.tft.drawString(CHAR_SET[idx], cx + offsets_r[i], cy, (strlen(CHAR_SET[idx]) > 2) ? 1 : 2);
      }
    };

    drawBuffer();
    drawCarousel();

    uint32_t u_press_t = 0;
    uint32_t d_press_t = 0;
    bool u_handled = false;
    bool d_handled = false;

    while (true) {
      if (Serial.available()) {
        String ser = Serial.readStringUntil('\n');
        ser.trim();
        if (ser == "screen" || ser == "screenshot") {
          display_obj.dumpScreenBMP();
          continue;
        } else if (ser == "btn down") {
          char_idx = (char_idx + 1) % NUM_CHARS;
          drawCarousel();
          continue;
        } else if (ser == "btn up") {
          char_idx = (char_idx > 0) ? char_idx - 1 : NUM_CHARS - 1;
          drawCarousel();
          continue;
        } else if (ser == "btn ok") {
          if (strcmp(CHAR_SET[char_idx], "<BS>") == 0) {
            size_t len = strlen(buffer);
            if (len > 0) {
              buffer[len - 1] = '\0';
              drawBuffer();
            }
          } else if (strcmp(CHAR_SET[char_idx], "<DONE>") == 0) {
            return (strlen(buffer) > 0);
          } else if (strcmp(CHAR_SET[char_idx], "<CANCEL>") == 0) {
            return false;
          } else {
            size_t len = strlen(buffer);
            if (len + 1 < bufLen) {
              buffer[len] = CHAR_SET[char_idx][0];
              buffer[len + 1] = '\0';
              drawBuffer();
            }
          }
          continue;
        } else if (ser == "btn back" || ser == "btn exit") {
          return false;
        } else if (ser.startsWith("type ")) {
          String val = ser.substring(5);
          strncpy(buffer, val.c_str(), bufLen - 1);
          buffer[bufLen - 1] = '\0';
          drawBuffer();
          continue;
        } else if (ser.length() > 0) {
          strncpy(buffer, ser.c_str(), bufLen - 1);
          buffer[bufLen - 1] = '\0';
          return true;
        }
      }

      if (u_btn.isPressedNow()) {
        if (u_press_t == 0) {
          u_press_t = millis();
          u_handled = false;
        } else if (!u_handled && (millis() - u_press_t >= 400)) {
          u_handled = true;
          while (u_btn.isPressedNow()) delay(10);
          return (strlen(buffer) > 0);
        }
      } else {
        if (u_press_t != 0) {
          if (!u_handled && (millis() - u_press_t < 400)) {
            char_idx = (char_idx > 0) ? char_idx - 1 : NUM_CHARS - 1;
            drawCarousel();
          }
          u_press_t = 0;
          u_handled = false;
        }
      }

      if (d_btn.isPressedNow()) {
        if (d_press_t == 0) {
          d_press_t = millis();
          d_handled = false;
        } else if (!d_handled && (millis() - d_press_t >= 400)) {
          d_handled = true;
          if (strcmp(CHAR_SET[char_idx], "<BS>") == 0) {
            size_t len = strlen(buffer);
            if (len > 0) {
              buffer[len - 1] = '\0';
              drawBuffer();
            }
          } else if (strcmp(CHAR_SET[char_idx], "<DONE>") == 0) {
            while (d_btn.isPressedNow()) delay(10);
            return (strlen(buffer) > 0);
          } else if (strcmp(CHAR_SET[char_idx], "<CANCEL>") == 0) {
            while (d_btn.isPressedNow()) delay(10);
            return false;
          } else {
            size_t len = strlen(buffer);
            if (len + 1 < bufLen) {
              buffer[len] = CHAR_SET[char_idx][0];
              buffer[len + 1] = '\0';
              drawBuffer();
            }
          }
          while (d_btn.isPressedNow()) delay(10);
        }
      } else {
        if (d_press_t != 0) {
          if (!d_handled && (millis() - d_press_t < 400)) {
            char_idx = (char_idx + 1) % NUM_CHARS;
            drawCarousel();
          }
          d_press_t = 0;
          d_handled = false;
        }
      }

      delay(15);
      yield();
    }
  #else
    return false;
  #endif
}

#ifdef HAS_DIRECT_UPLOAD
  void MenuFunctions::buildUploadFileMenu() {
    if (sd_obj.supported) {
      this->setupSDFileList();

      uploadLogsMenu.list->clear();
      delete uploadLogsMenu.list;
      uploadLogsMenu.list = new LinkedList<MenuNode>();
      uploadLogsMenu.name = "Logs";

      uploadLogsMenu.parentMenu = &wifiGeneralMenu;

      this->addNodes(&uploadLogsMenu, "Back", TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(uploadLogsMenu.parentMenu, true);
      });

      this->addNodes(&uploadLogsMenu, "Delete Wardrive Logs", TFTORANGE, 0, [this]() {
        this->changeMenu(&deleteAllMenu, true);
      });

      this->addNodes(&uploadLogsMenu, "Upload All", TFTGREEN, 0, [this]() {
        this->changeMenu(&uploadAllMenu, true);

      });

      for (int i = 0; i < sd_obj.sd_files->size(); i++) {
        File current_file = sd_obj.getFile("/" + sd_obj.sd_files->get(i));
        if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
          if (!sd_obj.sd_files->get(i).endsWith(".wdg") && !sd_obj.sd_files->get(i).endsWith(".wigle") && !sd_obj.sd_files->get(i).endsWith(".gpx")) {
            this->addNodes(&uploadLogsMenu, sd_obj.sd_files->get(i).c_str(), TFTCYAN, 0, [this, i]() {
              sd_obj.selected_file_name = sd_obj.sd_files->get(i);
              Serial.println(sd_obj.sd_files->get(i) + " selected");
              this->changeMenu(&actionMenu, true);
            });
          }
        }
      }

      Serial.println("Built SD file menu with " + (String)sd_obj.sd_files->size() + " files");
    } else {
      Serial.println("SD Card not detected. Skipping menu creation...");
    }
  }
#endif

const char* MenuFunctions::foxSortLabel() const {
  switch (fox_sort_mode) {
    case TargetSortMode::SIGNAL_DESC: return "Signal v";
    case TargetSortMode::NAME_ASC: return "Name A-Z";
    case TargetSortMode::CHANNEL_ASC: return "Channel 1-177";
  }
  return "Signal v";
}

const char* MenuFunctions::foxFilterLabel() const {
  switch (fox_filter_mode) {
    case TargetFilterMode::ALL: return "All";
    case TargetFilterMode::RECENT_30S: return "Seen <30s";
    case TargetFilterMode::BAND_24_GHZ: return "2.4 GHz";
    case TargetFilterMode::BAND_5_GHZ: return "5 GHz";
  }
  return "All";
}

bool MenuFunctions::foxListSupportsRecent() const {
  return fox_target_list == FoxHuntListKind::AP_TARGETS ||
         fox_target_list == FoxHuntListKind::APS_WITH_STATIONS ||
         fox_target_list == FoxHuntListKind::FINDMY_TARGETS ||
         fox_target_list == FoxHuntListKind::IBEACON_TARGETS;
}

bool MenuFunctions::foxListSupportsBand() const {
  return fox_target_list == FoxHuntListKind::AP_TARGETS ||
         fox_target_list == FoxHuntListKind::APS_WITH_STATIONS ||
         fox_target_list == FoxHuntListKind::STATION_TARGETS ||
         fox_target_list == FoxHuntListKind::PINEAPPLE_TARGETS ||
         fox_target_list == FoxHuntListKind::MULTISSID_TARGETS;
}

void MenuFunctions::buildFoxSortMenu() {
  foxSortMenu.list->clear();
  foxSortMenu.parentMenu = current_menu;
  this->addNodes(&foxSortMenu, text09, TFTLIGHTGREY, 0, [this]() { this->changeMenu(foxSortMenu.parentMenu, true); });
  this->addNodes(&foxSortMenu, "Signal: Strongest", TFTCYAN, 255, [this]() {
    fox_sort_mode = TargetSortMode::SIGNAL_DESC;
    buildFoxTargetList(fox_target_list, fox_target_context_ap);
  });
  this->addNodes(&foxSortMenu, "Name/MAC: A-Z", TFTCYAN, 255, [this]() {
    fox_sort_mode = TargetSortMode::NAME_ASC;
    buildFoxTargetList(fox_target_list, fox_target_context_ap);
  });
  if (foxListSupportsBand()) {
    this->addNodes(&foxSortMenu, "Channel: Low-High", TFTCYAN, 255, [this]() {
      fox_sort_mode = TargetSortMode::CHANNEL_ASC;
      buildFoxTargetList(fox_target_list, fox_target_context_ap);
    });
  }
  this->changeMenu(&foxSortMenu, true);
}

void MenuFunctions::buildFoxFilterMenu() {
  foxFilterMenu.list->clear();
  foxFilterMenu.parentMenu = current_menu;
  this->addNodes(&foxFilterMenu, text09, TFTLIGHTGREY, 0, [this]() { this->changeMenu(foxFilterMenu.parentMenu, true); });
  this->addNodes(&foxFilterMenu, "All", TFTGREEN, 255, [this]() {
    fox_filter_mode = TargetFilterMode::ALL;
    buildFoxTargetList(fox_target_list, fox_target_context_ap);
  });
  if (foxListSupportsRecent()) {
    this->addNodes(&foxFilterMenu, "Seen in 30 Seconds", TFTGREEN, 255, [this]() {
      fox_filter_mode = TargetFilterMode::RECENT_30S;
      buildFoxTargetList(fox_target_list, fox_target_context_ap);
    });
  }
  if (foxListSupportsBand()) {
    this->addNodes(&foxFilterMenu, "2.4 GHz", TFTGREEN, 255, [this]() {
      fox_filter_mode = TargetFilterMode::BAND_24_GHZ;
      buildFoxTargetList(fox_target_list, fox_target_context_ap);
    });
    #ifdef HAS_DUAL_BAND
      this->addNodes(&foxFilterMenu, "5 GHz", TFTGREEN, 255, [this]() {
        fox_filter_mode = TargetFilterMode::BAND_5_GHZ;
        buildFoxTargetList(fox_target_list, fox_target_context_ap);
      });
    #endif
  }
  this->changeMenu(&foxFilterMenu, true);
}

void MenuFunctions::buildFoxTargetList(FoxHuntListKind type, int context_ap) {
  fox_target_list = type;
  fox_target_context_ap = context_ap;
  if (!foxListSupportsRecent() && fox_filter_mode == TargetFilterMode::RECENT_30S)
    fox_filter_mode = TargetFilterMode::ALL;
  if (!foxListSupportsBand() && (fox_filter_mode == TargetFilterMode::BAND_24_GHZ || fox_filter_mode == TargetFilterMode::BAND_5_GHZ))
    fox_filter_mode = TargetFilterMode::ALL;
  if (!foxListSupportsBand() && fox_sort_mode == TargetSortMode::CHANNEL_ASC)
    fox_sort_mode = TargetSortMode::SIGNAL_DESC;

  Menu* menu = type == FoxHuntListKind::STATION_TARGETS ? &wifiStationMenu : &wifiAPMenu;
  menu->list->clear();
  menu->parentMenu = type == FoxHuntListKind::STATION_TARGETS ? &wifiAPMenu : &foxHuntMenu;
  this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this, menu]() { this->changeMenu(menu->parentMenu, true); });
  String sort_row = "Sort: " + String(foxSortLabel());
  this->addNodes(menu, sort_row.c_str(), TFTCYAN, 255, [this]() { buildFoxSortMenu(); });
  String filter_row = "Filter: " + String(foxFilterLabel());
  this->addNodes(menu, filter_row.c_str(), TFTGREEN, 255, [this]() { buildFoxFilterMenu(); });
  this->addNodes(menu, "Refresh List", TFTYELLOW, 255, [this]() { buildFoxTargetList(fox_target_list, fox_target_context_ap); });

  std::vector<TargetListItem> items;
  auto add_item = [&items](size_t index, int16_t rssi, uint8_t channel, uint32_t last_seen, const String& name) {
    TargetListItem item = {index, rssi, channel, last_seen, {}};
    strncpy(item.name, name.c_str(), sizeof(item.name) - 1);
    items.push_back(item);
  };

  if (type == FoxHuntListKind::AP_TARGETS || type == FoxHuntListKind::APS_WITH_STATIONS) {
    for (int i = 0; i < access_points->size(); i++) {
      const AccessPoint& ap = access_points->get(i);
      if (type == FoxHuntListKind::APS_WITH_STATIONS && ap.stations->size() == 0)
        continue;
      add_item(i, ap.rssi, ap.channel, ap.last_seen_ms, ap.essid.length() ? ap.essid : macToString(ap.bssid));
    }
  } else if (type == FoxHuntListKind::STATION_TARGETS && context_ap >= 0 && context_ap < access_points->size()) {
    const AccessPoint& ap = access_points->get(context_ap);
    for (int x = 0; x < ap.stations->size(); x++) {
      int station_index = ap.stations->get(x);
      add_item(station_index, -128, ap.channel, 0, macToString(stations->get(station_index).mac));
    }
  } else if (type == FoxHuntListKind::PINEAPPLE_TARGETS) {
    for (size_t i = 0; i < wifi_scan_obj.getPineScanCount(); i++)
      add_item(i, wifi_scan_obj.getPineScanRssi(i), wifi_scan_obj.getPineScanChannel(i), 0, wifi_scan_obj.getPineScanLabel(i));
  } else if (type == FoxHuntListKind::MULTISSID_TARGETS) {
    for (size_t i = 0; i < wifi_scan_obj.getMultiSSIDCount(); i++)
      add_item(i, wifi_scan_obj.getMultiSSIDRssi(i), wifi_scan_obj.getMultiSSIDChannel(i), 0, wifi_scan_obj.getMultiSSIDLabel(i));
  } else if (type == FoxHuntListKind::BLE_TARGETS) {
    for (int i = 0; i < ble_devices->size(); i++)
      add_item(i, ble_devices->get(i).rssi, 0, 0, ble_devices->get(i).name.length() ? ble_devices->get(i).name : macToString(ble_devices->get(i).mac));
  } else if (type == FoxHuntListKind::FINDMY_TARGETS) {
    for (int i = 0; i < airtags->size(); i++)
      add_item(i, airtags->get(i).rssi, 0, airtags->get(i).last_seen, airtags->get(i).mac);
  } else if (type == FoxHuntListKind::FLIPPER_TARGETS) {
    for (int i = 0; i < flippers->size(); i++)
      add_item(i, flippers->get(i).rssi, 0, flippers->get(i).last_seen,
               flippers->get(i).name.length() ? flippers->get(i).name : flippers->get(i).mac);
  } else if (type == FoxHuntListKind::IBEACON_TARGETS) {
    for (int i = 0; i < ble_devices->size(); i++) {
      const BleDevice& device = ble_devices->get(i);
      if (device.is_ibeacon)
        add_item(i, device.rssi, 0, device.last_seen_ms, device.name);
    }
  } else if (type == FoxHuntListKind::META_TARGETS || type == FoxHuntListKind::FLOCK_TARGETS) {
    const String device_type = type == FoxHuntListKind::META_TARGETS ? "Meta" : "Flock";
    for (int i = 0; i < ble_devices->size(); i++) {
      const BleDevice& device = ble_devices->get(i);
      if (device.device_type == device_type)
        add_item(i, device.rssi, 0, device.last_seen_ms,
                 device.name.length() ? device.name : macToString(device.mac));
    }
  }

  std::vector<TargetListItem> filtered;
  uint32_t now = millis();
  for (const TargetListItem& item : items) {
    if (targetListItemMatchesFilter(item, fox_filter_mode, now))
      filtered.push_back(item);
  }
  sortTargetList(filtered, fox_sort_mode);

  if (filtered.empty()) {
    this->addNodes(menu, "(No Targets Found)", TFTYELLOW, 255, []() {});
    uint8_t scan_cmd = WIFI_SCAN_AP;
    uint16_t scan_col = TFT_MAGENTA;
    if (type == FoxHuntListKind::AP_TARGETS) {
      scan_cmd = WIFI_SCAN_AP_STA; scan_col = 0x97e0;
    } else if (type == FoxHuntListKind::APS_WITH_STATIONS || type == FoxHuntListKind::STATION_TARGETS) {
      scan_cmd = WIFI_SCAN_AP_STA; scan_col = 0x97e0;
    } else if (type == FoxHuntListKind::PINEAPPLE_TARGETS) {
      scan_cmd = WIFI_SCAN_PINESCAN; scan_col = TFT_YELLOW;
    } else if (type == FoxHuntListKind::MULTISSID_TARGETS) {
      scan_cmd = WIFI_SCAN_MULTISSID; scan_col = TFT_ORANGE;
    }
    #ifdef HAS_BT
    else if (type == FoxHuntListKind::BLE_TARGETS || type == FoxHuntListKind::META_TARGETS || type == FoxHuntListKind::FLOCK_TARGETS) {
      scan_cmd = BT_SCAN_ALL; scan_col = TFT_GREEN;
    } else if (type == FoxHuntListKind::FINDMY_TARGETS) {
      scan_cmd = BT_SCAN_AIRTAG; scan_col = TFT_WHITE;
    } else if (type == FoxHuntListKind::FLIPPER_TARGETS) {
      scan_cmd = BT_SCAN_FLIPPER; scan_col = TFT_ORANGE;
    } else if (type == FoxHuntListKind::IBEACON_TARGETS) {
      scan_cmd = BT_SCAN_IBEACON; scan_col = TFT_CYAN;
    }
    #endif
    this->addNodes(menu, "+ Scan Targets Now", TFTGREEN, SCANNERS, [this, scan_cmd, scan_col, type, context_ap]() {
      startScanWithReturn(scan_cmd, scan_col, [this, type, context_ap]() {
        buildFoxTargetList(type, context_ap);
      });
    });
  } else {
    for (const TargetListItem& item : filtered) {
    int index = item.source_index;
    String label;
    uint16_t color = TFTCYAN;
    if (type == FoxHuntListKind::AP_TARGETS) {
      const AccessPoint& ap = access_points->get(index);
      label = String(ap.rssi) + " " + screenName(ap.essid);
      color = rssiToMenuColor(ap.rssi);
    } else if (type == FoxHuntListKind::APS_WITH_STATIONS) {
      const AccessPoint& ap = access_points->get(index);
      label = screenName(ap.essid) + " (" + String(ap.stations->size()) + ")";
      color = TFTMAGENTA;
    } else if (type == FoxHuntListKind::STATION_TARGETS) {
      label = screenMac(macToString(stations->get(index).mac));
      color = TFTMAGENTA;
    } else if (type == FoxHuntListKind::PINEAPPLE_TARGETS) {
      label = screenIdentifier(wifi_scan_obj.getPineScanLabel(index));
      color = TFTYELLOW;
    } else if (type == FoxHuntListKind::MULTISSID_TARGETS) {
      label = screenName(wifi_scan_obj.getMultiSSIDLabel(index));
      color = TFTORANGE;
    } else if (type == FoxHuntListKind::BLE_TARGETS) {
      label = String(ble_devices->get(index).rssi) + " " + screenIdentifier(ble_devices->get(index).name);
      color = rssiToMenuColor(ble_devices->get(index).rssi);
    } else if (type == FoxHuntListKind::FINDMY_TARGETS) {
      label = String(airtags->get(index).rssi) + " " + screenMac(airtags->get(index).mac);
      color = TFTWHITE;
    } else if (type == FoxHuntListKind::FLIPPER_TARGETS) {
      label = flippers->get(index).name.length() ? screenName(flippers->get(index).name) : screenMac(flippers->get(index).mac);
      color = TFTORANGE;
    } else if (type == FoxHuntListKind::IBEACON_TARGETS) {
      const BleDevice& device = ble_devices->get(index);
      label = String(device.rssi) + " " + screenIdentifier(device.name);
      color = rssiToMenuColor(device.rssi);
    } else {
      const BleDevice& device = ble_devices->get(index);
      label = String(device.rssi) + " " + (device.name.length() ? screenName(device.name) : screenMac(macToString(device.mac)));
      color = type == FoxHuntListKind::META_TARGETS ? TFTWHITE : TFTORANGE;
    }

    this->addNodes(menu, label.c_str(), color, 255, [this, type, context_ap, index]() {
      if (type == FoxHuntListKind::APS_WITH_STATIONS) {
        buildFoxTargetList(FoxHuntListKind::STATION_TARGETS, index);
        return;
      }
      if (type == FoxHuntListKind::PINEAPPLE_TARGETS) {
        if (!wifi_scan_obj.selectPineScanFoxTarget(index)) return;
      } else if (type == FoxHuntListKind::MULTISSID_TARGETS) {
        if (!wifi_scan_obj.selectMultiSSIDFoxTarget(index)) return;
      } else if (type == FoxHuntListKind::AP_TARGETS) {
        const AccessPoint& ap = access_points->get(index);
        wifi_scan_obj.setFoxHuntTarget(ap.bssid, ap.essid, ap.rssi, ap.channel, false);
      } else if (type == FoxHuntListKind::STATION_TARGETS) {
        const AccessPoint& ap = access_points->get(context_ap);
        const Station& station = stations->get(index);
        wifi_scan_obj.setFoxHuntTarget(station.mac, macToString(station.mac), -128, ap.channel, false);
      } else if (type == FoxHuntListKind::BLE_TARGETS) {
        const BleDevice& device = ble_devices->get(index);
        wifi_scan_obj.setFoxHuntTarget(device.mac, device.name, device.rssi, 0, true, macToString(device.mac));
      } else if (type == FoxHuntListKind::FINDMY_TARGETS) {
        uint8_t mac[6];
        convertMacStringToUint8(airtags->get(index).mac, mac);
        wifi_scan_obj.setFoxHuntTarget(mac, airtags->get(index).mac, airtags->get(index).rssi, 0, true, airtags->get(index).mac);
      } else if (type == FoxHuntListKind::FLIPPER_TARGETS) {
        uint8_t mac[6];
        convertMacStringToUint8(flippers->get(index).mac, mac);
        String name = flippers->get(index).name.length() ? flippers->get(index).name : flippers->get(index).mac;
        wifi_scan_obj.setFoxHuntTarget(mac, name, flippers->get(index).rssi, 0, true, flippers->get(index).mac);
      } else if (type == FoxHuntListKind::IBEACON_TARGETS) {
        const BleDevice& device = ble_devices->get(index);
        wifi_scan_obj.setFoxHuntTarget(device.mac, device.name, device.rssi, 0, true, macToString(device.mac));
      } else if (type == FoxHuntListKind::META_TARGETS || type == FoxHuntListKind::FLOCK_TARGETS) {
        const BleDevice& device = ble_devices->get(index);
        wifi_scan_obj.setFoxHuntTarget(device.mac, device.name, device.rssi, 0, true, macToString(device.mac));
      }
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(type == FoxHuntListKind::BLE_TARGETS ||
                              type == FoxHuntListKind::FINDMY_TARGETS ||
                              type == FoxHuntListKind::FLIPPER_TARGETS ||
                              type == FoxHuntListKind::IBEACON_TARGETS ||
                              type == FoxHuntListKind::META_TARGETS ||
                              type == FoxHuntListKind::FLOCK_TARGETS
                                ? BT_SCAN_FOX_HUNT : WIFI_SCAN_SIG_STREN, TFT_CYAN);
    });
  }
  }
  this->changeMenu(menu, true);
}

void MenuFunctions::buildWiFiFoxHuntMenu() {
  foxHuntMenu.list->clear();
  foxHuntMenu.parentMenu = &wifiSnifferMenu;
  this->addNodes(&foxHuntMenu, text09, TFTLIGHTGREY, 0, [this]() { this->changeMenu(foxHuntMenu.parentMenu, true); });
  this->addNodes(&foxHuntMenu, "APs", TFTLIME, WIFI, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points->size() == 0) {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        buildFoxTargetList(FoxHuntListKind::AP_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::AP_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "Stations", TFTMAGENTA, WIFI, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points->size() == 0) {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        buildFoxTargetList(FoxHuntListKind::APS_WITH_STATIONS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::APS_WITH_STATIONS);
  });
  this->addNodes(&foxHuntMenu, "WiFi Pineapples", TFTYELLOW, PINESCAN_SNIFF, [this]() {
    if (wifi_scan_obj.getPineScanCount() == 0) {
      startScanWithReturn(WIFI_SCAN_PINESCAN, TFT_YELLOW, [this]() {
        buildFoxTargetList(FoxHuntListKind::PINEAPPLE_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::PINEAPPLE_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "MultiSSID", TFTORANGE, MULTISSID_SNIFF, [this]() {
    if (wifi_scan_obj.getMultiSSIDCount() == 0) {
      startScanWithReturn(WIFI_SCAN_MULTISSID, TFT_ORANGE, [this]() {
        buildFoxTargetList(FoxHuntListKind::MULTISSID_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::MULTISSID_TARGETS);
  });
  this->changeMenu(&foxHuntMenu, true);
}

void MenuFunctions::buildBluetoothFoxHuntMenu() {
  foxHuntMenu.list->clear();
  foxHuntMenu.parentMenu = &bluetoothSnifferMenu;
  this->addNodes(&foxHuntMenu, text09, TFTLIGHTGREY, 0, [this]() { this->changeMenu(foxHuntMenu.parentMenu, true); });
  #ifdef HAS_BT
  this->addNodes(&foxHuntMenu, "BLE Devices", TFTCYAN, BLUETOOTH, [this]() {
    extern LinkedList<BleDevice>* ble_devices;
    if (ble_devices->size() == 0) {
      startScanWithReturn(BT_SCAN_ALL, TFT_GREEN, [this]() {
        buildFoxTargetList(FoxHuntListKind::BLE_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::BLE_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "FindMy", TFTWHITE, BLUETOOTH, [this]() {
    extern LinkedList<AirTag>* airtags;
    if (airtags->size() == 0) {
      startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
        buildFoxTargetList(FoxHuntListKind::FINDMY_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::FINDMY_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "Flipper Zero", TFTORANGE, FLIPPER, [this]() {
    extern LinkedList<Flipper>* flippers;
    if (flippers->size() == 0) {
      startScanWithReturn(BT_SCAN_FLIPPER, TFT_ORANGE, [this]() {
        buildFoxTargetList(FoxHuntListKind::FLIPPER_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::FLIPPER_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "iBeacons", TFTCYAN, BLUETOOTH, [this]() {
    extern LinkedList<BleDevice>* ble_devices;
    if (ble_devices->size() == 0) {
      startScanWithReturn(BT_SCAN_IBEACON, TFT_CYAN, [this]() {
        buildFoxTargetList(FoxHuntListKind::IBEACON_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::IBEACON_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "Meta", TFTWHITE, BLUETOOTH, [this]() {
    extern LinkedList<BleDevice>* ble_devices;
    if (ble_devices->size() == 0) {
      startScanWithReturn(BT_SCAN_ALL, TFT_GREEN, [this]() {
        buildFoxTargetList(FoxHuntListKind::META_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::META_TARGETS);
  });
  this->addNodes(&foxHuntMenu, "Flock", TFTORANGE, FLOCK, [this]() {
    extern LinkedList<BleDevice>* ble_devices;
    if (ble_devices->size() == 0) {
      startScanWithReturn(BT_SCAN_ALL, TFT_GREEN, [this]() {
        buildFoxTargetList(FoxHuntListKind::FLOCK_TARGETS);
      });
      return;
    }
    buildFoxTargetList(FoxHuntListKind::FLOCK_TARGETS);
  });
  #endif
  this->changeMenu(&foxHuntMenu, true);
}

// Function to build the menus
void MenuFunctions::RunSetup()
{
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  extern LinkedList<AirTag>* airtags;
  extern LinkedList<IPAddress>* ipList;
  extern LinkedList<ProbeReqSsid>* probe_req_ssids;
  extern LinkedList<ssid>* ssids;
  extern LinkedList<BleDevice>* ble_devices;

  this->disable_touch = false;

  #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD)
    marauder_keyboard.begin();
  #endif

  // root menu stuff
  mainMenu.buildKind = MenuBuildKind::MAIN;

  // Main menu stuff
  wifiMenu.buildKind = MenuBuildKind::WIFI_ROOT;
#ifdef HAS_BT
  bluetoothMenu.buildKind = MenuBuildKind::BLUETOOTH_ROOT;
#endif
  deviceMenu.buildKind = MenuBuildKind::DEVICE_ROOT;
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      gpsMenu.buildKind = MenuBuildKind::GPS_ROOT;
      gpsInfoMenu.list = new LinkedList<MenuNode>();
    }
  #endif

  // Device menu stuff
  failedUpdateMenu.list = new LinkedList<MenuNode>();
  confirmMenu.list = new LinkedList<MenuNode>();
  updateMenu.list = new LinkedList<MenuNode>();
  settingsMenu.list = new LinkedList<MenuNode>();
  specSettingMenu.list = new LinkedList<MenuNode>();
  geofenceMenu.list = new LinkedList<MenuNode>();
  geofenceActionMenu.list = new LinkedList<MenuNode>();
  #ifdef HAS_MINI_SCREEN
    geofenceRadiusMenu.list = new LinkedList<MenuNode>();
  #endif
  infoMenu.list = new LinkedList<MenuNode>();
  // WiFi menu stuff
  wifiSnifferMenu.buildKind = MenuBuildKind::WIFI_SNIFFER;
  wifiScannerMenu.buildKind = MenuBuildKind::WIFI_SCANNER;
  wifiAttackMenu.buildKind = MenuBuildKind::WIFI_ATTACK;
  attackModeMenu.list = new LinkedList<MenuNode>();
  attackTargetMenu.list = new LinkedList<MenuNode>();
  attackStationMenu.list = new LinkedList<MenuNode>();
  attackReviewMenu.list = new LinkedList<MenuNode>();
  /*#ifdef HAS_GPS
    wardrivingMenu.list = new LinkedList<MenuNode>();
  #endif*/
  wifiGeneralMenu.buildKind = MenuBuildKind::WIFI_GENERAL;
  wifiAPMenu.list = new LinkedList<MenuNode>();
  savedWifiMenu.list = nullptr;
  wifiIPMenu.list = new LinkedList<MenuNode>();
  apInfoMenu.list = new LinkedList<MenuNode>();
  setMacMenu.list = new LinkedList<MenuNode>();
  genAPMacMenu.list = new LinkedList<MenuNode>();
  wifiStationMenu.list = new LinkedList<MenuNode>();
  foxSortMenu.list = new LinkedList<MenuNode>();
  foxFilterMenu.list = new LinkedList<MenuNode>();
  selectProbeSSIDsMenu.list = new LinkedList<MenuNode>();

  // WiFi HTML menu stuff
  htmlMenu.list = new LinkedList<MenuNode>();
  miniKbMenu.list = new LinkedList<MenuNode>();
  #ifdef HAS_SD
    sdDeleteMenu.list = nullptr;
  #endif

  // Bluetooth menu stuff
  bluetoothSnifferMenu.buildKind = MenuBuildKind::BLUETOOTH_SNIFFER;
  bluetoothAttackMenu.buildKind = MenuBuildKind::BLUETOOTH_ATTACK;
  iBeaconMenu.list = new LinkedList<MenuNode>();
  iBeaconInfoMenu.list = new LinkedList<MenuNode>();
  remoteIdMenu.list = new LinkedList<MenuNode>();

  // Settings stuff
  generateSSIDsMenu.list = new LinkedList<MenuNode>();
  clearSSIDsMenu.list = new LinkedList<MenuNode>();
  clearAPsMenu.list = new LinkedList<MenuNode>();
  saveFileMenu.list = new LinkedList<MenuNode>();

  #ifdef HAS_DIRECT_UPLOAD
    uploadLogsMenu.list = new LinkedList<MenuNode>();
    uploadAllMenu.list = new LinkedList<MenuNode>();
    deleteAllMenu.list = new LinkedList<MenuNode>();
    actionMenu.list = new LinkedList<MenuNode>();
  #endif

  saveSSIDsMenu.list = new LinkedList<MenuNode>();
  loadSSIDsMenu.list = new LinkedList<MenuNode>();
  saveAPsMenu.list = new LinkedList<MenuNode>();
  loadAPsMenu.list = new LinkedList<MenuNode>();
  saveATsMenu.list = new LinkedList<MenuNode>();
  loadATsMenu.list = new LinkedList<MenuNode>();

  evilPortalMenu.list = new LinkedList<MenuNode>();
  apConfigMenu.list = new LinkedList<MenuNode>();
  ssidsMenu.list = new LinkedList<MenuNode>();

  #ifdef HAS_GPS
    gpsPOIMenu.list = new LinkedList<MenuNode>();
  #endif

  foxHuntMenu.list = new LinkedList<MenuNode>();
  reconMenu.buildKind = MenuBuildKind::RECON;

  // Work menu names
  mainMenu.name = text_table1[6];
  reconMenu.name = "Recon";
  wifiMenu.name = text_table1[7];
  deviceMenu.name = text_table1[9];
  failedUpdateMenu.name = text_table1[11];
  confirmMenu.name = text_table1[13];
  updateMenu.name = text_table1[15];
  infoMenu.name = text_table1[17];
  settingsMenu.name = text_table1[18];
  geofenceMenu.name = "Geofences";
  geofenceActionMenu.name = "Geofence";
  #ifdef HAS_MINI_SCREEN
    geofenceRadiusMenu.name = "Radius (miles)";
  #endif
  bluetoothMenu.name = text_table1[19];
  wifiSnifferMenu.name = text_table1[20];
  wifiScannerMenu.name = "Scanners";
  wifiAttackMenu.name = text_table1[21];
  attackModeMenu.name = "Target Mode";
  attackTargetMenu.name = "Choose Targets";
  attackStationMenu.name = "Pick Stations";
  attackReviewMenu.name = "Review Attack";
  wifiGeneralMenu.name = text_table1[22];
  saveFileMenu.name = "Save/Load Files";
  saveSSIDsMenu.name = "Save SSIDs";
  loadSSIDsMenu.name = "Load SSIDs";
  saveAPsMenu.name = "Save APs";
  loadAPsMenu.name = "Load APs";
  saveATsMenu.name = "Save Airtags";
  loadATsMenu.name = "Load Airtags";

  bluetoothSnifferMenu.name = text_table1[23];
  bluetoothAttackMenu.name = "Bluetooth Attacks";
  iBeaconMenu.name = "iBeacon Info";
  iBeaconInfoMenu.name = "iBeacon";
  remoteIdMenu.name = "Remote ID Devices";
  generateSSIDsMenu.name = text_table1[27];
  clearSSIDsMenu.name = text_table1[28];
  clearAPsMenu.name = text_table1[29];
  wifiAPMenu.name = "Select";
  wifiIPMenu.name = "Active IPs";
  apInfoMenu.name = "AP Info";
  setMacMenu.name = "Set MACs";
  genAPMacMenu.name = "Generate AP MAC";
  wifiStationMenu.name = "Select Stations";

  #ifdef HAS_DIRECT_UPLOAD
    uploadLogsMenu.name = "Upload Logs";
    uploadAllMenu.name = "Upload All?";
    deleteAllMenu.name = "Delete All?";
    actionMenu.name = "Destination";
  #endif

  #ifdef HAS_GPS
    gpsMenu.name = "GPS";
    gpsInfoMenu.name = "GPS Data";
    //wardrivingMenu.name = "Wardriving";
  #endif
  htmlMenu.name = "EP HTML List";
  miniKbMenu.name = "Mini Keyboard";

  #ifdef HAS_SD
    sdDeleteMenu.name = "Delete SD Files";
  #endif

  selectProbeSSIDsMenu.name = "Probe Requests";
  evilPortalMenu.name = "Evil Portal";
  apConfigMenu.name = "AP Config";
  ssidsMenu.name = "SSIDs";

  #ifdef HAS_GPS
    gpsPOIMenu.name = "GPS POI";
  #endif

  foxHuntMenu.name = "Fox Hunt";

  // The root navigation menus are populated only while active. Their node
  // storage is released on the next UI cycle after navigation, once the
  // selecting callback has returned.
  mainMenu.parentMenu = NULL;
  reconMenu.parentMenu = &mainMenu;

  // Build WiFi Menu
  wifiMenu.parentMenu = &mainMenu; // Main Menu is second menu parent

  // Build WiFi scanner Menu
  wifiScannerMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent

  // Build WiFi sniffer Menu
  wifiSnifferMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent

  // Build WiFi attack menu
  wifiAttackMenu.parentMenu = &wifiMenu; // Main Menu is second menu parent

  evilPortalMenu.parentMenu = &wifiAttackMenu;
  this->addNodes(&evilPortalMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(evilPortalMenu.parentMenu, true);
  });
  this->addNodes(&evilPortalMenu, "AP Config", TFTMAGENTA, SD_UPDATE, [this]() {
    this->buildApConfigMenu();
  });
  this->addNodes(&evilPortalMenu, "Access Points", TFTGREEN, BEACON_SNIFF, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points->size() == 0) {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        this->buildEvilPortalApMenu();
      });
      return;
    }
    this->buildEvilPortalApMenu();
  });
  this->addNodes(&evilPortalMenu, "User SSIDs", TFTCYAN, PROBE_SNIFF, [this]() {
    this->buildEvilPortalSsidMenu();
  });

  // WiFi General menu is populated on demand.
  wifiGeneralMenu.parentMenu = &wifiMenu;
  // Build once during setup so shared result/target menus retain the same
  // initial Back nodes and parent wiring they had before this refactor, then
  // release only the WiFi General launcher nodes.
  this->buildLazyMenu(&wifiGeneralMenu);
  this->releaseLazyMenu(&wifiGeneralMenu);

#ifdef HAS_BT
  // Bluetooth category menus are populated on demand.
  bluetoothMenu.parentMenu = &mainMenu;
  bluetoothSnifferMenu.parentMenu = &bluetoothMenu;
  bluetoothAttackMenu.parentMenu = &bluetoothMenu;
#endif

  // Device menu
  deviceMenu.parentMenu = &mainMenu;

#ifdef HAS_IR
  irMenu.list = new LinkedList<MenuNode>();
  irTvMenu.list = new LinkedList<MenuNode>();
  irFairyMenu.list = new LinkedList<MenuNode>();
  irLedMenu.list = new LinkedList<MenuNode>();
  irLoopMenu.list = new LinkedList<MenuNode>();

  irMenu.name = "IR Remote";
  irTvMenu.name = "Panasonic TV";
  irFairyMenu.name = "Window Lights";
  irLedMenu.name = "WS611I LED";
  irLoopMenu.name = "IR 3s Loop";

  irMenu.parentMenu = &mainMenu;
  irTvMenu.parentMenu = &irMenu;
  irFairyMenu.parentMenu = &irMenu;
  irLedMenu.parentMenu = &irMenu;
  irLoopMenu.parentMenu = &irMenu;

  // Build irMenu
  this->addNodes(&irMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(irMenu.parentMenu, true);
  });
  this->addNodes(&irMenu, "Window Lights (1-8)", TFTYELLOW, GENERAL_APPS, [this]() {
    this->changeMenu(&irFairyMenu, true);
  });
  this->addNodes(&irMenu, "WS611I LED Strip", TFTGREEN, GENERAL_APPS, [this]() {
    this->changeMenu(&irLedMenu, true);
  });
  this->addNodes(&irMenu, "Panasonic TV", TFTCYAN, GENERAL_APPS, [this]() {
    this->changeMenu(&irTvMenu, true);
  });
  this->addNodes(&irMenu, "IR 3s Loop (Test)", TFTMAGENTA, BEACON_SPAM, [this]() {
    this->changeMenu(&irLoopMenu, true);
  });

  // Build irTvMenu (Panasonic TV)
  this->addNodes(&irTvMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(irTvMenu.parentMenu, true);
  });
  this->addNodes(&irTvMenu, "Power (Вкл/Викл)", TFTRED, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_POWER);
  });
  this->addNodes(&irTvMenu, "Mute (Звук)", TFTORANGE, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_MUTE);
  });
  this->addNodes(&irTvMenu, "Volume +", TFTGREEN, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_VOL_UP);
  });
  this->addNodes(&irTvMenu, "Volume -", TFTGREEN, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_VOL_DOWN);
  });
  this->addNodes(&irTvMenu, "Channel +", TFTCYAN, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_CH_UP);
  });
  this->addNodes(&irTvMenu, "Channel -", TFTCYAN, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_CH_DOWN);
  });
  this->addNodes(&irTvMenu, "Input / AV", TFTYELLOW, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_INPUT);
  });
  this->addNodes(&irTvMenu, "OK / Enter", TFTWHITE, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_OK);
  });
  this->addNodes(&irTvMenu, "Menu", TFTLIGHTGREY, 0, []() {
    ir_blaster_obj.sendPanasonicTV(PANASONIC_CMD_MENU);
  });

  // Build irFairyMenu (Window Curtain 8-mode Garland)
  this->addNodes(&irFairyMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(irFairyMenu.parentMenu, true);
  });
  this->addNodes(&irFairyMenu, "Power ON (Вкл)", TFTGREEN, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_ON);
  });
  this->addNodes(&irFairyMenu, "Power OFF (Викл)", TFTRED, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_OFF);
  });
  this->addNodes(&irFairyMenu, "1: Combination", TFTYELLOW, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_1);
  });
  this->addNodes(&irFairyMenu, "2: In Waves", TFTCYAN, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_2);
  });
  this->addNodes(&irFairyMenu, "3: Sequential", TFTMAGENTA, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_3);
  });
  this->addNodes(&irFairyMenu, "4: Slo-Glo", TFTBLUE, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_4);
  });
  this->addNodes(&irFairyMenu, "5: Chasing", TFTORANGE, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_5);
  });
  this->addNodes(&irFairyMenu, "6: Slow Fade", TFTGREEN, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_6);
  });
  this->addNodes(&irFairyMenu, "7: Twinkle", TFTCYAN, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_7);
  });
  this->addNodes(&irFairyMenu, "8: Steady On", TFTWHITE, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_MODE_8);
  });
  this->addNodes(&irFairyMenu, "Brightness +", TFTYELLOW, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_DIM_UP);
  });
  this->addNodes(&irFairyMenu, "Brightness -", TFTLIGHTGREY, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_DIM_DOWN);
  });
  this->addNodes(&irFairyMenu, "Timer (6h)", TFTORANGE, 0, []() {
    ir_blaster_obj.sendFairyLights(FAIRY_CMD_TIMER);
  });

  // Build irLedMenu (WS611I LED Strip)
  this->addNodes(&irLedMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(irLedMenu.parentMenu, true);
  });
  this->addNodes(&irLedMenu, "Power ON", TFTGREEN, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_POWER_ON);
  });
  this->addNodes(&irLedMenu, "Power OFF", TFTRED, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_POWER_OFF);
  });
  this->addNodes(&irLedMenu, "Red (Червоний)", TFTRED, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_RED);
  });
  this->addNodes(&irLedMenu, "Green (Зелений)", TFTGREEN, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_GREEN);
  });
  this->addNodes(&irLedMenu, "Blue (Синій)", TFTBLUE, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_BLUE);
  });
  this->addNodes(&irLedMenu, "White (Білий)", TFTWHITE, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_WHITE);
  });
  this->addNodes(&irLedMenu, "Flash Effect", TFTYELLOW, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_FLASH);
  });
  this->addNodes(&irLedMenu, "Fade Effect", TFTMAGENTA, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_FADE);
  });
  this->addNodes(&irLedMenu, "Strobe Effect", TFTCYAN, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_STROBE);
  });
  this->addNodes(&irLedMenu, "Brightness +", TFTYELLOW, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_BRIGHT_UP);
  });
  this->addNodes(&irLedMenu, "Brightness -", TFTLIGHTGREY, 0, []() {
    ir_blaster_obj.sendLEDStrip(LED_CMD_BRIGHT_DOWN);
  });

  // Build irLoopMenu (3-Second Loop / Test Mode)
  this->addNodes(&irLoopMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(irLoopMenu.parentMenu, true);
  });
  this->addNodes(&irLoopMenu, "Fairy 3s Loop (1..8)", TFTYELLOW, BEACON_SPAM, [this]() {
    ir_blaster_obj.startLoop(IR_LOOP_FAIRY_LIGHTS, 3000);
    this->updateStatusBar();
  });
  this->addNodes(&irLoopMenu, "LED 3s Loop (Colors)", TFTGREEN, BEACON_SPAM, [this]() {
    ir_blaster_obj.startLoop(IR_LOOP_LED_STRIP, 3000);
    this->updateStatusBar();
  });
  this->addNodes(&irLoopMenu, "TV Power 3s Loop", TFTCYAN, BEACON_SPAM, [this]() {
    ir_blaster_obj.startLoop(IR_LOOP_PANASONIC_TV, 3000);
    this->updateStatusBar();
  });
  this->addNodes(&irLoopMenu, "All Burst 3s Loop", TFTMAGENTA, BEACON_SPAM, [this]() {
    ir_blaster_obj.startLoop(IR_LOOP_UNIVERSAL_ALL, 3000);
    this->updateStatusBar();
  });
  this->addNodes(&irLoopMenu, "STOP Loop", TFTRED, 0, [this]() {
    ir_blaster_obj.stopLoop();
    this->updateStatusBar();
  });
#endif

  // Save Files Menu
  saveFileMenu.parentMenu = &deviceMenu;
  this->addNodes(&saveFileMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(saveFileMenu.parentMenu, true);
  });
  this->addNodes(&saveFileMenu, "Save SSIDs", TFTCYAN, SD_UPDATE, [this]() {
    this->changeMenu(&saveSSIDsMenu, true);
    wifi_scan_obj.RunSaveSSIDList(true);
  });
  this->addNodes(&saveFileMenu, "Load SSIDs", TFTSKYBLUE, SD_UPDATE, [this]() {
    this->changeMenu(&loadSSIDsMenu, true);
    wifi_scan_obj.RunLoadSSIDList();
  });
  this->addNodes(&saveFileMenu, "Save APs", TFTNAVY, SD_UPDATE, [this]() {
    this->changeMenu(&saveAPsMenu, true);
    wifi_scan_obj.RunSaveAPList();
  });
  this->addNodes(&saveFileMenu, "Load APs", TFTBLUE, SD_UPDATE, [this]() {
    this->changeMenu(&loadAPsMenu, true);
    wifi_scan_obj.RunLoadAPList();
  });
  this->addNodes(&saveFileMenu, "Save Airtags", TFTWHITE, SD_UPDATE, [this]() {
    this->changeMenu(&saveAPsMenu, true);
    wifi_scan_obj.RunSaveATList();
  });
  this->addNodes(&saveFileMenu, "Load Airtags", TFTWHITE, SD_UPDATE, [this]() {
    this->changeMenu(&loadAPsMenu, true);
    wifi_scan_obj.RunLoadATList();
  });

  saveSSIDsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(saveSSIDsMenu.parentMenu, true);
  });

  loadSSIDsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(loadSSIDsMenu.parentMenu, true);
  });

  saveAPsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveAPsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(saveAPsMenu.parentMenu, true);
  });

  loadAPsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadAPsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(loadAPsMenu.parentMenu, true);
  });

  saveATsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&saveATsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(saveATsMenu.parentMenu, true);
  });

  loadATsMenu.parentMenu = &saveFileMenu;
  this->addNodes(&loadATsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(loadATsMenu.parentMenu, true);
  });

  // GPS Menu
  #ifdef HAS_GPS
    if (gps_obj.getGpsModuleStatus()) {
      gpsMenu.parentMenu = &mainMenu; // Main Menu is second menu parent

      // GPS POI Menu
      gpsPOIMenu.parentMenu = &gpsMenu;
      this->addNodes(&gpsPOIMenu, text09, TFTLIGHTGREY, 0, [this]() {
        wifi_scan_obj.currentScanMode = GPS_POI;
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        this->changeMenu(gpsPOIMenu.parentMenu, true);
      });
      this->addNodes(&gpsPOIMenu, "Mark POI", TFTCYAN, GPS_MENU, [this]() {
        wifi_scan_obj.currentScanMode = GPS_POI;
        display_obj.tft.setCursor(0, TFT_HEIGHT / 2);
        display_obj.clearScreen();
        if (wifi_scan_obj.RunGPSInfo(true, false, true))
          display_obj.showCenterText("POI Logged", TFT_HEIGHT / 2);
        else
          display_obj.showCenterText("POI Log Failed", TFT_HEIGHT / 2);
        wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
        delay(2000);
        this->changeMenu(&gpsPOIMenu, true);
      });

      // GPS Info Menu
      gpsInfoMenu.parentMenu = &gpsMenu;
      this->addNodes(&gpsInfoMenu, text09, TFTLIGHTGREY, 0, [this]() {
        if(wifi_scan_obj.currentScanMode != GPS_TRACKER)
          wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
        this->changeMenu(gpsInfoMenu.parentMenu, true);
      });
    }
  #endif

  // Settings menu
  // Device menu
  settingsMenu.parentMenu = &deviceMenu;
  this->addNodes(&settingsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    changeMenu(settingsMenu.parentMenu, true);
  });
  for (int i = 0; i < settings_obj.getNumberSettings(); i++) {
    String settingName = settings_obj.setting_index_to_name(i);
    const char* type = this->callSetting(settingName.c_str());
    if (type && strcmp(type, "bool") == 0) {
      const int settings_menu_index = settingsMenu.list->size();
      this->addNodes(&settingsMenu, settingName.c_str(), TFTLIGHTGREY, SETTINGS, [this, settings_menu_index, settingName]() {
          const bool setting_enabled = settings_obj.toggleSetting(settingName.c_str());
          this->callSetting(settingName.c_str());
          #if defined(MARAUDER_POOM) || defined(MARAUDER_LILYGO_T_DISPLAY_S3)
            specSettingMenu.list->clear();
            this->addNodes(&specSettingMenu, text09, TFTLIGHTGREY, 0, [this]() {
              this->changeMenu(&settingsMenu, true);
            });
            const String state_label = String(setting_enabled ? "ON: " : "OFF: ") + settingName;
            this->addNodes(&specSettingMenu, state_label.c_str(),
                           setting_enabled ? TFTGREEN : TFTRED, SETTINGS, []() {},
                           setting_enabled);
          #endif
          this->changeMenu(&specSettingMenu, true);
          this->displaySetting(settingName.c_str(), &settingsMenu, settings_menu_index);
          wifi_scan_obj.force_pmkid = settings_obj.loadSetting<bool>(text_table4[5]);
          wifi_scan_obj.force_probe = settings_obj.loadSetting<bool>(text_table4[6]);
          wifi_scan_obj.save_pcap = settings_obj.loadSetting<bool>(text_table4[7]);
          wifi_scan_obj.ep_deauth = settings_obj.loadSetting<bool>("EPDeauth");
          wifi_scan_obj.channel_hop = settings_obj.loadSetting<bool>("ChanHop");
      }, settings_obj.loadSetting<bool>(settingName.c_str()));
    }
  }

  Serial.println("Finished settings nodes");

  // Specific setting menu
  specSettingMenu.parentMenu = &settingsMenu;
  addNodes(&specSettingMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(specSettingMenu.parentMenu, true);
  });

  // Web Update
  updateMenu.parentMenu = &deviceMenu;

  // Failed update menu
  failedUpdateMenu.parentMenu = &deviceMenu;
  this->addNodes(&failedUpdateMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    this->changeMenu(failedUpdateMenu.parentMenu, true);
  });

  // Device info menu
  infoMenu.parentMenu = &deviceMenu;
  this->addNodes(&infoMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
    this->changeMenu(infoMenu.parentMenu, true);
  });
  iBeaconInfoMenu.parentMenu = &iBeaconMenu;
  this->addNodes(&iBeaconInfoMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(iBeaconInfoMenu.parentMenu, true);
  });

  Serial.println("Changing to main menu...");

  // Set the current menu to the mainMenu
  this->changeMenu(&mainMenu, true);

  this->initTime = millis();
}

String MenuFunctions::geofenceTextInput(const char* title) {
  #ifdef HAS_TOUCH
    char value[40] = {0};
    if (this->textInput(value, sizeof(value), title)) return String(value);
    return "";
  #elif defined(HAS_MINI_KB)
    miniKbMenu.name = title;
    miniKbMenu.parentMenu = &geofenceActionMenu;
    this->changeMenu(&miniKbMenu, true);
    return this->miniKeyboard(&miniKbMenu, true);
  #else
    return "";
  #endif
}

bool MenuFunctions::editGeofence(uint8_t slot, bool use_current_location) {
  GeofenceConfig fence;
  fence.enabled = true;
  fence.name = this->geofenceTextInput("Geofence name");
  fence.name.trim();
  if (fence.name.length() == 0 || fence.name.length() > GEOFENCE_NAME_MAX) return false;

  if (use_current_location) {
    #ifdef HAS_GPS
      if (!gps_obj.getFixStatus()) return false;
      fence.latitude = gps_obj.getLat().toDouble();
      fence.longitude = gps_obj.getLon().toDouble();
    #else
      return false;
    #endif
  } else {
    String latitude = this->geofenceTextInput("Latitude (-90 to 90)");
    String longitude = this->geofenceTextInput("Longitude (-180 to 180)");
    if (latitude.length() == 0 || longitude.length() == 0) return false;
    if (!parseGeofenceNumber(latitude, fence.latitude) || !parseGeofenceNumber(longitude, fence.longitude)) return false;
  }
  String radius = this->geofenceTextInput("Radius miles (0.1-1.0)");
  if (radius.length() == 0) return false;
  double radius_miles = 0;
  if (!parseGeofenceNumber(radius, radius_miles)) return false;
  fence.radiusMiles = radius_miles;
  if (!settings_obj.saveGeofence(slot, fence)) return false;
  wifi_scan_obj.reloadGeofences();
  return true;
}

#ifdef HAS_MINI_SCREEN
void MenuFunctions::beginMiniGeofenceEdit(uint8_t slot, bool use_current_location) {
  GeofenceConfig fence;
  fence.enabled = true;
  fence.name = this->geofenceTextInput("Geofence name");
  fence.name.trim();
  if (fence.name.length() == 0 || fence.name.length() > GEOFENCE_NAME_MAX) {
    this->buildGeofenceActionMenu(slot);
    return;
  }

  if (use_current_location) {
    #ifdef HAS_GPS
      if (!gps_obj.getFixStatus()) {
        display_obj.clearScreen();
        display_obj.showCenterText("GPS fix needed", TFT_HEIGHT / 2);
        delay(1200);
        this->buildGeofenceActionMenu(slot);
        return;
      }
      fence.latitude = gps_obj.getLat().toDouble();
      fence.longitude = gps_obj.getLon().toDouble();
    #else
      this->buildGeofenceActionMenu(slot);
      return;
    #endif
  } else {
    String latitude = this->geofenceTextInput("Latitude (-90 to 90)");
    String longitude = this->geofenceTextInput("Longitude (-180 to 180)");
    if (latitude.length() == 0 || longitude.length() == 0 ||
        !parseGeofenceNumber(latitude, fence.latitude) ||
        !parseGeofenceNumber(longitude, fence.longitude) ||
        fence.latitude < -90.0 || fence.latitude > 90.0 ||
        fence.longitude < -180.0 || fence.longitude > 180.0) {
      display_obj.clearScreen();
      display_obj.showCenterText("Invalid coordinates", TFT_HEIGHT / 2);
      delay(1200);
      this->buildGeofenceActionMenu(slot);
      return;
    }
  }

  this->buildGeofenceRadiusMenu(slot, fence);
}

void MenuFunctions::buildGeofenceRadiusMenu(uint8_t slot, const GeofenceConfig& fence) {
  this->pendingGeofenceSlot = slot;
  this->pendingGeofence = fence;
  geofenceRadiusMenu.list->clear();
  geofenceRadiusMenu.parentMenu = &geofenceActionMenu;

  for (uint8_t step = 1; step <= 10; step++) {
    String label = String(step / 10.0f, 1) + " mi";
    this->addNodes(&geofenceRadiusMenu, label.c_str(), TFTCYAN, GPS_MENU, [this, step]() {
      this->pendingGeofence.radiusMiles = step / 10.0f;
      const bool saved = settings_obj.saveGeofence(this->pendingGeofenceSlot, this->pendingGeofence);
      if (saved) wifi_scan_obj.reloadGeofences();
      display_obj.clearScreen();
      display_obj.showCenterText(saved ? "Geofence saved" : "Unable to save", TFT_HEIGHT / 2);
      delay(1200);
      this->deferGeofenceMenuRefresh();
    });
  }

  this->changeMenu(&geofenceRadiusMenu, false);
}

void MenuFunctions::deferGeofenceMenuRefresh() {
  // Menu callbacks run inside the button event that selected them. Rebuilding
  // immediately lets the remainder of that event redraw stale button objects
  // on Mini displays. Defer the rebuild until the next main UI cycle.
  geofenceMenuRefreshPending = true;
}
#endif

void MenuFunctions::buildGeofenceMenu() {
  geofenceMenu.list->clear();
  geofenceMenu.parentMenu = &deviceMenu;
  this->addNodes(&geofenceMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(geofenceMenu.parentMenu, true);
  });
  for (uint8_t i = 0; i < MAX_GEOFENCES; i++) {
    GeofenceConfig fence;
    const bool configured = settings_obj.loadGeofence(i, fence);
    String label = String(i + 1) + ": " + (configured ? fence.name : "Empty");
    this->addNodes(&geofenceMenu, label.c_str(), configured ? TFTGREEN : TFTLIGHTGREY, GPS_MENU, [this, i]() {
      this->buildGeofenceActionMenu(i);
    });
  }
  // A full display reinitialization clears keyboard/status font state and
  // guarantees newly persisted labels are visible immediately.
  this->changeMenu(&geofenceMenu, false);
}

void MenuFunctions::buildGeofenceActionMenu(uint8_t slot) {
  this->selectedGeofence = slot;
  geofenceActionMenu.list->clear();
  geofenceActionMenu.parentMenu = &geofenceMenu;
  this->addNodes(&geofenceActionMenu, text09, TFTLIGHTGREY, 0, [this]() { this->buildGeofenceMenu(); });
  this->addNodes(&geofenceActionMenu, "Enter coordinates", TFTCYAN, GPS_MENU, [this, slot]() {
    #ifdef HAS_MINI_SCREEN
      this->beginMiniGeofenceEdit(slot, false);
    #else
    const bool saved = this->editGeofence(slot, false);
    display_obj.clearScreen();
    display_obj.showCenterText(saved ? "Geofence saved" : "Invalid or cancelled", TFT_HEIGHT / 2);
    delay(1200);
    this->buildGeofenceMenu();
    #endif
  });
  #ifdef HAS_GPS
    this->addNodes(&geofenceActionMenu, "Use current GPS", TFTGREEN, GPS_MENU, [this, slot]() {
      #ifdef HAS_MINI_SCREEN
        this->beginMiniGeofenceEdit(slot, true);
      #else
      const bool saved = this->editGeofence(slot, true);
      display_obj.clearScreen();
      display_obj.showCenterText(saved ? "Geofence saved" : "GPS fix/input needed", TFT_HEIGHT / 2);
      delay(1200);
      this->buildGeofenceMenu();
      #endif
    });
  #endif
  GeofenceConfig fence;
  if (settings_obj.loadGeofence(slot, fence)) {
    this->addNodes(&geofenceActionMenu, "Clear geofence", TFTRED, CLEAR_ICO, [this, slot]() {
      settings_obj.clearGeofence(slot);
      wifi_scan_obj.reloadGeofences();
      #ifdef HAS_MINI_SCREEN
        this->deferGeofenceMenuRefresh();
      #else
        this->buildGeofenceMenu();
      #endif
    });
  }
  this->changeMenu(&geofenceActionMenu, true);
}

//#if (!defined(HAS_ILI9341) && defined(HAS_BUTTONS))
#ifdef HAS_MINI_KB
  String MenuFunctions::miniKeyboard(Menu * targetMenu, bool do_pass) {
    // Prepare a char array and reset temp SSID string
    extern LinkedList<ssid>* ssids;

    String ret_val = "";

    bool pressed = true;

    wifi_scan_obj.current_mini_kb_ssid = "";

    #ifdef MARAUDER_POOM
      // Compact six-button editor for POOM. The generic Mini keyboard help
      // consumes more than 64 pixels vertically and has no Back-button path.
      this->mini_kb_index = 0;
      const uint16_t alphabet_length = wifi_scan_obj.alfa.length();
      bool redraw = true;

      while (true) {
        if (l_btn.justPressed()) {
          this->mini_kb_index = this->mini_kb_index > 0
                                  ? this->mini_kb_index - 1 : alphabet_length - 1;
          redraw = true;
        }
        if (r_btn.justPressed()) {
          this->mini_kb_index = (this->mini_kb_index + 1) % alphabet_length;
          redraw = true;
        }
        if (u_btn.justPressed()) {
          if (wifi_scan_obj.current_mini_kb_ssid.length())
            wifi_scan_obj.current_mini_kb_ssid.remove(
                wifi_scan_obj.current_mini_kb_ssid.length() - 1);
          redraw = true;
        }
        if (d_btn.justPressed()) {
          wifi_scan_obj.current_mini_kb_ssid.concat(
              wifi_scan_obj.alfa.charAt(this->mini_kb_index));
          redraw = true;
        }
        if (b_btn.justPressed()) {
          while (!b_btn.justReleased()) delay(1);
          this->changeMenu(targetMenu->parentMenu, true);
          return "";
        }
        if (c_btn.justPressed()) {
          bool submit = false;
          while (!c_btn.justReleased()) {
            c_btn.justPressed();
            if (c_btn.isHeld()) {
              submit = true;
              break;
            }
            delay(1);
          }
          if (submit) {
            while (!c_btn.justReleased()) delay(1);
            this->changeMenu(targetMenu->parentMenu, true);
            return wifi_scan_obj.current_mini_kb_ssid;
          }
          wifi_scan_obj.current_mini_kb_ssid.concat(
              wifi_scan_obj.alfa.charAt(this->mini_kb_index));
          redraw = true;
        }

        if (redraw) {
          display_obj.clearScreen();
          display_obj.tft.setFreeFont(NULL);
          display_obj.tft.setTextSize(1);
          display_obj.tft.setTextWrap(false);
          display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
          String title = targetMenu->name;
          if (title.length() > 21) title = title.substring(0, 21);
          display_obj.tft.drawString(title, 0, 0, 1);

          String visible = wifi_scan_obj.current_mini_kb_ssid;
          if (visible.length() > 21) visible = visible.substring(visible.length() - 21);
          display_obj.tft.drawString(visible + "_", 0, 10, 1);
          display_obj.tft.drawString("Char: [" + String(wifi_scan_obj.alfa.charAt(this->mini_kb_index)) + "]", 0, 22, 1);
          display_obj.tft.drawString("L/R:char D:add", 0, 34, 1);
          display_obj.tft.drawString("U:del A:add/hold OK", 0, 44, 1);
          display_obj.tft.drawString("B:cancel", 0, 54, 1);
          // miniKeyboard() is a blocking input loop, so the normal POOM flush
          // at the end of loop() cannot run while this editor is open.
          display_obj.tft.display(true);
          redraw = false;
        }
        delay(1);
      }
    #endif

    #ifdef HAS_MINI_KB
      if (c_btn.isHeld()) {
        while (!c_btn.justReleased())
          delay(1);
      }
    #endif

    int str_len = wifi_scan_obj.alfa.length() + 1;

    char char_array[str_len];

    wifi_scan_obj.alfa.toCharArray(char_array, str_len);

    #ifdef HAS_TOUCH
      uint16_t t_x = 0, t_y = 0;

    #endif

    // Button loop until hold center button
    #ifdef HAS_BUTTONS
      //#if !(defined(MARAUDER_V6) || defined(MARAUDER_V6_1) || defined(MARAUDER_CYD_MICRO))
        while(true) {
          // Keyboard functions for switch hardware
          #ifdef HAS_MINI_KB
            // Cycle char previous
            #ifdef HAS_L
              if ((l_btn.justPressed()) || (l_btn.isHeld())) {
                pressed = true;
                if (this->mini_kb_index > 0)
                  this->mini_kb_index--;
                else
                  this->mini_kb_index = str_len - 2;

                targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                this->buildButtons(targetMenu);

                while (!l_btn.justReleased()) {
                  l_btn.justPressed();
                  if (!l_btn.isHeld())
                    delay(1);
                  else
                    break;
                }
              }
            #endif

            // Cycle char next
            #ifdef HAS_R
              if ((r_btn.justPressed()) || (r_btn.isHeld())) {
                pressed = true;
                if (this->mini_kb_index < str_len - 2)
                  this->mini_kb_index++;
                else
                  this->mini_kb_index = 0;

                targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                this->buildButtons(targetMenu, 0, &char_array[this->mini_kb_index]);

                while (!r_btn.justReleased()) {
                  r_btn.justPressed();
                  if (!r_btn.isHeld())
                    delay(1);
                  else
                    break;
                }
              }
            #endif

            //// 5-WAY SWITCH STUFF
            // Add character
            #if (defined(HAS_D) && defined(HAS_R))
              if (d_btn.justPressed()) {
                pressed = true;
                wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
                while (!d_btn.justReleased())
                  delay(1);
              }
            #endif

            // Remove character
            #if (defined(HAS_U) && defined(HAS_L))
              if (u_btn.justPressed()) {
                pressed = true;
                wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
                while (!u_btn.justReleased())
                  delay(1);
              }
            #endif

            //// PARTIAL SWITCH STUFF
            // Advance char or add char
            #if (defined(HAS_D) && !defined(HAS_R))
              if (d_btn.justPressed()) {
                bool was_held = false;
                pressed = true;
                while(!d_btn.justReleased()) {
                  d_btn.justPressed();

                  // Add letter to string
                  if (d_btn.isHeld()) {
                    wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
                    was_held = true;
                    break;
                  }
                }
                if (!was_held) {
                  if (this->mini_kb_index < str_len - 2)
                    this->mini_kb_index++;
                  else
                    this->mini_kb_index = 0;

                  targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                  this->buildButtons(targetMenu, 0, &char_array[this->mini_kb_index]);
                }
              }
            #endif

            // Prev char or remove char
            #if (defined(HAS_U) && !defined(HAS_L))
              if (u_btn.justPressed()) {
                bool was_held = false;
                pressed = true;
                while(!u_btn.justReleased()) {
                  u_btn.justPressed();

                  // Remove letter from string
                  if (u_btn.isHeld()) {
                    wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
                    was_held = true;
                    break;
                  }
                }
                if (!was_held) {
                  if (this->mini_kb_index > 0)
                    this->mini_kb_index--;
                  else
                    this->mini_kb_index = str_len - 2;

                  targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                  this->buildButtons(targetMenu);
                }
              }
            #endif

            // Add SSID
            #if defined(HAS_C) && !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV)
              if (c_btn.justPressed()) {
                while (!c_btn.justReleased()) {
                  c_btn.justPressed(); // Need to continue updating button hold status. My shitty library.

                  // Exit
                  if (c_btn.isHeld()) {
                    this->changeMenu(targetMenu->parentMenu);
                    return wifi_scan_obj.current_mini_kb_ssid;
                  }
                  delay(1);
                }

                if (!do_pass) {
                // If we have a string, add it to list of SSIDs
                  if (wifi_scan_obj.current_mini_kb_ssid != "") {
                    pressed = true;
                    ssid s = {wifi_scan_obj.current_mini_kb_ssid, random(1, 12), {random(256), random(256), random(256), random(256), random(256), random(256)}, false};
                    ssids->unshift(s);
                    wifi_scan_obj.current_mini_kb_ssid = "";
                  }
                }
              }
            #endif
          #endif

          #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV) || defined(HAS_FACES_KEYBOARD)
            for (int i = 0; i < 95; i++) {
              if ((marauder_keyboard._ascii_list[i] != '(') &&
                  (marauder_keyboard._ascii_list[i] != '`')) {
                if (this->isKeyPressed(marauder_keyboard._ascii_list[i])) {
                  pressed = true;
                  wifi_scan_obj.current_mini_kb_ssid.concat(marauder_keyboard._ascii_list[i]);
                }
                if (this->isKeyPressed(KEY_BACKSPACE)) {
                  pressed = true;
                  wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
                }
              }
            }

            if (!do_pass) {
              if (this->isKeyPressed('`')) {
                this->changeMenu(targetMenu->parentMenu, true);
                return wifi_scan_obj.current_mini_kb_ssid;
              }

              if (this->isKeyPressed('(')) {
                if (!do_pass) {
                  if (wifi_scan_obj.current_mini_kb_ssid != "") {
                    pressed = true;
                    ssid s = {wifi_scan_obj.current_mini_kb_ssid, random(1, 12), {random(256), random(256), random(256), random(256), random(256), random(256)}, false};
                    ssids->unshift(s);
                    wifi_scan_obj.current_mini_kb_ssid = "";
                  }
                }
              }
            }
            else {
              if (this->isKeyPressed('(')) {
                this->changeMenu(targetMenu->parentMenu, true);
                return wifi_scan_obj.current_mini_kb_ssid;
              }

              if (this->isKeyPressed('`')) {
                this->changeMenu(targetMenu->parentMenu, true);
                return "";
              }
            }

          #endif

          // Keyboard functions for touch hardware
          #ifdef HAS_TOUCH
            bool touched = display_obj.updateTouch(&t_x, &t_y);

            uint8_t menu_button = display_obj.menuButton(&t_x, &t_y, touched);

            // Cycle char previous
            if (menu_button == UP_BUTTON) {
              pressed = true;
              if (this->mini_kb_index > 0)
                this->mini_kb_index--;
              else
                this->mini_kb_index = str_len - 2;

              targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
              this->buildButtons(targetMenu);
              while (display_obj.updateTouch(&t_x, &t_y) > 0)
                delay(1);
              display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y));
            }

            // Cycle char next
            if (menu_button == DOWN_BUTTON) {
              pressed = true;
              if (this->mini_kb_index < str_len - 2)
                this->mini_kb_index++;
              else
                this->mini_kb_index = 0;

              targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
              this->buildButtons(targetMenu, 0, &char_array[this->mini_kb_index]);
              while (display_obj.updateTouch(&t_x, &t_y) > 0)
                delay(1);
              display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y));
            }

            //// 5-WAY SWITCH STUFF
            // Add character when select button is pressed
            if (menu_button == SELECT_BUTTON) {
              pressed = true;
              wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
              while (display_obj.updateTouch(&t_x, &t_y) > 0)
                delay(1);
              display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y));
            }

            // Remove character when select button is held
            if ((display_obj.isTouchHeld()) && (display_obj.menuButton(&t_x, &t_y, touched, true) == SELECT_BUTTON)) {
              pressed = true;
              wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
              while (display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y)) < 0)
                delay(1);
            }

            //// PARTIAL SWITCH STUFF
            // Advance char or add char
            #if (defined(HAS_D) && !defined(HAS_R))
              if (d_btn.justPressed()) {
                bool was_held = false;
                pressed = true;
                while(!d_btn.justReleased()) {
                  d_btn.justPressed();

                  // Add letter to string
                  if (d_btn.isHeld()) {
                    wifi_scan_obj.current_mini_kb_ssid.concat(String(char_array[this->mini_kb_index]).c_str());
                    was_held = true;
                    break;
                  }
                }
                if (!was_held) {
                  if (this->mini_kb_index < str_len - 2)
                    this->mini_kb_index++;
                  else
                    this->mini_kb_index = 0;

                  targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                  this->buildButtons(targetMenu, 0, &char_array[this->mini_kb_index]);
                }
              }
            #endif

            // Prev char or remove char
            #if (defined(HAS_U) && !defined(HAS_L))
              if (u_btn.justPressed()) {
                bool was_held = false;
                pressed = true;
                while(!u_btn.justReleased()) {
                  u_btn.justPressed();

                  // Remove letter from string
                  if (u_btn.isHeld()) {
                    wifi_scan_obj.current_mini_kb_ssid.remove(wifi_scan_obj.current_mini_kb_ssid.length() - 1);
                    was_held = true;
                    break;
                  }
                }
                if (!was_held) {
                  if (this->mini_kb_index > 0)
                    this->mini_kb_index--;
                  else
                    this->mini_kb_index = str_len - 2;

                  targetMenu->list->set(0, MenuNode{String(char_array[this->mini_kb_index]).c_str(), false, TFTCYAN, 0, true, NULL});
                  this->buildButtons(targetMenu);
                }
              }
            #endif

            // Exit if UP button is held
            if ((display_obj.isTouchHeld()) && (display_obj.menuButton(&t_x, &t_y, touched, true) == UP_BUTTON)) {
              display_obj.clearScreen();
              while (display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y)) < 0)
                delay(1);

              // Reset the touch keys so we don't activate the keys when we go back
              display_obj.menuButton(&t_x, &t_y, display_obj.updateTouch(&t_x, &t_y));
              this->changeMenu(targetMenu->parentMenu, true);
              return wifi_scan_obj.current_mini_kb_ssid;
            }

            // If the screen is touched but none of the keys are used, don't refresh display
            if (menu_button < 0)
              pressed = false;

          #endif

          // Display info on screen
          if (pressed) {
            this->displayCurrentMenu();
            display_obj.tft.setTextWrap(false);
            display_obj.tft.fillRect(0, SCREEN_HEIGHT / 3, SCREEN_WIDTH, STATUS_BAR_WIDTH, TFT_BLACK);
            display_obj.tft.fillRect(0, SCREEN_HEIGHT / 3 + TEXT_HEIGHT * 2, SCREEN_WIDTH, STATUS_BAR_WIDTH, TFT_BLACK);
            display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
            display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
            display_obj.tft.println(screenName(wifi_scan_obj.current_mini_kb_ssid) + "\n");
            display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);

            display_obj.tft.println(screenName(ssids->get(0).essid));

            display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
            #ifdef HAS_MINI_KB
              #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV)
              display_obj.tft.println("U/D - Rem/Add Char");
              display_obj.tft.println("L/R - Prev/Nxt Char");
              #endif
              if (!do_pass) {
                #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
                  display_obj.tft.println("Enter - Save");
                  display_obj.tft.println("Esc - Exit");
                #else
                  display_obj.tft.println("C - Save");
                  display_obj.tft.println("C(Hold) - Exit");
                #endif
              }
              else {
                #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
                  display_obj.tft.println("Enter - Enter");
                #else
                  display_obj.tft.println("C(Hold) - Enter");
                #endif
              }
            #endif

            #ifdef HAS_TOUCH
              display_obj.tft.println("U/D - Prev/Nxt Char");
              display_obj.tft.println("C - Add Char");
              display_obj.tft.println("C(Hold) - Rem Char");
              display_obj.tft.println("U(Hold) - Enter");
            #endif
            pressed = false;
          }
        }
      //#endif
    #endif
  }
#endif

void MenuFunctions::releaseSavedWifiMenu() {
  if (savedWifiMenu.list != nullptr) {
    savedWifiMenu.list->clear();
    delete savedWifiMenu.list;
    savedWifiMenu.list = nullptr;
  }
}

void MenuFunctions::buildSavedWifiMenu(bool replace_mode) {
  this->releaseSavedWifiMenu();
  savedWifiMenu.list = new LinkedList<MenuNode>();
  savedWifiMenu.selected = 0;
  savedWifiMenu.parentMenu = &wifiGeneralMenu;
  savedWifiMenu.name = replace_mode ? "Replace Saved WiFi" : "Saved WiFi";

  this->addNodes(&savedWifiMenu, replace_mode ? "Cancel" : text09, TFTLIGHTGREY, 0, [this, replace_mode]() {
    if (replace_mode)
      wifi_scan_obj.discardPendingWifiCredential();
    this->changeMenu(savedWifiMenu.parentMenu, true);
    saved_wifi_release_pending = true;
  });

  const uint8_t count = settings_obj.getSavedWifiCount();
  for (uint8_t index = 0; index < count; index++) {
    String ssid;
    String password;
    if (!settings_obj.loadSavedWifiCredential(index, ssid, password))
      continue;
    String label = replace_mode ? "Replace " + screenName(ssid) : "Remove " + screenName(ssid);
    this->addNodes(&savedWifiMenu, label.c_str(), replace_mode ? TFTORANGE : TFTRED, 0, [this, index, replace_mode]() {
      if (replace_mode)
        wifi_scan_obj.savePendingWifiCredential(index);
      else
        settings_obj.removeSavedWifiCredential(index);

      // Leave before freeing the node whose callback is currently running.
      // The menu is rebuilt on demand, so another profile can be removed by
      // reopening Manage Saved WiFi.
      this->changeMenu(savedWifiMenu.parentMenu, true);
      saved_wifi_release_pending = true;
    });
  }
}

void MenuFunctions::setupSDFileList(bool update) {
  resetOwnedList(sd_obj.sd_files);

  if (!update)
    sd_obj.listDirToLinkedList(sd_obj.sd_files);
  else
    sd_obj.listDirToLinkedList(sd_obj.sd_files, "/", ".bin");
}

void MenuFunctions::buildSDFileMenu(bool update) {
  if (!update) {
    this->buildSDDeleteBrowser("/", true);
    return;
  }

  this->setupSDFileList(update);

  resetOwnedList(sdDeleteMenu.list);

  sdDeleteMenu.name = "Bin Files";

  this->addNodes(&sdDeleteMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(sdDeleteMenu.parentMenu, true);
  });

  for (int x = 0; x < sd_obj.sd_files->size(); x++) {
    this->addNodes(&sdDeleteMenu, sd_obj.sd_files->get(x).c_str(), TFTCYAN, SD_UPDATE, [this, x]() {
      wifi_scan_obj.currentScanMode = OTA_UPDATE;
      this->changeMenu(&failedUpdateMenu, true);
      sd_obj.runUpdate("/" + sd_obj.sd_files->get(x));
    });
  }
}

String MenuFunctions::parentSDPath(const String& path) const {
  if (path == "/")
    return "/";

  int slash = path.lastIndexOf('/');
  return slash <= 0 ? "/" : path.substring(0, slash);
}

bool MenuFunctions::isSDFileSelected(const String& path) const {
  if (sd_delete_selection == nullptr)
    return false;

  for (int i = 0; i < sd_delete_selection->size(); i++) {
    if (sd_delete_selection->get(i) == path)
      return true;
  }
  return false;
}

void MenuFunctions::toggleSDDeleteSelection(const String& path) {
  for (int i = 0; i < sd_delete_selection->size(); i++) {
    if (sd_delete_selection->get(i) == path) {
      sd_delete_selection->remove(i);
      return;
    }
  }
  sd_delete_selection->add(path);
}

void MenuFunctions::ensureSDDeleteBrowserResources() {
  if (sdDeleteMenu.list == nullptr)
    sdDeleteMenu.list = new LinkedList<MenuNode>();
  if (sd_browser_entries == nullptr)
    sd_browser_entries = new LinkedList<SDDirectoryEntry>();
  if (sd_delete_selection == nullptr)
    sd_delete_selection = new LinkedList<String>();
  sd_browser_release_pending = false;
}

void MenuFunctions::releaseSDDeleteBrowserResources() {
  if (sdDeleteMenu.list != nullptr) {
    sdDeleteMenu.list->clear();
    delete sdDeleteMenu.list;
    sdDeleteMenu.list = nullptr;
  }
  if (sd_browser_entries != nullptr) {
    sd_browser_entries->clear();
    delete sd_browser_entries;
    sd_browser_entries = nullptr;
  }
  if (sd_delete_selection != nullptr) {
    sd_delete_selection->clear();
    delete sd_delete_selection;
    sd_delete_selection = nullptr;
  }
  sd_browser_path = "/";
  sd_browser_release_pending = false;
}

void MenuFunctions::buildSDDeleteBrowser(const String& path, bool reset_selection) {
  this->ensureSDDeleteBrowserResources();

  if (reset_selection)
    sd_delete_selection->clear();

  sd_browser_path = path;
  sd_browser_entries->clear();
  sd_obj.listDirectory(path, sd_browser_entries);

  sdDeleteMenu.list->clear();
  delete sdDeleteMenu.list;
  sdDeleteMenu.list = new LinkedList<MenuNode>();
  sdDeleteMenu.selected = 0;
  sdDeleteMenu.name = path == "/" ? "SD File Browser" : path;

  this->addNodes(&sdDeleteMenu, text09, TFTLIGHTGREY, 0, [this, path]() {
    if (path == "/") {
      sd_delete_selection->clear();
      this->changeMenu(sdDeleteMenu.parentMenu, true);
      return;
    }
    this->buildSDDeleteBrowser(this->parentSDPath(path));
    this->changeMenu(&sdDeleteMenu, true);
  });

  if (path == "/") {
    this->addNodes(&sdDeleteMenu, "Delete Selected", TFTRED, 0, [this]() {
      display_obj.clearScreen();
      display_obj.tft.setTextWrap(false);
      display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      int deleted = 0;
      for (int i = 0; i < sd_delete_selection->size(); i++) {
        String selected_path = sd_delete_selection->get(i);
        if (sd_obj.removeFile(selected_path)) {
          Serial.println("Deleted " + selected_path);
          deleted++;
        }
        else {
          Serial.println("Could not delete " + selected_path);
        }
      }
      sd_delete_selection->clear();
      display_obj.tft.println("Deleted " + String(deleted) + " file(s)");
      delay(1000);
      this->buildSDDeleteBrowser("/");
      this->changeMenu(&sdDeleteMenu, true);
    });

    this->addNodes(&sdDeleteMenu, "Deselect All", TFTYELLOW, 0, [this]() {
      sd_delete_selection->clear();
      for (int index = 0; index < current_menu->list->size(); index++) {
        MenuNode node = current_menu->list->get(index);
        node.selected = false;
        current_menu->list->set(index, node);
      }
      this->buildButtons(current_menu, menu_start_index);
      this->displayCurrentMenu(menu_start_index);
    });
  }

  for (int i = 0; i < sd_browser_entries->size(); i++) {
    SDDirectoryEntry entry = sd_browser_entries->get(i);
    if (entry.is_directory) {
      String label = "[DIR] " + entry.name;
      this->addNodes(&sdDeleteMenu, label.c_str(), TFTYELLOW, SD_UPDATE, [this, entry]() {
        this->buildSDDeleteBrowser(entry.path);
        this->changeMenu(&sdDeleteMenu, true);
      });
    }
    else {
      this->addNodes(
        &sdDeleteMenu,
        entry.name.c_str(),
        TFTCYAN,
        SD_UPDATE,
        [this, entry]() {
          this->toggleSDDeleteSelection(entry.path);
          MenuNode node = current_menu->list->get(current_menu->selected);
          node.selected = this->isSDFileSelected(entry.path);
          current_menu->list->set(current_menu->selected, node);
        },
        this->isSDFileSelected(entry.path)
      );
    }
  }
}



void MenuFunctions::buildWiFiGeneralMenu()
{
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  extern LinkedList<IPAddress>* ipList;
  extern LinkedList<ProbeReqSsid>* probe_req_ssids;
  extern LinkedList<ssid>* ssids;

  Menu* dependent_menus[] = {
    &selectProbeSSIDsMenu, &clearSSIDsMenu, &clearAPsMenu, &htmlMenu,
    &miniKbMenu, &wifiAPMenu, &apInfoMenu, &wifiIPMenu,
    &wifiStationMenu, &ssidsMenu, &setMacMenu, &genAPMacMenu,
    &generateSSIDsMenu
    #ifdef HAS_DIRECT_UPLOAD
      , &uploadAllMenu, &deleteAllMenu, &actionMenu
    #endif
  };
  constexpr size_t dependent_count = sizeof(dependent_menus) / sizeof(dependent_menus[0]);
  int dependent_sizes[dependent_count] = {};
  if (wifi_general_dependents_initialized) {
    for (size_t i = 0; i < dependent_count; i++)
      dependent_sizes[i] = dependent_menus[i]->list ? dependent_menus[i]->list->size() : 0;
  }
  // Build WiFi General menu
  wifiGeneralMenu.parentMenu = &wifiMenu;
  this->addNodes(&wifiGeneralMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(wifiGeneralMenu.parentMenu, true);
  });
  this->addNodes(&wifiGeneralMenu, text_table1[27], TFTSKYBLUE, GENERATE, [this]() {
    this->changeMenu(&generateSSIDsMenu, true);
    wifi_scan_obj.RunGenerateSSIDs();
  });

	//Add Select probe ssid
  this->addNodes(&wifiGeneralMenu, text_table1[65], TFTCYAN, KEYBOARD_ICO, [this]() {
    selectProbeSSIDsMenu.list->clear();

    // Add the back button
    this->addNodes(&selectProbeSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(&wifiGeneralMenu, true);

      // TODO: TBD - Should probe_req_ssids have it´s own life and override ap.config and/or ssids -list for EP?
      // If so, then we should not add selected ssids to ssids list

      // Add selected ssid names to ssids list when clicking back button
      if (probe_req_ssids->size() > 0) {

        //TODO: TBD - Clear ssids list before adding new ones??

        for (int i = 0; i < probe_req_ssids->size(); i++) {
          ProbeReqSsid cur_probe_ssid = probe_req_ssids->get(i);
          if (cur_probe_ssid.selected) {
            bool ssidExists = false;
            for (int i = 0; i < ssids->size(); i++) {
              if (ssids->get(i).essid == cur_probe_ssid.essid) {
                ssidExists = true;
                break;
              }
            }
            if (!ssidExists) {
              wifi_scan_obj.addSSID(cur_probe_ssid.essid);
            }
          }
        }
      }
    });

    // Populate the menu with buttons
    if (probe_req_ssids->size() == 0) {
      this->addNodes(&selectProbeSSIDsMenu, "+ Sniff Probes Now", TFTGREEN, PROBE_SNIFF, [this]() {
        startScanWithReturn(WIFI_SCAN_PROBE, TFT_CYAN, [this]() {
          this->changeMenu(&wifiGeneralMenu, true);
        });
      });
      this->addNodes(&selectProbeSSIDsMenu, "(No Probes Sniffed)", TFTYELLOW, 255, []() {});
    } else {
      this->addNodes(&selectProbeSSIDsMenu, "+ Sniff More Probes", TFTLIME, PROBE_SNIFF, [this]() {
        startScanWithReturn(WIFI_SCAN_PROBE, TFT_CYAN, [this]() {
          this->changeMenu(&wifiGeneralMenu, true);
        });
      });
      for (int i = 0; i < probe_req_ssids->size(); i++) {
        ProbeReqSsid cur_ssid = probe_req_ssids->get(i);
        // This is the menu node
        String button_name = "[" + String(cur_ssid.requests) + "]" + screenName(cur_ssid.essid);
        this->addNodes(
          &selectProbeSSIDsMenu,
          button_name.c_str(),
          TFTCYAN,
          255,
          [this, i]() {
            ProbeReqSsid new_ssid = probe_req_ssids->get(i);
            new_ssid.selected = !probe_req_ssids->get(i).selected;

            // Change selection status of menu node
            MenuNode new_node = current_menu->list->get(i + 1);
            new_node.selected = !current_menu->list->get(i + 1).selected;
            current_menu->list->set(i + 1, new_node);

            probe_req_ssids->set(i, new_ssid);
          },
          probe_req_ssids->get(i).selected);
      }
    }
    this->changeMenu(&selectProbeSSIDsMenu, true);
  });

  clearSSIDsMenu.parentMenu = &wifiGeneralMenu;

  #ifdef HAS_TOUCH
    this->addNodes(&wifiGeneralMenu, text_table1[1], TFTNAVY, KEYBOARD_ICO, [this](){
      char ssidBuf[64] = {0};
      bool keep_going = true;
      while (keep_going) {
        display_obj.clearScreen();
        if (this->textInput(ssidBuf, sizeof(ssidBuf), "Enter SSID")) {
          if (ssidBuf[0] != 0)
            wifi_scan_obj.addSSID(String(ssidBuf));
          #ifdef HAS_FACES_KEYBOARD
            if (marauder_keyboard.available()) keep_going = false;
          #endif
          for (int i = 0; i < 64; i++)
            ssidBuf[i] = NULL;
        }
        else
          keep_going = false;
      }

      this->changeMenu(current_menu);
    });
  #endif
  #if (!defined(HAS_TOUCH) && defined(HAS_BUTTONS))
    this->addNodes(&wifiGeneralMenu, text_table1[1], TFTNAVY, KEYBOARD_ICO, [this](){
      char ssidBuf[64] = {0};
      if (this->textInput(ssidBuf, sizeof(ssidBuf), "Enter SSID")) {
        if (ssidBuf[0] != 0)
          wifi_scan_obj.addSSID(String(ssidBuf));
      }
      this->changeMenu(current_menu, true);
    });
  #endif
  this->addNodes(&wifiGeneralMenu, text_table1[28], TFTSILVER, CLEAR_ICO, [this]() {
    this->changeMenu(&clearSSIDsMenu, true);
    wifi_scan_obj.RunClearSSIDs();
  });
  this->addNodes(&wifiGeneralMenu, text_table1[29], TFTDARKGREY, CLEAR_ICO, [this]() {
    this->changeMenu(&clearAPsMenu, true);
    wifi_scan_obj.RunClearAPs();
  });
  this->addNodes(&wifiGeneralMenu, text_table1[60], TFTBLUE, CLEAR_ICO, [this]() {
    this->changeMenu(&clearAPsMenu, true);
    wifi_scan_obj.RunClearStations();
  });
  //#else // Mini EP HTML select
    this->addNodes(&wifiGeneralMenu, "Select EP HTML File", TFTCYAN, KEYBOARD_ICO, [this](){
      // Add the back button
      htmlMenu.list->clear();
        this->addNodes(&htmlMenu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(htmlMenu.parentMenu, true);
      });

      // Populate the menu with buttons
      for (int i = 0; i < evil_portal_obj.html_files->size(); i++) {
        // This is the menu node
        this->addNodes(&htmlMenu, evil_portal_obj.html_files->get(i).c_str(), TFTCYAN, 255, [this, i](){
          evil_portal_obj.selected_html_index = i;
          evil_portal_obj.target_html_name = evil_portal_obj.html_files->get(evil_portal_obj.selected_html_index);
          Serial.println("Set Evil Portal HTML as " + evil_portal_obj.target_html_name);
          evil_portal_obj.using_serial_html = false;
          this->changeMenu(htmlMenu.parentMenu, true);
          return;
        });
      }
      this->changeMenu(&htmlMenu, true);
    });

    //#if (!defined(HAS_ILI9341) && defined(HAS_BUTTONS))
      miniKbMenu.parentMenu = &wifiGeneralMenu;
      #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV)
        this->addNodes(&miniKbMenu, "a", TFTCYAN, 0, [this]() {
          this->changeMenu(miniKbMenu.parentMenu, true);
        });
      #endif
    //#endif

    htmlMenu.parentMenu = &wifiGeneralMenu;
    this->addNodes(&htmlMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(htmlMenu.parentMenu, true);
    });

    // Select APs on Mini
    this->addNodes(&wifiGeneralMenu, "Select APs", TFTNAVY, KEYBOARD_ICO, [this](){
      buildSelectAPsMenu();
      wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
    });

    this->addNodes(&wifiGeneralMenu, "View AP Info", TFTCYAN, KEYBOARD_ICO, [this](){
      buildViewApInfoMenu();
      wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
    });

    apInfoMenu.parentMenu = &wifiAPMenu;
    this->addNodes(&apInfoMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(apInfoMenu.parentMenu, true);
    });

    wifiAPMenu.parentMenu = &wifiGeneralMenu;
    this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(wifiAPMenu.parentMenu, true);
    });

    wifiIPMenu.parentMenu = &wifiScannerMenu;
    this->addNodes(&wifiIPMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(wifiIPMenu.parentMenu, true);
    });

    // Select Stations on Mini v2
    this->addNodes(&wifiGeneralMenu, "Select Stations", TFTCYAN, KEYBOARD_ICO, [this](){
      buildSelectStationsMenu();
      wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
    });

    this->addNodes(&wifiGeneralMenu, "Join WiFi", TFTWHITE, KEYBOARD_ICO, [this](){
      extern LinkedList<AccessPoint>* access_points;
      if (access_points->size() == 0) {
        startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
          this->changeMenu(&wifiGeneralMenu, true);
        });
        return;
      }
      wifiAPMenu.parentMenu = &wifiGeneralMenu;

      // Add the back button
      wifiAPMenu.list->clear();
      this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(wifiAPMenu.parentMenu, true);
      });

      if (access_points->size() == 0) {
        this->addNodes(&wifiAPMenu, "+ Scan APs Now", TFTGREEN, BEACON_SNIFF, [this]() {
          startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
            this->changeMenu(&wifiGeneralMenu, true);
          });
        });
        this->addNodes(&wifiAPMenu, "(No APs Found)", TFTYELLOW, 255, []() {});
      } else {
        this->addNodes(&wifiAPMenu, "+ Rescan APs", TFTLIME, BEACON_SNIFF, [this]() {
          startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
            this->changeMenu(&wifiGeneralMenu, true);
          });
        });
        // Populate the menu with buttons
        for (int i = 0; i < access_points->size(); i++) {
          const String label = screenName(access_points->get(i).essid);
          this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i](){
            // Join WiFi using mini keyboard
            #ifdef HAS_MINI_KB
              #ifdef MARAUDER_POOM
                miniKbMenu.parentMenu = &wifiAPMenu;
                miniKbMenu.name = "WiFi password";
              #endif
              this->changeMenu(&miniKbMenu, true);
              String password = this->miniKeyboard(&miniKbMenu, true);
              if (password != "") {
                Serial.println("Using SSID: " + (String)access_points->get(i).essid);
                #ifndef MARAUDER_POOM
                  wifi_scan_obj.currentScanMode = LV_JOIN_WIFI;
                  wifi_scan_obj.StartScan(LV_JOIN_WIFI, TFT_YELLOW);
                #else
                display_obj.clearScreen();
                display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
                display_obj.tft.drawString("Connecting to:", 0, 12, 1);
                display_obj.tft.drawString(screenName(access_points->get(i).essid).substring(0, 21), 0, 24, 1);
                display_obj.tft.display(true);
              #endif
              const bool connected = wifi_scan_obj.joinWiFi(access_points->get(i).essid, password);
              #ifdef MARAUDER_POOM
                display_obj.clearScreen();
                display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
                display_obj.tft.drawCentreString(connected ? "Connected" : "Connection failed",
                                                 SCREEN_WIDTH / 2, 20, 1);
                display_obj.tft.drawCentreString(screenName(access_points->get(i).essid).substring(0, 21),
                                                 SCREEN_WIDTH / 2, 34, 1);
                display_obj.tft.display(true);
                delay(1000);
              #endif
              if (wifi_scan_obj.hasPendingWifiCredential()) {
                this->buildSavedWifiMenu(true);
                this->changeMenu(&savedWifiMenu, true);
              }
              else {
                this->changeMenu(&wifiGeneralMenu, true);
              }
            }
            #ifdef MARAUDER_POOM
              else {
                this->changeMenu(&wifiAPMenu, true);
              }
            #endif
          #endif

          // Join WiFi using touch screen keyboard
          #ifdef HAS_TOUCH
            char passwordBuf[64] = {0};  // or prefill with existing SSID
            if (this->textInput(passwordBuf, sizeof(passwordBuf), "Enter Password")) {
              wifi_scan_obj.joinWiFi(access_points->get(i).essid, String(passwordBuf), true);
              if (wifi_scan_obj.hasPendingWifiCredential()) {
                this->buildSavedWifiMenu(true);
                this->changeMenu(&savedWifiMenu, true);
                return;
              }
            }

            this->changeMenu(&wifiGeneralMenu, true);
          #endif
        });
      }
      }
      this->changeMenu(&wifiAPMenu, true);
    });

    this->addNodes(&wifiGeneralMenu, "Join Saved WiFi", TFTWHITE, KEYBOARD_ICO, [this](){
      wifi_scan_obj.joinSavedWiFi(true);
      this->changeMenu(&wifiGeneralMenu, true);
    });

    this->addNodes(&wifiGeneralMenu, "Manage Saved WiFi", TFTWHITE, SETTINGS, [this](){
      this->buildSavedWifiMenu(false);
      this->changeMenu(&savedWifiMenu, true);
    });

    this->addNodes(&wifiGeneralMenu, "Start AP", TFTGREEN, KEYBOARD_ICO, [this](){
      ssidsMenu.parentMenu = &wifiGeneralMenu;

      // Add the back button
      ssidsMenu.list->clear();
        this->addNodes(&ssidsMenu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(ssidsMenu.parentMenu, true);
      });

      // Populate the menu with buttons
      for (int i = 0; i < ssids->size(); i++) {
        // This is the menu node
        const String label = screenName(ssids->get(i).essid);
        this->addNodes(&ssidsMenu, label.c_str(), TFTCYAN, 255, [this, i](){
          // Join WiFi using mini keyboard
          #ifdef HAS_MINI_KB
            this->changeMenu(&miniKbMenu, true);
            String password = this->miniKeyboard(&miniKbMenu, true);
            if (password != "") {
              Serial.println("Using SSID: " + (String)ssids->get(i).essid);
              wifi_scan_obj.currentScanMode = LV_JOIN_WIFI;
              wifi_scan_obj.StartScan(LV_JOIN_WIFI, TFT_YELLOW);
              wifi_scan_obj.startWiFi(ssids->get(i).essid, password);
              this->changeMenu(current_menu, true);
            }
          #endif

          // Join WiFi using touch screen keyboard
          #ifdef HAS_TOUCH
            char passwordBuf[64] = {0};  // or prefill with existing SSID
            if (this->textInput(passwordBuf, sizeof(passwordBuf), "Enter Password")) {
              Serial.println("Using SSID: " + (String)ssids->get(i).essid);
              wifi_scan_obj.startWiFi(ssids->get(i).essid, String(passwordBuf));
            }

            this->changeMenu(&wifiGeneralMenu, false);
          #endif
        });
      }
      this->changeMenu(&ssidsMenu, true);
    });

    this->addNodes(&wifiGeneralMenu, "Host AP Info", TFTGREEN, BEACON_SNIFF, [this]() {
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(WIFI_SCAN_DISPLAY_AP_INFO, TFT_GREEN);
    });

    wifiStationMenu.parentMenu = &ssidsMenu;
    this->addNodes(&wifiStationMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(wifiStationMenu.parentMenu, true);
    });

  this->addNodes(&wifiGeneralMenu, "Set MACs", TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(&setMacMenu, true);
  });

  this->addNodes(&wifiGeneralMenu, "Shutdown WiFi", TFTRED, 0, [this]() {
    WiFi.softAPdisconnect(true); // Also shut down the SoftAP if it is running
	WiFi.disconnect(true);
    delay(100);
    wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
    this->changeMenu(current_menu, true);
  });

  #ifdef HAS_DIRECT_UPLOAD
    this->addNodes(&wifiGeneralMenu, "Upload Wardrive Logs", TFTGREEN, 0, [this]() {
      display_obj.clearScreen();
      display_obj.tft.setTextWrap(false);
      display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
      display_obj.tft.println("Loading...");

      this->buildUploadFileMenu();

      this->changeMenu(&uploadLogsMenu, true);
    });

    uploadAllMenu.parentMenu = &uploadLogsMenu;
    this->addNodes(&uploadAllMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(uploadAllMenu.parentMenu, true);
    });
    this->addNodes(&uploadAllMenu, "WiGLE", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          for (int i = 0; i < sd_obj.sd_files->size(); i++) {
            if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
              if (!sd_obj.sd_files->get(i).endsWith(".wigle") && !sd_obj.sd_files->get(i).endsWith(".wdg") && !sd_obj.sd_files->get(i).endsWith(".gpx")) {
                Serial.println("Uploading " + sd_obj.sd_files->get(i) + "...");
                if (wifi_scan_obj.uploadFile("/" + sd_obj.sd_files->get(i), true, WIGLE_UPLOAD)) {
                  display_obj.clearScreen();
                  display_obj.showCenterText("WiGLE OK", TFT_HEIGHT / 2);
                } else {
                  display_obj.clearScreen();
                  display_obj.showCenterText("WiGLE failed", TFT_HEIGHT / 2);
                }
              }
            }
          }
          WiFi.disconnect(true);
          delay(100);
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(uploadAllMenu.parentMenu, true);
    });
    this->addNodes(&uploadAllMenu, "WDGWars", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          for (int i = 0; i < sd_obj.sd_files->size(); i++) {
            if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
              if (!sd_obj.sd_files->get(i).endsWith(".wigle") && !sd_obj.sd_files->get(i).endsWith(".wdg") && !sd_obj.sd_files->get(i).endsWith(".gpx")) {
                Serial.println("Uploading " + sd_obj.sd_files->get(i) + "...");
                if (wifi_scan_obj.uploadFile("/" + sd_obj.sd_files->get(i), true, WDG_UPLOAD)) {
                  display_obj.clearScreen();
                  display_obj.showCenterText("WDG OK", TFT_HEIGHT / 2);
                } else {
                  display_obj.clearScreen();
                  display_obj.showCenterText("WDG failed", TFT_HEIGHT / 2);
                }
              }
            }
          }
          WiFi.disconnect(true);
          delay(100);
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(uploadAllMenu.parentMenu, true);
    });
    this->addNodes(&uploadAllMenu, "Both", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          for (int i = 0; i < sd_obj.sd_files->size(); i++) {
            if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
              if (!sd_obj.sd_files->get(i).endsWith(".wigle") && !sd_obj.sd_files->get(i).endsWith(".wdg") && !sd_obj.sd_files->get(i).endsWith(".gpx")) {
                Serial.println("Uploading " + sd_obj.sd_files->get(i) + "...");
                if (wifi_scan_obj.uploadFile("/" + sd_obj.sd_files->get(i), true, BOTH_UPLOAD)) {
                  display_obj.clearScreen();
                  display_obj.showCenterText("Upload OK", TFT_HEIGHT / 2);
                } else {
                  display_obj.clearScreen();
                  display_obj.showCenterText("Upload failed", TFT_HEIGHT / 2);
                }
              }
            }
          }
          WiFi.disconnect(true);
          delay(100);
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(uploadAllMenu.parentMenu, true);
    });

    deleteAllMenu.parentMenu = &uploadLogsMenu;
    this->addNodes(&deleteAllMenu, "No", TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(deleteAllMenu.parentMenu, true);
    });
    this->addNodes(&deleteAllMenu, "Yes", TFTRED, 0, [this]() {
      display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);

      display_obj.clearScreen();

      display_obj.showCenterText("Deleting logs...", TFT_HEIGHT / 2, true);

      for (int i = 0; i < sd_obj.sd_files->size(); i++) {
        if (sd_obj.sd_files->get(i).startsWith("wardrive_") || sd_obj.sd_files->get(i).startsWith("wigle-")) {
          if (sd_obj.removeFile("/" + sd_obj.sd_files->get(i))) {
            Serial.println("Removed file: " + sd_obj.sd_files->get(i));
            sd_obj.removeFile("/" + sd_obj.sd_files->get(i) + ".wdg");
            sd_obj.removeFile("/" + sd_obj.sd_files->get(i) + ".wigle");
          }
          else {
            Serial.println("Could not remove file: " + sd_obj.sd_files->get(i));
          }
        }
      }
      display_obj.clearScreen();

      display_obj.showCenterText("Logs removed", TFT_HEIGHT / 2, true);

      delay(2000);

      this->buildUploadFileMenu();

      this->changeMenu(&uploadLogsMenu, true);
    });

    actionMenu.parentMenu = &uploadLogsMenu;
    this->addNodes(&actionMenu, text09, TFTLIGHTGREY, 0, [this]() {
      this->changeMenu(actionMenu.parentMenu, true);
    });
    this->addNodes(&actionMenu, "WiGLE", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          Serial.println("Uploading " + sd_obj.selected_file_name + "...");
          if (wifi_scan_obj.uploadFile("/" + sd_obj.selected_file_name, true, WIGLE_UPLOAD)) {
            display_obj.clearScreen();
            display_obj.showCenterText("WiGLE OK", TFT_HEIGHT / 2, true);
          } else {
            display_obj.clearScreen();
            display_obj.showCenterText("WiGLE failed", TFT_HEIGHT / 2, true);
          }

          WiFi.disconnect(true);
          delay(100);
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(&actionMenu, true);
    });
    this->addNodes(&actionMenu, "WDGWars", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          Serial.println("Uploading " + sd_obj.selected_file_name + "...");
          if (wifi_scan_obj.uploadFile("/" + sd_obj.selected_file_name, true, WDG_UPLOAD)) {
            display_obj.clearScreen();
            display_obj.showCenterText("WDG OK", TFT_HEIGHT / 2, true);
          } else {
            display_obj.clearScreen();
            display_obj.showCenterText("WDG failed", TFT_HEIGHT / 2, true);
          }

          WiFi.disconnect(true);
        delay(100);
        wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(&actionMenu, true);
    });
    this->addNodes(&actionMenu, "Both", TFTLIGHTGREY, 0, [this]() {
      display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);

      if (settings_obj.getSavedWifiCount() == 0) {
        display_obj.clearScreen();
        display_obj.tft.setTextWrap(true);
        display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
        display_obj.tft.println("WiFi Credentials Empty.");
        display_obj.tft.println("Returning...");
        display_obj.tft.setTextWrap(false);
      }
      else {
        if (!wifi_scan_obj.joinSavedWiFi(true)) {
          display_obj.clearScreen();
          display_obj.tft.setTextWrap(true);
          display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
          display_obj.tft.println("Could not connect to WiFi.");
          display_obj.tft.println("Returning...");
          display_obj.tft.setTextWrap(false);
        }
        else {
          delay(1000);
          Serial.println("Uploading " + sd_obj.selected_file_name + "...");
          if (wifi_scan_obj.uploadFile("/" + sd_obj.selected_file_name, true, BOTH_UPLOAD)) {
            display_obj.clearScreen();
            display_obj.showCenterText("Upload OK", TFT_HEIGHT / 2, true);
          } else {
            display_obj.clearScreen();
            display_obj.showCenterText("Upload failed", TFT_HEIGHT / 2, true);
          }

          WiFi.disconnect(true);
          delay(100);
          wifi_scan_obj.StartScan(WIFI_SCAN_OFF, TFT_RED);
        }
      }

      delay(2000);

      this->changeMenu(&actionMenu, true);
    });
  #endif


  // Menu for generating and setting MAC addrs for AP and STA
  setMacMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&setMacMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(setMacMenu.parentMenu, true);
  });

  // Generate random MAC for AP
  this->addNodes(&setMacMenu, "Generate AP MAC", TFTLIME, 0, [this]() {
    this->changeMenu(&genAPMacMenu, true);
    wifi_scan_obj.RunGenerateRandomMac(true);
  });

  // Generate random MAC for AP
  this->addNodes(&setMacMenu, "Generate STA MAC", TFTCYAN, 0, [this]() {
    this->changeMenu(&genAPMacMenu, true);
    wifi_scan_obj.RunGenerateRandomMac(false);
  });

  // Clone AP MAC to ESP32 for button folks
  //#ifndef HAS_ILI9341
    this->addNodes(&setMacMenu, "Clone AP MAC", TFTRED, CLEAR_ICO, [this](){
      buildCloneApMacMenu();
      wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
    });

    this->addNodes(&setMacMenu, "Clone STA MAC", TFTMAGENTA, CLEAR_ICO, [this](){
      buildCloneStaMacMenu();
      wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
    });
  //#endif

  // Menu for generating and setting access point MAC (just goes bacK)
  genAPMacMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&genAPMacMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(genAPMacMenu.parentMenu, true);
  });

  // Build generate ssids menu
  generateSSIDsMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&generateSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(generateSSIDsMenu.parentMenu, true);
  });

  // Build clear ssids menu

  this->addNodes(&clearSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(clearSSIDsMenu.parentMenu, true);
  });
  clearAPsMenu.parentMenu = &wifiGeneralMenu;
  this->addNodes(&clearAPsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(clearAPsMenu.parentMenu, true);
  });

  if (wifi_general_dependents_initialized) {
    for (size_t i = 0; i < dependent_count; i++) {
      while (dependent_menus[i]->list &&
             dependent_menus[i]->list->size() > dependent_sizes[i])
        dependent_menus[i]->list->remove(dependent_menus[i]->list->size() - 1);
    }
  }
  else {
    wifi_general_dependents_initialized = true;
  }

}

#ifdef HAS_BT
void MenuFunctions::buildBluetoothAttackMenu()
{
  extern LinkedList<AirTag>* airtags;
  const int wifi_ap_size = wifiAPMenu.list ? wifiAPMenu.list->size() : 0;
#ifdef HAS_BT
  // Build Bluetooth Menu
  bluetoothMenu.parentMenu = &mainMenu; // Second Menu is third menu parent

  // Build bluetooth sniffer Menu
  bluetoothSnifferMenu.parentMenu = &bluetoothMenu; // Second Menu is third menu parent

  // Bluetooth Attack menu
  bluetoothAttackMenu.parentMenu = &bluetoothMenu; // Second Menu is third menu parent
  this->addNodes(&bluetoothAttackMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(bluetoothAttackMenu.parentMenu, true);
  });
  this->addNodes(&bluetoothAttackMenu, "Sour Apple", TFTGREEN, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SOUR_APPLE, TFT_GREEN);
  });
  this->addNodes(&bluetoothAttackMenu, "Apple Juice", TFTYELLOW, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_APPLE_JUICE, TFT_YELLOW);
  });
  this->addNodes(&bluetoothAttackMenu, "Swiftpair Spam", TFTCYAN, KEYBOARD_ICO, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SWIFTPAIR_SPAM, TFT_CYAN);
  });
  this->addNodes(&bluetoothAttackMenu, "Samsung BLE Spam", TFTRED, GENERAL_APPS, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SAMSUNG_SPAM, TFT_RED);
  });
  this->addNodes(&bluetoothAttackMenu, "Google BLE Spam", TFTPURPLE, LANGUAGE, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_GOOGLE_SPAM, TFT_PURPLE);
  });
  this->addNodes(&bluetoothAttackMenu, "Flipper BLE Spam", TFTORANGE, FLIPPER, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_FLIPPER_SPAM, TFT_ORANGE);
  });
  this->addNodes(&bluetoothAttackMenu, "BLE Spam All", TFTMAGENTA, DEAUTH_SNIFF, [this]() {
    display_obj.clearScreen();
    this->drawStatusBar();
    wifi_scan_obj.StartScan(BT_ATTACK_SPAM_ALL, TFT_MAGENTA);
  });

#endif

  //#ifndef HAS_ILI9341
    #ifdef HAS_BT
      this->addNodes(&bluetoothAttackMenu, "Spoof Airtag", TFTWHITE, ATTACKS, [this](){
        extern LinkedList<AirTag>* airtags;
        if (airtags->size() == 0) {
          startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
            buildAirTagSpoofMenu();
          });
          return;
        }
        buildAirTagSpoofMenu();
      });

      #ifdef HAS_NIMBLE_2
      this->addNodes(&bluetoothAttackMenu, "FindMy Sound", TFTCYAN, ATTACKS, [this](){
        extern LinkedList<AirTag>* airtags;
        if (airtags->size() == 0) {
          startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
            buildFindMySoundMenu();
          });
          return;
        }
        buildFindMySoundMenu();
      });
      #endif
    #endif

}
#endif

// Function to add MenuNodes to a menu
int MenuFunctions::selectedAttackApCount() const {
  extern LinkedList<AccessPoint>* access_points;
  int count = 0;
  for (int i = 0; i < access_points->size(); ++i)
    if (access_points->get(i).selected) ++count;
  return count;
}

int MenuFunctions::selectedAttackStationCount() const {
  extern LinkedList<Station>* stations;
  int count = 0;
  for (int i = 0; i < stations->size(); ++i)
    if (stations->get(i).selected) ++count;
  return count;
}

int MenuFunctions::selectedAttackSsidCount() const {
  extern LinkedList<ssid>* ssids;
  int count = 0;
  for (int i = 0; i < ssids->size(); ++i)
    if (ssids->get(i).selected) ++count;
  return count;
}

void MenuFunctions::clearAttackApSelections() {
  extern LinkedList<AccessPoint>* access_points;
  for (int i = 0; i < access_points->size(); ++i) {
    AccessPoint ap = access_points->get(i);
    ap.selected = false;
    ap.attack_scope = ATTACK_AP_SCOPE_NONE;
    access_points->set(i, ap);
  }
}

void MenuFunctions::clearAttackStationSelections() {
  extern LinkedList<Station>* stations;
  for (int i = 0; i < stations->size(); ++i) {
    Station station = stations->get(i);
    station.selected = false;
    stations->set(i, station);
  }
}

void MenuFunctions::clearAttackStationsForAp(uint16_t ap_index) {
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  if (ap_index >= access_points->size()) return;
  AccessPoint ap = access_points->get(ap_index);
  if (!ap.stations) return;
  for (int i = 0; i < ap.stations->size(); ++i) {
    const int station_index = ap.stations->get(i);
    if (station_index >= 0 && station_index < stations->size())
      wifi_scan_obj.setStationSelected(station_index, false);
  }
}

void MenuFunctions::setAttackApScope(uint16_t ap_index, AttackTargetMode mode) {
  extern LinkedList<AccessPoint>* access_points;
  if (ap_index >= access_points->size()) return;
  AccessPoint ap = access_points->get(ap_index);
  ap.attack_scope = static_cast<uint8_t>(mode);
  ap.selected = mode != AttackTargetMode::NONE;
  access_points->set(ap_index, ap);
}

int MenuFunctions::attackApProfileCount() const {
  extern LinkedList<AccessPoint>* access_points;
  int count = 0;
  for (int i = 0; i < access_points->size(); ++i)
    if (access_points->get(i).attack_scope != ATTACK_AP_SCOPE_NONE) ++count;
  return count;
}

int MenuFunctions::selectedAttackStationCount(uint16_t ap_index) const {
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  if (ap_index >= access_points->size()) return 0;
  AccessPoint ap = access_points->get(ap_index);
  if (!ap.stations) return 0;
  int count = 0;
  for (int i = 0; i < ap.stations->size(); ++i) {
    const int station_index = ap.stations->get(i);
    if (station_index >= 0 && station_index < stations->size() &&
        stations->get(station_index).selected) ++count;
  }
  return count;
}

void MenuFunctions::startScanWithReturn(uint8_t scan_mode, uint16_t color, std::function<void()> on_return) {
  post_scan_callback = on_return;
  display_obj.clearScreen();
  this->drawStatusBar();
  wifi_scan_obj.StartScan(scan_mode, color);
}

void MenuFunctions::beginAttackPlan(const char* name, uint8_t scan_mode,
                                    uint16_t color, AttackTargetKind target_kind) {
  attack_plan_name = name;
  attack_plan_scope = "";
  attack_plan_scan_mode = scan_mode;
  attack_plan_color = color;
  attack_target_kind = target_kind;
  attack_target_mode = AttackTargetMode::NONE;

  if (target_kind == AttackTargetKind::DEAUTH_SCOPE ||
      target_kind == AttackTargetKind::STATION_SCOPE) {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int i = 0; i < access_points->size(); ++i) {
        AccessPoint ap = access_points->get(i);
        ap.attack_scope = ap.selected
            ? (target_kind == AttackTargetKind::DEAUTH_SCOPE
                ? ATTACK_AP_SCOPE_BROADCAST : ATTACK_AP_SCOPE_ALL_STATIONS)
            : ATTACK_AP_SCOPE_NONE;
        access_points->set(i, ap);
      }
    }
    buildAttackStationApPicker();
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  } else if (target_kind == AttackTargetKind::ACCESS_POINTS) {
    buildAttackApPicker();
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  } else if (target_kind == AttackTargetKind::SSIDS) {
    buildAttackSsidPicker();
  } else {
    buildAttackReviewMenu();
  }
}

void MenuFunctions::buildAttackModeMenu() {
  attackModeMenu.list->clear();
  attackModeMenu.parentMenu = &attackTargetMenu;
  this->addNodes(&attackModeMenu, text09, TFTLIGHTGREY, 0, [this]() {
    buildAttackStationApPicker();
  });

  if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE) {
    this->addNodes(&attackModeMenu, "Broadcast", TFTRED, DEAUTH_SNIFF, [this]() {
      attack_target_mode = AttackTargetMode::BROADCAST;
      attack_plan_scan_mode = WIFI_ATTACK_DEAUTH;
      wifi_scan_obj.dst_mac = "ff:ff:ff:ff:ff:ff";
      clearAttackStationsForAp(attack_active_ap_index);
      setAttackApScope(attack_active_ap_index, attack_target_mode);
      buildAttackReviewMenu();
    });
  }

  this->addNodes(&attackModeMenu, "All Stations", TFTORANGE, DEAUTH_SNIFF, [this]() {
    attack_target_mode = AttackTargetMode::ALL_STATIONS;
    if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE)
      attack_plan_scan_mode = WIFI_ATTACK_DEAUTH;
    clearAttackStationsForAp(attack_active_ap_index);
    setAttackApScope(attack_active_ap_index, attack_target_mode);
    buildAttackReviewMenu();
  });

  this->addNodes(&attackModeMenu, "Pick Stations", TFTCYAN, KEYBOARD_ICO, [this]() {
    attack_target_mode = AttackTargetMode::PICK_STATIONS;
    if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE)
      attack_plan_scan_mode = WIFI_ATTACK_DEAUTH;
    setAttackApScope(attack_active_ap_index, attack_target_mode);
    buildAttackStationPicker(attack_active_ap_index);
  });
  extern LinkedList<AccessPoint>* access_points;
  if (attack_active_ap_index < access_points->size() &&
      access_points->get(attack_active_ap_index).attack_scope != ATTACK_AP_SCOPE_NONE) {
    this->addNodes(&attackModeMenu, "Remove from Profile", TFTYELLOW, CLEAR_ICO, [this]() {
      clearAttackStationsForAp(attack_active_ap_index);
      setAttackApScope(attack_active_ap_index, AttackTargetMode::NONE);
      buildAttackStationApPicker();
    });
  }
  this->changeMenu(&attackModeMenu, true);
}

void MenuFunctions::populateAttackApPickerNodes() {
  extern LinkedList<AccessPoint>* access_points;
  attackTargetMenu.list->clear();
  attackTargetMenu.parentMenu = (attack_target_mode == AttackTargetMode::BROADCAST)
      ? &attackModeMenu : &wifiAttackMenu;

  // Node 0: Back
  this->addNodes(&attackTargetMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(attackTargetMenu.parentMenu, true);
  });

  // Node 1: Review / Continue
  this->addNodes(&attackTargetMenu, "Review / Continue", TFTGREEN, ATTACKS, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    buildAttackReviewMenu();
  });

  // Node 2: Scan APs / Rescan APs: No '+', color TFTGREEN (same as Select All)
  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan APs" : "Scan APs";
  this->addNodes(&attackTargetMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  // Node 3: Select All (TFTGREEN)
  this->addNodes(&attackTargetMenu, "Select All", TFTGREEN, KEYBOARD_ICO, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int i = 0; i < access_points->size(); ++i) {
        AccessPoint ap = access_points->get(i);
        ap.selected = true;
        access_points->set(i, ap);
      }
    }
    refreshAttackApPickerLive();
  });

  // Node 4: Clear All (TFTYELLOW)
  this->addNodes(&attackTargetMenu, "Clear All", TFTYELLOW, CLEAR_ICO, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int i = 0; i < access_points->size(); ++i) {
        AccessPoint ap = access_points->get(i);
        ap.selected = false;
        access_points->set(i, ap);
      }
    }
    refreshAttackApPickerLive();
  });

  // Discovered APs or scanning indicator
  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); ++i) {
      const String label = access_points->get(i).essid.length()
          ? screenName(access_points->get(i).essid)
          : screenMac(macToString(access_points->get(i).bssid));
      this->addNodes(&attackTargetMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        extern LinkedList<AccessPoint>* access_points;
        if (access_points && i < access_points->size()) {
          AccessPoint ap = access_points->get(i);
          ap.selected = !ap.selected;
          access_points->set(i, ap);
          refreshAttackApPickerLive();
        }
      }, access_points->get(i).selected);
    }
  } else {
    this->addNodes(&attackTargetMenu, "(Scanning APs...)", TFTYELLOW, 255, []() {});
  }
}

void MenuFunctions::buildAttackApPicker() {
  populateAttackApPickerNodes();
  this->changeMenu(&attackTargetMenu, true);
}

void MenuFunctions::refreshAttackApPickerLive() {
  if (current_menu != &attackTargetMenu)
    return;
  int prev_selected = current_menu->selected;
  int prev_start = this->menu_start_index;
  populateAttackApPickerNodes();
  if (prev_selected < current_menu->list->size())
    current_menu->selected = prev_selected;
  else if (current_menu->list->size() > 0)
    current_menu->selected = current_menu->list->size() - 1;
  this->buildButtons(current_menu, prev_start);
  this->displayCurrentMenu(this->menu_start_index);
}

void MenuFunctions::populateAttackStationApPickerNodes() {
  extern LinkedList<AccessPoint>* access_points;
  attackTargetMenu.list->clear();
  attackTargetMenu.parentMenu = &wifiAttackMenu;
  this->addNodes(&attackTargetMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(&wifiAttackMenu, true);
  });
  this->addNodes(&attackTargetMenu, "Review / Continue", TFTGREEN, ATTACKS, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    buildAttackReviewMenu();
  });
  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan AP/STA" : "Scan AP/STA";
  this->addNodes(&attackTargetMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });
  this->addNodes(&attackTargetMenu, "Clear Profile", TFTYELLOW, CLEAR_ICO, [this]() {
    clearAttackStationSelections();
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int i = 0; i < access_points->size(); ++i)
        setAttackApScope(i, AttackTargetMode::NONE);
    }
    refreshAttackStationApPickerLive();
  });
  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); ++i) {
      String label = screenName(access_points->get(i).essid) + " [" +
                     String(access_points->get(i).stations ? access_points->get(i).stations->size() : 0) + "]";
      this->addNodes(&attackTargetMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        attack_active_ap_index = i;
        extern LinkedList<AccessPoint>* access_points;
        if (access_points && i < access_points->size()) {
          attack_target_mode = static_cast<AttackTargetMode>(access_points->get(i).attack_scope);
          buildAttackModeMenu();
        }
      }, access_points->get(i).attack_scope != ATTACK_AP_SCOPE_NONE);
    }
  } else {
    this->addNodes(&attackTargetMenu, "(Scanning AP/STA...)", TFTYELLOW, 255, []() {});
  }
}

void MenuFunctions::buildAttackStationApPicker() {
  populateAttackStationApPickerNodes();
  this->changeMenu(&attackTargetMenu, true);
}

void MenuFunctions::refreshAttackStationApPickerLive() {
  if (current_menu != &attackTargetMenu)
    return;
  int prev_selected = current_menu->selected;
  int prev_start = this->menu_start_index;
  populateAttackStationApPickerNodes();
  if (prev_selected < current_menu->list->size())
    current_menu->selected = prev_selected;
  else if (current_menu->list->size() > 0)
    current_menu->selected = current_menu->list->size() - 1;
  this->buildButtons(current_menu, prev_start);
  this->displayCurrentMenu(this->menu_start_index);
}

void MenuFunctions::buildAttackStationPicker(uint16_t ap_index) {
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  attackStationMenu.list->clear();
  attackStationMenu.parentMenu = &attackTargetMenu;
  this->addNodes(&attackStationMenu, text09, TFTLIGHTGREY, 0, [this]() {
    buildAttackStationApPicker();
  });
  this->addNodes(&attackStationMenu, "Review / Continue", TFTGREEN, ATTACKS, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    buildAttackReviewMenu();
  });
  this->addNodes(&attackStationMenu, "Rescan Stations", TFTGREEN, BEACON_SNIFF, [this, ap_index]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });
  this->addNodes(&attackStationMenu, "Add Another AP", TFTGREEN, WIFI, [this]() {
    buildAttackStationApPicker();
  });
  if (ap_index < access_points->size()) {
    AccessPoint ap = access_points->get(ap_index);
    this->addNodes(&attackStationMenu, "Select AP Stations", TFTGREEN, KEYBOARD_ICO, [this, ap_index]() {
      extern LinkedList<AccessPoint>* access_points;
      if (access_points && ap_index < access_points->size()) {
        AccessPoint ap = access_points->get(ap_index);
        if (ap.stations) {
          for (int i = 0; i < ap.stations->size(); ++i)
            wifi_scan_obj.setStationSelected(ap.stations->get(i), true);
        }
      }
      buildAttackStationPicker(ap_index);
    });
    this->addNodes(&attackStationMenu, "Clear AP Stations", TFTYELLOW, CLEAR_ICO, [this, ap_index]() {
      clearAttackStationsForAp(ap_index);
      buildAttackStationPicker(ap_index);
    });
    if (ap.stations && ap.stations->size() > 0) {
      for (int i = 0; i < ap.stations->size(); ++i) {
        const int station_index = ap.stations->get(i);
        if (station_index < 0 || station_index >= stations->size()) continue;
        const String label = screenMac(macToString(stations->get(station_index))) + " via " + screenName(ap.essid);
        this->addNodes(&attackStationMenu, label.c_str(), TFTCYAN, 255, [this, ap_index, station_index]() {
          extern LinkedList<Station>* stations;
          if (stations && station_index < stations->size()) {
            wifi_scan_obj.setStationSelected(station_index, !stations->get(station_index).selected);
          }
          buildAttackStationPicker(ap_index);
        }, stations->get(station_index).selected);
      }
    } else {
      this->addNodes(&attackStationMenu, "(Scanning Stations...)", TFTYELLOW, 255, []() {});
    }
  }
  this->changeMenu(&attackStationMenu, true);
}

void MenuFunctions::buildAttackSsidPicker() {
  extern LinkedList<ssid>* ssids;
  attackTargetMenu.list->clear();
  attackTargetMenu.parentMenu = &wifiAttackMenu;
  this->addNodes(&attackTargetMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(&wifiAttackMenu, true);
  });
  if (ssids->size() == 0) {
    this->addNodes(&attackTargetMenu, "+ Add Popular SSIDs", TFTGREEN, KEYBOARD_ICO, [this]() {
      extern LinkedList<ssid>* ssids;
      static const char* const popular[] = {
        "Starbucks WiFi", "Free WiFi", "Guest WiFi", "xfinitywifi",
        "attwifi", "GoogleWiFi", "Airport Free WiFi", "Hotel Guest"
      };
      for (size_t i = 0; i < sizeof(popular) / sizeof(popular[0]); ++i) {
        ssid s;
        s.essid = popular[i];
        s.selected = true;
        ssids->add(s);
      }
      buildAttackSsidPicker();
    });
    this->addNodes(&attackTargetMenu, "+ Import Scanned APs", TFTCYAN, BEACON_SNIFF, [this]() {
      extern LinkedList<AccessPoint>* access_points;
      extern LinkedList<ssid>* ssids;
      for (int i = 0; i < access_points->size(); ++i) {
        String name = access_points->get(i).essid;
        if (name.length() > 0) {
          ssid s;
          s.essid = name;
          s.selected = true;
          ssids->add(s);
        }
      }
      buildAttackSsidPicker();
    });
    this->addNodes(&attackTargetMenu, "+ Scan APs First", TFTORANGE, BEACON_SNIFF, [this]() {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        buildAttackSsidPicker();
      });
    });
    this->addNodes(&attackTargetMenu, "(No saved SSIDs)", TFTYELLOW, 255, []() {});
  } else {
    this->addNodes(&attackTargetMenu, "Review / Continue", TFTGREEN, ATTACKS, [this]() {
      buildAttackReviewMenu();
    });
    this->addNodes(&attackTargetMenu, "+ Add Popular SSIDs", TFTGREEN, KEYBOARD_ICO, [this]() {
      extern LinkedList<ssid>* ssids;
      static const char* const popular[] = {
        "Starbucks WiFi", "Free WiFi", "Guest WiFi", "xfinitywifi",
        "attwifi", "GoogleWiFi", "Airport Free WiFi", "Hotel Guest"
      };
      for (size_t i = 0; i < sizeof(popular) / sizeof(popular[0]); ++i) {
        ssid s;
        s.essid = popular[i];
        s.selected = true;
        ssids->add(s);
      }
      buildAttackSsidPicker();
    });
    this->addNodes(&attackTargetMenu, "Select All", TFTGREEN, KEYBOARD_ICO, [this]() {
      extern LinkedList<ssid>* ssids;
      for (int i = 0; i < ssids->size(); ++i) { ssid item = ssids->get(i); item.selected = true; ssids->set(i, item); }
      buildAttackSsidPicker();
    });
    this->addNodes(&attackTargetMenu, "Clear All", TFTYELLOW, CLEAR_ICO, [this]() {
      extern LinkedList<ssid>* ssids;
      for (int i = 0; i < ssids->size(); ++i) { ssid item = ssids->get(i); item.selected = false; ssids->set(i, item); }
      buildAttackSsidPicker();
    });
    for (int i = 0; i < ssids->size(); ++i) {
      const String label = screenName(ssids->get(i).essid);
      this->addNodes(&attackTargetMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        extern LinkedList<ssid>* ssids;
        ssid item = ssids->get(i); item.selected = !item.selected; ssids->set(i, item);
        buildAttackSsidPicker();
      }, ssids->get(i).selected);
    }
  }
  this->changeMenu(&attackTargetMenu, true);
}

void MenuFunctions::buildAttackReviewMenu() {
  attackReviewMenu.list->clear();
  attackReviewMenu.parentMenu = &wifiAttackMenu;
  this->addNodes(&attackReviewMenu, text09, TFTLIGHTGREY, 0, [this]() {
    if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE ||
        attack_target_kind == AttackTargetKind::STATION_SCOPE)
      buildAttackStationApPicker();
    else if (attack_target_kind == AttackTargetKind::EVIL_PORTAL)
      this->changeMenu(&evilPortalMenu, true);
    else if (attack_target_kind == AttackTargetKind::KARMA)
      this->changeMenu(&selectProbeSSIDsMenu, true);
    else if (attack_target_kind == AttackTargetKind::ACCESS_POINTS)
      buildAttackApPicker();
    else if (attack_target_kind == AttackTargetKind::SSIDS)
      buildAttackSsidPicker();
    else
      this->changeMenu(&wifiAttackMenu, true);
  });
  const String attack_line = "Attack: " + attack_plan_name;
  this->addNodes(&attackReviewMenu, attack_line.c_str(), TFTCYAN, 255, []() {});

  bool valid = true;
  String scope_line = "Scope: Targetless";
  if (attack_target_kind == AttackTargetKind::DEAUTH_SCOPE ||
      attack_target_kind == AttackTargetKind::STATION_SCOPE) {
    extern LinkedList<AccessPoint>* access_points;
    scope_line = "Profile: " + String(attackApProfileCount()) + " AP(s)";
    valid = attackApProfileCount() > 0;
    for (int i = 0; i < access_points->size(); ++i) {
      const AccessPoint ap = access_points->get(i);
      if (ap.attack_scope == ATTACK_AP_SCOPE_NONE) continue;
      String line = screenName(ap.essid) + ": ";
      if (ap.attack_scope == ATTACK_AP_SCOPE_BROADCAST)
        line += "Broadcast";
      else if (ap.attack_scope == ATTACK_AP_SCOPE_ALL_STATIONS) {
        line += "All ";
        line += String(ap.stations ? ap.stations->size() : 0);
        if (!ap.stations || ap.stations->size() == 0) valid = false;
      } else {
        const int picked = selectedAttackStationCount(i);
        line += "Picked ";
        line += String(picked);
        if (picked == 0) valid = false;
      }
      this->addNodes(&attackReviewMenu, line.c_str(), valid ? TFTGREEN : TFTORANGE, 255, []() {});
    }
    this->addNodes(&attackReviewMenu, "Add Another AP", TFTGREEN, WIFI, [this]() {
      buildAttackStationApPicker();
    });
  } else if (attack_target_kind == AttackTargetKind::ACCESS_POINTS) {
    scope_line = "Access Points: " + String(selectedAttackApCount());
    valid = selectedAttackApCount() > 0;
    this->addNodes(&attackReviewMenu, "Add Another AP", TFTGREEN, WIFI, [this]() {
      buildAttackApPicker();
    });
  } else if (attack_target_kind == AttackTargetKind::SSIDS) {
    scope_line = "SSIDs: " + String(selectedAttackSsidCount());
    valid = selectedAttackSsidCount() > 0;
  } else if (attack_target_kind == AttackTargetKind::EVIL_PORTAL ||
             attack_target_kind == AttackTargetKind::KARMA) {
    scope_line = "Portal: " + screenName(attack_plan_scope);
    valid = attack_plan_scope.length() > 0;
  }
  this->addNodes(&attackReviewMenu, scope_line.c_str(), valid ? TFTGREEN : TFTRED, 255, []() {});
  if (valid) {
    this->addNodes(&attackReviewMenu, "Start Attack", TFTRED, ATTACKS, [this]() {
      startAttackPlan();
    });
  } else {
    this->addNodes(&attackReviewMenu, "Select targets first", TFTORANGE, 255, []() {});
  }
  this->changeMenu(&attackReviewMenu, true);
}

void MenuFunctions::buildApConfigMenu() {
  apConfigMenu.parentMenu = &evilPortalMenu;
  if (!apConfigMenu.list)
    apConfigMenu.list = new LinkedList<MenuNode>();
  apConfigMenu.list->clear();
  apConfigMenu.name = "AP Config";

  // Back button
  this->addNodes(&apConfigMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(apConfigMenu.parentMenu, true);
  });

  // Current AP status
  String cur_status = "AP: ";
  if (evil_portal_obj.has_ap && strlen(apName) > 0)
    cur_status += apName;
  else
    cur_status += "(Not Set)";
  this->addNodes(&apConfigMenu, cur_status.c_str(), TFTGREEN, 255, []() {});

  // SD file option
  #ifdef HAS_SD
    if (sd_obj.supported && sd_obj.checkExist("/ap.config.txt")) {
      this->addNodes(&apConfigMenu, "Load ap.config.txt", TFTMAGENTA, SD_UPDATE, [this]() {
        if (evil_portal_obj.setAPFromConfig()) {
          attack_plan_scope = apName;
          buildAttackReviewMenu();
        } else {
          this->changeMenu(&apConfigMenu, true);
        }
      });
    } else {
      this->addNodes(&apConfigMenu, "(No ap.config.txt)", TFTDARKGREY, 255, []() {});
    }
  #else
    this->addNodes(&apConfigMenu, "(No SD card)", TFTDARKGREY, 255, []() {});
  #endif

  // Enter custom AP Name
  this->addNodes(&apConfigMenu, "+ Enter AP Name", TFTCYAN, KEYBOARD_ICO, [this]() {
    char buf[MAX_AP_NAME_SIZE] = {0};
    if (evil_portal_obj.has_ap && strlen(apName) > 0)
      strncpy(buf, apName, sizeof(buf) - 1);
    if (this->textInput(buf, sizeof(buf), "Enter AP Name")) {
      if (strlen(buf) > 0) {
        evil_portal_obj.setAP(String(buf));
        attack_plan_scope = String(buf);
        buildAttackReviewMenu();
        return;
      }
    }
    this->buildApConfigMenu();
  });

  // Presets
  static const char* const AP_PRESETS[] = {
    "Free WiFi",
    "Guest WiFi",
    "Starbucks WiFi",
    "Airport Free WiFi",
    "Hotel Guest",
    "Public WiFi"
  };
  for (size_t i = 0; i < sizeof(AP_PRESETS)/sizeof(AP_PRESETS[0]); ++i) {
    const char* preset = AP_PRESETS[i];
    this->addNodes(&apConfigMenu, preset, TFTWHITE, 255, [this, preset]() {
      evil_portal_obj.setAP(String(preset));
      attack_plan_scope = String(preset);
      buildAttackReviewMenu();
    });
  }

  this->changeMenu(&apConfigMenu, true);
}

void MenuFunctions::buildEvilPortalApMenu() {
  wifiAPMenu.parentMenu = &evilPortalMenu;
  if (!wifiAPMenu.list)
    wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  wifiAPMenu.name = "Access Points";

  // Back button
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  if (access_points->size() == 0) {
    this->addNodes(&wifiAPMenu, "+ Scan APs Now", TFTGREEN, BEACON_SNIFF, [this]() {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        buildEvilPortalApMenu();
      });
    });
    this->addNodes(&wifiAPMenu, "(No APs Scanned)", TFTYELLOW, 255, []() {});
  } else {
    this->addNodes(&wifiAPMenu, "+ Rescan APs", TFTLIME, BEACON_SNIFF, [this]() {
      startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
        buildEvilPortalApMenu();
      });
    });
    for (int i = 0; i < access_points->size(); i++) {
      const String label = screenName(access_points->get(i).essid);
      this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        clearAttackApSelections();
        if (evil_portal_obj.setAP(access_points->get(i).essid)) {
          AccessPoint new_ap = access_points->get(i);
          new_ap.selected = true;
          access_points->set(i, new_ap);
          evil_portal_obj.ap_index = i;
          attack_plan_scope = access_points->get(i).essid;
          buildAttackReviewMenu();
        } else {
          this->changeMenu(&evilPortalMenu, true);
        }
      });
    }
  }

  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildEvilPortalSsidMenu() {
  extern LinkedList<ssid>* ssids;
  extern LinkedList<ProbeReqSsid>* probe_req_ssids;

  ssidsMenu.parentMenu = &evilPortalMenu;
  if (!ssidsMenu.list)
    ssidsMenu.list = new LinkedList<MenuNode>();
  ssidsMenu.list->clear();
  ssidsMenu.name = "User SSIDs";

  // Back button
  this->addNodes(&ssidsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(ssidsMenu.parentMenu, true);
  });

  // + Enter Custom SSID
  this->addNodes(&ssidsMenu, "+ Enter Custom SSID", TFTCYAN, KEYBOARD_ICO, [this]() {
    char buf[MAX_AP_NAME_SIZE] = {0};
    if (this->textInput(buf, sizeof(buf), "Enter SSID")) {
      if (strlen(buf) > 0) {
        wifi_scan_obj.addSSID(String(buf));
        evil_portal_obj.setAP(String(buf));
        attack_plan_scope = String(buf);
        buildAttackReviewMenu();
        return;
      }
    }
    this->buildEvilPortalSsidMenu();
  });

  // + Add Popular SSIDs
  this->addNodes(&ssidsMenu, "+ Add Popular SSIDs", TFTGREEN, 255, [this]() {
    static const char* const POPULAR_SSIDS[] = {
      "Free WiFi",
      "Guest WiFi",
      "Starbucks WiFi",
      "Airport Free WiFi",
      "Public WiFi"
    };
    for (size_t i = 0; i < sizeof(POPULAR_SSIDS)/sizeof(POPULAR_SSIDS[0]); ++i) {
      wifi_scan_obj.addSSID(POPULAR_SSIDS[i]);
    }
    this->buildEvilPortalSsidMenu();
  });

  // + Import Sniffed Probes (if available)
  if (probe_req_ssids && probe_req_ssids->size() > 0) {
    this->addNodes(&ssidsMenu, "+ Import Sniffed Probes", TFTMAGENTA, PROBE_SNIFF, [this]() {
      extern LinkedList<ProbeReqSsid>* probe_req_ssids;
      for (int i = 0; i < probe_req_ssids->size(); ++i) {
        wifi_scan_obj.addSSID(probe_req_ssids->get(i).essid);
      }
      this->buildEvilPortalSsidMenu();
    });
  }

  // + Generate Random SSIDs
  this->addNodes(&ssidsMenu, "+ Generate Random SSIDs", TFTYELLOW, RICK_ROLL, [this]() {
    wifi_scan_obj.generateSSIDs();
    this->buildEvilPortalSsidMenu();
  });

  if (ssids->size() == 0) {
    this->addNodes(&ssidsMenu, "(No SSIDs in List)", TFTORANGE, 255, []() {});
  } else {
    for (int i = 0; i < ssids->size(); i++) {
      const String label = screenName(ssids->get(i).essid);
      this->addNodes(&ssidsMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        clearAttackApSelections();
        if (evil_portal_obj.setAP(ssids->get(i).essid)) {
          attack_plan_scope = ssids->get(i).essid;
          buildAttackReviewMenu();
        } else {
          this->changeMenu(&evilPortalMenu, true);
        }
      });
    }
  }

  this->changeMenu(&ssidsMenu, true);
}

void MenuFunctions::populateKarmaProbeNodes() {
  extern LinkedList<ProbeReqSsid>* probe_req_ssids;
  selectProbeSSIDsMenu.list->clear();
  selectProbeSSIDsMenu.parentMenu = &wifiAttackMenu;
  this->addNodes(&selectProbeSSIDsMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(&wifiAttackMenu, true);
  });
  const char* sniff_label = (probe_req_ssids && probe_req_ssids->size() > 0) ? "Sniff Probes" : "Scan Probes";
  this->addNodes(&selectProbeSSIDsMenu, sniff_label, TFTGREEN, PROBE_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_PROBE, TFT_CYAN);
  });
  if (probe_req_ssids && probe_req_ssids->size() > 0) {
    for (int i = 0; i < probe_req_ssids->size(); i++) {
      const String label = screenName(probe_req_ssids->get(i).essid);
      this->addNodes(&selectProbeSSIDsMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        wifi_scan_obj.stopBackgroundScan();
        clearAttackApSelections();
        extern LinkedList<ProbeReqSsid>* probe_req_ssids;
        if (probe_req_ssids && i < probe_req_ssids->size() && evil_portal_obj.setAP(probe_req_ssids->get(i).essid)) {
          attack_plan_scope = probe_req_ssids->get(i).essid;
          buildAttackReviewMenu();
        } else {
          this->changeMenu(&wifiAttackMenu, true);
        }
      });
    }
  } else {
    this->addNodes(&selectProbeSSIDsMenu, "(Sniffing Probes...)", TFTYELLOW, 255, []() {});
  }
}

void MenuFunctions::buildKarmaProbePicker() {
  populateKarmaProbeNodes();
  this->changeMenu(&selectProbeSSIDsMenu, true);
}

void MenuFunctions::refreshKarmaProbeLive() {
  if (current_menu != &selectProbeSSIDsMenu)
    return;
  int prev_selected = current_menu->selected;
  int prev_start = this->menu_start_index;
  populateKarmaProbeNodes();
  if (prev_selected < current_menu->list->size())
    current_menu->selected = prev_selected;
  else if (current_menu->list->size() > 0)
    current_menu->selected = current_menu->list->size() - 1;
  this->buildButtons(current_menu, prev_start);
  this->displayCurrentMenu(this->menu_start_index);
}

void MenuFunctions::startAttackPlan() {
  wifi_scan_obj.stopBackgroundScan();
  wifi_scan_obj.guided_attack_profile =
      attack_target_kind == AttackTargetKind::DEAUTH_SCOPE ||
      attack_target_kind == AttackTargetKind::STATION_SCOPE;
  display_obj.clearScreen();
  this->drawStatusBar();
  wifi_scan_obj.StartScan(attack_plan_scan_mode, attack_plan_color);
  if (attack_target_kind == AttackTargetKind::EVIL_PORTAL ||
      attack_target_kind == AttackTargetKind::KARMA)
    wifi_scan_obj.setMac();
}

void MenuFunctions::populateSelectAPsNodes() {
  extern LinkedList<AccessPoint>* access_points;
  wifiAPMenu.parentMenu = &wifiGeneralMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan APs" : "Scan APs";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  this->addNodes(&wifiAPMenu, "Select ALL", TFTGREEN, KEYBOARD_ICO, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int x = 0; x < access_points->size(); x++) {
        AccessPoint new_ap = access_points->get(x);
        new_ap.selected = true;
        access_points->set(x, new_ap);
      }
    }
    refreshSelectAPsLive();
  });

  this->addNodes(&wifiAPMenu, "Clear ALL", TFTYELLOW, CLEAR_ICO, [this]() {
    extern LinkedList<AccessPoint>* access_points;
    if (access_points) {
      for (int x = 0; x < access_points->size(); x++) {
        AccessPoint new_ap = access_points->get(x);
        new_ap.selected = false;
        access_points->set(x, new_ap);
      }
    }
    refreshSelectAPsLive();
  });

  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); i++) {
      const String label = screenName(access_points->get(i).essid);
      this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        extern LinkedList<AccessPoint>* access_points;
        if (access_points && i < access_points->size()) {
          AccessPoint new_ap = access_points->get(i);
          new_ap.selected = !new_ap.selected;
          access_points->set(i, new_ap);
          refreshSelectAPsLive();
        }
      }, access_points->get(i).selected);
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning APs...)", TFTYELLOW, 255, []() {});
  }
}

void MenuFunctions::buildSelectAPsMenu() {
  populateSelectAPsNodes();
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::refreshSelectAPsLive() {
  if (current_menu != &wifiAPMenu)
    return;
  int prev_selected = current_menu->selected;
  int prev_start = this->menu_start_index;
  populateSelectAPsNodes();
  if (prev_selected < current_menu->list->size())
    current_menu->selected = prev_selected;
  else if (current_menu->list->size() > 0)
    current_menu->selected = current_menu->list->size() - 1;
  this->buildButtons(current_menu, prev_start);
  this->displayCurrentMenu(this->menu_start_index);
}

void MenuFunctions::buildViewApInfoMenu() {
  extern LinkedList<AccessPoint>* access_points;
  wifiAPMenu.parentMenu = &wifiGeneralMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan APs" : "Scan APs";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); i++) {
      const String label = screenName(access_points->get(i).essid);
      this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        wifi_scan_obj.stopBackgroundScan();
        apInfoMenu.parentMenu = &wifiAPMenu;
        this->changeMenu(&apInfoMenu, true);
        wifi_scan_obj.RunAPInfo(i);
      });
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning APs...)", TFTYELLOW, 255, []() {});
  }
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildSelectStationsMenu() {
  extern LinkedList<AccessPoint>* access_points;
  extern LinkedList<Station>* stations;
  wifiAPMenu.parentMenu = &wifiGeneralMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan AP/STA" : "Scan AP/STA";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); i++) {
      const String label = screenName(access_points->get(i).essid) + " (" + String(access_points->get(i).stations ? access_points->get(i).stations->size() : 0) + ")";
      this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        extern LinkedList<AccessPoint>* access_points;
        extern LinkedList<Station>* stations;
        wifiStationMenu.list->clear();
        wifiStationMenu.parentMenu = &wifiAPMenu;
        this->addNodes(&wifiStationMenu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(wifiStationMenu.parentMenu, true);
        });

        if (!access_points->get(i).stations || access_points->get(i).stations->size() == 0) {
          this->addNodes(&wifiStationMenu, "(No Stations for AP)", TFTYELLOW, 255, []() {});
          this->changeMenu(&wifiStationMenu, true);
          return;
        }

        this->addNodes(&wifiStationMenu, "Select ALL", TFTGREEN, 255, [this, i]() {
          extern LinkedList<AccessPoint>* access_points;
          extern LinkedList<Station>* stations;
          for (int y = 0; y < access_points->get(i).stations->size(); y++) {
            int cur_ap_sta_inx = access_points->get(i).stations->get(y);
            if (cur_ap_sta_inx >= 0 && cur_ap_sta_inx < stations->size()) {
              const bool selected = !stations->get(cur_ap_sta_inx).selected;
              wifi_scan_obj.setStationSelected(cur_ap_sta_inx, selected);
            }
          }
          this->changeMenu(current_menu, true);
        });

        for (int x = 0; x < access_points->get(i).stations->size(); x++) {
          int cur_ap_sta = access_points->get(i).stations->get(x);
          if (cur_ap_sta >= 0 && cur_ap_sta < stations->size()) {
            const String station_label = screenMac(macToString(stations->get(cur_ap_sta)));
            this->addNodes(&wifiStationMenu, station_label.c_str(), TFTCYAN, 255, [this, cur_ap_sta]() {
              extern LinkedList<Station>* stations;
              if (cur_ap_sta >= 0 && cur_ap_sta < stations->size()) {
                const bool selected = !stations->get(cur_ap_sta).selected;
                wifi_scan_obj.setStationSelected(cur_ap_sta, selected);
              }
              this->changeMenu(current_menu, true);
            }, stations->get(cur_ap_sta).selected);
          }
        }
        this->changeMenu(&wifiStationMenu, true);
      });
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning AP & STA...)", TFTYELLOW, 255, []() {});
  }
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildCloneApMacMenu() {
  extern LinkedList<AccessPoint>* access_points;
  wifiAPMenu.parentMenu = &setMacMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan APs" : "Scan APs";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); i++) {
      const String label = screenName(access_points->get(i).essid);
      this->addNodes(&wifiAPMenu, label.c_str(), TFTLIME, 255, [this, i]() {
        extern LinkedList<AccessPoint>* access_points;
        wifi_scan_obj.stopBackgroundScan();
        genAPMacMenu.parentMenu = &wifiAPMenu;
        this->changeMenu(&genAPMacMenu, true);
        wifi_scan_obj.RunSetMac(access_points->get(i).bssid, true);
      });
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning APs...)", TFTYELLOW, 255, []() {});
  }
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildCloneStaMacMenu() {
  extern LinkedList<Station>* stations;
  wifiAPMenu.parentMenu = &setMacMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (stations && stations->size() > 0) ? "Rescan Stations" : "Scan Stations";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  if (stations && stations->size() > 0) {
    for (int i = 0; i < stations->size(); i++) {
      const String label = screenMac(macToString(stations->get(i).mac));
      this->addNodes(&wifiAPMenu, label.c_str(), TFTMAGENTA, 255, [this, i]() {
        extern LinkedList<Station>* stations;
        wifi_scan_obj.stopBackgroundScan();
        genAPMacMenu.parentMenu = &wifiAPMenu;
        this->changeMenu(&genAPMacMenu, true);
        wifi_scan_obj.RunSetMac(stations->get(i).mac, false);
      });
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning Stations...)", TFTYELLOW, 255, []() {});
  }
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildCsiApMenu() {
  extern LinkedList<AccessPoint>* access_points;
  wifiAPMenu.parentMenu = &wifiSnifferMenu;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    wifi_scan_obj.stopBackgroundScan();
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  const char* scan_label = (access_points && access_points->size() > 0) ? "Rescan APs" : "Scan APs";
  this->addNodes(&wifiAPMenu, scan_label, TFTGREEN, BEACON_SNIFF, [this]() {
    wifi_scan_obj.startBackgroundScan(WIFI_SCAN_AP_STA, 0x97e0);
  });

  if (access_points && access_points->size() > 0) {
    for (int i = 0; i < access_points->size(); ++i) {
      const String label = access_points->get(i).essid.length() ?
          screenName(access_points->get(i).essid) : screenMac(macToString(access_points->get(i).bssid));
      this->addNodes(&wifiAPMenu, label.c_str(), TFTCYAN, 255, [this, i]() {
        extern LinkedList<AccessPoint>* access_points;
        wifi_scan_obj.stopBackgroundScan();
        const AccessPoint& ap = access_points->get(i);
        #if defined(HAS_CSI_MOTION)
        if (!csi_motion_obj.start(ap.bssid, ap.essid, ap.channel)) {
          display_obj.clearScreen();
          display_obj.showCenterText("CSI start failed", SCREEN_HEIGHT / 2);
          delay(1500);
          this->changeMenu(&wifiSnifferMenu, true);
        }
        #endif
      });
    }
  } else {
    this->addNodes(&wifiAPMenu, "(Scanning APs...)", TFTYELLOW, 255, []() {});
  }
  this->changeMenu(&wifiAPMenu, true);
}

#ifdef HAS_BT
void MenuFunctions::buildAirTagSpoofMenu() {
  extern LinkedList<AirTag>* airtags;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  wifiAPMenu.parentMenu = &bluetoothAttackMenu;
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  if (airtags->size() == 0) {
    this->addNodes(&wifiAPMenu, "+ Scan AirTags Now", TFTGREEN, BLUETOOTH, [this]() {
      startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
        buildAirTagSpoofMenu();
      });
    });
    this->addNodes(&wifiAPMenu, "(No AirTags Found)", TFTYELLOW, 255, []() {});
    this->changeMenu(&wifiAPMenu, true);
    return;
  }

  this->addNodes(&wifiAPMenu, "+ Rescan AirTags", TFTLIME, BLUETOOTH, [this]() {
    startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
      buildAirTagSpoofMenu();
    });
  });

  int menu_limit = airtags->size();
  for (int i = 0; i < menu_limit; i++) {
    const String label = screenMac(airtags->get(i).mac);
    this->addNodes(&wifiAPMenu, label.c_str(), TFTWHITE, BLUETOOTH, [this, i]() {
      extern LinkedList<AirTag>* airtags;
      AirTag new_at = airtags->get(i);
      new_at.selected = true;
      airtags->set(i, new_at);
      for (int x = 0; x < airtags->size(); x++) {
        if (x != i) {
          AirTag new_atx = airtags->get(x);
          new_atx.selected = false;
          airtags->set(x, new_atx);
        }
      }
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.StartScan(BT_SPOOF_AIRTAG, TFT_WHITE);
    });
  }
  this->changeMenu(&wifiAPMenu, true);
}

void MenuFunctions::buildFindMySoundMenu() {
  #ifdef HAS_NIMBLE_2
  extern LinkedList<AirTag>* airtags;
  if (!wifiAPMenu.list) wifiAPMenu.list = new LinkedList<MenuNode>();
  wifiAPMenu.list->clear();
  wifiAPMenu.parentMenu = &bluetoothAttackMenu;
  this->addNodes(&wifiAPMenu, text09, TFTLIGHTGREY, 0, [this]() {
    this->changeMenu(wifiAPMenu.parentMenu, true);
  });

  if (airtags->size() == 0) {
    this->addNodes(&wifiAPMenu, "+ Scan AirTags Now", TFTGREEN, BLUETOOTH, [this]() {
      startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
        buildFindMySoundMenu();
      });
    });
    this->addNodes(&wifiAPMenu, "(No AirTags Found)", TFTYELLOW, 255, []() {});
    this->changeMenu(&wifiAPMenu, true);
    return;
  }

  this->addNodes(&wifiAPMenu, "+ Rescan AirTags", TFTLIME, BLUETOOTH, [this]() {
    startScanWithReturn(BT_SCAN_AIRTAG, TFT_WHITE, [this]() {
      buildFindMySoundMenu();
    });
  });

  int menu_limit = airtags->size();
  for (int i = 0; i < menu_limit; i++) {
    uint8_t node_color = rssiToMenuColor(airtags->get(i).rssi);
    String node_name = String(airtags->get(i).rssi) + " " + screenMac(airtags->get(i).mac);
    this->addNodes(&wifiAPMenu, node_name.c_str(), node_color, BLUETOOTH, [this, i]() {
      extern LinkedList<AirTag>* airtags;
      AirTag new_at = airtags->get(i);
      new_at.selected = true;
      new_at.connectable = true;
      airtags->set(i, new_at);
      for (int x = 0; x < airtags->size(); x++) {
        if (x != i) {
          AirTag new_atx = airtags->get(x);
          new_atx.selected = false;
          airtags->set(x, new_atx);
        }
      }
      display_obj.clearScreen();
      this->drawStatusBar();
      wifi_scan_obj.executeFindMySound(true);
      delay(2000);
      this->changeMenu(&wifiAPMenu, true);
    });
  }
  this->changeMenu(&wifiAPMenu, true);
  #endif
}
#endif

void MenuFunctions::buildLazyMenu(Menu* menu)
{
  if (!menu || menu->buildKind == MenuBuildKind::EAGER)
    return;

  this->releaseLazyMenu(menu);
  menu->list = new LinkedList<MenuNode>();

  switch (menu->buildKind) {
    case MenuBuildKind::MAIN:
      this->addNodes(menu, "Recon", TFTMAGENTA, GENERAL_APPS, [this]() {
        this->changeMenu(&reconMenu, true);
      });
      this->addNodes(menu, text_table1[7], TFTGREEN, WIFI, [this]() {
        this->changeMenu(&wifiMenu, true);
      });
      #ifdef HAS_BT
        this->addNodes(menu, text_table1[19], TFTCYAN, BLUETOOTH, [this]() {
          this->changeMenu(&bluetoothMenu, true);
        });
      #endif
      #ifdef HAS_GPS
        if (gps_obj.getGpsModuleStatus()) {
          this->addNodes(menu, text1_66, TFTRED, GPS_MENU, [this]() {
            this->changeMenu(&gpsMenu, true);
          });
        }
      #endif
      this->addNodes(menu, text_table1[9], TFTBLUE, DEVICE, [this]() {
        this->changeMenu(&deviceMenu, true);
      });
      #ifdef HAS_IR
        this->addNodes(menu, "IR Remote", TFTYELLOW, GENERAL_APPS, [this]() {
          this->changeMenu(&irMenu, true);
        });
      #endif
      this->addNodes(menu, text_table1[30], TFTLIGHTGREY, REBOOT, []() {
        ESP.restart();
      });
      break;

    case MenuBuildKind::RECON:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(&mainMenu, true);
      });
      this->addNodes(menu, "Start WiFi", TFTGREEN, WIFI, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        recon_obj.start(ReconMode::WIFI_RECON);
      });
      #ifdef HAS_BT
        this->addNodes(menu, "Start BLE", TFTCYAN, BLUETOOTH, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          recon_obj.start(ReconMode::BLE_RECON);
        });
      #endif
      break;

    case MenuBuildKind::WIFI_ROOT:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(wifiMenu.parentMenu, true);
      });
      this->addNodes(menu, text_table1[31], TFTYELLOW, SNIFFERS, [this]() {
        this->changeMenu(&wifiSnifferMenu, true);
      });
      this->addNodes(menu, "Scanners", TFTORANGE, SCANNERS, [this]() {
        this->changeMenu(&wifiScannerMenu, true);
      });
      this->addNodes(menu, text_table1[32], TFTRED, ATTACKS, [this]() {
        this->changeMenu(&wifiAttackMenu, true);
      });
      this->addNodes(menu, text_table1[33], TFTPURPLE, GENERAL_APPS, [this]() {
        this->changeMenu(&wifiGeneralMenu, true);
      });
      break;

    case MenuBuildKind::WIFI_SCANNER:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(wifiScannerMenu.parentMenu, true);
      });
      this->addNodes(menu, "Ping Scan", TFTGREEN, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_PING_SCAN, TFT_CYAN);
      });
      this->addNodes(menu, "ARP Scan", TFTCYAN, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_ARP_SCAN, TFT_CYAN);
      });
      this->addNodes(menu, "Port Scan All", TFTMAGENTA, BEACON_LIST, [this]() {
        wifiIPMenu.list->clear();
        this->addNodes(&wifiIPMenu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(wifiIPMenu.parentMenu, true);
        });
        if (ipList->size() == 0) {
          this->addNodes(&wifiIPMenu, "(No IPs Discovered)", TFTYELLOW, 255, []() {});
          this->addNodes(&wifiIPMenu, "Run ARP Scan first", TFTCYAN, 255, []() {});
        } else {
          for (int i = 0; i < ipList->size(); i++) {
            this->addNodes(&wifiIPMenu, ipList->get(i).toString().c_str(), TFTBLUE, 255, [this, i]() {
              Serial.println("Selected: " + ipList->get(i).toString());
              wifi_scan_obj.current_scan_ip = ipList->get(i);
              display_obj.clearScreen();
              this->drawStatusBar();
              wifi_scan_obj.StartScan(WIFI_PORT_SCAN_ALL, TFT_BLUE);
            });
          }
        }
        this->changeMenu(&wifiIPMenu, true);
      });
      this->addNodes(menu, "SSH Scan", TFTORANGE, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_SSH, TFT_CYAN);
      });
      this->addNodes(menu, "Telnet Scan", TFTRED, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_TELNET, TFT_CYAN);
      });
      this->addNodes(menu, "SMTP Scan", TFTWHITE, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_SMTP, TFT_CYAN);
      });
      this->addNodes(menu, "DNS Scan", TFTLIME, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_DNS, TFT_CYAN);
      });
      this->addNodes(menu, "HTTP Scan", TFTSKYBLUE, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_HTTP, TFT_CYAN);
      });
      this->addNodes(menu, "HTTPS Scan", TFTYELLOW, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_HTTPS, TFT_CYAN);
      });
      this->addNodes(menu, "RDP Scan", TFTPURPLE, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_RDP, TFT_CYAN);
      });
      break;

    case MenuBuildKind::WIFI_SNIFFER:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(wifiSnifferMenu.parentMenu, true);
      });
      this->addNodes(menu, text_table1[42], TFTCYAN, PROBE_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_PROBE, TFT_CYAN);
      });
      this->addNodes(menu, text_table1[43], TFTMAGENTA, BEACON_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_AP, TFT_MAGENTA);
      });
      this->addNodes(menu, text_table1[44], TFTRED, DEAUTH_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_DEAUTH, TFT_RED);
      });
      this->addNodes(menu, "Remote ID Scan", TFTCYAN, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(REMOTE_ID_SCAN_ALL, TFT_CYAN);
      });
      this->addNodes(menu, "Remote ID Devices", TFTMAGENTA, SCANNERS, [this]() {
        remoteIdMenu.list->clear();
        remoteIdMenu.parentMenu = &wifiSnifferMenu;
        this->addNodes(&remoteIdMenu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(remoteIdMenu.parentMenu, true);
        });
        if (wifi_scan_obj.remoteIdCount() == 0) {
          this->addNodes(&remoteIdMenu, "(No Drones Found)", TFTYELLOW, 255, []() {});
          this->addNodes(&remoteIdMenu, "Run Remote ID Scan first", TFTCYAN, 255, []() {});
        } else {
          for (size_t i = 0; i < wifi_scan_obj.remoteIdCount(); ++i) {
            String label = wifi_scan_obj.remoteIdLabel(i);
            this->addNodes(&remoteIdMenu, label.c_str(), TFTCYAN, SCANNERS, [this, i]() {
              if (!wifi_scan_obj.selectRemoteIdTarget(i)) return;
              display_obj.clearScreen();
              this->drawStatusBar();
              wifi_scan_obj.StartScan(REMOTE_ID_SCAN_TARGET, TFT_MAGENTA);
            });
          }
        }
        this->changeMenu(&remoteIdMenu, true);
      });
      this->addNodes(menu, "Packet Count", TFTORANGE, PACKET_MONITOR, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_PACKET_RATE, TFT_ORANGE);
        wifi_scan_obj.renderPacketRate();
      });
      #ifdef HAS_ILI9341
        this->addNodes(menu, text_table1[46], TFTVIOLET, EAPOL, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(WIFI_SCAN_EAPOL, TFT_VIOLET);
        });
        this->addNodes(menu, text_table1[45], TFTBLUE, PACKET_MONITOR, [this]() {
          wifi_scan_obj.StartScan(WIFI_PACKET_MONITOR, TFT_BLUE);
        });
      #else
        this->addNodes(menu, text_table1[46], TFTVIOLET, EAPOL, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(WIFI_SCAN_EAPOL, TFT_VIOLET);
        });
        this->addNodes(menu, text_table1[45], TFTBLUE, PACKET_MONITOR, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(WIFI_PACKET_MONITOR, TFT_BLUE);
        });
      #endif
      this->addNodes(menu, "Channel Analyzer", TFTCYAN, PACKET_MONITOR, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        this->renderGraphUI(WIFI_SCAN_CHAN_ANALYZER);
        wifi_scan_obj.StartScan(WIFI_SCAN_CHAN_ANALYZER, TFT_CYAN);
      });
      this->addNodes(menu, "Channel Summary", TFTORANGE, PACKET_MONITOR, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        this->renderGraphUI(WIFI_SCAN_CHAN_ACT);
        wifi_scan_obj.StartScan(WIFI_SCAN_CHAN_ACT, TFT_CYAN);
      });
      this->addNodes(menu, text_table1[58], TFTWHITE, PACKET_MONITOR, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_RAW_CAPTURE, TFT_WHITE);
      });
      this->addNodes(menu, text_table1[47], TFTRED, PWNAGOTCHI, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_PWN, TFT_RED);
      });
      this->addNodes(menu, text_table1[63], TFTYELLOW, PINESCAN_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_PINESCAN, TFT_YELLOW);
      });
      this->addNodes(menu, text_table1[64], TFTORANGE, MULTISSID_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_MULTISSID, TFT_ORANGE);
      });
      this->addNodes(menu, "Scan AP/STA", TFTLIME, BEACON_SNIFF, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_AP_STA, 0x97e0);
      });
      this->addNodes(menu, "Fox Hunt", TFTCYAN, SCANNERS, [this]() {
        this->buildWiFiFoxHuntMenu();
      });
      this->addNodes(menu, "MAC Monitor", TFTMAGENTA, SCANNERS, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_DETECT_FOLLOW, TFT_MAGENTA);
      });
      this->addNodes(menu, "SAE Commit", TFTLIME, EAPOL, [this]() {
        display_obj.clearScreen();
        this->drawStatusBar();
        wifi_scan_obj.StartScan(WIFI_SCAN_SAE_COMMIT, TFT_GREEN);
      });
      #if defined(HAS_CSI_MOTION)
        this->addNodes(menu, "CSI Motion Monitor", TFTCYAN, PACKET_MONITOR, [this]() {
          extern LinkedList<AccessPoint>* access_points;
          if (access_points->size() == 0) {
            startScanWithReturn(WIFI_SCAN_AP_STA, 0x97e0, [this]() {
              buildCsiApMenu();
            });
            return;
          }
          buildCsiApMenu();
        });
      #endif
      #ifdef HAS_GPS
        if (gps_obj.getGpsModuleStatus()) {
          this->addNodes(menu, "Wardrive", TFTGREEN, BEACON_SNIFF, [this]() {
            display_obj.clearScreen();
            this->drawStatusBar();
            wifi_scan_obj.StartScan(WIFI_SCAN_WAR_DRIVE, TFT_GREEN);
          });
        }
      #endif
      break;

    case MenuBuildKind::WIFI_ATTACK:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(wifiAttackMenu.parentMenu, true);
      });
      this->addNodes(menu, text_table1[50], TFTRED, BEACON_LIST, [this]() {
        beginAttackPlan(text_table1[50], WIFI_ATTACK_BEACON_LIST, TFT_RED,
                        AttackTargetKind::SSIDS);
      });
      this->addNodes(menu, text_table1[51], TFTORANGE, BEACON_SPAM, [this]() {
        beginAttackPlan(text_table1[51], WIFI_ATTACK_BEACON_SPAM, TFT_ORANGE,
                        AttackTargetKind::NONE);
      });
      this->addNodes(menu, text1_67, TFTCYAN, FUNNY_BEACON, [this]() {
        beginAttackPlan(text1_67, WIFI_ATTACK_FUNNY_BEACON, TFT_CYAN,
                        AttackTargetKind::NONE);
      });
      this->addNodes(menu, text_table1[52], TFTYELLOW, RICK_ROLL, [this]() {
        beginAttackPlan(text_table1[52], WIFI_ATTACK_RICK_ROLL, TFT_YELLOW,
                        AttackTargetKind::NONE);
      });
      this->addNodes(menu, text_table1[53], TFTRED, PROBE_SNIFF, [this]() {
        beginAttackPlan(text_table1[53], WIFI_ATTACK_AUTH, TFT_RED,
                        AttackTargetKind::ACCESS_POINTS);
      });
      this->addNodes(menu, "Evil Portal", TFTORANGE, BEACON_SNIFF, [this]() {
        attack_plan_name = "Evil Portal";
        attack_plan_scope = "";
        attack_plan_scan_mode = WIFI_SCAN_EVIL_PORTAL;
        attack_plan_color = TFT_ORANGE;
        attack_target_kind = AttackTargetKind::EVIL_PORTAL;
        attack_target_mode = AttackTargetMode::NONE;
        clearAttackApSelections();
        this->changeMenu(&evilPortalMenu, true);
      });
      this->addNodes(menu, text_table1[54], TFTRED, DEAUTH_SNIFF, [this]() {
        beginAttackPlan("Deauth", WIFI_ATTACK_DEAUTH, TFT_RED,
                        AttackTargetKind::DEAUTH_SCOPE);
      });
      this->addNodes(menu, text_table1[57], TFTMAGENTA, BEACON_LIST, [this]() {
        beginAttackPlan(text_table1[57], WIFI_ATTACK_AP_SPAM, TFT_MAGENTA,
                        AttackTargetKind::ACCESS_POINTS);
      });
      this->addNodes(menu, "Karma", TFTORANGE, KEYBOARD_ICO, [this]() {
        attack_plan_name = "Karma";
        attack_plan_scope = "";
        attack_plan_scan_mode = WIFI_SCAN_EVIL_PORTAL;
        attack_plan_color = TFT_ORANGE;
        attack_target_kind = AttackTargetKind::KARMA;
        attack_target_mode = AttackTargetMode::NONE;
        clearAttackApSelections();
        buildKarmaProbePicker();
        wifi_scan_obj.startBackgroundScan(WIFI_SCAN_PROBE, TFT_CYAN);
      });
      this->addNodes(menu, "Bad Msg", TFTRED, DEAUTH_SNIFF, [this]() {
        beginAttackPlan("Bad Msg", WIFI_ATTACK_BAD_MSG_TARGETED, TFT_RED,
                        AttackTargetKind::STATION_SCOPE);
      });
      this->addNodes(menu, "Assoc Sleep", TFTRED, DEAUTH_SNIFF, [this]() {
        beginAttackPlan("Assoc Sleep", WIFI_ATTACK_SLEEP_TARGETED, TFT_MAGENTA,
                        AttackTargetKind::STATION_SCOPE);
      });
      this->addNodes(menu, "SAE Commit Flood", TFTLIME, EAPOL, [this]() {
        beginAttackPlan("SAE Commit Flood", WIFI_ATTACK_SAE_COMMIT, TFT_GREEN,
                        AttackTargetKind::ACCESS_POINTS);
      });
      this->addNodes(menu, "Channel Switch", TFTORANGE, BEACON_LIST, [this]() {
        beginAttackPlan("Channel Switch", WIFI_ATTACK_CSA, TFT_GREEN,
                        AttackTargetKind::ACCESS_POINTS);
      });
      this->addNodes(menu, "Quiet Time", TFTRED, BEACON_LIST, [this]() {
        beginAttackPlan("Quiet Time", WIFI_ATTACK_QUIET, TFT_GREEN,
                        AttackTargetKind::ACCESS_POINTS);
      });
      break;

    case MenuBuildKind::WIFI_GENERAL:
      this->buildWiFiGeneralMenu();
      break;

    #ifdef HAS_BT
      case MenuBuildKind::BLUETOOTH_ROOT:
        this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(bluetoothMenu.parentMenu, true);
        });
        this->addNodes(menu, text_table1[31], TFTYELLOW, SNIFFERS, [this]() {
          this->changeMenu(&bluetoothSnifferMenu, true);
        });
        this->addNodes(menu, "Bluetooth Attacks", TFTRED, ATTACKS, [this]() {
          this->changeMenu(&bluetoothAttackMenu, true);
        });
        break;

      case MenuBuildKind::BLUETOOTH_SNIFFER:
        this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(bluetoothSnifferMenu.parentMenu, true);
        });
        this->addNodes(menu, text_table1[34], TFTGREEN, BLUETOOTH_SNIFF, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_ALL, TFT_GREEN);
        });
        this->addNodes(menu, "Flipper Sniff", TFTORANGE, FLIPPER, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_FLIPPER, TFT_ORANGE);
        });
        this->addNodes(menu, "FindMy Sniff", TFTWHITE, BLUETOOTH_SNIFF, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_AIRTAG, TFT_WHITE);
        });
        this->addNodes(menu, "iBeacon Sniff", TFTCYAN, BLUETOOTH_SNIFF, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_IBEACON, TFT_CYAN);
        });
        this->addNodes(menu, "iBeacon Info", TFTCYAN, KEYBOARD_ICO, [this]() {
          iBeaconMenu.name = "iBeacon Info";
          iBeaconMenu.list->clear();
          iBeaconMenu.parentMenu = &bluetoothSnifferMenu;
          this->addNodes(&iBeaconMenu, text09, TFTLIGHTGREY, 0, [this]() {
            this->changeMenu(iBeaconMenu.parentMenu, true);
          });
          int ibeacon_count = 0;
          for (int i = 0; i < ble_devices->size(); i++) {
            if (!ble_devices->get(i).is_ibeacon) continue;
            ibeacon_count++;
            String label = String(ble_devices->get(i).rssi) + " " + screenIdentifier(ble_devices->get(i).name);
            this->addNodes(&iBeaconMenu, label.c_str(), rssiToMenuColor(ble_devices->get(i).rssi), 255, [this, i]() {
              const BleDevice& device = ble_devices->get(i);
              char uuid[37];
              snprintf(uuid, sizeof(uuid),
                       "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                       device.ibeacon.uuid[0], device.ibeacon.uuid[1], device.ibeacon.uuid[2], device.ibeacon.uuid[3],
                       device.ibeacon.uuid[4], device.ibeacon.uuid[5], device.ibeacon.uuid[6], device.ibeacon.uuid[7],
                       device.ibeacon.uuid[8], device.ibeacon.uuid[9], device.ibeacon.uuid[10], device.ibeacon.uuid[11],
                       device.ibeacon.uuid[12], device.ibeacon.uuid[13], device.ibeacon.uuid[14], device.ibeacon.uuid[15]);
              iBeaconInfoMenu.list->clear();
              iBeaconInfoMenu.parentMenu = &iBeaconMenu;
              this->addNodes(&iBeaconInfoMenu, text09, TFTLIGHTGREY, 0, [this]() {
                this->changeMenu(iBeaconInfoMenu.parentMenu, true);
              });
              String mac = "MAC: " + screenMac(macToString(device.mac));
              String uuid_a = "UUID: " + String(uuid).substring(0, 18);
              String uuid_b = "      " + String(uuid).substring(18);
              String major = "Major: " + String(device.ibeacon.major);
              String minor = "Minor: " + String(device.ibeacon.minor);
              String power = "Tx Power: " + String(device.ibeacon.measured_power) + " dBm";
              String rssi = "RSSI: " + String(device.rssi) + " dBm";
              auto no_action = []() {};
              this->addNodes(&iBeaconInfoMenu, mac.c_str(), TFTCYAN, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, uuid_a.c_str(), TFTWHITE, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, uuid_b.c_str(), TFTWHITE, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, major.c_str(), TFTCYAN, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, minor.c_str(), TFTCYAN, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, power.c_str(), TFTCYAN, 255, no_action);
              this->addNodes(&iBeaconInfoMenu, rssi.c_str(), rssiToMenuColor(device.rssi), 255, no_action);
              this->changeMenu(&iBeaconInfoMenu, true);
            });
          }
          if (ibeacon_count == 0) {
            this->addNodes(&iBeaconMenu, "(No iBeacons Found)", TFTYELLOW, 255, []() {});
            this->addNodes(&iBeaconMenu, "Run iBeacon Sniff", TFTCYAN, 255, []() {});
          }
          this->changeMenu(&iBeaconMenu, true);
        });
        this->addNodes(menu, "BLE Devices", TFTGREEN, BLUETOOTH, [this]() {
          iBeaconMenu.name = "BLE Devices";
          iBeaconMenu.list->clear();
          iBeaconMenu.parentMenu = &bluetoothSnifferMenu;
          this->addNodes(&iBeaconMenu, text09, TFTLIGHTGREY, 0, [this]() {
            this->changeMenu(iBeaconMenu.parentMenu, true);
          });
          if (ble_devices->size() == 0) {
            this->addNodes(&iBeaconMenu, "(No Devices Found)", TFTYELLOW, 255, []() {});
            this->addNodes(&iBeaconMenu, "Run Bluetooth Sniffer", TFTCYAN, 255, []() {});
          } else {
            for (int i = 0; i < ble_devices->size(); i++) {
              const BleDevice& dev = ble_devices->get(i);
              String dev_name = dev.name.length() ? screenName(dev.name) : macToString(dev.mac);
              String label = String(dev.rssi) + " " + dev_name;
              this->addNodes(&iBeaconMenu, label.c_str(), rssiToMenuColor(dev.rssi), 255, [this, i]() {
                const BleDevice& d = ble_devices->get(i);
                iBeaconInfoMenu.name = "Device Info";
                iBeaconInfoMenu.list->clear();
                iBeaconInfoMenu.parentMenu = &iBeaconMenu;
                this->addNodes(&iBeaconInfoMenu, text09, TFTLIGHTGREY, 0, [this]() {
                  this->changeMenu(iBeaconInfoMenu.parentMenu, true);
                });
                String mac = "MAC: " + screenMac(macToString(d.mac));
                String name = "Name: " + (d.name.length() ? screenName(d.name) : String("Unknown"));
                String rssi = "RSSI: " + String(d.rssi) + " dBm";
                String type = "Type: " + (d.is_ibeacon ? String("iBeacon") : (d.device_type.length() ? d.device_type : String("BLE")));
                auto no_action = []() {};
                this->addNodes(&iBeaconInfoMenu, mac.c_str(), TFTCYAN, 255, no_action);
                this->addNodes(&iBeaconInfoMenu, name.c_str(), TFTWHITE, 255, no_action);
                this->addNodes(&iBeaconInfoMenu, rssi.c_str(), rssiToMenuColor(d.rssi), 255, no_action);
                this->addNodes(&iBeaconInfoMenu, type.c_str(), TFTYELLOW, 255, no_action);
                this->changeMenu(&iBeaconInfoMenu, true);
              });
            }
          }
          this->changeMenu(&iBeaconMenu, true);
        });
        this->addNodes(menu, "FindMy Monitor", TFTWHITE, BLUETOOTH_SNIFF, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_AIRTAG_MON, TFT_WHITE);
        });
        this->addNodes(menu, text_table1[35], TFTMAGENTA, CC_SKIMMERS, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_SKIMMERS, TFT_MAGENTA);
        });
        this->addNodes(menu, "Bluetooth Analyzer", TFTCYAN, PACKET_MONITOR, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          this->renderGraphUI(BT_SCAN_ANALYZER);
          wifi_scan_obj.StartScan(BT_SCAN_ANALYZER, TFT_CYAN);
        });
        this->addNodes(menu, "Flock Sniff", TFTORANGE, FLOCK, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_FLOCK, TFT_ORANGE);
        });
        this->addNodes(menu, "Meta Detect", TFTWHITE, BLUETOOTH_SNIFF, [this]() {
          display_obj.clearScreen();
          this->drawStatusBar();
          wifi_scan_obj.StartScan(BT_SCAN_RAYBAN, TFT_CYAN);
        });
        this->addNodes(menu, "Fox Hunt", TFTCYAN, SCANNERS, [this]() {
          this->buildBluetoothFoxHuntMenu();
        });
        break;
    #endif

    #ifdef HAS_BT
      case MenuBuildKind::BLUETOOTH_ATTACK:
        this->buildBluetoothAttackMenu();
        break;
    #endif

    case MenuBuildKind::DEVICE_ROOT:
      this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
        this->changeMenu(deviceMenu.parentMenu, true);
      });
      #ifdef HAS_SD
        if (sd_obj.supported) {
          sdDeleteMenu.parentMenu = &deviceMenu;
          this->addNodes(menu, "Update Firmware", TFTORANGE, SD_UPDATE, [this]() {
            display_obj.clearScreen();
            display_obj.tft.setTextWrap(false);
            display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
            display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
            display_obj.tft.println("Loading...");
            this->buildSDFileMenu(true);
            this->changeMenu(&sdDeleteMenu, true);
          });
          this->addNodes(menu, "Backup SPIFFS", TFTGREEN, SD_UPDATE, [this]() {
            cli_obj.runCommand(BACKUP_SPIFFS_CMD);
          });
          this->addNodes(menu, "Restore SPIFFS", TFTORANGE, SD_UPDATE, [this]() {
            cli_obj.runCommand(RESTORE_SPIFFS_CMD);
          });
        }
      #endif
      this->addNodes(menu, "Save/Load Files", TFTCYAN, SD_UPDATE, [this]() {
        this->changeMenu(&saveFileMenu, true);
      });
      #ifndef HAS_MINI_SCREEN
        this->addNodes(menu, "Brightness", TFTYELLOW, BRIGHTNESS, [this]() {
          this->brightnessMode();
        });
      #endif
      this->addNodes(menu, text_table1[17], TFTWHITE, DEVICE_INFO, [this]() {
        wifi_scan_obj.currentScanMode = SHOW_INFO;
        this->changeMenu(&infoMenu, true);
        wifi_scan_obj.RunInfo();
      });
      this->addNodes(menu, text08, TFTBLUE, SETTINGS, [this]() {
        this->changeMenu(&settingsMenu, true);
      });
      this->addNodes(menu, "Geofences", TFTGREEN, GPS_MENU, [this]() {
        this->buildGeofenceMenu();
      });
      #ifdef HAS_SD
        if (sd_obj.supported) {
          sdDeleteMenu.parentMenu = &deviceMenu;
          this->addNodes(menu, "SD File Browser", TFTCYAN, SD_UPDATE, [this]() {
            display_obj.clearScreen();
            display_obj.tft.setTextWrap(false);
            display_obj.tft.setCursor(0, SCREEN_HEIGHT / 3);
            display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
            display_obj.tft.println("Loading...");
            this->buildSDDeleteBrowser("/", true);
            this->changeMenu(&sdDeleteMenu, true);
          });
        }
      #endif
      break;

    #ifdef HAS_GPS
      case MenuBuildKind::GPS_ROOT:
        this->addNodes(menu, text09, TFTLIGHTGREY, 0, [this]() {
          this->changeMenu(gpsMenu.parentMenu, true);
        });
        this->addNodes(menu, "GPS Data", TFTRED, GPS_MENU, [this]() {
          wifi_scan_obj.currentScanMode = WIFI_SCAN_GPS_DATA;
          this->changeMenu(&gpsInfoMenu, true);
          wifi_scan_obj.StartScan(WIFI_SCAN_GPS_DATA, TFT_CYAN);
        });
        this->addNodes(menu, "NMEA Stream", TFTORANGE, GPS_MENU, [this]() {
          wifi_scan_obj.currentScanMode = WIFI_SCAN_GPS_NMEA;
          this->changeMenu(&gpsInfoMenu, true);
          wifi_scan_obj.StartScan(WIFI_SCAN_GPS_NMEA, TFT_ORANGE);
        });
        this->addNodes(menu, "GPS Tracker", TFTGREEN, GPS_MENU, [this]() {
          wifi_scan_obj.currentScanMode = GPS_TRACKER;
          this->changeMenu(&gpsInfoMenu, true);
          wifi_scan_obj.StartScan(GPS_TRACKER, TFT_CYAN);
        });
        this->addNodes(menu, "GPS POI", TFTCYAN, GPS_MENU, [this]() {
          wifi_scan_obj.StartScan(GPS_POI, TFT_CYAN);
          wifi_scan_obj.currentScanMode = WIFI_SCAN_OFF;
          this->changeMenu(&gpsPOIMenu, true);
        });
        break;
    #endif

    case MenuBuildKind::EAGER:
      break;
  }
}

void MenuFunctions::releaseLazyMenu(Menu* menu)
{
  if (!menu || menu->buildKind == MenuBuildKind::EAGER || !menu->list)
    return;

  menu->list->clear();
  delete menu->list;
  menu->list = nullptr;
}

void MenuFunctions::addNodes(Menu * menu, const char* name, uint8_t color, int place, std::function<void()> callable, bool selected)
{
  //Serial.println("Building node: " + name);
  menu->list->add(MenuNode{String(name), false, color, place, selected, callable});
}

void MenuFunctions::setGraphScale(float scale) {
  this->_graph_scale = scale;
}

float MenuFunctions::calculateGraphScale(uint8_t value) {
  if ((value * this->_graph_scale < GRAPH_VERT_LIM) && (value * this->_graph_scale > GRAPH_VERT_LIM * 0.75)) {
    return this->_graph_scale;  // No scaling needed if the value is within the limit
  }

  if (value < GRAPH_VERT_LIM)
    return 1.0;

  // Calculate the multiplier proportionally
  return (0.75 * GRAPH_VERT_LIM) / value;
}

float MenuFunctions::calculateGraphScale(int16_t value) {
  if ((value * this->_graph_scale < GRAPH_VERT_LIM) && (value * this->_graph_scale > GRAPH_VERT_LIM * 0.75)) {
    return this->_graph_scale;  // No scaling needed if the value is within the limit
  }

  if (value < GRAPH_VERT_LIM)
    return 1.0;

  // Calculate the multiplier proportionally
  return (0.75 * GRAPH_VERT_LIM) / value;
}

float MenuFunctions::graphScaleCheck(const int16_t array[SCREEN_WIDTH]) {
  int16_t maxValue = 0;

  // Iterate through the array to find the highest value
  for (int16_t i = 0; i < SCREEN_WIDTH; i++) {
    if (array[i] > maxValue) {
      maxValue = array[i];
    }
  }

  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    if (maxValue <= 0) return 1.0f;
    // Auto-scale so that max activity fills ~75% of GRAPH_VERT_LIM
    float scale = ((float)GRAPH_VERT_LIM * 0.75f) / (float)maxValue;
    if (scale > 8.0f) scale = 8.0f;
    if (scale < 0.05f) scale = 0.05f;
    return scale;
  #else
    // If the highest value exceeds GRAPH_VERT_LIM, call calculateMultiplier
    if (maxValue > GRAPH_VERT_LIM) {
      return this->calculateGraphScale(maxValue);
    }

    // If the highest value does not exceed GRAPH_VERT_LIM, return 1.0
    return 1.0;
  #endif
}

float MenuFunctions::graphScaleCheckSmall(const uint8_t array[CHAN_PER_PAGE]) {
  uint8_t maxValue = 0;

  // Iterate through the array to find the highest value
  for (uint8_t i = 0; i < CHAN_PER_PAGE; i++) {
    if (array[i] > maxValue) {
      maxValue = array[i];
    }
  }

  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    if (maxValue == 0) return 1.0;
    // Auto-scale so that max activity fills ~75% of GRAPH_VERT_LIM
    return ((float)GRAPH_VERT_LIM * 0.75f) / (float)maxValue;
  #else
    // If the highest value exceeds GRAPH_VERT_LIM, call calculateMultiplier
    if (maxValue > GRAPH_VERT_LIM) {
      return this->calculateGraphScale(maxValue);
    }
    return 1.0;
  #endif
}

void MenuFunctions::drawMaxLine(int16_t value, uint16_t color) {
  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    const int16_t baseline = SCREEN_HEIGHT - 14;
    int16_t y = baseline - (int16_t)(value * this->_graph_scale);
    if (y < baseline - GRAPH_VERT_LIM) y = baseline - GRAPH_VERT_LIM;
    display_obj.tft.drawLine(0, y, SCREEN_WIDTH, y, color);
  #else
    display_obj.tft.drawLine(0, TFT_HEIGHT - (value * this->_graph_scale), TFT_WIDTH, TFT_HEIGHT - (value * this->_graph_scale), color);
    display_obj.tft.setCursor(0, TFT_HEIGHT - (value * this->_graph_scale));
    display_obj.tft.setTextColor(color, TFT_BLACK);
    display_obj.tft.setTextSize(1);
    display_obj.tft.println((String)(value / BASE_MULTIPLIER));
  #endif
}

void MenuFunctions::drawMaxLine(uint8_t value, uint16_t color) {
  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    const int16_t baseline = SCREEN_HEIGHT - 14;
    int16_t y = baseline - (int16_t)(value * this->_graph_scale);
    if (y < baseline - GRAPH_VERT_LIM) y = baseline - GRAPH_VERT_LIM;
    display_obj.tft.drawLine(0, y, SCREEN_WIDTH, y, color);
  #else
    display_obj.tft.setCursor(0, TFT_HEIGHT - (value * this->_graph_scale));
    display_obj.tft.setTextColor(color, TFT_BLACK);
    display_obj.tft.setTextSize(1);
    display_obj.tft.println((String)value);
  #endif
}

void MenuFunctions::drawGraphSmall(uint8_t *values) {
  uint8_t maxValue = 0;

  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    const int16_t baseline = SCREEN_HEIGHT - 14;
    const int bar_width = SCREEN_WIDTH / (CHAN_PER_PAGE * 2);
    const int small_graph_lim = 96;

    // Refresh header bar
    display_obj.tft.fillRect(0, 32, SCREEN_WIDTH, 14, 0x18C3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.drawCentreString(wifi_scan_obj.activity_page == 1 ? "Channels 1 - 7 Activity" : "Channels 8 - 14 Activity", SCREEN_WIDTH / 2, 35, 1);

    for (int i = 1; i < CHAN_PER_PAGE + 1; i++) {
      int targ_val = i + (CHAN_PER_PAGE * (wifi_scan_obj.activity_page - 1)) - 1;
      int x_mult = (i * 2) - 1;
      int x_coord = (SCREEN_WIDTH / (CHAN_PER_PAGE * 2)) * (x_mult - 1);

      if (values[targ_val] > maxValue) {
        maxValue = values[targ_val];
      }

      int16_t bar_h = (int16_t)(values[targ_val] * this->_graph_scale);
      if (bar_h > small_graph_lim) bar_h = small_graph_lim;

      // Erase bar column
      display_obj.tft.fillRect(x_coord, baseline - small_graph_lim, bar_width, small_graph_lim, TFT_BLACK);
      // Draw bar
      if (bar_h > 0)
        display_obj.tft.fillRect(x_coord, baseline - bar_h, bar_width, bar_h, TFT_CYAN);

      // Dividing line
      display_obj.tft.drawLine(x_coord - 2, 48, x_coord - 2, baseline, TFT_DARKGREY);

      // Channel number above the bar
      display_obj.tft.setCursor(x_coord + (bar_width / 2) - 3, 49);
      display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      display_obj.tft.setTextSize(1);
      display_obj.tft.print(targ_val + 1);
    }

    display_obj.tft.drawFastHLine(0, baseline, SCREEN_WIDTH, TFT_DARKGREY);

    // Footer controls
    display_obj.tft.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, 0x18C3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.drawCentreString("UP/DN: Page | Hold: Exit", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11, 1);

    if (maxValue > 0)
      this->drawMaxLine(maxValue, TFT_GREEN);
  #else
    int bar_width = SCREEN_WIDTH / (CHAN_PER_PAGE * 2);

    #ifndef HAS_DUAL_BAND
      for (int i = 1; i < CHAN_PER_PAGE + 1; i++) {
        int targ_val = i + (CHAN_PER_PAGE * (wifi_scan_obj.activity_page - 1)) - 1;
        int x_mult = (i * 2) - 1;
        int x_coord = (SCREEN_WIDTH / (CHAN_PER_PAGE * 2)) * (x_mult - 1);

        if (values[targ_val] > maxValue) {
          maxValue = values[targ_val];
        }

        if (values[targ_val] * this->_graph_scale <= GRAPH_VERT_LIM) {
          display_obj.tft.fillRect(x_coord, SCREEN_HEIGHT / 2 + 1, bar_width, SCREEN_HEIGHT / 2 + 1, TFT_BLACK);
          display_obj.tft.fillRect(x_coord, SCREEN_HEIGHT - (values[targ_val] * this->_graph_scale), bar_width, values[targ_val] * this->_graph_scale, TFT_CYAN);
        }

        display_obj.tft.drawLine(x_coord - 2, SCREEN_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), x_coord - 2, SCREEN_HEIGHT, TFT_WHITE);
      }
    #else
      for (int i = 1; i < CHAN_PER_PAGE + 1; i++) {
        int targ_val = i + (CHAN_PER_PAGE * (wifi_scan_obj.activity_page - 1)) - 1;
        int x_mult = (i * 2) - 1;
        int x_coord = (SCREEN_WIDTH / (CHAN_PER_PAGE * 2)) * (x_mult - 1);

        if (values[targ_val] > maxValue) {
          maxValue = values[targ_val];
        }

        if (values[targ_val] * this->_graph_scale <= GRAPH_VERT_LIM) {
          display_obj.tft.fillRect(x_coord, SCREEN_HEIGHT / 2 + 1, bar_width + 3, SCREEN_HEIGHT / 2 + 1, TFT_BLACK);
          display_obj.tft.fillRect(x_coord, SCREEN_HEIGHT - (values[targ_val] * this->_graph_scale), bar_width, values[targ_val] * this->_graph_scale, TFT_CYAN);
        }

        display_obj.tft.drawLine(x_coord - 2, SCREEN_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), x_coord - 2, SCREEN_HEIGHT, TFT_WHITE);
      }
    #endif

    this->drawMaxLine(maxValue, TFT_GREEN); // Draw max
  #endif
}

void MenuFunctions::drawGraph(int16_t *values) {
  #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV) && !defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    int width = TFT_WIDTH;
  #else
    int width = SCREEN_WIDTH;
  #endif

  int16_t maxValue = 0;
  int total = 0;
  for (int i = width - 1; i >= 0; i--) {
    if (values[i] >= 0) {
      total = total + values[i];
      if (values[i] > maxValue) {
        maxValue = values[i];
      }
      #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV) && !defined(MARAUDER_LILYGO_T_DISPLAY_S3)
        display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_BLACK);
        display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - (values[i] * this->_graph_scale), TFT_CYAN);
      #elif defined(MARAUDER_LILYGO_T_DISPLAY_S3)
        const int16_t baseline = SCREEN_HEIGHT - 14;
        display_obj.tft.drawLine(i, baseline, i, baseline - GRAPH_VERT_LIM, TFT_BLACK);
        int16_t h = (int16_t)(values[i] * this->_graph_scale);
        if (h > GRAPH_VERT_LIM) h = GRAPH_VERT_LIM;
        if (h > 0)
          display_obj.tft.drawLine(i, baseline, i, baseline - h, TFT_CYAN);
      #else
        display_obj.tft.drawLine(i, TFT_WIDTH, i, TFT_WIDTH - GRAPH_VERT_LIM, TFT_BLACK);
        display_obj.tft.drawLine(i, TFT_WIDTH, i, TFT_WIDTH - (values[i] * this->_graph_scale), TFT_CYAN);
        display_obj.tft.setCursor(0, 0);
        display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
      #endif
    }
    else {
      int16_t ch_val = values[i] * -1;
      #if !defined(MARAUDER_CARDPUTER) && !defined(MARAUDER_CARDPUTER_ADV) && !defined(MARAUDER_LILYGO_T_DISPLAY_S3)
        display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_BLACK);
        display_obj.tft.drawLine(i, TFT_HEIGHT, i, TFT_HEIGHT - GRAPH_VERT_LIM, TFT_RED);
        display_obj.tft.setCursor(i, TFT_HEIGHT - GRAPH_VERT_LIM);
      #elif defined(MARAUDER_LILYGO_T_DISPLAY_S3)
        const int16_t baseline = SCREEN_HEIGHT - 14;
        display_obj.tft.drawLine(i, baseline, i, baseline - GRAPH_VERT_LIM, TFT_BLACK);
        display_obj.tft.drawLine(i, baseline, i, baseline - GRAPH_VERT_LIM, TFT_RED);
        display_obj.tft.setCursor(i, baseline - GRAPH_VERT_LIM);
      #else
        display_obj.tft.drawLine(i, TFT_WIDTH, i, TFT_WIDTH - GRAPH_VERT_LIM, TFT_BLACK);
        display_obj.tft.drawLine(i, TFT_WIDTH, i, TFT_WIDTH - GRAPH_VERT_LIM, TFT_RED);
        display_obj.tft.setCursor(i, TFT_WIDTH - GRAPH_VERT_LIM);
      #endif
      display_obj.tft.setTextColor(TFT_BLACK, TFT_RED);
      display_obj.tft.setTextSize(1);
      display_obj.tft.println((String)ch_val);
    }
  }

  this->drawMaxLine(maxValue, TFT_GREEN); // Draw max
  #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
    this->drawMaxLine((int16_t)(total / SCREEN_WIDTH), TFT_ORANGE); // Draw average

    // Refresh legend values in the top bar at Y=32..45
    display_obj.tft.fillRect(0, 32, SCREEN_WIDTH, 14, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.setCursor(6, 35);
    display_obj.tft.setTextColor(TFT_CYAN, 0x18C3);
    if (wifi_scan_obj.currentScanMode == BT_SCAN_ANALYZER)
      display_obj.tft.print("Beacons  ");
    else
      display_obj.tft.print("Packets  ");

    display_obj.tft.setTextColor(TFT_GREEN, 0x18C3);
    display_obj.tft.print("Max:" + String(maxValue) + "  ");
    display_obj.tft.setTextColor(TFT_ORANGE, 0x18C3);
    display_obj.tft.print("Avg:" + String(total / SCREEN_WIDTH) + "  ");

    if (wifi_scan_obj.currentScanMode == WIFI_SCAN_CHAN_ANALYZER) {
      display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
      display_obj.tft.drawRightString("CH: " + String(wifi_scan_obj.set_channel), SCREEN_WIDTH - 6, 35, 1);
    }

    display_obj.tft.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, 0x18C3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.drawCentreString("UP/DN: Channel | Hold: Exit", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11, 1);
  #else
    this->drawMaxLine((int16_t)(total / TFT_WIDTH), TFT_ORANGE); // Draw average
  #endif
}

void MenuFunctions::renderGraphUI(uint8_t scan_mode) {
  display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  #ifdef MARAUDER_LILYGO_T_DISPLAY_S3
    display_obj.tft.fillRect(0, 32, SCREEN_WIDTH, SCREEN_HEIGHT - 32, TFT_BLACK);

    if (scan_mode == WIFI_SCAN_CHAN_ACT) {
      display_obj.tft.fillRect(0, 32, SCREEN_WIDTH, 14, 0x18C3);
      display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
      display_obj.tft.setTextSize(1);
      display_obj.tft.drawCentreString(wifi_scan_obj.activity_page == 1 ? "WiFi Channels 1-7" : "WiFi Channels 8-14", SCREEN_WIDTH / 2, 35, 1);

      const int16_t baseline = SCREEN_HEIGHT - 14;
      display_obj.tft.drawFastHLine(0, baseline, SCREEN_WIDTH, TFT_DARKGREY);
      display_obj.tft.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, 0x18C3);
      display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
      display_obj.tft.drawCentreString("UP/DN: Page | Hold: Exit", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11, 1);
      return;
    }

    // Initial legend row
    display_obj.tft.fillRect(0, 32, SCREEN_WIDTH, 14, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.setCursor(6, 35);
    display_obj.tft.setTextColor(TFT_CYAN, 0x18C3);
    display_obj.tft.print(scan_mode == BT_SCAN_ANALYZER ? "Beacons  " : "Packets  ");
    display_obj.tft.setTextColor(TFT_GREEN, 0x18C3);
    display_obj.tft.print("Max:0  ");
    display_obj.tft.setTextColor(TFT_ORANGE, 0x18C3);
    display_obj.tft.print("Avg:0  ");
    if (scan_mode == WIFI_SCAN_CHAN_ANALYZER) {
      display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
      display_obj.tft.drawRightString("CH: " + String(wifi_scan_obj.set_channel), SCREEN_WIDTH - 6, 35, 1);
    }

    const int16_t baseline = SCREEN_HEIGHT - 14;
    display_obj.tft.drawFastHLine(0, baseline - GRAPH_VERT_LIM, SCREEN_WIDTH, TFT_DARKGREY);
    display_obj.tft.drawFastHLine(0, baseline, SCREEN_WIDTH, TFT_DARKGREY);

    // Footer
    display_obj.tft.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, 0x18C3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.drawCentreString("UP/DN: Channel | Hold: Exit", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11, 1);
    return;
  #endif
  #ifdef MARAUDER_POOM
    display_obj.tft.fillRect(0, STATUS_BAR_WIDTH, SCREEN_WIDTH,
                             SCREEN_HEIGHT - GRAPH_VERT_LIM - STATUS_BAR_WIDTH - 1,
                             TFT_BLACK);
    const char *title = scan_mode == BT_SCAN_ANALYZER ? "BLE Beacons/50ms" : "Frames/50ms";
    display_obj.tft.drawCentreString(title, SCREEN_WIDTH / 2, STATUS_BAR_WIDTH, 1);
    display_obj.tft.drawLine(0, SCREEN_HEIGHT - GRAPH_VERT_LIM - 1,
                             SCREEN_WIDTH, SCREEN_HEIGHT - GRAPH_VERT_LIM - 1,
                             TFT_WHITE);
    return;
  #endif
  if (scan_mode == WIFI_SCAN_CHAN_ANALYZER)
    display_obj.tft.drawCentreString("Frames/" + (String)BANNER_TIME + "ms", SCREEN_WIDTH / 2, SCREEN_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), 1);
  else if (scan_mode == BT_SCAN_ANALYZER)
    display_obj.tft.drawCentreString("BLE Beacons/" + (String)BANNER_TIME + "ms", SCREEN_WIDTH / 2, SCREEN_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 2), 1);
  display_obj.tft.drawLine(0, SCREEN_HEIGHT - GRAPH_VERT_LIM - 1, SCREEN_WIDTH, SCREEN_HEIGHT - GRAPH_VERT_LIM - 1, TFT_WHITE);
  display_obj.tft.setCursor(0, SCREEN_HEIGHT - GRAPH_VERT_LIM - (CHAR_WIDTH * 8));
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
  display_obj.tft.println("Max");
  display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  display_obj.tft.println("Average");
  display_obj.tft.setTextColor(TFT_RED, TFT_BLACK);
  if (scan_mode != BT_SCAN_ANALYZER)
    display_obj.tft.println("Channel Marker");
}

uint16_t MenuFunctions::getColor(uint16_t color) {
  if (color == TFTWHITE) return TFT_WHITE;
  else if (color == TFTCYAN) return TFT_CYAN;
  else if (color == TFTBLUE) return TFT_BLUE;
  else if (color == TFTRED) return TFT_RED;
  else if (color == TFTGREEN) return TFT_GREEN;
  else if (color == TFTGREY) return TFT_LIGHTGREY;
  else if (color == TFTGRAY) return TFT_LIGHTGREY;
  else if (color == TFTMAGENTA) return TFT_MAGENTA;
  else if (color == TFTVIOLET) return TFT_VIOLET;
  else if (color == TFTORANGE) return TFT_ORANGE;
  else if (color == TFTYELLOW) return TFT_YELLOW;
  else if (color == TFTLIGHTGREY) return TFT_LIGHTGREY;
  else if (color == TFTPURPLE) return TFT_PURPLE;
  else if (color == TFTNAVY) return TFT_NAVY;
  else if (color == TFTSILVER) return TFT_SILVER;
  else if (color == TFTDARKGREY) return TFT_DARKGREY;
  else if (color == TFTSKYBLUE) return TFT_SKYBLUE;
  else if (color == TFTLIME) return 0x97e0;
  else return color;
}

// Function to change menu
void MenuFunctions::changeMenu(Menu* menu, bool simple_change) {
  if (!menu)
    return;

  Menu* previous_menu = current_menu;

  if (menu->buildKind != MenuBuildKind::EAGER)
    this->buildLazyMenu(menu);

  #ifdef HAS_SD
    const bool leaving_sd_browser = current_menu == &sdDeleteMenu && menu != &sdDeleteMenu;
  #endif

  if (!simple_change) {
    //display_obj.initScrollValues();
    //display_obj.setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);
    display_obj.init();

    #ifdef HAS_ILI9341
      extern void backlightOn();
	  backlightOn();
    #endif
  }
  current_menu = menu;

  if (previous_menu && previous_menu != current_menu) {
    if (wifi_scan_obj.background_scan &&
        (previous_menu == &attackTargetMenu ||
         previous_menu == &attackStationMenu ||
         previous_menu == &selectProbeSSIDsMenu ||
         previous_menu == &wifiAPMenu ||
         previous_menu == &wifiStationMenu)) {
      wifi_scan_obj.stopBackgroundScan();
    }
    if (previous_menu->buildKind != MenuBuildKind::EAGER)
      lazy_menu_release_pending = previous_menu;
  }

  current_menu->selected = 0;
  marquee_menu = nullptr;
  marquee_selected = 0xFFFF;
  marquee_rendered_offset = 0;
  marquee_max_offset = 0;

  buildButtons(menu);

  displayCurrentMenu();

  #ifdef HAS_SD
    if (leaving_sd_browser)
      sd_browser_release_pending = true;
  #endif

  //#ifdef MARAUDER_V8
  //  digitalWrite(TFT_BL, HIGH);
  //#endif
}

void MenuFunctions::buildButtons(Menu *menu, int starting_index, const char* button_name) {
  if (menu->list == NULL || menu->list->size() == 0)
      return;

  if (starting_index >= menu->list->size())
    starting_index = menu->list->size() - BUTTON_SCREEN_LIMIT;
  if (starting_index < 0)
    starting_index = 0;

  this->menu_start_index = starting_index;

  uint8_t visible_buttons = min(BUTTON_SCREEN_LIMIT, menu->list->size() - starting_index);

  for (uint8_t i = 0; i < visible_buttons; i++) {
    MenuNode node = menu->list->get(starting_index + i);
    uint16_t color = (node.icon == SETTINGS && node.color == TFTLIGHTGREY) ? (node.selected ? TFT_GREEN : TFT_RED) : this->getColor(node.color);

    char buf[64];

    if (button_name != nullptr && button_name[0] != '\0') {
      strncpy(buf, button_name, sizeof(buf));
      buf[sizeof(buf) - 1] = '\0';
    } else {
      node.name.toCharArray(buf, sizeof(buf));
    }

    display_obj.key[i].initButton(&display_obj.tft,
                                  KEY_X,
                                  KEY_Y + i * (KEY_H + KEY_SPACING_Y),
                                  KEY_W,
                                  KEY_H,
                                  TFT_BLACK,
                                  TFT_BLACK,
                                  color,
                                  buf,
                                  KEY_TEXTSIZE);

    #if defined(MARAUDER_CARDPUTER) || defined(MARAUDER_CARDPUTER_ADV)
      display_obj.key[i].setLabelDatum(BUTTON_PADDING - (KEY_W / 2), 4, ML_DATUM);
    #else
      display_obj.key[i].setLabelDatum(BUTTON_PADDING - (KEY_W / 2), 2, ML_DATUM);
    #endif
  }

  for (int i = BUTTON_ARRAY_LEN; i < BUTTON_ARRAY_LEN + 3; i++) {
    uint16_t x = TFT_WIDTH / 2;
    uint16_t y = TFT_HEIGHT / 3 * (i - BUTTON_ARRAY_LEN) + ((TFT_HEIGHT / 3) / 2);
    uint16_t w = TFT_WIDTH;
    uint16_t h = TFT_HEIGHT / 3 - 1;

    display_obj.key[i].initButton(&display_obj.tft,
                                  x,
                                  y,
                                  w,
                                  h,
                                  TFT_LIGHTGREY,
                                  TFT_BLACK,
                                  TFT_BLACK,
                                  "Chicken",
                                  1);
  }
}

void MenuFunctions::displayCurrentMenu(int start_index)
{
  //Serial.println(F("Displaying current menu..."));
  marquee_menu = nullptr;
  marquee_selected = 0xFFFF;
  marquee_rendered_offset = 0;
  marquee_max_offset = 0;
  display_obj.clearScreen();
  display_obj.updateBanner(current_menu->name);
  display_obj.tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  this->drawStatusBar();

  if (current_menu->list != NULL)
  {
    #ifdef HAS_FULL_SCREEN
      display_obj.tft.setFreeFont(MENU_FONT);
    #endif

    #ifdef HAS_MINI_SCREEN
      display_obj.tft.setFreeFont(NULL);
      display_obj.tft.setTextSize(1);
    #endif

    for (uint16_t i = start_index; i < min(start_index + BUTTON_SCREEN_LIMIT, current_menu->list->size()); i++)
    {
      if (!current_menu || !current_menu->list || i >= current_menu->list->size())
        continue;
      uint16_t color = this->getColor(current_menu->list->get(i).color);
      #ifdef HAS_FULL_SCREEN
        bool is_setting_node = (current_menu->list->get(i).icon == SETTINGS && current_menu->list->get(i).color == TFTLIGHTGREY);
        if (is_setting_node && current_menu->selected == i) {
          uint16_t setting_color = current_menu->list->get(i).selected ? TFT_GREEN : TFT_RED;
          display_obj.key[i - start_index].initButton(&display_obj.tft, KEY_X, KEY_Y + (i - start_index) * (KEY_H + KEY_SPACING_Y), KEY_W, KEY_H, TFT_BLACK, TFT_LIGHTGREY, setting_color, (char*)"", KEY_TEXTSIZE);
          display_obj.key[i - start_index].drawButton(false, this->menuLabelWindow(current_menu->list->get(i).name));
          display_obj.tft.drawXBitmap(0,
                                      KEY_Y + (i - start_index) * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                                      menu_icons[current_menu->list->get(i).icon],
                                      ICON_W,
                                      ICON_H,
                                      TFT_BLACK,
                                      TFT_LIGHTGREY);
        } else if ((!is_setting_node && current_menu->list->get(i).selected) || (current_menu->selected == i)) {
          display_obj.key[i - start_index].drawButton(true, this->menuLabelWindow(current_menu->list->get(i).name));
          if ((current_menu->list->get(i).name != text09) && (current_menu->list->get(i).icon != 255))
            display_obj.tft.drawXBitmap(0,
                                        KEY_Y + (i - start_index) * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                                        menu_icons[current_menu->list->get(i).icon],
                                        ICON_W,
                                        ICON_H,
                                        TFT_BLACK,
                                        color);
        } else {
          display_obj.key[i - start_index].drawButton(false, this->menuLabelWindow(current_menu->list->get(i).name));
          if ((current_menu->list->get(i).name != text09) && (current_menu->list->get(i).icon != 255))
            display_obj.tft.drawXBitmap(0,
                                        KEY_Y + (i - start_index) * (KEY_H + KEY_SPACING_Y) - (ICON_H / 2),
                                        menu_icons[current_menu->list->get(i).icon],
                                        ICON_W,
                                        ICON_H,
                                        TFT_BLACK,
                                        is_setting_node ? TFT_LIGHTGREY : color);
        }

      #endif

      #ifdef HAS_MINI_SCREEN
        if ((current_menu->selected == i) || ((current_menu->list->get(i).icon != SETTINGS || current_menu->list->get(i).color != TFTLIGHTGREY) && current_menu->list->get(i).selected))
          this->drawMiniMenuButton(i - start_index, i, true);
        else
          this->drawMiniMenuButton(i - start_index, i, false);
      #endif
    }
    display_obj.tft.setFreeFont(NULL);
  }

  this->displayMenuButtons();

  #ifdef MARAUDER_LILYGO_T_DISPLAY_S3
    display_obj.tft.setFreeFont(NULL);
    display_obj.tft.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, 0x18C3);
    display_obj.tft.setTextColor(TFT_WHITE, 0x18C3);
    display_obj.tft.setTextSize(1);
    display_obj.tft.drawCentreString("UP/DN:Nav | Hold DN:OK | Hold UP:Back", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11, 1);
  #endif
}

// ============================================================
// BRIGHTNESS ADJUSTMENT MODE
// Hold top/bottom zone 1.5s to enter. TAP TOP = brighter, TAP BOTTOM = dimmer.
// TAP MIDDLE or wait 3s = save & exit.
// ============================================================
#ifndef HAS_MINI_SCREEN
  void MenuFunctions::brightnessMode() {
    extern void brightnessSave(uint8_t level);
    extern uint8_t getBrightnessLevel();

    const uint8_t levels[] = {26, 51, 77, 102, 128, 153, 179, 204, 230, 255};
    const uint8_t numLevels = 10;
    uint8_t level = getBrightnessLevel();

    // LEDC write compatibility (2.x vs 3.x board package)
    #if defined(MARAUDER_WS_C5_35)
      // Backlight is the I/O expander's PWM register, not a GPIO.
      #define BL_PREVIEW(duty) ws_exio_write(WS_EXIO_PWM_REG, (duty))
    #elif ESP_ARDUINO_VERSION_MAJOR >= 3
      #define BL_PREVIEW(duty) ledcWrite(TFT_BL, (duty))
    #else
      #define BL_PREVIEW(duty) ledcWrite(0, (duty))
    #endif

    display_obj.tft.fillScreen(TFT_BLACK);
    display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
    #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
      display_obj.tft.drawCentreString("BRIGHTNESS", SCREEN_WIDTH/2, 16, 2);
      display_obj.tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      display_obj.tft.drawCentreString("TOP = Brighter  |  BOT = Dimmer", SCREEN_WIDTH/2, 38, 1);
      display_obj.tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      display_obj.tft.drawCentreString("Hold UP or Wait 3s to Save", SCREEN_WIDTH/2, 115, 1);
    #else
      display_obj.tft.drawCentreString("BRIGHTNESS", TFT_WIDTH/2, 30, 2);
      display_obj.tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      display_obj.tft.drawCentreString("TAP TOP = BRIGHTER", TFT_WIDTH/2, 10, 1);
      display_obj.tft.drawCentreString("TAP BOTTOM = DIMMER", TFT_WIDTH/2, TFT_HEIGHT - 20, 1);
      display_obj.tft.setTextColor(TFT_RED, TFT_BLACK);
      display_obj.tft.drawCentreString("TAP MIDDLE or WAIT 3s = SAVE", TFT_WIDTH/2, TFT_HEIGHT/2 + 50, 1);
    #endif

    auto drawBar = [&]() {
      #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
        uint16_t barX = 40, barY = 56, barW = SCREEN_WIDTH - 80, barH = 26;
        display_obj.tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
        uint16_t fillW = (barW - 4) * (level + 1) / numLevels;
        display_obj.tft.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, TFT_BLACK);
        display_obj.tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, TFT_CYAN);
        display_obj.tft.fillRect(0, barY + barH + 6, SCREEN_WIDTH, 20, TFT_BLACK);
        display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
        String pct = String(levels[level] * 100 / 255) + "%";
        display_obj.tft.drawCentreString(pct, SCREEN_WIDTH/2, barY + barH + 8, 2);
      #else
        uint16_t barX = 30, barY = TFT_HEIGHT/2 - 25, barW = TFT_WIDTH - 60, barH = 30;
        display_obj.tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
        uint16_t fillW = (barW - 4) * (level + 1) / numLevels;
        display_obj.tft.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, TFT_BLACK);
        display_obj.tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, TFT_CYAN);
        display_obj.tft.fillRect(0, barY + barH + 5, TFT_WIDTH, 20, TFT_BLACK);
        display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
        String pct = String(levels[level] * 100 / 255) + "%";
        display_obj.tft.drawCentreString(pct, TFT_WIDTH/2, barY + barH + 8, 2);
      #endif
    };
    drawBar();

    #if defined(MARAUDER_LILYGO_T_DISPLAY_S3)
      uint32_t lastAction = millis();
      while (true) {
        if (millis() - lastAction >= 3000) {
          brightnessSave(level);
          break;
        }
        if (u_btn.isPressedNow()) {
          uint32_t t0 = millis();
          while (u_btn.isPressedNow()) {
            if (millis() - t0 >= 400) {
              brightnessSave(level);
              goto exit_brightness;
            }
            delay(10);
          }
          if (level < numLevels - 1) {
            level++;
            BL_PREVIEW(levels[level]);
            drawBar();
          }
          lastAction = millis();
        }
        else if (d_btn.isPressedNow()) {
          uint32_t t0 = millis();
          while (d_btn.isPressedNow()) {
            if (millis() - t0 >= 400) {
              brightnessSave(level);
              goto exit_brightness;
            }
            delay(10);
          }
          if (level > 0) {
            level--;
            BL_PREVIEW(levels[level]);
            drawBar();
          }
          lastAction = millis();
        }
        delay(20);
      }
      exit_brightness:;
    #else
    uint16_t zoneUp = TFT_HEIGHT * 25 / 100;
    uint16_t zoneDown = TFT_HEIGHT * 75 / 100;
    uint32_t lastTouch = millis();

    while (true) {
      // Auto-save after 3s of no touch
      if (millis() - lastTouch >= 3000) {
        brightnessSave(level);
        break;
      }

      uint16_t tx, ty;
      if (display_obj.updateTouch(&tx, &ty)) {
        lastTouch = millis();
        // Wait for release
        while (display_obj.updateTouch(&tx, &ty)) delay(10);

        if (ty < zoneUp) {
          if (level < numLevels - 1) {
            level++;
            BL_PREVIEW(levels[level]);
            drawBar();
          }
        } else if (ty >= zoneDown) {
          if (level > 0) {
            level--;
            BL_PREVIEW(levels[level]);
            drawBar();
          }
        } else {
          // Middle = save now
          brightnessSave(level);
          break;
        }
        delay(150);
      }
      delay(30);
    }
    #endif

    #undef BL_PREVIEW
    this->changeMenu(current_menu, true);
  }
#endif

void MenuFunctions::navUp() {
  if (current_menu == nullptr || current_menu->list == nullptr || current_menu->list->size() == 0) return;
  const uint16_t prev_selected = current_menu->selected;
  if (current_menu->selected > 0) {
    current_menu->selected--;
    if (current_menu->selected < this->menu_start_index) {
      this->buildButtons(current_menu, current_menu->selected);
      this->displayCurrentMenu(current_menu->selected);
    } else {
      this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
      if (!current_menu->list->get(prev_selected).selected ||
          (current_menu->list->get(prev_selected).icon == SETTINGS && current_menu->list->get(prev_selected).color == TFTLIGHTGREY))
        this->buttonNotSelected(prev_selected - this->menu_start_index, prev_selected);
    }
  } else {
    // Wrap to bottom
    current_menu->selected = current_menu->list->size() - 1;
    if (current_menu->selected >= BUTTON_SCREEN_LIMIT) {
      this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
      this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
    } else {
      this->buttonSelected(current_menu->selected, current_menu->selected);
      if (!current_menu->list->get(prev_selected).selected ||
          (current_menu->list->get(prev_selected).icon == SETTINGS && current_menu->list->get(prev_selected).color == TFTLIGHTGREY))
        this->buttonNotSelected(prev_selected, prev_selected);
    }
  }
}

void MenuFunctions::navDown() {
  if (current_menu == nullptr || current_menu->list == nullptr || current_menu->list->size() == 0) return;
  const uint16_t prev_selected = current_menu->selected;
  if (current_menu->selected < current_menu->list->size() - 1) {
    current_menu->selected++;
    if (current_menu->selected - this->menu_start_index >= BUTTON_SCREEN_LIMIT) {
      this->buildButtons(current_menu, current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
      this->displayCurrentMenu(current_menu->selected + 1 - BUTTON_SCREEN_LIMIT);
    } else {
      this->buttonSelected(current_menu->selected - this->menu_start_index, current_menu->selected);
      if (!current_menu->list->get(prev_selected).selected ||
          (current_menu->list->get(prev_selected).icon == SETTINGS && current_menu->list->get(prev_selected).color == TFTLIGHTGREY))
        this->buttonNotSelected(prev_selected - this->menu_start_index, prev_selected);
    }
  } else {
    // Wrap to top
    current_menu->selected = 0;
    if (prev_selected >= BUTTON_SCREEN_LIMIT) {
      this->buildButtons(current_menu, 0);
      this->displayCurrentMenu(0);
    } else {
      this->buttonSelected(0, 0);
      if (!current_menu->list->get(prev_selected).selected ||
          (current_menu->list->get(prev_selected).icon == SETTINGS && current_menu->list->get(prev_selected).color == TFTLIGHTGREY))
        this->buttonNotSelected(prev_selected, prev_selected);
    }
  }
}

void MenuFunctions::navOk() {
  if (current_menu && current_menu->list && current_menu->selected < current_menu->list->size()) {
    current_menu->list->get(current_menu->selected).callable();
  }
}

void MenuFunctions::navBack() {
  if (current_menu && current_menu->parentMenu != nullptr) {
    if (wifi_scan_obj.background_scan &&
        (current_menu == &attackTargetMenu ||
         current_menu == &attackStationMenu ||
         current_menu == &selectProbeSSIDsMenu ||
         current_menu == &wifiAPMenu ||
         current_menu == &wifiStationMenu)) {
      wifi_scan_obj.stopBackgroundScan();
    }
    this->changeMenu(current_menu->parentMenu, true);
  }
}

void MenuFunctions::simulateButton(String direction) {
  direction.toLowerCase();
  direction.trim();

  const bool menu_nav_active = (wifi_scan_obj.currentScanMode == WIFI_SCAN_OFF) ||
                               (wifi_scan_obj.currentScanMode == WIFI_CONNECTED) ||
                               (wifi_scan_obj.currentScanMode == OTA_UPDATE) ||
                               wifi_scan_obj.background_scan;

  if (!menu_nav_active) {
    if (direction == "back" || direction == "ok" || direction == "exit") {
      wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
      recon_obj.stop();
      display_obj.init();
      if (post_scan_callback) {
        auto cb = post_scan_callback;
        post_scan_callback = nullptr;
        cb();
      } else {
        changeMenu(current_menu, true);
      }
      Serial.println(F("SIM_BTN: Stopped scan and returned to menu"));
      return;
    } else if (direction == "up") {
      if (wifi_scan_obj.set_channel < 14)
        wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel + 1);
      else
        wifi_scan_obj.changeChannel(1);
      Serial.print(F("SIM_BTN: Channel UP -> "));
      Serial.println(wifi_scan_obj.set_channel);
      return;
    } else if (direction == "down") {
      if (wifi_scan_obj.set_channel > 1)
        wifi_scan_obj.changeChannel(wifi_scan_obj.set_channel - 1);
      else
        wifi_scan_obj.changeChannel(14);
      Serial.print(F("SIM_BTN: Channel DOWN -> "));
      Serial.println(wifi_scan_obj.set_channel);
      return;
    }
  }

  if (direction == "up") {
    this->navUp();
    Serial.print(F("SIM_BTN: UP -> selected="));
    Serial.print(current_menu ? current_menu->selected : -1);
    Serial.print(F(" ("));
    Serial.print((current_menu && current_menu->list && current_menu->selected < current_menu->list->size()) ? current_menu->list->get(current_menu->selected).name : "");
    Serial.println(F(")"));
  } else if (direction == "down") {
    this->navDown();
    Serial.print(F("SIM_BTN: DOWN -> selected="));
    Serial.print(current_menu ? current_menu->selected : -1);
    Serial.print(F(" ("));
    Serial.print((current_menu && current_menu->list && current_menu->selected < current_menu->list->size()) ? current_menu->list->get(current_menu->selected).name : "");
    Serial.println(F(")"));
  } else if (direction == "ok") {
    Serial.print(F("SIM_BTN: OK -> invoking: "));
    Serial.println((current_menu && current_menu->list && current_menu->selected < current_menu->list->size()) ? current_menu->list->get(current_menu->selected).name : "");
    this->navOk();
  } else if (direction == "back") {
    Serial.print(F("SIM_BTN: BACK -> from: "));
    Serial.print(current_menu ? current_menu->name : "");
    Serial.print(F(" to parent: "));
    Serial.println((current_menu && current_menu->parentMenu) ? current_menu->parentMenu->name : "NONE");
    this->navBack();
  } else {
    Serial.println(F("SIM_BTN: Unknown direction (use up, down, ok, back, exit)"));
  }
}

void MenuFunctions::dumpMenuStatus() {
  extern LinkedList<AccessPoint>* access_points;
  Serial.println(F("=== MENU DUMP ==="));
  Serial.print(F("scan_mode: "));
  Serial.println(wifi_scan_obj.currentScanMode);
  Serial.print(F("background_scan: "));
  Serial.println(wifi_scan_obj.background_scan ? F("true") : F("false"));
  Serial.print(F("ap_count: "));
  Serial.println(access_points ? access_points->size() : -1);
  Serial.print(F("free_internal_heap: "));
  Serial.println(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  Serial.print(F("largest_block: "));
  Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  Serial.print(F("guard_rejected: "));
  Serial.println(marauder::RuntimeMemoryGuard::instance().rejectedAllocations());
  if (current_menu != nullptr) {
    Serial.print(F("menu_name: "));
    Serial.println(current_menu->name);
    Serial.print(F("parent_name: "));
    Serial.println(current_menu->parentMenu != nullptr ? current_menu->parentMenu->name : "NONE");
    Serial.print(F("selected: "));
    Serial.println(current_menu->selected);
    Serial.print(F("start_index: "));
    Serial.println(this->menu_start_index);
    if (current_menu->list != nullptr) {
      Serial.print(F("count: "));
      Serial.println(current_menu->list->size());
      Serial.println(F("items:"));
      for (int i = 0; i < current_menu->list->size(); i++) {
        MenuNode node = current_menu->list->get(i);
        Serial.print(F("  "));
        Serial.print(i);
        Serial.print(F(": ["));
        Serial.print(i == current_menu->selected ? F("*") : F(" "));
        Serial.print(F("] "));
        Serial.print(node.name);
        if (node.icon == SETTINGS) {
          Serial.print(node.selected ? F(" (ON)") : F(" (OFF)"));
        }
        Serial.println();
      }
    } else {
      Serial.println(F("count: 0"));
    }
  } else {
    Serial.println(F("current_menu: NULL"));
  }
  Serial.println(F("=== END MENU DUMP ==="));
}

#endif
