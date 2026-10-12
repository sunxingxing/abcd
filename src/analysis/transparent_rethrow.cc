#include "abcd/analysis/transparent_rethrow.h"

#include <algorithm>
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
