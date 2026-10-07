#include "LineDecoder.h"

#include <stdio.h>
#include <string.h>

#include "Nmea0183.h"

const char* protocolLabel(DataProtocol p) {
  switch (p) {
    case DataProtocol::Auto: return "auto";
    case DataProtocol::Nmea0183: return "nmea0183";
    case DataProtocol::Raw: return "raw";
  }
  return "auto";
}

void LineDecoder::deliver(const Decoded& d, uint32_t src, const char* via, uint32_t nowMs, BoatModel& model) {
  model.ingest(d, src, via, nowMs);
  stats_.samples += d.count;
  if (d.hasTime) {
    time_ = d.epochSeconds;
    timePending_ = true;
  }
}

void LineDecoder::feed(const char* line, uint32_t nowMs, BoatModel& model) {
  if (!line || !*line) return;
  stats_.lines++;

  if (protocol_ != DataProtocol::Raw) {
    nmea0183::Sentence s;
    switch (nmea0183::parse(line, s)) {
      case nmea0183::ParseResult::Ok: {
        stats_.nmeaLines++;
        stats_.detected = DataProtocol::Nmea0183;
        if (strcmp(s.type, "DPT") == 0 && s.data.has(Quantity::Dpt)) {
          seenDpt_ = true;
          lastDptMs_ = nowMs;
        } else if (strcmp(s.type, "DBT") == 0 && seenDpt_ && nowMs - lastDptMs_ < kDptPreferMs) {
          return;
        }
        deliver(s.data, nmeaSourceId(s.talker), s.type, nowMs, model);
        return;
      }
      case nmea0183::ParseResult::BadChecksum:
        stats_.badChecksum++;
        return;
      case nmea0183::ParseResult::Malformed:
        stats_.ignored++;
        return;
      case nmea0183::ParseResult::NotNmea:
        break;
    }
  }

  if (protocol_ != DataProtocol::Nmea0183) {
    ydraw::Frame f;
    if (ydraw::parseLine(line, f)) {
      stats_.rawLines++;
      stats_.detected = DataProtocol::Raw;
      if (f.direction != 'R') return;
      Decoded d;
      if (!raw_.feed(f, d)) return;
      char via[8];
      snprintf(via, sizeof(via), "%u", static_cast<unsigned>(f.pgn));
      deliver(d, n2kSourceId(f.source), via, nowMs, model);
      return;
    }
  }

  stats_.ignored++;
}

bool LineDecoder::takeTime(int64_t& epochSeconds) {
  if (!timePending_) return false;
  timePending_ = false;
  epochSeconds = time_;
  return true;
}
