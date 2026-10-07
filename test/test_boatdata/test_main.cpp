#include <math.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "LineDecoder.h"
#include "Nmea0183.h"
#include "BoatModel.h"
#include "Items.h"
#include "YdRaw.h"

void setUp() {}
void tearDown() {}

using nmea0183::ParseResult;

static constexpr float kKn = 0.514444f;

// Value of `q` in the sentence, or -9999 when absent.
static float val(const Decoded& d, Quantity q) {
  float v;
  return d.get(q, v) ? v : -9999.0f;
}
static float valKn(const Decoded& d, Quantity q) {
  float v;
  return d.get(q, v) ? v / kKn : -9999.0f;
}

static void test_vhw_knots() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDVHW,,T,,M,5.20,N,9.63,K*43\r\n", s));
  TEST_ASSERT_EQUAL_STRING("YD", s.talker);
  TEST_ASSERT_EQUAL_STRING("VHW", s.type);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.20f, valKn(s.data, Quantity::Stw));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Sog));
}

static void test_vhw_kmh_only_falls_back_to_kmh_field() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIVHW,,T,,M,,N,11.11,K*7B", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, valKn(s.data, Quantity::Stw));
}

static void test_rmc_active_gives_sog_and_time() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok,
                    nmea0183::parse("$YDRMC,123519.00,A,4807.038,N,01131.000,E,6.4,84.4,230394,3.1,W,A*15", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.4f, valKn(s.data, Quantity::Sog));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 84.4f, val(s.data, Quantity::CogTrue));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -3.1f, val(s.data, Quantity::Variation));
  TEST_ASSERT_TRUE(s.data.hasTime);
  TEST_ASSERT_EQUAL_INT64(764426119, s.data.epochSeconds);
}

static void test_rmc_void_status_has_no_sog() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok,
                    nmea0183::parse("$GPRMC,123519.00,V,4807.038,N,01131.000,E,6.4,84.4,230394,3.1,W,N*07", s));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Sog));
}

static void test_vtg_valid_and_invalid_mode() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDVTG,054.7,T,034.4,M,005.5,N,010.2,K,A*2F", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.5f, valKn(s.data, Quantity::Sog));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 54.7f, val(s.data, Quantity::CogTrue));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 34.4f, val(s.data, Quantity::Cog));

  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDVTG,054.7,T,034.4,M,005.5,N,010.2,K,N*20", s));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Sog));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Cog));
}

static void test_vbw_gives_both_speeds() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDVBW,4.10,0.10,A,4.50,0.20,A,,V,,V*59", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.10f, valKn(s.data, Quantity::Stw));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.50f, valKn(s.data, Quantity::Sog));
}

static void test_zda_time() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDZDA,201530.00,04,10,2026,00,00*6A", s));
  TEST_ASSERT_TRUE(s.data.hasTime);
  TEST_ASSERT_EQUAL_INT64(1791144930, s.data.epochSeconds);
}

static void test_bad_checksum_rejected() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::BadChecksum, nmea0183::parse("$YDVHW,,T,,M,5.20,N,9.63,K*44", s));
  TEST_ASSERT_EQUAL_UINT8(0, s.data.count);
}

static void test_unrelated_sentence_ok_without_values() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00", s));
  TEST_ASSERT_EQUAL_UINT8(0, s.data.count);
}

static void test_mwv_apparent_and_true() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDMWV,045.0,R,10.5,N,A*15", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, val(s.data, Quantity::Awa));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, valKn(s.data, Quantity::Aws));

  // 315 deg off the bow = 45 to port; speed in m/s
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIMWV,315.0,T,6.0,M,A", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0f, val(s.data, Quantity::Twa));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, val(s.data, Quantity::Tws));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Awa));

  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIMWV,315.0,T,6.0,M,V", s));
  TEST_ASSERT_EQUAL_UINT8(0, s.data.count);
}

static void test_vwr_port_side_is_negative() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIVWR,32.0,L,15.2,N,7.8,M,28.2,K", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -32.0f, val(s.data, Quantity::Awa));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.2f, valKn(s.data, Quantity::Aws));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIVWT,120.0,R,9.0,N,,,,", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, val(s.data, Quantity::Twa));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f, valKn(s.data, Quantity::Tws));
}

static void test_mwd_both_references() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$WIMWD,270.0,T,250.5,M,12.0,N,6.2,M", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 250.5f, val(s.data, Quantity::Twd));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 270.0f, val(s.data, Quantity::TwdTrue));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Tws));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$WIMWD,270.0,T,,M,12.0,N,6.2,M", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 270.0f, val(s.data, Quantity::TwdTrue));
}

static void test_heading_sentences() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$HCHDG,205.0,2.0,W,19.5,E", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 203.0f, val(s.data, Quantity::Hdg));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.5f, val(s.data, Quantity::Variation));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$HCHDM,359.7,M", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 359.7f, val(s.data, Quantity::Hdg));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$GPHDT,10.0,T", s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, val(s.data, Quantity::HdgTrue));
}

static void test_depth_sentences() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$SDDPT,12.4,-0.8,", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.6f, val(s.data, Quantity::Dpt));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$SDDBT,40.7,f,12.4,M,6.8,F", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.4f, val(s.data, Quantity::Dpt));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$SDDBT,10.0,f,,M,,F", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.048f, val(s.data, Quantity::Dpt));
}

static void test_waypoint_sentences() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok,
                    nmea0183::parse("$GPRMB,A,0.66,L,003,004,4917.24,N,12309.57,W,001.3,052.5,000.5,V,A", s));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.3f * 1852, val(s.data, Quantity::Dtw));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 52.5f, val(s.data, Quantity::BtwTrue));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, valKn(s.data, Quantity::Vmc));
  TEST_ASSERT_EQUAL(ParseResult::Ok,
                    nmea0183::parse("$GPRMB,V,0.66,L,003,004,4917.24,N,12309.57,W,001.3,052.5,000.5,V,A", s));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Dtw));
  TEST_ASSERT_EQUAL(ParseResult::Ok,
                    nmea0183::parse("$GPBWC,220516,5130.02,N,00046.34,W,213.8,T,218.0,M,0004.6,N,EGLM,A", s));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.6f * 1852, val(s.data, Quantity::Dtw));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 213.8f, val(s.data, Quantity::BtwTrue));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 218.0f, val(s.data, Quantity::Btw));
}

static void test_vpw_vmg() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$IIVPW,-4.5,N,,M", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -4.5f, valKn(s.data, Quantity::Vmg));
}

static void test_mtw_water_temperature() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDMTW,18.4,C", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.4f, val(s.data, Quantity::Mtw));
  TEST_ASSERT_EQUAL(ParseResult::Ok, nmea0183::parse("$YDMTW,65.0,F", s));
  TEST_ASSERT_FALSE(s.data.has(Quantity::Mtw));
}

static void test_raw_line_is_not_nmea() {
  nmea0183::Sentence s;
  TEST_ASSERT_EQUAL(ParseResult::NotNmea, nmea0183::parse("17:33:21.107 R 19F51323 01 2F 30 70", s));
}

static void test_pgn_from_can_id_matches_manual_examples() {
  TEST_ASSERT_EQUAL_UINT32(128275, ydraw::pgnFromCanId(0x19F51323));
  TEST_ASSERT_EQUAL_UINT32(129025, ydraw::pgnFromCanId(0x09F80115));
  TEST_ASSERT_EQUAL_UINT32(130306, ydraw::pgnFromCanId(0x09FD0205));
  TEST_ASSERT_EQUAL_UINT32(126720, ydraw::pgnFromCanId(0x1DEFFF00));
}

static void test_raw_line_parse() {
  ydraw::Frame f;
  TEST_ASSERT_TRUE(ydraw::parseLine("17:33:21.107 R 19F51323 01 2F 30 70 00 2F 30 70\r\n", f));
  TEST_ASSERT_EQUAL_CHAR('R', f.direction);
  TEST_ASSERT_EQUAL_HEX32(0x19F51323, f.canId);
  TEST_ASSERT_EQUAL_UINT8(0x23, f.source);
  TEST_ASSERT_EQUAL_UINT8(6, f.priority);
  TEST_ASSERT_EQUAL_UINT8(8, f.len);
  TEST_ASSERT_EQUAL_HEX8(0x70, f.data[7]);

  TEST_ASSERT_TRUE(ydraw::parseLine("17:33:21.108 R 19F51323 02 00", f));
  TEST_ASSERT_EQUAL_UINT8(2, f.len);
}

static void test_raw_line_rejects_garbage() {
  ydraw::Frame f;
  TEST_ASSERT_FALSE(ydraw::parseLine("$YDVHW,,T,,M,5.20,N,9.63,K*43", f));
  TEST_ASSERT_FALSE(ydraw::parseLine("17:33:21.107 R 19F51323", f));
  TEST_ASSERT_FALSE(ydraw::parseLine("17:33:21.107 R 19F51323 01 02 03 04 05 06 07 08 09", f));
  TEST_ASSERT_FALSE(ydraw::parseLine("17:33:21.107 R XYZ 01", f));
  TEST_ASSERT_FALSE(ydraw::parseLine("17:33:21.107 19F51323 01", f));
}

static void test_decode_128259_speed_water() {
  ydraw::Frame f;
  TEST_ASSERT_TRUE(ydraw::parseLine("10:00:00.000 R 09F50305 01 34 01 FF FF 00 00 FF", f));
  TEST_ASSERT_EQUAL_UINT32(128259, f.pgn);
  Decoded d;
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.08f, val(d, Quantity::Stw));
  TEST_ASSERT_FALSE(d.has(Quantity::Sog));
}

static void test_decode_129026_cog_sog() {
  ydraw::Frame f;
  TEST_ASSERT_TRUE(ydraw::parseLine("10:00:00.000 R 09F80216 00 FC 34 12 01 01 FF FF", f));
  TEST_ASSERT_EQUAL_UINT32(129026, f.pgn);
  Decoded d;
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.57f, val(d, Quantity::Sog));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.70f, val(d, Quantity::CogTrue));  // 0x1234 * 1e-4 rad
  TEST_ASSERT_FALSE(d.has(Quantity::Cog));

  TEST_ASSERT_TRUE(ydraw::parseLine("10:00:00.000 R 09F80216 00 FD 34 12 01 01 FF FF", f));  // magnetic
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.70f, val(d, Quantity::Cog));
}

static void test_decode_129026_not_available() {
  ydraw::Frame f;
  TEST_ASSERT_TRUE(ydraw::parseLine("10:00:00.000 R 09F80216 00 FC FF FF FF FF FF FF", f));
  Decoded d;
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FALSE(d.has(Quantity::Sog));
}

static void test_decode_126992_system_time() {
  ydraw::Frame f;
  TEST_ASSERT_TRUE(ydraw::parseLine("10:00:00.000 R 0DF01001 00 F0 20 4E 00 CC BF 19", f));
  TEST_ASSERT_EQUAL_UINT32(126992, f.pgn);
  Decoded d;
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_TRUE(d.hasTime);
  TEST_ASSERT_EQUAL_INT64(1728043200, d.epochSeconds);
}

static void test_model_prefers_stw_and_falls_back_to_sog_when_stale() {
  BoatModel m;
  ModelConfig cfg;
  cfg.mode = SourceMode::PreferStw;
  cfg.smoothingSec = 0;
  cfg.staleMs = 3000;
  m.configure(cfg);

  m.ingest(Quantity::Stw, 3.0f, n2kSourceId(5), "128259", 1000);
  m.ingest(Quantity::Sog, 4.0f, n2kSourceId(22), "129026", 1000);
  BoatModel::Reading r = m.speed(1500);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL(Quantity::Stw, r.q);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, r.value);

  m.ingest(Quantity::Sog, 4.5f, n2kSourceId(22), "129026", 4500);
  r = m.speed(4600);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL(Quantity::Sog, r.q);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, r.value);

  r = m.speed(9000);
  TEST_ASSERT_FALSE(r.valid);
}

static void test_model_strict_mode_does_not_fall_back() {
  BoatModel m;
  ModelConfig cfg;
  cfg.mode = SourceMode::Stw;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  m.ingest(Quantity::Sog, 4.0f, n2kSourceId(22), "129026", 1000);
  TEST_ASSERT_FALSE(m.speed(1100).valid);
}

static void test_model_source_filter_ignores_other_sources_but_lists_them() {
  BoatModel m;
  ModelConfig cfg;
  cfg.mode = SourceMode::Sog;
  cfg.smoothingSec = 0;
  cfg.sogFilter = n2kSourceId(22);
  m.configure(cfg);
  m.ingest(Quantity::Sog, 9.0f, n2kSourceId(7), "129026", 1000);
  TEST_ASSERT_FALSE(m.speed(1000).valid);
  m.ingest(Quantity::Sog, 2.0f, n2kSourceId(22), "129026", 1100);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, m.speed(1100).value);
  TEST_ASSERT_EQUAL(2, m.sourceCount());
}

static void test_model_smoothing_converges() {
  BoatModel m;
  ModelConfig cfg;
  cfg.mode = SourceMode::Stw;
  cfg.smoothingSec = 1.0f;
  m.configure(cfg);
  m.ingest(Quantity::Stw, 0.0f, 1, "VHW", 0);
  m.ingest(Quantity::Stw, 10.0f, 1, "VHW", 1000);
  float afterOneTau = m.speed(1000).value;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.32f, afterOneTau);
  for (uint32_t t = 1100; t <= 10000; t += 100) m.ingest(Quantity::Stw, 10.0f, 1, "VHW", t);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, m.speed(10000).value);
}

static void test_model_source_table_evicts_stalest_when_full() {
  BoatModel m;
  for (uint8_t i = 0; i < BoatModel::kMaxSources; i++) {
    m.ingest(Quantity::Sog, 1.0f, n2kSourceId(i), "129026", 1000 + i);
  }
  m.ingest(Quantity::Sog, 1.0f, n2kSourceId(200), "129026", 5000);
  TEST_ASSERT_EQUAL(BoatModel::kMaxSources, m.sourceCount());
  bool hasNew = false, hasOldest = false;
  for (size_t i = 0; i < m.sourceCount(); i++) {
    hasNew |= m.source(i).id == n2kSourceId(200);
    hasOldest |= m.source(i).id == n2kSourceId(0);
  }
  TEST_ASSERT_TRUE(hasNew);
  TEST_ASSERT_FALSE(hasOldest);
}

static void test_model_sample_newer_than_query_time_is_fresh() {
  BoatModel m;
  ModelConfig cfg;
  cfg.mode = SourceMode::Stw;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  m.ingest(Quantity::Stw, 3.0f, 1, "VHW", 1000);
  BoatModel::Reading r = m.speed(999);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_UINT32(0, r.ageMs);
}

static void test_unit_conversion() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, convertSpeed(0.514444f, SpeedUnit::Knots));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.6f, convertSpeed(1.0f, SpeedUnit::Kmh));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.237f, convertSpeed(1.0f, SpeedUnit::Mph));
}

static void test_format_source_id() {
  char buf[16];
  formatSourceId(n2kSourceId(35), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("N2K #35", buf);
  formatSourceId(nmeaSourceId("YD"), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0183 YD", buf);
}

static void test_decoder_auto_handles_both_protocols() {
  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  cfg.mode = SourceMode::PreferStw;
  m.configure(cfg);
  LineDecoder d;
  d.feed("$YDVHW,,T,,M,5.20,N,9.63,K*43", 100, m);
  TEST_ASSERT_EQUAL(DataProtocol::Nmea0183, d.stats().detected);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.20f, convertSpeed(m.speed(100).value, SpeedUnit::Knots));

  d.feed("10:00:00.000 R 09F50305 01 34 01 FF FF 00 00 FF", 200, m);
  TEST_ASSERT_EQUAL(DataProtocol::Raw, d.stats().detected);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.08f, m.speed(200).value);
  TEST_ASSERT_EQUAL_UINT32(2, d.stats().samples);
}

static void test_decoder_fixed_protocol_ignores_other() {
  BoatModel m;
  LineDecoder d;
  d.setProtocol(DataProtocol::Raw);
  d.feed("$YDVHW,,T,,M,5.20,N,9.63,K*43", 100, m);
  TEST_ASSERT_FALSE(m.speed(100).valid);
  TEST_ASSERT_EQUAL_UINT32(1, d.stats().ignored);

  d.setProtocol(DataProtocol::Nmea0183);
  d.feed("10:00:00.000 R 09F50305 01 34 01 FF FF 00 00 FF", 200, m);
  TEST_ASSERT_FALSE(m.speed(200).valid);
}

static void test_decoder_ignores_transmitted_raw_frames() {
  BoatModel m;
  LineDecoder d;
  d.feed("10:00:00.000 T 09F50305 01 34 01 FF FF 00 00 FF", 100, m);
  TEST_ASSERT_FALSE(m.speed(100).valid);
}

static void test_decoder_reports_time_once() {
  BoatModel m;
  LineDecoder d;
  int64_t t = 0;
  d.feed("$YDZDA,201530.00,04,10,2026,00,00*6A", 100, m);
  TEST_ASSERT_TRUE(d.takeTime(t));
  TEST_ASSERT_EQUAL_INT64(1791144930, t);
  TEST_ASSERT_FALSE(d.takeTime(t));
}

static ydraw::Frame frame(uint32_t pgn, uint8_t src, std::initializer_list<uint8_t> bytes) {
  ydraw::Frame f;
  f.pgn = pgn;
  f.source = src;
  for (uint8_t b : bytes) f.data[f.len++] = b;
  return f;
}

static void test_decode_130306_wind() {
  Decoded d;
  ydraw::Frame f = frame(130306, 0x10, {0x00, 0xD0, 0x02, 0xAE, 0x1E, 0xFA, 0xFF, 0xFF});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, val(d, Quantity::Awa));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.2f, val(d, Quantity::Aws));

  f = frame(130306, 0x10, {0x00, 0xD0, 0x02, 0xC2, 0xD6, 0xFB, 0xFF, 0xFF});  // 315 deg, true (boat)
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0f, val(d, Quantity::Twa));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.2f, val(d, Quantity::Tws));

  f = frame(130306, 0x10, {0x00, 0xD0, 0x02, 0xC2, 0xD6, 0xF8, 0xFF, 0xFF});  // true, north referenced
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 315.0f, val(d, Quantity::TwdTrue));
  TEST_ASSERT_FALSE(d.has(Quantity::Tws));
}

static void test_decode_127250_heading_and_true_to_magnetic() {
  Decoded d;
  ydraw::Frame f = frame(127250, 0x11, {0x00, 0x5B, 0x88, 0xFF, 0x7F, 0xD1, 0x06, 0xFC});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, val(d, Quantity::HdgTrue));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, val(d, Quantity::Variation));

  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  m.ingest(d, n2kSourceId(0x11), "127250", 1000);
  BoatModel::Reading r = m.bearing(Quantity::Hdg, BearingRef::Magnetic, 1000);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 190.0f, r.value);
  r = m.bearing(Quantity::Hdg, BearingRef::True, 1000);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 200.0f, r.value);
}

static void test_bearing_reference_conversion() {
  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  // Without a variation only the received reference is known.
  m.ingest(Quantity::HdgTrue, 200.0f, 1, "HDT", 1000);
  TEST_ASSERT_TRUE(m.bearing(Quantity::Hdg, BearingRef::True, 1000).valid);
  TEST_ASSERT_FALSE(m.bearing(Quantity::Hdg, BearingRef::Magnetic, 1000).valid);
  // true = magnetic + variation (east positive)
  m.ingest(Quantity::Variation, -5.0f, 1, "RMC", 1100);
  m.ingest(Quantity::TwdTrue, 2.0f, 1, "MWD", 1200);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, m.bearing(Quantity::Twd, BearingRef::Magnetic, 1200).value);
  m.ingest(Quantity::Cog, 358.0f, 1, "VTG", 1200);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 353.0f, m.bearing(Quantity::Cog, BearingRef::True, 1200).value);
  // A value received in the asked-for reference wins over a conversion.
  m.ingest(Quantity::CogTrue, 10.0f, 1, "RMC", 1200);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, m.bearing(Quantity::Cog, BearingRef::True, 1200).value);
  TEST_ASSERT_EQUAL(Quantity::CogTrue, m.bearing(Quantity::CogTrue, BearingRef::True, 1200).q);
  // Items read bearings in the chosen reference.
  BoatModel::Reading r = readItem(m, Item::Hdg, BearingRef::Magnetic, 1200);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 205.0f, r.value);
}

static void test_decode_water_temperature_pgns() {
  Decoded d;
  // 291.55 K = 18.4 C
  ydraw::Frame f = frame(130312, 0x23, {0x00, 0x00, 0x00, 0xE3, 0x71, 0xFF, 0xFF, 0xFF});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.4f, val(d, Quantity::Mtw));
  f = frame(130312, 0x23, {0x00, 0x00, 0x01, 0xE3, 0x71, 0xFF, 0xFF, 0xFF});  // outside air
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FALSE(d.has(Quantity::Mtw));
  f = frame(130310, 0x23, {0x00, 0xE3, 0x71, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.4f, val(d, Quantity::Mtw));
  f = frame(130311, 0x23, {0x00, 0xC0, 0xE3, 0x71, 0xFF, 0xFF, 0xFF, 0xFF});  // sea, humidity source 3
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.4f, val(d, Quantity::Mtw));
  // 291550 mK, 24 bit
  f = frame(130316, 0x23, {0x00, 0x00, 0x00, 0xDE, 0x72, 0x04, 0xFF, 0xFF});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.4f, val(d, Quantity::Mtw));
}

static void test_decode_128267_depth_with_offset() {
  Decoded d;
  ydraw::Frame f = frame(128267, 0x20, {0x00, 0xD2, 0x04, 0x00, 0x00, 0x0C, 0xFE, 0xFF});
  TEST_ASSERT_TRUE(ydraw::decodePgn(f.pgn, f.data, f.len, d));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.84f, val(d, Quantity::Dpt));
}

static void test_fast_packet_129284_navigation() {
  uint8_t payload[34];
  memset(payload, 0xFF, sizeof(payload));
  const uint8_t head[] = {0x01, 0x98, 0x10, 0x07, 0x00, 0x01};  // magnetic bearings
  memcpy(payload, head, sizeof(head));
  payload[14] = 0x34;  // bearing position -> destination, 0x1234 * 1e-4 rad
  payload[15] = 0x12;
  payload[32] = 0x96;
  payload[33] = 0x00;

  ydraw::Decoder dec;
  Decoded d;
  const uint8_t seq = 3;
  ydraw::Frame f;
  f.pgn = 129284;
  f.source = 0x05;
  f.len = 8;
  f.data[0] = (seq << 5) | 0;
  f.data[1] = 34;
  memcpy(&f.data[2], payload, 6);
  TEST_ASSERT_FALSE(dec.feed(f, d));
  for (uint8_t n = 1; n <= 4; n++) {
    f.data[0] = (seq << 5) | n;
    memcpy(&f.data[1], payload + 6 + (n - 1) * 7, 7);
    bool done = dec.feed(f, d);
    TEST_ASSERT_EQUAL(n == 4, done);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4630.0f, val(d, Quantity::Dtw));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 26.70f, val(d, Quantity::Btw));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, val(d, Quantity::Vmc));

  // A missing frame discards the packet.
  f.data[0] = (seq << 5) | 0;
  f.data[1] = 34;
  memcpy(&f.data[2], payload, 6);
  dec.feed(f, d);
  f.data[0] = (seq << 5) | 2;
  TEST_ASSERT_FALSE(dec.feed(f, d));
  f.data[0] = (seq << 5) | 4;
  TEST_ASSERT_FALSE(dec.feed(f, d));
}

static void test_model_angles_average_across_zero() {
  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 1.0f;
  m.configure(cfg);
  m.ingest(Quantity::Hdg, 350.0f, 1, "HDM", 0);
  m.ingest(Quantity::Hdg, 10.0f, 1, "HDM", 700);  // ~50% weight
  float h = m.get(Quantity::Hdg, 700).value;
  TEST_ASSERT_TRUE(h > 355.0f || h < 5.0f);
  m.ingest(Quantity::Awa, -170.0f, 1, "MWV", 0);
  m.ingest(Quantity::Awa, 170.0f, 1, "MWV", 700);
  float a = m.get(Quantity::Awa, 700).value;
  TEST_ASSERT_TRUE(fabsf(a) > 175.0f);
}

static void test_decoder_prefers_dpt_over_dbt() {
  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  LineDecoder d;
  d.feed("$SDDBT,40.7,f,12.4,M,6.8,F", 100, m);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.4f, m.get(Quantity::Dpt, 100).value);
  d.feed("$SDDPT,12.4,-0.8,", 200, m);
  d.feed("$SDDBT,40.7,f,12.4,M,6.8,F", 300, m);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.6f, m.get(Quantity::Dpt, 300).value);
}

static void test_decoder_raw_fast_packet_lines() {
  BoatModel m;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  m.configure(cfg);
  LineDecoder d;
  d.feed("10:00:00.000 R 09F90405 60 22 01 98 10 07 00 00", 100, m);
  d.feed("10:00:00.001 R 09F90405 61 FF FF FF FF FF FF FF", 100, m);
  d.feed("10:00:00.002 R 09F90405 62 FF FF FF FF FF FF FF", 100, m);
  d.feed("10:00:00.003 R 09F90405 63 FF FF FF FF FF FF FF", 100, m);
  TEST_ASSERT_FALSE(m.get(Quantity::Dtw, 100).valid);
  d.feed("10:00:00.004 R 09F90405 64 FF FF FF FF FF 96 00", 100, m);
  BoatModel::Reading r = m.get(Quantity::Dtw, 100);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4630.0f, r.value);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, m.get(Quantity::Vmc, 100).value);
}

static void test_item_ids_and_quantities() {
  Item it;
  TEST_ASSERT_TRUE(itemFromId("AWA", it));
  TEST_ASSERT_EQUAL(Item::Awa, it);
  TEST_ASSERT_FALSE(itemFromId("xyz", it));
  Quantity q;
  TEST_ASSERT_TRUE(itemQuantity(Item::Dpt, q));
  TEST_ASSERT_EQUAL(Quantity::Dpt, q);
  TEST_ASSERT_FALSE(itemQuantity(Item::Speed, q));
  TEST_ASSERT_EQUAL(Item::Tws, itemForQuantity(Quantity::Tws));
  TEST_ASSERT_EQUAL(Item::Cog, itemForQuantity(Quantity::CogTrue));
  TEST_ASSERT_TRUE(itemFromId("btw", it));
  TEST_ASSERT_TRUE(itemQuantity(it, q));
  TEST_ASSERT_EQUAL(Quantity::Btw, q);
  for (size_t i = 0; i < kItemCount; i++) TEST_ASSERT_EQUAL(static_cast<Item>(i), itemInfo(static_cast<Item>(i)).item);
}

static void test_item_formatting() {
  char b[16];
  formatItem(Item::Stw, true, 6.43f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 6.4", b);
  formatItem(Item::Stw, false, 0, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("--.-", b);
  formatItem(Item::Vmg, true, -4.5f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 4.5", b);
  formatItem(Item::Vmc, true, -0.02f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 0.0", b);
  formatItem(Item::Awa, true, -42.4f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 42", b);
  TEST_ASSERT_EQUAL_CHAR('P', itemSide(Item::Awa, -42.4f));
  TEST_ASSERT_EQUAL_CHAR('S', itemSide(Item::Twa, 120.0f));
  TEST_ASSERT_EQUAL_CHAR(0, itemSide(Item::Twa, 180.0f));
  formatItem(Item::Hdg, true, 359.7f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("000", b);
  formatItem(Item::Twd, true, 5.2f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("005", b);
  formatItem(Item::Dtw, true, 2.5f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("2.50", b);
  formatItem(Item::Dtw, true, 123.4f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("123", b);
  formatItem(Item::Dpt, true, 3.21f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 3.2", b);
  formatItem(Item::Dtw, true, 456.4f, -1, b, sizeof(b), true);
  TEST_ASSERT_EQUAL_STRING("456", b);
  formatItem(Item::Cog, true, 7.0f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("007", b);
  formatItem(Item::Mtw, true, 8.44f, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(" 8.4", b);
  formatItem(Item::Mtw, false, 0, -1, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("--.-", b);

  DisplayUnits u;
  u.depth = DepthUnit::Feet;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.81f, itemDisplay(Item::Dpt, 10.0f, u).value);
  TEST_ASSERT_EQUAL_STRING("FT", itemUnit(Item::Dpt, u));
  TEST_ASSERT_EQUAL_STRING("MAG", itemUnit(Item::Btw, u));
  u.bearing = BearingRef::True;
  TEST_ASSERT_EQUAL_STRING("TRUE", itemUnit(Item::Hdg, u));
  TEST_ASSERT_EQUAL_STRING("C", itemUnit(Item::Mtw, u));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.4f, itemDisplay(Item::Mtw, 18.4f, u).value);
  u.temperature = TempUnit::Fahrenheit;
  TEST_ASSERT_EQUAL_STRING("F", itemUnit(Item::Mtw, u));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.12f, itemDisplay(Item::Mtw, 18.4f, u).value);
}

static void test_distance_units() {
  DisplayUnits u;
  DisplayValue d = itemDisplay(Item::Dtw, 500.0f, u);  // no short unit: always NM
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.27f, d.value);
  TEST_ASSERT_EQUAL_STRING("NM", d.unit);
  TEST_ASSERT_FALSE(d.shortDistance);

  u.distance = DistanceUnit::Kilometers;
  u.shortDistance = ShortDistanceUnit::Meters;
  d = itemDisplay(Item::Dtw, 999.0f, u);
  TEST_ASSERT_TRUE(d.shortDistance);
  TEST_ASSERT_EQUAL_STRING("M", d.unit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 999.0f, d.value);
  d = itemDisplay(Item::Dtw, 999.6f, u);  // would round to 1000 m
  TEST_ASSERT_FALSE(d.shortDistance);
  TEST_ASSERT_EQUAL_STRING("KM", d.unit);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9996f, d.value);

  u.distance = DistanceUnit::Miles;
  u.shortDistance = ShortDistanceUnit::Yards;
  d = itemDisplay(Item::Dtw, 1609.344f, u);
  TEST_ASSERT_EQUAL_STRING("MI", d.unit);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, d.value);
  d = itemDisplay(Item::Dtw, 9.144f, u);
  TEST_ASSERT_EQUAL_STRING("YD", d.unit);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, d.value);

  u.distance = DistanceUnit::NauticalMiles;
  u.shortDistance = ShortDistanceUnit::Feet;
  d = itemDisplay(Item::Dtw, 300.0f, u);  // 984 ft
  TEST_ASSERT_EQUAL_STRING("FT", d.unit);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 984.0f, d.value);
  d = itemDisplay(Item::Dtw, 310.0f, u);  // 1017 ft
  TEST_ASSERT_EQUAL_STRING("NM", d.unit);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_vhw_knots);
  RUN_TEST(test_vhw_kmh_only_falls_back_to_kmh_field);
  RUN_TEST(test_rmc_active_gives_sog_and_time);
  RUN_TEST(test_rmc_void_status_has_no_sog);
  RUN_TEST(test_vtg_valid_and_invalid_mode);
  RUN_TEST(test_vbw_gives_both_speeds);
  RUN_TEST(test_zda_time);
  RUN_TEST(test_bad_checksum_rejected);
  RUN_TEST(test_unrelated_sentence_ok_without_values);
  RUN_TEST(test_mwv_apparent_and_true);
  RUN_TEST(test_vwr_port_side_is_negative);
  RUN_TEST(test_mwd_both_references);
  RUN_TEST(test_heading_sentences);
  RUN_TEST(test_depth_sentences);
  RUN_TEST(test_waypoint_sentences);
  RUN_TEST(test_vpw_vmg);
  RUN_TEST(test_mtw_water_temperature);
  RUN_TEST(test_raw_line_is_not_nmea);
  RUN_TEST(test_pgn_from_can_id_matches_manual_examples);
  RUN_TEST(test_raw_line_parse);
  RUN_TEST(test_raw_line_rejects_garbage);
  RUN_TEST(test_decode_128259_speed_water);
  RUN_TEST(test_decode_129026_cog_sog);
  RUN_TEST(test_decode_129026_not_available);
  RUN_TEST(test_decode_126992_system_time);
  RUN_TEST(test_model_prefers_stw_and_falls_back_to_sog_when_stale);
  RUN_TEST(test_model_strict_mode_does_not_fall_back);
  RUN_TEST(test_model_source_filter_ignores_other_sources_but_lists_them);
  RUN_TEST(test_model_smoothing_converges);
  RUN_TEST(test_model_source_table_evicts_stalest_when_full);
  RUN_TEST(test_model_sample_newer_than_query_time_is_fresh);
  RUN_TEST(test_unit_conversion);
  RUN_TEST(test_format_source_id);
  RUN_TEST(test_decoder_auto_handles_both_protocols);
  RUN_TEST(test_decoder_fixed_protocol_ignores_other);
  RUN_TEST(test_decoder_ignores_transmitted_raw_frames);
  RUN_TEST(test_decoder_reports_time_once);
  RUN_TEST(test_decode_130306_wind);
  RUN_TEST(test_decode_127250_heading_and_true_to_magnetic);
  RUN_TEST(test_bearing_reference_conversion);
  RUN_TEST(test_decode_water_temperature_pgns);
  RUN_TEST(test_decode_128267_depth_with_offset);
  RUN_TEST(test_fast_packet_129284_navigation);
  RUN_TEST(test_model_angles_average_across_zero);
  RUN_TEST(test_decoder_prefers_dpt_over_dbt);
  RUN_TEST(test_decoder_raw_fast_packet_lines);
  RUN_TEST(test_item_ids_and_quantities);
  RUN_TEST(test_item_formatting);
  RUN_TEST(test_distance_units);
  return UNITY_END();
}
