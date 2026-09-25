#include "abcd/source/semantic_source.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>

#include "lexical_name.h"
#include "capture_positions.h"

namespace abcd::source {
namespace {
SourceFailure Invalid(abc::CodeOffset pc = {}) {
  return {SourceError::kInvalidEffect, pc};
}
SourceFailure Resource() { return {SourceError::kResourceLimit, {}}; }

void Advance(GeneratedPosition& position, std::string_view text) {
  position.byte += text.size();
  for (const char ch : text) {
    if (ch == '\n') {
      if (position.line == std::numeric_limits<std::uint32_t>::max())
        throw Resource();
      ++position.line;
      position.column = 0;
    } else {
      if (position.column == std::numeric_limits<std::uint32_t>::max())
        throw Resource();
      ++position.column;
    }
  }
}

}  // namespace

GeneratedPosition RelocateCapturePosition(const PrintedTemplate& source,
                           std::span<const CaptureShift> prefix,
                           GeneratedPosition position, abc::CodeOffset pc) {
  const auto count = position.captures;
  if (prefix.size() != source.captures.size() + 1 || prefix.front().bytes != 0 ||
      prefix.front().captures != 0 ||
      !position.line || position.byte > source.text.size() ||
      count > source.captures.size()) throw Invalid(pc);
  if (count) {
    const auto& preceding = source.captures[count - 1].position;
    if (preceding.byte > position.byte || preceding.line > position.line)
      throw Invalid(pc);
  }
  if (count < source.captures.size()) {
    const auto& following = source.captures[count].position;
    if (following.byte < position.byte || following.line < position.line)
      throw Invalid(pc);
  }
  // Inserted names contain no newline. Byte shifts are global prefix sums;
  // column shifts include only insertions on the endpoint's original line.
  const auto begin = source.captures.begin();
  const auto first = std::lower_bound(
      begin, begin + count, position.line,
      [](const SourceCapture& capture, std::uint32_t line) {
        return capture.position.line < line;
      });
  const auto column_shift = prefix[count].bytes - prefix[first - begin].bytes;
  if (column_shift > std::numeric_limits<std::uint32_t>::max() - position.column ||
      prefix[count].bytes > std::numeric_limits<std::size_t>::max() - position.byte)
    throw Resource();
  position.byte += prefix[count].bytes;
  position.column += static_cast<std::uint32_t>(column_shift);
  position.captures = prefix[count].captures;
  return position;
}

std::variant<PrintedSource, SourceFailure> BindSourceTemplate(
    const PrintedTemplate& source,
    std::span<const analysis::LocalCell> bindings, std::size_t budget_bytes) {
  if (bindings.size() != source.captures.size()) return Invalid();
  for (const auto& capture : source.captures)
    if (capture.requirement != analysis::CaptureDomain::kLanguage)
      return Invalid();
  try {
    if (bindings.size() >= budget_bytes / sizeof(CaptureShift)) return Resource();
    std::vector<CaptureShift> prefix(bindings.size() + 1);
    if (prefix.capacity() > budget_bytes / sizeof(CaptureShift)) return Resource();
    auto remaining = budget_bytes - prefix.capacity() * sizeof(CaptureShift);
    if (source.text.size() > remaining) return Resource();
    const auto name_budget = remaining - source.text.size();
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const LexicalName name{bindings[i]};
      if (name.Text().size() > name_budget - prefix[i].bytes) return Resource();
      prefix[i + 1].bytes = prefix[i].bytes + name.Text().size();
    }
    const auto bytes = source.text.size() + prefix.back().bytes;
    PrintedSource result;
    result.contract = source.contract;
    if (source.host_realm) {
      if (source.host_realm->binding.size() >= remaining) return Resource();
      result.host_realm.emplace();
      auto& binding = result.host_realm->binding;
      binding.reserve(source.host_realm->binding.size());
      if (binding.capacity() >= remaining) return Resource();
      remaining -= binding.capacity() + 1;
      binding = source.host_realm->binding;
    }
    // Admit final text, maps and simultaneous prefix scratch before allocation.
    if (bytes >= remaining || source.spans.size() >
        (remaining - bytes - 1) / sizeof(GeneratedSpan)) return Resource();
    result.spans.reserve(source.spans.size());
    if (result.spans.capacity() > remaining / sizeof(GeneratedSpan)) return Resource();
    remaining -= result.spans.capacity() * sizeof(GeneratedSpan);
    if (bytes >= remaining || source.method_spans.size() >
        (remaining - bytes - 1) / sizeof(MethodGeneratedSpan)) return Resource();
    result.method_spans.reserve(source.method_spans.size());
    if (result.method_spans.capacity() > remaining / sizeof(MethodGeneratedSpan))
      return Resource();
    remaining -= result.method_spans.capacity() * sizeof(MethodGeneratedSpan);
    result.text.reserve(bytes);
    if (result.text.capacity() >= remaining) return Resource();

    GeneratedPosition original;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const auto& capture = source.captures[i];
      if (capture.position.captures != i ||
          capture.position.byte < original.byte ||
          capture.position.byte > source.text.size()) return Invalid();
      const auto part = std::string_view(source.text).substr(
          original.byte, capture.position.byte - original.byte);
      Advance(original, part);
      if (original.line != capture.position.line ||
          original.column != capture.position.column) return Invalid();
      result.text.append(part);
      const LexicalName name{bindings[i]};
      result.text.append(name.Text());
    }
    const auto tail = std::string_view(source.text).substr(original.byte);
    Advance(original, tail);
    result.text.append(tail);
    const auto relocate = [&](const GeneratedSpan& span) {
      if (std::tie(span.begin.byte, span.begin.captures) >
          std::tie(span.end.byte, span.end.captures)) throw Invalid(span.pc);
      const auto begin = RelocateCapturePosition(source, prefix, span.begin, span.pc);
      const auto end = RelocateCapturePosition(source, prefix, span.end, span.pc);
      if (begin.byte > result.text.size() || end.byte > result.text.size() ||
          begin.line > original.line || end.line > original.line)
        throw Invalid(span.pc);
      return GeneratedSpan{span.pc, begin, end, span.origin};
    };
    for (const auto& span : source.spans) result.spans.push_back(relocate(span));
    for (const auto& mapped : source.method_spans)
      result.method_spans.push_back({mapped.method, relocate(mapped.span)});
    return result;
  } catch (const SourceFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return Resource();
  } catch (const std::length_error&) {
    return Resource();
  }
}
}  // namespace abcd::source
