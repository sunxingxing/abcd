#include "abcd/analysis/transparent_rethrow.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
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

// A rethrow sequence: optional `sta vR` / `lda vR` moves, then THROW, where
// every `lda vR` reads a register this sequence stored from acc. acc thus still
// holds the caught exception at THROW, and the written registers are dead
// because THROW leaves the method (checked below). Returns the instruction
// indexes [entry, throw] or nothing. es2abc's optimizer emits `sta v; lda v;
// throw` for the same implicit super-call check handler.
std::optional<std::pair<std::size_t, std::size_t>> RethrowSequence(
    const abc::MethodBody& body, abc::CodeOffset pc) {
  const auto* entry = At(body, pc);
  if (!entry) return std::nullopt;
  const auto first = static_cast<std::size_t>(entry - body.instructions.data());
  std::vector<std::uint64_t> stored;
  for (std::size_t i = first; i < body.instructions.size() && i - first < 16; ++i) {
    const auto& decoded = body.instructions[i].decoded;
    if (decoded.opcode == abc::Opcode::kTHROW_PREF_NONE) return std::pair{first, i};
    const bool sta = decoded.opcode == abc::Opcode::kSTA_V8, lda = decoded.opcode == abc::Opcode::kLDA_V8;
    if ((!sta && !lda) || decoded.operand_count != 1 ||
        decoded.operands[0].kind != abc::OperandKind::kRegister) return std::nullopt;
    const auto reg = decoded.operands[0].raw_bits;
    if (sta) stored.push_back(reg);
    else if (std::find(stored.begin(), stored.end(), reg) == stored.end()) return std::nullopt;
  }
  return std::nullopt;
}

bool EraseTransparentRethrowRegions(abc::MethodBody& body) {
  if (body.tries.empty()) return false;
  std::vector<abc::CodeOffset> handler_pcs;
  std::vector<std::pair<std::size_t, std::size_t>> sequences;
  handler_pcs.reserve(body.handlers.size());
  for (const auto& region : body.tries) {
    if (region.handler_count == 0 ||
        region.first_handler > body.handlers.size() ||
        region.handler_count > body.handlers.size() - region.first_handler)
      return false;
    for (std::uint32_t i = 0; i < region.handler_count; ++i) {
      const auto& handler = body.handlers[region.first_handler + i];
      const auto sequence = RethrowSequence(body, handler.offset);
      if (!sequence) return false;
      handler_pcs.push_back(handler.offset);
      sequences.push_back(*sequence);
    }
  }
  // A multi-instruction sequence is admitted only in the trailing chain of
  // handler sequences that end the method, which the cleanup below removes
  // completely. Elsewhere (e.g. a for-in acquisition handler before later
  // code) the region is kept for the protocol recognizers, as before.
  {
    std::vector<std::pair<std::size_t, std::size_t>> ordered(sequences);
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    std::size_t tail = body.instructions.size();
    std::vector<std::uint8_t> trailing(ordered.size());
    for (std::size_t k = ordered.size(); k-- > 0;) {
      if (ordered[k].second + 1 != tail) break;
      trailing[k] = 1; tail = ordered[k].first;
    }
    for (std::size_t k = 0; k < ordered.size(); ++k)
      if (ordered[k].first != ordered[k].second && !trailing[k]) return false;
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
  // A handler instruction must not be a normal branch target; otherwise a
  // normal path could run the moves and THROW, which the erasure keeps anyway.
  std::vector<abc::CodeOffset> targets;
  for (const auto& instruction : body.instructions)
    if (instruction.branch_target) targets.push_back(*instruction.branch_target);
  std::sort(targets.begin(), targets.end());
  body.tries.clear();
  body.handlers.clear();
  // Drop trailing whole rethrow sequences that no normal edge can reach.
  std::sort(sequences.begin(), sequences.end());
  sequences.erase(std::unique(sequences.begin(), sequences.end()), sequences.end());
  while (!sequences.empty() && body.instructions.size() > 1) {
    const auto [first, last] = sequences.back();
    if (last + 1 != body.instructions.size() || first == 0 ||
        FallsThrough(body.instructions[first - 1])) break;
    bool reached = false;
    for (std::size_t i = first; i <= last; ++i)
      reached |= std::binary_search(targets.begin(), targets.end(), body.instructions[i].offset);
    if (reached) break;
    body.instructions.erase(body.instructions.begin() + static_cast<std::ptrdiff_t>(first), body.instructions.end());
    sequences.pop_back();
  }
  return true;
}
}  // namespace abcd::analysis
