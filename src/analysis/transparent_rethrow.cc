#include "abcd/analysis/transparent_rethrow.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "abcd/abc/control_transfer.h"

namespace abcd::analysis {
namespace {
const abc::BodyInstruction* At(const abc::MethodBody& body, abc::CodeOffset pc) {
  const auto found = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
      [](const abc::BodyInstruction& instruction, abc::CodeOffset key) {
        return instruction.offset < key;
      });
  return found != body.instructions.end() && found->offset == pc ? &*found : nullptr;
}
bool FallsThrough(const abc::BodyInstruction& instruction) {
  const auto transfer = abc::GetControlTransfer(instruction.decoded.opcode);
  return !transfer || (*transfer != abc::ControlTransfer::kJump &&
                       *transfer != abc::ControlTransfer::kReturn &&
                       *transfer != abc::ControlTransfer::kThrow);
}
}  // namespace

bool EraseTransparentRethrowRegions(abc::MethodBody& body) {
  if (body.tries.empty()) return false;
  std::vector<abc::CodeOffset> handler_pcs;
  handler_pcs.reserve(body.handlers.size());
  for (const auto& region : body.tries) {
    if (region.handler_count == 0 ||
        region.first_handler > body.handlers.size() ||
        region.handler_count > body.handlers.size() - region.first_handler)
      return false;
    for (std::uint32_t i = 0; i < region.handler_count; ++i) {
      const auto& handler = body.handlers[region.first_handler + i];
      const auto* entry = At(body, handler.offset);
      if (!entry || entry->decoded.opcode != abc::Opcode::kTHROW_PREF_NONE) return false;
      handler_pcs.push_back(handler.offset);
    }
  }
  // A rethrow re-enters exception dispatch at the handler PC (ets_runtime
  // Method::FindCatchBlock, first covering try in file order). Erasure is
  // exact only if that dispatch leaves the method: no try region may cover
  // any handler, otherwise a rethrow can be caught again (including itself).
  // Covered intervals sorted by start and merged; O((R + H) log R).
  std::vector<std::pair<std::uint64_t, std::uint64_t>> covered;
  covered.reserve(body.tries.size());
  for (const auto& region : body.tries)
    covered.emplace_back(region.offset.value,
                         std::uint64_t{region.offset.value} + region.length);
  std::sort(covered.begin(), covered.end());
  std::size_t merged = 0;
  for (const auto& interval : covered) {
    if (merged != 0 && interval.first <= covered[merged - 1].second)
      covered[merged - 1].second = std::max(covered[merged - 1].second, interval.second);
    else
      covered[merged++] = interval;
  }
  covered.resize(merged);
  for (const auto pc : handler_pcs) {
    const auto after = std::upper_bound(covered.begin(), covered.end(),
        std::pair<std::uint64_t, std::uint64_t>{pc.value, UINT64_MAX});
    if (after != covered.begin() && pc.value < std::prev(after)->second) return false;
  }
  body.tries.clear();
  body.handlers.clear();
  std::sort(handler_pcs.begin(), handler_pcs.end());
  // Drop trailing handler-only instructions that no normal edge can reach.
  std::vector<abc::CodeOffset> targets;
  for (const auto& instruction : body.instructions)
    if (instruction.branch_target) targets.push_back(*instruction.branch_target);
  std::sort(targets.begin(), targets.end());
  while (body.instructions.size() > 1) {
    const auto& last = body.instructions.back();
    if (!std::binary_search(handler_pcs.begin(), handler_pcs.end(), last.offset) ||
        std::binary_search(targets.begin(), targets.end(), last.offset) ||
        FallsThrough(body.instructions[body.instructions.size() - 2]))
      break;
    body.instructions.pop_back();
  }
  return true;
}
}  // namespace abcd::analysis
