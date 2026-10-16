#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "BoatModel.h"
#include "DemoBoat.h"
#include "LineDecoder.h"
#include "Settings.h"

// Connection to a Yacht Devices server port (TCP client or UDP listener),
// or the simulated boat in demo mode, running in its own task so a slow
// connect never stalls the display or the portal. The decoder, boat model and sniffer are shared with the main loop
// and must only be touched between lock() and unlock().
class DataLink {
 public:
  void begin(const Settings& s);
  void apply(const Settings& s);
  void setNetworkUp(bool up) { netUp_ = up; }

  void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex_); }
  BoatModel& model() { return model_; }
  LineDecoder& decoder() { return decoder_; }

  bool linkUp() const { return state_ == State::Connected || state_ == State::Listening || state_ == State::Demo; }
  bool demo() const { return state_ == State::Demo; }
  uint32_t msSinceLine() const { return millis() - lastLineMs_; }
  bool everReceived() const { return lastLineMs_ != 0; }

  // Call with the lock held.
  void statusJson(JsonObject out);
  void sniffJson(JsonObject out, uint32_t since, bool speedOnly);

  void startDiscovery();
  void discoveryJson(JsonObject out);

 private:
  enum class State : uint8_t { NoNetwork, Connecting, Connected, Retrying, Listening, Demo };

  struct Target {
    Transport transport = Transport::Tcp;
    String host;
    uint16_t port = 1456;
    bool demo = false;
  };

  static constexpr uint32_t kDemoIntervalMs = 500;

  static constexpr size_t kLineMax = 160;
  static constexpr size_t kSniffLines = 48;
  static constexpr size_t kSniffWidth = 112;

  struct SniffEntry {
    uint32_t seq;
    uint32_t ms;
    bool speed;
    char text[kSniffWidth];
  };

  static void taskEntry(void* arg);
  static void discoveryEntry(void* arg);
  void run();
  void runDiscovery();
  bool resolve(const String& host, IPAddress& out);
  void consume(const uint8_t* buf, size_t len);
  void handleLine(const char* line);
  static void demoLine(const char* line, void* self);
  void updateRate(uint32_t now);
  void setError(const char* fmt, ...);
  void setRemote(const IPAddress& ip);

  SemaphoreHandle_t mutex_ = nullptr;
  BoatModel model_;
  LineDecoder decoder_;
  DemoBoat demoBoat_;

  volatile bool netUp_ = false;
  volatile uint32_t configGen_ = 0;
  Target pending_;  // guarded by mutex_

  volatile State state_ = State::NoNetwork;
  Target active_;
  IPAddress remote_;
  char lastError_[64] = {0};
  uint32_t connects_ = 0;
  uint32_t bytes_ = 0;
  volatile uint32_t lastLineMs_ = 0;
  uint32_t rateWindowStart_ = 0;
  uint32_t rateWindowLines_ = 0;
  float lineRate_ = 0;

  char line_[kLineMax];
  size_t lineLen_ = 0;
  bool lineOverflow_ = false;

  SniffEntry sniff_[kSniffLines];
  uint32_t sniffSeq_ = 0;

  // Discovery (TCP port scan of the local /24)
  volatile bool discovering_ = false;
  volatile uint8_t discoveryProgress_ = 0;
  uint16_t discoveryPort_ = 1456;
  static constexpr size_t kMaxFound = 12;
  struct Found {
    uint32_t ip;
    uint16_t port;
  };
  Found found_[kMaxFound];
  volatile size_t nFound_ = 0;
  bool discoveryRan_ = false;
};
