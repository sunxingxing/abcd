#include "record_declarations.h"

#include <limits>

namespace abcd::source {
namespace {
[[noreturn]] void Resource() {
  throw SourceFailure{SourceError::kResourceLimit, {}};
}
[[noreturn]] void Invalid() {
  throw SourceFailure{SourceError::kInvalidEffect, {}};
}
}  // namespace

void InsertRecordDeclarations(PrintedSource& source, ScalarNames names,
                              GeneratedPosition body, std::size_t bytes) {
  if (source.vm_slots == VmSlotUse::kUnknown ||
      source.register_uses.size() != names.registers.size() ||
      body.byte > source.text.size() || !body.line || body.column || body.captures)
    Invalid();
  constexpr std::string_view kLet = "let ";
  constexpr std::string_view kEnd = ";\n";
  std::size_t declaration_bytes = 0;
  std::uint32_t lines = 0;
  const auto measure = [&](std::string_view name) {
    if (name.size() > SIZE_MAX - kLet.size() - kEnd.size()) Resource();
    const auto size = name.size() + kLet.size() + kEnd.size();
    if (size > SIZE_MAX - declaration_bytes || lines == UINT32_MAX) Resource();
    declaration_bytes += size;
    ++lines;
  };
  if (source.accumulator_used) measure(names.accumulator);
  for (std::size_t i = 0; i < source.register_uses.size(); ++i)
    if (source.register_uses[i] == RegisterUse::kUsed) measure(names.registers[i]);
  if (!declaration_bytes) return;
  const auto charge = [&](std::size_t count, std::size_t width) {
    if (count > bytes / width) Resource();
    bytes -= count * width;
  };
  charge(source.text.capacity(), sizeof(char));
  charge(1, sizeof(char));
  charge(source.spans.capacity(), sizeof(GeneratedSpan));
  if (declaration_bytes >= bytes) Resource();
  std::string declarations;
  declarations.reserve(declaration_bytes);
  charge(declarations.capacity(), sizeof(char));
  charge(1, sizeof(char));
  const auto append = [&](std::string_view name) {
    declarations.append(kLet);
    declarations.append(name);
    declarations.append(kEnd);
  };
  if (source.accumulator_used) append(names.accumulator);
  for (std::size_t i = 0; i < source.register_uses.size(); ++i)
    if (source.register_uses[i] == RegisterUse::kUsed) append(names.registers[i]);
  if (declaration_bytes > SIZE_MAX - source.text.size()) Resource();
  const auto needed = source.text.size() + declaration_bytes;
  if (needed > source.text.capacity()) {
    // The old text, declaration text and replacement coexist during reserve.
    if (needed >= bytes) Resource();
    source.text.reserve(needed);
    if (source.text.capacity() >= bytes) Resource();
  }
  const auto relocate = [&](GeneratedPosition& position) {
    if (position.byte < body.byte || position.line < body.line || position.captures)
      Invalid();
    if (declaration_bytes > SIZE_MAX - position.byte ||
        lines > UINT32_MAX - position.line) Resource();
    position.byte += declaration_bytes;
    position.line += lines;
    // Complete newline-terminated declarations do not move body columns.
  };
  for (auto& span : source.spans) {
    relocate(span.begin);
    relocate(span.end);
  }
  for (auto& mapped : source.method_spans) {
    relocate(mapped.span.begin);
    relocate(mapped.span.end);
  }
  source.text.insert(body.byte, declarations);
}
}  // namespace abcd::source
