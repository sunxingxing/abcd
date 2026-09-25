#ifndef ABCD_SOURCE_CAPTURE_STREAM_H_
#define ABCD_SOURCE_CAPTURE_STREAM_H_

#include <streambuf>
#include <span>

#include "abcd/analysis/creation_binding.h"

namespace abcd::source {
struct GeneratedSpan;
struct MethodGeneratedSpan;
// Internal publication capability, not a general stream optimization. The
// owner must discard the entire buffered artifact on any descendant failure.
// A sink that has published bytes or may retry into the same prefix must not
// inherit this marker. It permits one emission pass, not skipping validation.
class UnpublishedOutput {
 public:
  virtual ~UnpublishedOutput() = default;
 protected:
  UnpublishedOutput() = default;
};
struct CaptureHole {
  std::size_t offset;
  analysis::CapturedSlot cell;
  analysis::CaptureDomain requirement = analysis::CaptureDomain::kLanguage;
};
// Private templates carry typed tokens, not textual placeholders. Every
// captured load/store must emit one token, even if its value is unused.
class CaptureBuffer : public std::streambuf {
 public:
  // Positions are relative to the next text chunk, with no capture holes.
  // Sinks without a position owner intentionally do not claim map coverage.
  virtual void Positions(abc::EntityOffset, std::span<const GeneratedSpan>,
                         std::span<const MethodGeneratedSpan>) {}
  virtual bool Capture(analysis::CapturedSlot cell,
      analysis::CaptureDomain requirement =
          analysis::CaptureDomain::kLanguage) = 0;
};
class CaptureDiscard : public CaptureBuffer, public UnpublishedOutput {
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
