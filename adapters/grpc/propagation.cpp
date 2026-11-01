#include "adapters/grpc/propagation.h"

#include <cctype>

#include <vmon/vmon.h>

namespace vmon {
namespace adapters {

const char* const kHeaderTraceparent = "traceparent";
const char* const kHeaderRequestId = "x-request-id";
const char* const kHeaderTaskId = "x-task-id";

namespace {

std::string hex128(uint64_t hi, uint64_t lo) {
  static const char* digits = "0123456789abcdef";
  std::string out(32, '0');
  for (int i = 15; i >= 0; --i) {
    // 高位在前：前 16 位是 trace 高 64 位，后 16 位是低 64 位。
    out[static_cast<size_t>(i)] = digits[hi & 0xF];
    out[static_cast<size_t>(16 + i)] = digits[lo & 0xF];
    hi >>= 4;
    lo >>= 4;
  }
  return out;
}

std::string hex64(uint64_t v) {
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = digits[v & 0xF];
    v >>= 4;
  }
  return out;
}

bool is_hex(const std::string& s) {
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

uint64_t from_hex(const std::string& s, size_t begin, size_t len) {
  uint64_t v = 0;
  for (size_t i = begin; i < begin + len; ++i) {
    char c = s[i];
    unsigned d;
    if (c >= '0' && c <= '9') {
      d = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<unsigned>(c - 'a' + 10);
    } else {
      d = static_cast<unsigned>(c - 'A' + 10);
    }
    v = (v << 4) | d;
  }
  return v;
}

}  // namespace

std::string make_traceparent(uint64_t trace_hi, uint64_t trace_lo,
                             uint64_t span, bool sampled) {
  return std::string("00-") + hex128(trace_hi, trace_lo) + "-" + hex64(span) +
         (sampled ? "-01" : "-00");
}

bool parse_traceparent(const std::string& header, Traceparent* out) {
  if (out == nullptr) return false;
  *out = Traceparent();

  // 00-<32hex>-<16hex>-<2hex>，仅接受 version 00，长度固定 55。
  if (header.size() != 55) return false;
  if (header[2] != '-' || header[35] != '-' || header[52] != '-') return false;
  if (header[0] != '0' || header[1] != '0') return false;

  const std::string trace = header.substr(3, 32);
  const std::string span = header.substr(36, 16);
  const std::string flags = header.substr(53, 2);
  if (!is_hex(trace) || !is_hex(span) || !is_hex(flags)) return false;

  Traceparent tp;
  tp.trace_hi = from_hex(trace, 0, 16);
  tp.trace_lo = from_hex(trace, 16, 16);
  tp.span = from_hex(span, 0, 16);
  tp.sampled = from_hex(flags, 0, 2) != 0;
  tp.valid = true;

  // 全零 trace/span 视为非法（W3C 要求拒绝）。
  if (tp.trace_hi == 0 && tp.trace_lo == 0) return false;
  if (tp.span == 0) return false;

  *out = tp;
  return true;
}

}  // namespace adapters

bool context_from_traceparent(const char* header, SpanContext* out) noexcept {
  if (header == nullptr || out == nullptr) return false;
  adapters::Traceparent parsed;
  if (!adapters::parse_traceparent(header, &parsed) || !parsed.valid) {
    *out = SpanContext();
    return false;
  }
  out->trace_hi = parsed.trace_hi;
  out->trace_lo = parsed.trace_lo;
  out->span = parsed.span;
  out->valid = true;
  return true;
}

}  // namespace vmon
