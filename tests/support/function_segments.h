#ifndef ABCD_TESTS_SUPPORT_FUNCTION_SEGMENTS_H_
#define ABCD_TESTS_SUPPORT_FUNCTION_SEGMENTS_H_

#include <cstdlib>
#include <sstream>

#include "../../src/source/class_source.h"
#include "../../src/source/function_segments.h"
#include "abcd/source/function_emission.h"

namespace abcd::test {
inline void CheckSegment(bool condition) { if (!condition) std::abort(); }
class BoundSegmentSink final : public source::CaptureBuffer {
 public:
  bool Position(source::GeneratedPosition& result) const override {
    result = position; return true;
  }
  bool Capture(analysis::CapturedSlot, analysis::CaptureDomain) override {
    constexpr std::string_view kBinding = "captured";
    xsputn(kBinding.data(), kBinding.size());
    return true;
  }
  bool RetainFunction(const source::FunctionSegment& segment) override {
    source::AppendFunctionSegment(segments, segment, remaining);
    return true;
  }
  void CapturedPositions(abc::EntityOffset, const source::PrintedTemplate& input,
      std::span<const source::CaptureShift> prefix,
      const source::GeneratedPosition& origin) override {
    for (const auto& segment : input.function_segments)
      RetainFunction(source::RebaseFunctionSegment(
          source::BindFunctionSegment(segment, input, prefix), origin));
  }
  std::string text;
  source::GeneratedPosition position;
  std::vector<source::FunctionSegment> segments;
 private:
  std::streamsize xsputn(const char* bytes, std::streamsize count) override {
    if (count < 0) return 0;
    for (const char byte : std::string_view(bytes, count)) {
      ++position.byte;
      if (byte == '\n') { ++position.line; position.column = 0; }
      else ++position.column;
    }
    text.append(bytes, count);
    return count;
  }
  std::size_t remaining = 64 * 1024 * 1024;
};

// Reuse a real prepared member supplied by the encoded-class fixture. No raw
// production tree/certificate constructor or execution of generated JS.
inline void CheckFunctionTransport(source::FunctionEmission emission,
                                   std::string_view method_text) {
  using namespace source;
  const auto function = std::make_shared<const FunctionEmission>(std::move(emission));
  constexpr std::size_t kBudget = 64 * 1024 * 1024;
  constexpr std::string_view before = "const outer = ";
  constexpr std::string_view after = ";\nclass Fixture {\n";
  constexpr std::string_view wrapper = "// wrapper\n";
  constexpr std::string_view parent = "// parent\n";
  constexpr std::string_view final = "// final\n";
  ClassSource child(kBudget);
  std::ostream child_output(&child);
  child_output << before;
  CheckSegment(child.Capture({{0}, {0}}, analysis::CaptureDomain::kLanguage));
  child_output << after;
  GeneratedPosition begin, end;
  CheckSegment(child.Position(begin));
  child_output << method_text;
  CheckSegment(child.Position(end));
  CheckSegment(child.RetainFunction({function, begin, end}));
  child_output << "}\n";

  ClassSource intermediate(kBudget);
  std::ostream intermediate_output(&intermediate);
  intermediate_output << parent;
  child.Replay(intermediate_output, wrapper);
  BoundSegmentSink sink;
  std::ostream output(&sink);
  intermediate.Replay(output, final);
  CheckSegment(sink.segments.size() == 1);
  const auto& mapped = sink.segments.front();
  CheckSegment(mapped.function == function && mapped.begin.captures == 0 && mapped.end.captures == 0);
  constexpr std::string_view binding = "captured";
  CheckSegment(mapped.begin.byte == final.size() + parent.size() + wrapper.size() + before.size() + binding.size() + after.size());
  CheckSegment(mapped.end.byte - mapped.begin.byte == method_text.size());
  CheckSegment(std::string_view(sink.text).substr(mapped.begin.byte, method_text.size()) == method_text);
  CheckFunctionSegments(sink.segments, sink.text.size(), 0);

  ClassSource plain(kBudget);
  std::ostream plain_output(&plain);
  CheckSegment(plain.Position(begin));
  plain_output << method_text;
  CheckSegment(plain.Position(end));
  CheckSegment(plain.RetainFunction({function, begin, end}));
  BoundSegmentSink plain_sink;
  std::ostream plain_stream(&plain_sink);
  plain.Replay(plain_stream, wrapper);
  CheckSegment(plain_sink.segments.size() == 1 && plain_sink.segments[0].begin.byte == wrapper.size());

  bool refused = false;
  std::ostringstream unmapped;
  try { plain.Replay(unmapped, {}); }
  catch (const SourceFailure& failure) { refused = failure.kind == SourceError::kInvalidEffect; }
  CheckSegment(refused && unmapped.view().empty());
  auto invalid = mapped;
  ++invalid.end.captures;
  refused = false;
  try { CheckFunctionSegment(invalid); }
  catch (const SourceFailure&) { refused = true; }
  CheckSegment(refused);
  std::vector<FunctionSegment> denied;
  std::size_t remaining = 0;
  refused = false;
  try { AppendFunctionSegment(denied, mapped, remaining); }
  catch (const SourceFailure& failure) { refused = failure.kind == SourceError::kResourceLimit; }
  CheckSegment(refused && denied.empty());
  for (const std::size_t count : {200, 201}) {
    std::vector<FunctionSegment> many;
    remaining = kBudget;
    for (std::size_t i = 0; i < count; ++i) {
      auto shifted = mapped;
      const auto byte_shift = i * method_text.size();
      shifted.begin.byte += byte_shift;
      shifted.end.byte += byte_shift;
      AppendFunctionSegment(many, shifted, remaining);
    }
    CheckSegment(many.size() == count);
    CheckFunctionSegments(many, mapped.end.byte + (count - 1) * method_text.size(), 0);
  }
}
}  // namespace abcd::test
#endif
