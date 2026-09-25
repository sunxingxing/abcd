#include "await_positions.h"

#include <algorithm>

namespace abcd::source {
namespace {
struct AwaitAnchor {
  const analysis::AwaitRegion* region;
  std::optional<GeneratedSpan> span;
};
const analysis::AwaitValue* Awaited(const SourceStatement& statement) {
  const auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
  return assignment ? std::get_if<analysis::AwaitValue>(&assignment->value) : nullptr;
}
bool Retained(const analysis::AwaitRegion& region, abc::CodeOffset pc) {
  return pc == region.await || pc == region.result_store || pc == region.mode ||
         pc == region.mode_store || pc == region.continuation;
}
}
std::optional<SourceFailure> AppendAwaitPositions(
    const SourceAst& ast, PrintedSource& source, std::size_t bytes) {
  const auto resource = [] { return SourceFailure{SourceError::kResourceLimit, {}}; };
  const auto invalid = [](abc::CodeOffset pc) {
    return SourceFailure{SourceError::kInvalidEffect, pc};
  };
  std::size_t count = 0;
  for (const auto& statement : ast.statements)
    if (Awaited(statement)) ++count;
  if (!count) return {};
  if (source.text.capacity() >= bytes) return resource();
  bytes -= source.text.capacity() + 1;
  if (source.spans.capacity() > bytes / sizeof(GeneratedSpan)) return resource();
  bytes -= source.spans.capacity() * sizeof(GeneratedSpan);
  if (count > bytes / sizeof(AwaitAnchor)) return resource();
  std::vector<AwaitAnchor> anchors;
  anchors.reserve(count);
  if (anchors.capacity() > bytes / sizeof(AwaitAnchor)) return resource();
  bytes -= anchors.capacity() * sizeof(AwaitAnchor);
  std::size_t extra = 0;
  for (const auto& statement : ast.statements) {
    const auto* awaited = Awaited(statement);
    if (!awaited) continue;
    const auto& region = awaited->origin;
    if (!region.instructions || statement.pc != region.await ||
        (!anchors.empty() && anchors.back().region->continuation >= region.await))
      return invalid(statement.pc);
    const auto& pcs = *region.instructions;
    if (pcs.front() != region.await || pcs.back() != region.continuation)
      return invalid(statement.pc);
    for (std::size_t i = 0; i < pcs.size(); ++i) {
      if (i && pcs[i - 1] >= pcs[i]) return invalid(pcs[i]);
      if (!Retained(region, pcs[i])) {
        if (extra >= bytes / sizeof(GeneratedSpan)) return resource();
        ++extra;
      }
    }
    anchors.push_back({&region, {}});
  }
  // Source emission order need not be PC order. Index the sorted IR await
  // certificates, not the generated text. No per-await scan of all spans.
  for (const auto& span : source.spans) {
    const auto found = std::lower_bound(anchors.begin(), anchors.end(), span.pc,
        [](const AwaitAnchor& anchor, abc::CodeOffset pc) {
          return anchor.region->await < pc;
        });
    if (found == anchors.end() || found->region->await != span.pc) continue;
    if (found->span || span.origin != SpanOrigin::kInstruction) return invalid(span.pc);
    found->span = span;
  }
  // Existing capacity remains live during reserve. Charge both old and new
  // buffers, even when an allocator might be able to extend in place.
  if (source.spans.size() > bytes / sizeof(GeneratedSpan) ||
      extra > bytes / sizeof(GeneratedSpan) - source.spans.size()) return resource();
  source.spans.reserve(source.spans.size() + extra);
  if (source.spans.capacity() > bytes / sizeof(GeneratedSpan)) return resource();
  for (const auto& anchor : anchors) {
    if (!anchor.span) return invalid(anchor.region->await);
    for (const auto pc : *anchor.region->instructions)
      if (!Retained(*anchor.region, pc))
        source.spans.push_back({pc, anchor.span->begin, anchor.span->end,
                                SpanOrigin::kAwaitProtocol});
  }
  return {};
}
}  // namespace abcd::source
