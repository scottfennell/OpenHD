//
// rewritten by Rapha 04.08.24
// based on consti10's code

#include "openhd_led.h"

#include <chrono>
#include <thread>

#include "openhd_platform.h"
#include "openhd_spdlog.h"
#include "openhd_util.h"
#include "openhd_util_filesystem.h"

static void toggle_secondary_led(const bool on) {
  if (OHDPlatform::instance().is_rpi()) {
    static constexpr auto filename = "/sys/class/leds/PWR/brightness";
    ;
    const auto content = on ? "1" : "0";
    if (!OHDFilesystemUtil::exists(filename)) {
      OHDFilesystemUtil::write_file(filename, content);
    }
  } else if (OHDPlatform::instance().is_radxa_cm3()) {
    static constexpr auto filename = "/sys/class/leds/pwr-led-red/brightness";
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_rock5_a()) {
    static constexpr auto filename = "/sys/class/leds/user-led2/brightness";
    const auto content = on ? "1" : "0";
    //OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_x20()) {
    static constexpr auto filename =
        "/sys/class/leds/openhd-x20dev:red:usr/brightness";
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  }
}

static void toggle_primary_led(const bool on) {
  if (OHDPlatform::instance().is_rpi()) {
    static constexpr auto filename = "/sys/class/leds/ACT/brightness";
    ;
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_zero3w()) {
    static constexpr auto filename = "/sys/class/leds/board-led/brightness";
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_radxa_cm3()) {
    static constexpr auto filename = "/sys/class/leds/pi-led-green/brightness";
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_rock5_a()) {
    static constexpr auto filename = "/sys/class/leds/user-led1/brightness";
    const auto content = on ? "1" : "0";
    //OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_rock5_b()) {
    static constexpr auto filename = "/sys/class/leds/mmc0::/brightness";
    const auto content = on ? "1" : "0";
    //OHDFilesystemUtil::write_file(filename, content);
  } else if (OHDPlatform::instance().is_x20()) {
    static constexpr auto filename =
        "/sys/class/leds/openhd-x20dev:green:usr/brightness";
    const auto content = on ? "1" : "0";
    OHDFilesystemUtil::write_file(filename, content);
  }
}

static void secondary_led_on_off_delayed(
    const std::chrono::milliseconds &delay1,
    const std::chrono::milliseconds &delay2) {
  toggle_secondary_led(false);
  std::this_thread::sleep_for(delay1);
  toggle_secondary_led(true);
  std::this_thread::sleep_for(delay2);
}

static void primary_led_on_off_delayed(
    const std::chrono::milliseconds &delay1,
    const std::chrono::milliseconds &delay2) {
  toggle_primary_led(false);
  toggle_secondary_led(false);
  std::this_thread::sleep_for(delay1);
  toggle_primary_led(true);
  std::this_thread::sleep_for(delay2);
}

static void blink_leds_fast(const std::chrono::milliseconds &delay) {
  secondary_led_on_off_delayed(delay, delay);
  primary_led_on_off_delayed(delay, delay);
}

static void blink_leds_slow(const std::chrono::milliseconds &delay) {
  primary_led_on_off_delayed(delay, delay);
}

static void blink_leds_alternating(const std::chrono::milliseconds &delay,
                                   std::atomic<bool> &running) {
  while (running) {
    toggle_secondary_led(true);
    toggle_primary_led(false);
    std::this_thread::sleep_for(delay);
    toggle_secondary_led(false);
    toggle_primary_led(true);
    std::this_thread::sleep_for(delay);
  }
}

openhd::LEDManager &openhd::LEDManager::instance() {
  static LEDManager instance{};
  return instance;
}

void openhd::LEDManager::set_secondary_led_status(int status) {
  const bool on = status != STATUS_ON;
  toggle_secondary_led(on);
}

void openhd::LEDManager::set_primary_led_status(int status) {
  const bool on = status != STATUS_ON;
  toggle_primary_led(on);
}

openhd::LEDManager::LEDManager()
    : m_loading_thread(nullptr),
      m_running(false),
      m_has_error(false),
      m_is_loading(false) {}

openhd::LEDManager::~LEDManager() { stop_loading_thread(); }

void openhd::LEDManager::start_loading_thread() {
  if (m_running) return;  // already running

  m_running = true;
  m_loading_thread =
      std::make_unique<std::thread>(&LEDManager::loading_loop, this);
}

void openhd::LEDManager::stop_loading_thread() {
  if (m_running) {
    m_running = false;
    if (m_loading_thread && m_loading_thread->joinable()) {
      m_loading_thread->join();
    }
    m_loading_thread = nullptr;
  }
}

void openhd::LEDManager::loading_loop() {
  while (m_running) {
    if (m_has_error) {
      blink_error();
    } else if (m_is_loading) {
      blink_loading();
    } else {
      blink_okay();
    }
  }
}

void openhd::LEDManager::blink_okay() {
  blink_leds_fast(std::chrono::milliseconds(50));
}

void openhd::LEDManager::blink_loading() {
  blink_leds_slow(std::chrono::milliseconds(200));
}

void openhd::LEDManager::blink_error() {
  blink_leds_alternating(std::chrono::milliseconds(50), m_running);
}

void openhd::LEDManager::set_status_okay() {
  if (m_is_loading) {
    stop_loading_thread();
  }
  m_has_error = false;
  m_is_loading = false;
  start_loading_thread();
}

void openhd::LEDManager::set_status_loading() {
  if (m_has_error) {
    stop_loading_thread();
  }
  m_has_error = false;
  m_is_loading = true;
  start_loading_thread();
}

void openhd::LEDManager::set_status_error() {
  if (m_is_loading) {
    stop_loading_thread();
  }
  m_has_error = true;
  m_is_loading = false;
  start_loading_thread();
}

void openhd::LEDManager::set_status_stopped() {
  stop_loading_thread();
  // it's weird but it's inverted
  set_primary_led_status(STATUS_ON);
  set_secondary_led_status(STATUS_ON);
}
