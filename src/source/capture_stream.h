#ifndef ABCD_SOURCE_CAPTURE_STREAM_H_
#define ABCD_SOURCE_CAPTURE_STREAM_H_

#include <streambuf>

#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
struct CaptureHole {
  std::size_t offset;
  analysis::CapturedSlot cell;
  analysis::CaptureDomain requirement = analysis::CaptureDomain::kLanguage;
};
// Private templates carry typed tokens, not textual placeholders. Every
// captured load/store must emit one token, even if its value is unused.
class CaptureBuffer : public std::streambuf {
 public:
  virtual bool Capture(analysis::CapturedSlot cell,
      analysis::CaptureDomain requirement =
          analysis::CaptureDomain::kLanguage) = 0;
};
class CaptureDiscard : public CaptureBuffer {
 public:
  bool Capture(analysis::CapturedSlot, analysis::CaptureDomain) override {
    return true;
  }

 private:
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CAPTURE_STREAM_H_
