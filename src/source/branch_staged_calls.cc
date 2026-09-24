#include "branch_staged_calls.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/abc/control_transfer.h"
#include "abcd/analysis/register_ssa.h"

namespace abcd::source {
namespace {
constexpr std::uint8_t kReturnWidth = 1;  // Pinned isa.yaml RETURN format.
BodyEmitFailure Fail(BodyEmitError kind, abc::CodeOffset offset = {}) {
  return {kind, offset};
}
void Use(std::vector<std::uint8_t>& counts, analysis::SsaValueId value) {
  auto& count = counts[value.value];
  if (count != 2) ++count;
}
}  // namespace

std::variant<BranchStagedCalls, BodyEmitFailure> BuildBranchStagedCalls(
    const abc::MethodBody& body, const analysis::NormalFlow& graph,
    std::uint64_t frame_size, std::size_t bytes, std::uint64_t work) {
  using namespace analysis;
  try {
    std::size_t receiver_calls = 0;
    for (const auto& site : body.instructions)
      receiver_calls += std::holds_alternative<ReceiverCall>(
          LiftReceiverCall(site.decoded, frame_size));
    BranchStagedCalls result;
    if (receiver_calls == 0) return result;
    if (receiver_calls > bytes / sizeof(StagedNamedInvocation))
      return Fail(BodyEmitError::kResourceLimit);
    result.calls.reserve(receiver_calls);
    if (result.AllocatedBytes() > bytes)
      return Fail(BodyEmitError::kResourceLimit);
    work = work > body.instructions.size() ? work - body.instructions.size() : 0;
    for (const auto& block : graph.blocks) {
      if (block.instruction_count < 6) continue;
      const auto span = std::span(body.instructions).subspan(
          block.first_instruction, block.instruction_count);
      const auto transfer = abc::GetControlTransfer(span.back().decoded.opcode);
      if (!transfer) return Fail(BodyEmitError::kUnsupportedInstruction,
                                 span.back().offset);
      const bool terminal = *transfer == abc::ControlTransfer::kReturn ||
                            *transfer == abc::ControlTransfer::kThrow;
      const auto count = span.size() - (*transfer == abc::ControlTransfer::kJump ||
                                        *transfer == abc::ControlTransfer::kBranch);
      if (count < 5) continue;
      if (count + !terminal >
          (bytes - result.AllocatedBytes()) / sizeof(abc::BodyInstruction))
        return Fail(BodyEmitError::kResourceLimit);
      abc::MethodBody copied;
      copied.instructions.reserve(count + !terminal);
      const auto copy_bytes = copied.instructions.capacity() *
                              sizeof(abc::BodyInstruction);
      if (copy_bytes > bytes - result.AllocatedBytes())
        return Fail(BodyEmitError::kResourceLimit);
      copied.instructions.insert(copied.instructions.end(), span.begin(),
                                 span.begin() + count);
      if (!terminal) {
        const auto next_pc = std::uint64_t(copied.instructions.back().offset.value) +
                             copied.instructions.back().decoded.size;
        if (next_pc >= std::numeric_limits<std::uint32_t>::max())
          return Fail(BodyEmitError::kResourceLimit);
        abc::DecodedInstruction returned;
        returned.opcode = abc::Opcode::kRETURN;
        returned.size = kReturnWidth;
        returned.accumulator_read = true;
        copied.instructions.push_back(
            {{static_cast<std::uint32_t>(next_pc)}, returned, {}});
      }
      const auto matched = MatchStagedNamedInvocations(
          copied, frame_size, bytes - result.AllocatedBytes() - copy_bytes,
          work);
      if (const auto* error = std::get_if<StagedMatchError>(&matched))
        return Fail(*error == StagedMatchError::kWorkLimit
                        ? BodyEmitError::kWorkLimit
                        : BodyEmitError::kResourceLimit);
      const auto& found = std::get<StagedMatches>(matched);
      work -= found.work_units;
      for (auto call : found.calls) {
        call.property += block.first_instruction;
        call.spill += block.first_instruction;
        call.reload += block.first_instruction;
        call.call += block.first_instruction;
        result.calls.push_back(call);
      }
    }
    if (result.calls.empty()) return result;
    IdfQuota quota{work};
    const auto built = RegisterSsa::Build(
        body, graph, frame_size, quota,
        {bytes - result.AllocatedBytes()});
    if (const auto* error = std::get_if<RegisterUseFailure>(&built))
      return Fail(error->kind == RegisterUseError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : error->kind == RegisterUseError::kResourceLimit
                            ? BodyEmitError::kResourceLimit
                            : BodyEmitError::kUnsupportedInstruction,
                  error->offset);
    if (const auto* error = std::get_if<IdfError>(&built))
      return Fail(*error == IdfError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : *error == IdfError::kResourceLimit
                            ? BodyEmitError::kResourceLimit
                            : BodyEmitError::kUnsupportedInstruction);
    if (const auto* error = std::get_if<DominatorError>(&built))
      return Fail(*error == DominatorError::kResourceLimit
                      ? BodyEmitError::kResourceLimit
                      : BodyEmitError::kUnsupportedInstruction);
    if (const auto* error = std::get_if<SsaError>(&built))
      return Fail(*error == SsaError::kWorkLimit
                      ? BodyEmitError::kWorkLimit
                      : BodyEmitError::kResourceLimit);
    const auto& ssa = std::get<RegisterSsa>(built);
    const auto remaining = bytes - result.AllocatedBytes();
    if (ssa.AllocatedBytes() > remaining ||
        ssa.Values().size() > remaining - ssa.AllocatedBytes())
      return Fail(BodyEmitError::kResourceLimit);
    std::vector<std::uint8_t> uses(ssa.Values().size());
    for (std::size_t i = 0; i < body.instructions.size(); ++i)
      for (const auto& read : ssa.Reads({static_cast<std::uint32_t>(i)}))
        Use(uses, read.value);
    for (std::size_t i = 0; i < ssa.Values().size(); ++i)
      if (std::holds_alternative<PhiValue>(ssa.Values()[i].origin))
        for (const auto& input : ssa.Inputs({static_cast<std::uint32_t>(i)}))
          Use(uses, input.value);
    std::size_t retained = 0;
    for (const auto& call : result.calls) {
      const auto spill = ssa.Write({static_cast<std::uint32_t>(call.spill)});
      if (!spill || uses[spill->value] != 1) continue;
      bool reloaded = false;
      for (const auto& read :
           ssa.Reads({static_cast<std::uint32_t>(call.reload)}))
        reloaded |= read.value == *spill;
      if (reloaded) result.calls[retained++] = call;
    }
    result.calls.resize(retained);
    return result;
  } catch (const std::bad_alloc&) {
    return Fail(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(BodyEmitError::kResourceLimit);
  }
}
}  // namespace abcd::source
