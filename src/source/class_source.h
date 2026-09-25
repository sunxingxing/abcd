#ifndef ABCD_SOURCE_CLASS_SOURCE_H_
#define ABCD_SOURCE_CLASS_SOURCE_H_

#include <ostream>

#include "abcd/source/semantic_source.h"
#include "capture_stream.h"

namespace abcd::source {
// A private class template, never published on preparation failure. Captures
// stay typed until the creation-site owner resolves them during replay.
class ClassSource final : public CaptureBuffer, public UnpublishedOutput {
 public:
  explicit ClassSource(std::size_t bytes);
  bool Position(GeneratedPosition& position) const override;
  bool Capture(analysis::CapturedSlot cell,
               analysis::CaptureDomain requirement) override;
  void Positions(abc::EntityOffset method, std::span<const GeneratedSpan> spans,
                 std::span<const MethodGeneratedSpan> descendants) override;
  void MappedSpan(abc::EntityOffset method, const GeneratedSpan& span) override;
  void CapturedPositions(abc::EntityOffset method, const PrintedTemplate& source,
      std::span<const CaptureShift> prefix,
      const GeneratedPosition& origin) override;
  void Replay(std::ostream& output, std::string_view prefix);

 private:
  std::streamsize xsputn(const char* text, std::streamsize count) override;
  int_type overflow(int_type ch) override;
  void Append(abc::EntityOffset method, GeneratedSpan span,
              const GeneratedPosition& origin);

  PrintedTemplate source_;
  GeneratedPosition position_;
  std::size_t remaining_;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_CLASS_SOURCE_H_
