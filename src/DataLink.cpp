#include "DataLink.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <stdarg.h>

namespace {

constexpr uint32_t kConnectTimeoutMs = 3000;
constexpr uint32_t kSilenceReconnectMs = 15000;
constexpr uint32_t kMaxBackoffMs = 15000;

const char* transportName(Transport t) { return t == Transport::Tcp ? "tcp" : "udp"; }

}  // namespace

void DataLink::begin(const Settings& s) {
  mutex_ = xSemaphoreCreateMutex();
  apply(s);
  xTaskCreatePinnedToCore(taskEntry, "datalink", 6144, this, 2, nullptr, 0);
}

void DataLink::apply(const Settings& s) {
  lock();
  decoder_.setProtocol(s.protocol);
  model_.configure(s.modelConfig());
  if (s.transport != pending_.transport || s.host != pending_.host || s.port != pending_.port ||
      s.demo != pending_.demo || configGen_ == 0) {
    pending_.transport = s.transport;
    pending_.host = s.host;
    pending_.port = s.port;
    pending_.demo = s.demo;
    configGen_ = configGen_ + 1;
  }
  unlock();
}

void DataLink::taskEntry(void* arg) { static_cast<DataLink*>(arg)->run(); }

// The status endpoint reads these from the loop task, so write them under
// the lock.
void DataLink::setError(const char* fmt, ...) {
  char msg[sizeof(lastError_)];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  log_w("%s", msg);
  lock();
  strlcpy(lastError_, msg, sizeof(lastError_));
  unlock();
}

void DataLink::setRemote(const IPAddress& ip) {
  lock();
  remote_ = ip;
  unlock();
}

bool DataLink::resolve(const String& host, IPAddress& out) {
  if (host.length() == 0) {
    out = WiFi.gatewayIP();
    return out != IPAddress(0, 0, 0, 0);
  }
  if (out.fromString(host)) return true;
  return WiFi.hostByName(host.c_str(), out) == 1;
}

void DataLink::run() {
  WiFiClient client;
  WiFiUDP udp;
  bool udpOpen = false;
  uint32_t appliedGen = 0;
  uint32_t nextAttemptMs = 0;
  uint32_t backoffMs = 1000;
  uint32_t lastByteMs = 0;
  uint32_t demoStartMs = 0;
  uint8_t buf[512];

  auto closeAll = [&]() {
    client.stop();
    if (udpOpen) {
      udp.stop();
      udpOpen = false;
    }
    lineLen_ = 0;
    lineOverflow_ = false;
  };

  for (;;) {
    uint32_t now = millis();

    if (appliedGen != configGen_) {
      lock();
      active_ = pending_;
      appliedGen = configGen_;
      decoder_.resetStats();
      lastError_[0] = 0;
      unlock();
      closeAll();
      lastLineMs_ = 0;
      nextAttemptMs = now;
      backoffMs = 1000;
      demoStartMs = now;
    }

    // Demo mode needs no network: the simulated boat's sentences go through
    // the same decoder as the gateway's.
    if (active_.demo) {
      state_ = State::Demo;
      lock();
      remote_ = IPAddress();
      unlock();
      demoBoat_.sentences(now - demoStartMs, demoLine, this);
      updateRate(now);
      vTaskDelay(pdMS_TO_TICKS(kDemoIntervalMs));
      continue;
    }

    if (!netUp_) {
      if (state_ != State::NoNetwork) closeAll();
      state_ = State::NoNetwork;
      nextAttemptMs = now;  // connect straight away once Wi-Fi is back
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    updateRate(now);

    if (active_.transport == Transport::Udp) {
      if (!udpOpen) {
        udpOpen = udp.begin(active_.port);
        if (!udpOpen) {
          setError("cannot listen on UDP port %u", active_.port);
          state_ = State::Retrying;
          vTaskDelay(pdMS_TO_TICKS(2000));
          continue;
        }
        setRemote(IPAddress());
        state_ = State::Listening;
      }
      int size = udp.parsePacket();
      if (size > 0) {
        if (udp.remoteIP() != remote_) setRemote(udp.remoteIP());
        while (size > 0) {
          int n = udp.read(buf, sizeof(buf));
          if (n <= 0) break;
          consume(buf, n);
          size -= n;
        }
        const uint8_t eol = '\n';
        consume(&eol, 1);  // a datagram never continues in the next one
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      continue;
    }

    // TCP
    if (!client.connected()) {
      if (state_ == State::Connected) {
        setError("connection closed by %s", remote_.toString().c_str());
        nextAttemptMs = now + 1000;
        backoffMs = 1000;
        lineLen_ = 0;
      }
      state_ = State::Retrying;
      if (static_cast<int32_t>(now - nextAttemptMs) < 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      IPAddress ip;
      if (!resolve(active_.host, ip)) {
        setError("cannot resolve '%s'", active_.host.length() ? active_.host.c_str() : "gateway");
        nextAttemptMs = now + 5000;
        continue;
      }
      setRemote(ip);
      state_ = State::Connecting;
      if (client.connect(ip, active_.port, kConnectTimeoutMs)) {
        client.setNoDelay(true);
        connects_++;
        lastError_[0] = 0;
        lastByteMs = millis();
        backoffMs = 1000;
        state_ = State::Connected;
        log_i("connected to %s:%u", ip.toString().c_str(), active_.port);
      } else {
        setError("no answer from %s:%u", ip.toString().c_str(), active_.port);
        nextAttemptMs = millis() + backoffMs;
        backoffMs = min(backoffMs * 2, kMaxBackoffMs);
        state_ = State::Retrying;
      }
      continue;
    }

    int avail = client.available();
    if (avail > 0) {
      int n = client.read(buf, min(avail, static_cast<int>(sizeof(buf))));
      if (n > 0) {
        consume(buf, n);
        lastByteMs = millis();
      }
    } else {
      if (now - lastByteMs > kSilenceReconnectMs) {
        setError("no data for %us, reconnecting", static_cast<unsigned>(kSilenceReconnectMs / 1000));
        client.stop();
        state_ = State::Retrying;
        nextAttemptMs = now + 500;
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void DataLink::consume(const uint8_t* buf, size_t len) {
  bytes_ += len;
  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(buf[i]);
    if (c == '\r' || c == '\n') {
      if (lineLen_ > 0 && !lineOverflow_) {
        line_[lineLen_] = 0;
        handleLine(line_);
      }
      lineLen_ = 0;
      lineOverflow_ = false;
    } else if (lineLen_ < kLineMax - 1) {
      line_[lineLen_++] = c;
    } else {
      lineOverflow_ = true;
    }
  }
}

void DataLink::updateRate(uint32_t now) {
  if (now - rateWindowStart_ >= 2000) {
    lineRate_ = rateWindowLines_ * 1000.0f / (now - rateWindowStart_);
    rateWindowLines_ = 0;
    rateWindowStart_ = now;
  }
}

void DataLink::demoLine(const char* line, void* self) { static_cast<DataLink*>(self)->handleLine(line); }

void DataLink::handleLine(const char* line) {
  uint32_t now = millis();
  lock();
  uint32_t before = decoder_.stats().samples;
  decoder_.feed(line, now, model_);
  bool speed = decoder_.stats().samples != before;
  SniffEntry& e = sniff_[sniffSeq_ % kSniffLines];
  e.seq = ++sniffSeq_;
  e.ms = now;
  e.speed = speed;
  strlcpy(e.text, line, sizeof(e.text));
  unlock();
  lastLineMs_ = now;
  rateWindowLines_++;
}

void DataLink::statusJson(JsonObject out) {
  static const char* kStates[] = {"no_network", "connecting", "connected", "retrying", "listening", "demo"};
  out["transport"] = transportName(active_.transport);
  out["host"] = active_.host;
  out["port"] = active_.port;
  out["remote"] = remote_ == IPAddress() ? String() : remote_.toString();
  out["state"] = kStates[static_cast<int>(state_)];
  out["lastError"] = lastError_;
  out["connects"] = connects_;
  out["bytes"] = bytes_;
  out["lineRate"] = roundf(lineRate_ * 10) / 10;
  out["lastLineAgo"] = lastLineMs_ ? static_cast<int32_t>(millis() - lastLineMs_) : -1;
  const LineDecoder::Stats& st = decoder_.stats();
  out["protocol"] = protocolLabel(decoder_.protocol());
  out["detected"] = st.detected == DataProtocol::Auto ? "" : protocolLabel(st.detected);
  out["lines"] = st.lines;
  out["nmeaLines"] = st.nmeaLines;
  out["rawLines"] = st.rawLines;
  out["badChecksum"] = st.badChecksum;
  out["ignored"] = st.ignored;
  out["samples"] = st.samples;
}

void DataLink::sniffJson(JsonObject out, uint32_t since, bool speedOnly) {
  out["seq"] = sniffSeq_;
  JsonArray lines = out["lines"].to<JsonArray>();
  uint32_t first = sniffSeq_ > kSniffLines ? sniffSeq_ - kSniffLines + 1 : 1;
  if (since + 1 > first) first = since + 1;
  uint32_t now = millis();
  for (uint32_t seq = first; seq <= sniffSeq_; seq++) {
    const SniffEntry& e = sniff_[(seq - 1) % kSniffLines];
    if (e.seq != seq) continue;
    if (speedOnly && !e.speed) continue;
    JsonObject o = lines.add<JsonObject>();
    o["seq"] = e.seq;
    o["ago"] = now - e.ms;
    o["speed"] = e.speed;
    o["text"] = e.text;
  }
}

// ---------------------------------------------------------------------------
// Discovery: find hosts with the server port open. Yacht Devices gateways
// listen on TCP 1456 (server #1) by default; servers #2/#3 commonly use
// 1457/1458. The gateway is probed on all of those first, then the rest of
// the /24 on the configured port.

void DataLink::startDiscovery() {
  if (discovering_ || !netUp_) return;
  discovering_ = true;
  discoveryRan_ = true;
  discoveryProgress_ = 0;
  nFound_ = 0;
  discoveryPort_ = active_.port;
  if (xTaskCreatePinnedToCore(discoveryEntry, "discover", 4096, this, 1, nullptr, 0) != pdPASS) {
    discovering_ = false;
  }
}

void DataLink::discoveryEntry(void* arg) {
  static_cast<DataLink*>(arg)->runDiscovery();
  vTaskDelete(nullptr);
}

namespace {

constexpr int kBatch = 6;  // lwIP has 16 sockets in total
constexpr int kProbeTimeoutMs = 350;

struct Probe {
  uint32_t ip;  // network byte order
  uint16_t port;
};

// Non-blocking connect to each probe; sets open[i] when the 3-way handshake
// completes within the timeout.
void probeBatch(const Probe* probes, int n, bool* open) {
  int fds[kBatch];
  for (int i = 0; i < n; i++) {
    open[i] = false;
    fds[i] = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fds[i] < 0) continue;
    lwip_fcntl(fds[i], F_SETFL, O_NONBLOCK);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(probes[i].port);
    addr.sin_addr.s_addr = probes[i].ip;
    lwip_connect(fds[i], reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  }
  uint32_t start = millis();
  int pending = n;
  bool done[kBatch] = {false};
  while (pending > 0 && millis() - start < static_cast<uint32_t>(kProbeTimeoutMs)) {
    fd_set wfds;
    FD_ZERO(&wfds);
    int maxfd = -1;
    for (int i = 0; i < n; i++) {
      if (fds[i] >= 0 && !done[i]) {
        FD_SET(fds[i], &wfds);
        maxfd = max(maxfd, fds[i]);
      }
    }
    if (maxfd < 0) break;
    timeval tv = {0, 50 * 1000};
    if (lwip_select(maxfd + 1, nullptr, &wfds, nullptr, &tv) <= 0) continue;
    for (int i = 0; i < n; i++) {
      if (fds[i] >= 0 && !done[i] && FD_ISSET(fds[i], &wfds)) {
        int err = 0;
        socklen_t len = sizeof(err);
        lwip_getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
        open[i] = err == 0;
        done[i] = true;
        pending--;
      }
    }
  }
  for (int i = 0; i < n; i++) {
    if (fds[i] >= 0) lwip_close(fds[i]);
  }
}

}  // namespace

void DataLink::runDiscovery() {
  IPAddress self = WiFi.localIP();
  IPAddress gw = WiFi.gatewayIP();
  IPAddress mask = WiFi.subnetMask();

  static constexpr uint16_t kGatewayPorts[] = {1456, 1457, 1458, 1459, 1460, 10110, 2000};
  Probe probes[64];
  int nProbes = 0;
  auto add = [&](uint32_t ip, uint16_t port) {
    if (nProbes < 64) probes[nProbes++] = {ip, port};
  };
  if (gw != IPAddress(0, 0, 0, 0)) {
    add(static_cast<uint32_t>(gw), discoveryPort_);
    for (uint16_t p : kGatewayPorts) {
      if (p != discoveryPort_) add(static_cast<uint32_t>(gw), p);
    }
  }

  // Host range: the /24 around our address, regardless of a wider netmask.
  uint32_t selfHost = static_cast<uint32_t>(self);  // network byte order
  uint32_t base = selfHost & 0x00FFFFFF;
  (void)mask;
  const int kHosts = 254;
  int total = nProbes + kHosts;
  int doneCount = 0;

  auto record = [&](const Probe& p) {
    for (size_t i = 0; i < nFound_; i++) {
      if (found_[i].ip == p.ip && found_[i].port == p.port) return;
    }
    if (nFound_ < kMaxFound) {
      found_[nFound_] = {p.ip, p.port};
      nFound_ = nFound_ + 1;
    }
  };

  bool open[kBatch];
  for (int i = 0; i < nProbes; i += kBatch) {
    int n = min(kBatch, nProbes - i);
    probeBatch(&probes[i], n, open);
    for (int j = 0; j < n; j++) {
      if (open[j]) record(probes[i + j]);
    }
    doneCount += n;
    discoveryProgress_ = doneCount * 100 / total;
  }

  Probe batch[kBatch];
  int nb = 0;
  for (int host = 1; host <= kHosts && netUp_; host++) {
    uint32_t ip = base | (static_cast<uint32_t>(host) << 24);
    if (ip != selfHost && ip != static_cast<uint32_t>(gw)) batch[nb++] = {ip, discoveryPort_};
    if (nb == kBatch || host == kHosts) {
      probeBatch(batch, nb, open);
      for (int j = 0; j < nb; j++) {
        if (open[j]) record(batch[j]);
      }
      nb = 0;
    }
    doneCount++;
    discoveryProgress_ = doneCount * 100 / total;
  }
  discoveryProgress_ = 100;
  discovering_ = false;
}

void DataLink::discoveryJson(JsonObject out) {
  out["running"] = discovering_;
  out["ran"] = discoveryRan_;
  out["progress"] = discoveryProgress_;
  out["port"] = discoveryPort_;
  JsonArray list = out["found"].to<JsonArray>();
  for (size_t i = 0; i < nFound_; i++) {
    JsonObject o = list.add<JsonObject>();
    o["ip"] = IPAddress(found_[i].ip).toString();
    o["port"] = found_[i].port;
  }
}
