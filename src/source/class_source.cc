#include "class_source.h"

#include <algorithm>
#include <limits>
#include <type_traits>

#include "capture_positions.h"
#include "class_emission.h"

namespace abcd::source {
namespace {
[[noreturn]] void Invalid() {
  throw ClassFailure{EmitError::kInvalidExpression};
}

// Geometric growth admits the old and replacement allocations simultaneously.
// remaining excludes all retained capacities, including the string terminator.
template <typename Container>
void Reserve(Container& values, std::size_t needed, std::size_t& remaining) {
  if (needed <= values.capacity()) return;
  const auto old = values.capacity();
  const auto next = std::max(needed, old <= SIZE_MAX / 2 ? old * 2 : needed);
  constexpr auto width = sizeof(typename Container::value_type);
  constexpr std::size_t terminator =
      std::is_same_v<Container, std::string> ? sizeof(char) : 0;
  if (remaining < terminator || next > (remaining - terminator) / width)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  values.reserve(next);
  if (values.capacity() > (remaining - terminator) / width)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  remaining -= (values.capacity() - old) * width;
}

GeneratedPosition Rebase(GeneratedPosition point,
                         const GeneratedPosition& origin) {
  if (!point.line || !origin.line ||
      point.byte > SIZE_MAX - origin.byte ||
      point.captures > SIZE_MAX - origin.captures ||
      point.line - 1 > UINT32_MAX - origin.line ||
      (point.line == 1 && point.column > UINT32_MAX - origin.column))
    Invalid();
  point.byte += origin.byte;
  point.captures += origin.captures;
  if (point.line == 1) point.column += origin.column;
  point.line += origin.line - 1;
  return point;
}
}  // namespace

ClassSource::ClassSource(std::size_t bytes) : remaining_(bytes) {
  AdmitClassBytes(source_.text.capacity(), sizeof(char), remaining_);
  AdmitClassBytes(1, sizeof(char), remaining_);
}

bool ClassSource::Position(GeneratedPosition& position) const {
  position = position_;
  return true;
}

bool ClassSource::Capture(analysis::CapturedSlot cell,
                          analysis::CaptureDomain requirement) {
  if (source_.captures.size() == SIZE_MAX)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  Reserve(source_.captures, source_.captures.size() + 1, remaining_);
  source_.captures.push_back({position_, cell, requirement});
  ++position_.captures;
  return true;
}

void ClassSource::Append(abc::EntityOffset method, GeneratedSpan span,
                         const GeneratedPosition& origin) {
  if (source_.method_spans.size() == SIZE_MAX)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  Reserve(source_.method_spans, source_.method_spans.size() + 1, remaining_);
  span.begin = Rebase(span.begin, origin);
  span.end = Rebase(span.end, origin);
  source_.method_spans.push_back({method, span});
}

void ClassSource::Positions(abc::EntityOffset method,
    std::span<const GeneratedSpan> spans,
    std::span<const MethodGeneratedSpan> descendants) {
  for (const auto& span : spans) Append(method, span, position_);
  for (const auto& mapped : descendants)
    Append(mapped.method, mapped.span, position_);
}

void ClassSource::MappedSpan(abc::EntityOffset method,
                             const GeneratedSpan& span) {
  Append(method, span, {});
}

void ClassSource::CapturedPositions(abc::EntityOffset method,
    const PrintedTemplate& source, std::span<const CaptureShift> prefix,
    const GeneratedPosition& origin) {
  const auto append = [&](abc::EntityOffset owner, GeneratedSpan span) {
    span.begin = RelocateCapturePosition(source, prefix, span.begin, span.pc);
    span.end = RelocateCapturePosition(source, prefix, span.end, span.pc);
    Append(owner, span, origin);
  };
  for (const auto& span : source.spans) append(method, span);
  for (const auto& mapped : source.method_spans)
    append(mapped.method, mapped.span);
}

std::streamsize ClassSource::xsputn(const char* text, std::streamsize count) {
  if (count < 0) return 0;
  const auto size = static_cast<std::size_t>(count);
  if (size > SIZE_MAX - source_.text.size())
    throw ClassFailure{CreatedClassError::kResourceLimit};
  Reserve(source_.text, source_.text.size() + size, remaining_);
  for (const char ch : std::string_view{text, size}) {
    if (ch == '\n') {
      if (position_.line == UINT32_MAX)
        throw ClassFailure{CreatedClassError::kResourceLimit};
      ++position_.line;
      position_.column = 0;
    } else {
      if (position_.column == UINT32_MAX)
        throw ClassFailure{CreatedClassError::kResourceLimit};
      ++position_.column;
    }
  }
  source_.text.append(text, size);
  position_.byte += size;
  return count;
}

ClassSource::int_type ClassSource::overflow(int_type ch) {
  if (traits_type::eq_int_type(ch, traits_type::eof()))
    return traits_type::not_eof(ch);
  const char value = traits_type::to_char_type(ch);
  xsputn(&value, 1);
  return ch;
}

void ClassSource::Replay(std::ostream& output, std::string_view prefix_text) {
  auto* sink = dynamic_cast<CaptureBuffer*>(output.rdbuf());
  if (!sink && !source_.captures.empty()) Invalid();
  GeneratedPosition origin;
  const bool mapped = sink && sink->Position(origin);
  std::vector<CaptureShift> prefix;
  // Allocate replay scratch before publishing any byte. The class template
  // and this prefix table are charged to the same retained-output allowance.
  if (mapped && !source_.captures.empty()) {
    if (source_.captures.size() == SIZE_MAX)
      throw ClassFailure{CreatedClassError::kResourceLimit};
    Reserve(prefix, source_.captures.size() + 1, remaining_);
    prefix.push_back({});
  }
  const auto text = [&](std::string_view part) {
    if (part.size() > static_cast<std::size_t>(
                          std::numeric_limits<std::streamsize>::max()))
      throw ClassFailure{CreatedClassError::kResourceLimit};
    output.write(part.data(), static_cast<std::streamsize>(part.size()));
    if (!output) throw ClassFailure{EmitError::kWrite};
  };
  text(prefix_text);
  if (mapped && !sink->Position(origin)) Invalid();
  if (source_.captures.empty()) {
    if (sink) sink->Positions({}, {}, source_.method_spans);
    text(source_.text);
    return;
  }
  std::size_t begin = 0;
  for (const auto& capture : source_.captures) {
    const auto end = capture.position.byte;
    if (end < begin || end > source_.text.size()) Invalid();
    text(std::string_view(source_.text).substr(begin, end - begin));
    GeneratedPosition before;
    if (mapped && !sink->Position(before)) Invalid();
    if (!sink->Capture(capture.cell, capture.requirement))
      throw ClassFailure{EmitError::kWrite};
    if (mapped) {
      GeneratedPosition after;
      if (!sink->Position(after) || after.line != before.line ||
          after.byte < before.byte || after.column < before.column ||
          after.captures < before.captures ||
          after.byte - before.byte != after.column - before.column)
        Invalid();
      const auto bytes = after.byte - before.byte;
      const auto captures = after.captures - before.captures;
      if (bytes > SIZE_MAX - prefix.back().bytes ||
          captures > SIZE_MAX - prefix.back().captures)
        throw ClassFailure{CreatedClassError::kResourceLimit};
      prefix.push_back({prefix.back().bytes + bytes,
                        prefix.back().captures + captures});
    }
    begin = end;
  }
  text(std::string_view(source_.text).substr(begin));
  if (mapped) sink->CapturedPositions({}, source_, prefix, origin);
}
}  // namespace abcd::source
