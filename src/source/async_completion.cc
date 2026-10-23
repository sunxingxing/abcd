#include "abcd/source/async_completion.h"
#include "abcd/abc/profile_capabilities.h"

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/register_access.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/semantic_ir.h"
#include "async_handlers.h"

namespace abcd::source {
namespace {
// allocate_shared's implementation-owned control block is charged before its
// actual allocation, rather than guessing its size from the standard library.
template<class T> struct CatchDiagnosticAllocator {
  using value_type = T;
  std::size_t allowance;
  explicit CatchDiagnosticAllocator(std::size_t bytes) : allowance(bytes) {}
  template<class U> CatchDiagnosticAllocator(const CatchDiagnosticAllocator<U>& a) : allowance(a.allowance) {}
  T* allocate(std::size_t n) {
    if (n > allowance / sizeof(T)) throw std::bad_alloc{};
    return std::allocator<T>{}.allocate(n);
  }
  void deallocate(T* p, std::size_t n) { std::allocator<T>{}.deallocate(p,n); }
  template<class U> bool operator==(const CatchDiagnosticAllocator<U>& a) const { return allowance == a.allowance; }
};
// Allocation admission for a small diagnostic object and shared ownership;
// the rebound allocator checks the implementation's real request against it.
constexpr std::size_t kCatchDiagnosticBytes = 256;
bool IsRegister(const abc::BodyInstruction& instruction,
                abc::Opcode opcode, abc::RegisterId* register_id) {
  const auto& decoded = instruction.decoded;
  if (decoded.opcode != opcode || decoded.operand_count != 1 ||
      decoded.operands[0].kind != abc::OperandKind::kRegister ||
      decoded.operands[0].raw_bits >
          std::numeric_limits<std::uint32_t>::max())
    return false;
  *register_id = abc::RegisterId{
      static_cast<std::uint32_t>(decoded.operands[0].raw_bits)};
  return true;
}
bool IsReturn(const abc::BodyInstruction& instruction) {
  return instruction.decoded.opcode == abc::Opcode::kRETURN &&
         instruction.decoded.operand_count == 0;
}
bool AllowsLexicalPrelude(abc::ProfileId profile, ResourceFailureScope resources) {
  if (resources != ResourceFailureScope::kExcluded) return false;
  // Same resume protocol as FindAwaitRegions: the pinned-runtime column of
  // the profile capability table, never an opcode spelling match.
  return abc::HasPinnedRuntimeSemantics(profile);
}
bool TouchesRegister(const abc::BodyInstruction& instruction,
                     abc::RegisterId register_id, std::uint32_t count) {
  const auto accesses = abc::GetRegisterAccesses(instruction.decoded, count);
  const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
  if (!known) return true;
  for (std::size_t i = 0; i < known->count; ++i) {
    const auto& window = known->windows[i];
    if (register_id.value >= window.first.value &&
        register_id.value - window.first.value < window.count)
      return true;
  }
  return false;
}
bool RebaseBranches(const abc::MethodBody& original,
                    std::span<const abc::CodeOffset> origins,
                    abc::MethodBody& source, AsyncCompletionEvidence& evidence) {
  for (auto& site : source.instructions) {
    if (!site.branch_target) continue;
    const auto target = *site.branch_target;
    evidence.check = AsyncCompletionCheck::kBranchRebase;
    evidence.location.working_pc = site.offset;
    const auto ordinal = static_cast<std::size_t>(&site - source.instructions.data());
    evidence.location.pc = origins[ordinal]; evidence.related_pc = target;
    const auto retained = std::lower_bound(origins.begin(), origins.end(), target);
    if (retained == origins.end()) return false;
    const auto target_index = static_cast<std::size_t>(retained - origins.begin());
    // A direct jump to RETURN would bypass the original promise settlement.
    // Only a jump to the removed RESOLVE may be redirected to that return.
    if (*retained == target && IsReturn(source.instructions[target_index]))
      return false;
    if (*retained != target) {
      const auto old = std::lower_bound(
          original.instructions.begin(), original.instructions.end(), target,
          [](const auto& instruction, auto pc) { return instruction.offset < pc; });
      // Only a proven settlement immediately followed by its retained return
      // can be a removed branch target. Never round into a deleted VM region.
      if (old == original.instructions.end() || old->offset != target ||
          old->decoded.opcode != abc::Opcode::kASYNCFUNCTIONRESOLVE_V8 ||
          std::uint64_t(target.value) + old->decoded.size != retained->value ||
          !IsReturn(source.instructions[target_index]))
        return false;
    }
    const auto destination = source.instructions[target_index].offset;
    const auto delta = std::int64_t(destination.value) - site.offset.value;
    if (site.decoded.operand_count != 1) return false;
    auto& operand = site.decoded.operands[0];
    const auto width = operand.width_bits;
    if (operand.kind != abc::OperandKind::kImmediate || !width ||
        width > sizeof(std::int32_t) * CHAR_BIT)
      return false;
    const auto magnitude = std::int64_t{1} << (width - 1);
    if (delta < -magnitude || delta >= magnitude) return false;
    operand.raw_bits = static_cast<std::uint64_t>(delta) &
                       ((std::uint64_t{1} << width) - 1);
    site.decoded.branch_delta = delta;
    site.branch_target = destination;
  }
  return true;
}
}  // namespace
AsyncCompletion ProveAsyncCompletion(const abc::MethodBody& body,
                                     const abc::FrameLayout& frame,
                                     std::size_t bytes,
                                     std::vector<abc::CodeOffset>* source_pcs,
                                     std::vector<analysis::AwaitSite>* awaits,
                                     abc::ProfileId profile,
                                     ResourceFailureScope resources) {
  using abc::Opcode;
  const auto& sites = body.instructions;
  using Check = AsyncCompletionCheck;
  const auto fail = [&](Check check, std::optional<std::size_t> index = {}) {
    return AsyncCompletionFailure{check, index && *index < sites.size() ?
        std::optional{sites[*index].offset} : std::nullopt};
  };
  if (sites.size() < 7 || body.tries.empty() || body.handlers.empty())
    return fail(Check::kInput);
  std::size_t enter = 0;
  // NOPs carry no semantics; leading ones stay in the normalized prefix.
  while (enter < sites.size() && sites[enter].decoded.opcode == Opcode::kNOP) ++enter;
  // v7 NEWLEXENV[WITHNAME] allocates VM-only state before promise creation.
  // Keep it in normalized IR; only OOM timing is excluded by the contract.
  bool lexical_prelude = false;
  if (enter < sites.size() && AllowsLexicalPrelude(profile, resources) &&
      std::holds_alternative<analysis::PushEnvironment>(
          analysis::LiftLexicalInstruction(sites[enter].decoded, frame.registers))) {
    ++enter;
    lexical_prelude = true;
  }
  // Function instantiation before ASYNCFUNCTIONENTER is retained verbatim in
  // the normalized prefix. Admit it by its typed semantics, not by opcode
  // layout: every instruction must lift through the common lifter, cannot
  // throw or transfer control, and is either an effect-free no-op/scalar
  // assignment (register/acc copy or constant) or, with the fresh environment
  // above, a depth-0 store into it (e.g. a captured parameter). No user code,
  // heap access or throw precedes the promise. Promise storage is checked once
  // its register is known.
  const auto prefix_begin = enter;
  while (enter < sites.size() &&
         sites[enter].decoded.opcode != Opcode::kASYNCFUNCTIONENTER) {
    const auto lifted = analysis::LiftSemanticInstruction(sites[enter], profile, frame.registers, &frame);
    const auto* instruction = std::get_if<analysis::SemanticInstruction>(&lifted);
    // A hoisted function declaration (DEFINEFUNC) is created before the
    // promise in Ark, after it in the spec (EvaluateAsyncFunctionBody runs
    // FunctionDeclarationInstantiation after NewPromiseCapability). Creation
    // runs no user code and its only throw is allocation failure, which the
    // same excluded-resource contract as the NEWLEXENV prelude covers; its
    // reads are of the method/module/environment being closed over.
    constexpr auto kCreationReads = analysis::LanguageEffect::kHeapRead | analysis::LanguageEffect::kModuleRead |
        analysis::LanguageEffect::kAllocation | analysis::LanguageEffect::kEnvironmentRead;
    const auto* creation = instruction ? std::get_if<analysis::FunctionCreation>(&instruction->operation) : nullptr;
    const bool hoisted = creation && creation->kind == analysis::CreationKind::kFunction &&
        creation->home_object == analysis::HomeObjectInput::kCurrentFunction &&
        AllowsLexicalPrelude(profile, resources) &&
        (static_cast<std::uint16_t>(instruction->effects.normal) & ~static_cast<std::uint16_t>(kCreationReads)) == 0 &&
        (static_cast<std::uint16_t>(instruction->effects.before_throw) & ~static_cast<std::uint16_t>(kCreationReads)) == 0 &&
        abc::GetControlTransfer(sites[enter].decoded.opcode) == abc::ControlTransfer::kFallthrough;
    if (hoisted) { ++enter; continue; }
    if (!instruction || instruction->effects.exceptional != analysis::ExceptionalCompletion::kImpossible ||
        abc::GetControlTransfer(sites[enter].decoded.opcode) != abc::ControlTransfer::kFallthrough)
      return fail(Check::kPreludeEffects, enter);
    const auto& operation = instruction->operation;
    const auto* store = std::get_if<analysis::LexicalStore>(&operation);
    // A depth-0 store writes the fresh environment's own cell: its lexical
    // write and environment read are unobservable here, because no code runs
    // before the promise: a hoisted closure may exist, but no call is admitted.
    constexpr auto kFreshCellStore = analysis::LanguageEffect::kLexicalWrite | analysis::LanguageEffect::kEnvironmentRead;
    const bool cell = store && lexical_prelude && store->address.depth.value == 0 &&
        (static_cast<std::uint16_t>(instruction->effects.normal) & ~static_cast<std::uint16_t>(kFreshCellStore)) == 0;
    const bool pure = instruction->effects.normal == analysis::LanguageEffect::kNone &&
        (std::holds_alternative<analysis::NoOperation>(operation) ||
         std::holds_alternative<analysis::ScalarAssignment>(operation));
    if (!cell && !pure) return fail(Check::kPreludeOperation, enter);
    ++enter;
  }
  if (enter + 5 >= sites.size() ||
      sites[enter].decoded.operand_count != 0)
    return fail(Check::kEnter, enter);
  abc::RegisterId promise;
  if (!IsRegister(sites[enter + 1], Opcode::kSTA_V8, &promise) ||
      promise.value >= frame.locals)
    return fail(Check::kPromiseStore, enter + 1);
  for (std::size_t i = prefix_begin; i < enter; ++i)
    if (TouchesRegister(sites[i], promise, frame.registers)) return fail(Check::kPromisePreludeUse, i);
  // Normalization erases ASYNCFUNCTIONENTER's accumulator definition (the
  // async object) with its `sta promise`. In the original, acc still holds
  // that object after the store; the normalized body would instead see the
  // prefix's last acc value. Prove on the normal CFG that this definition
  // reaches no read: propagate it from the instruction after the store along
  // fallthrough/jump/branch successors, stopping where acc is definitely
  // redefined. Exception edges are not followed: handler entry defines acc as
  // the caught value. A suspension or an unresolved target refuses. Each
  // instruction is visited once; targets are found by binary search.
  try {  // Scratch released before the normalization allocations below.
    if (sites.size() > bytes / (sizeof(std::uint8_t) + sizeof(std::size_t))) return FunctionEmitError::kResourceLimit;
    std::vector<std::uint8_t> visited(sites.size());
    std::vector<std::size_t> pending;
    pending.reserve(sites.size());
    if (visited.capacity() > bytes / sizeof(std::uint8_t) ||
        pending.capacity() > (bytes - visited.capacity()) / sizeof(std::size_t))
      return FunctionEmitError::kResourceLimit;
    const auto index_of = [&](abc::CodeOffset target) -> std::optional<std::size_t> {
      const auto found = std::lower_bound(sites.begin(), sites.end(), target,
          [](const auto& site, auto pc) { return site.offset < pc; });
      if (found == sites.end() || found->offset != target) return std::nullopt;
      return static_cast<std::size_t>(found - sites.begin());
    };
    if (enter + 2 >= sites.size()) return fail(Check::kAccumulatorFallthrough, enter + 1);
    pending.push_back(enter + 2); visited[enter + 2] = 1;
    while (!pending.empty()) {
      const auto i = pending.back(); pending.pop_back();
      const auto effect = analysis::LiftRegisterEffect(sites[i].decoded, frame.registers);
      const auto* registers = std::get_if<analysis::RegisterEffect>(&effect);
      if (!registers) return fail(Check::kAccumulatorEffect, i);
      for (std::uint8_t r = 0; r < registers->read_count; ++r)
        if (std::holds_alternative<analysis::Accumulator>(registers->reads[r])) return fail(Check::kAccumulatorRead, i);
      if (registers->write && std::holds_alternative<analysis::Accumulator>(*registers->write)) continue;
      const auto control = abc::GetControlTransfer(sites[i].decoded.opcode);
      if (!control || *control == abc::ControlTransfer::kSuspend) return fail(Check::kAccumulatorControl, i);
      std::array<std::optional<std::size_t>, 2> next{};
      if (*control == abc::ControlTransfer::kFallthrough || *control == abc::ControlTransfer::kBranch) {
        if (i + 1 >= sites.size()) return fail(Check::kAccumulatorFallthrough, i);
        next[0] = i + 1;
      }
      if (*control == abc::ControlTransfer::kJump || *control == abc::ControlTransfer::kBranch) {
        if (!sites[i].branch_target) return fail(Check::kAccumulatorTarget, i);
        next[1] = index_of(*sites[i].branch_target);
        if (!next[1]) return fail(Check::kAccumulatorTarget, i);
      }
      for (const auto successor : next)
        if (successor && !visited[*successor]) { visited[*successor] = 1; pending.push_back(*successor); }
    }
  } catch (const std::bad_alloc&) { return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) { return FunctionEmitError::kResourceLimit; }
  // Completion catch: `sta caught; [lda caught;] asyncfunctionreject p;
  // return`. The optimizer's optional reload of the same register leaves acc
  // unchanged (still the caught value), so the rejection reason is the same.
  abc::RegisterId caught, resolved, rejected, reloaded, saved;
  const bool reload = sites.size() >= 4 &&
      IsRegister(sites[sites.size() - 3], Opcode::kLDA_V8, &reloaded) &&
      IsRegister(sites[sites.size() - 4], Opcode::kSTA_V8, &saved) && reloaded == saved;
  const auto handler = sites.size() - (reload ? 4 : 3);
  const auto reject_at = handler + (reload ? 2 : 1);
  if (handler <= enter + 4) return fail(Check::kInput, handler);
  if (!IsRegister(sites[handler], Opcode::kSTA_V8, &caught)) return fail(Check::kCatchStore, handler);
  if (caught == promise || caught.value >= frame.locals) return fail(Check::kCatchRegister, handler);
  if (!IsRegister(sites[reject_at], Opcode::kASYNCFUNCTIONREJECT_V8, &rejected)) return fail(Check::kReject, reject_at);
  if (!IsReturn(sites[reject_at + 1])) return fail(Check::kRejectReturn, reject_at + 1);
  if (!IsRegister(sites[handler - 2], Opcode::kASYNCFUNCTIONRESOLVE_V8, &resolved)) return fail(Check::kResolve, handler - 2);
  if (!IsReturn(sites[handler - 1])) return fail(Check::kResolveReturn, handler - 1);
  if (resolved != promise || rejected != promise) return fail(Check::kPromiseIdentity, handler);
  auto retained = handler - 3;
  const auto site_bytes = sizeof(abc::BodyInstruction) + sizeof(abc::CodeOffset);
  if (retained > bytes / site_bytes)
    return FunctionEmitError::kResourceLimit;
  const std::uint64_t code_end =
      std::uint64_t(sites.back().offset.value) + sites.back().decoded.size;
  if (!HasAsyncCompletionCatch(body, sites[enter + 2].offset,
                               sites[handler].offset, code_end))
    return fail(Check::kCompletionCatch, handler);
  try {
  std::vector<analysis::AwaitRegion> regions;
  const bool has_await = awaits && std::any_of(sites.begin(), sites.end(),
      [&](const auto& site) {
        const auto lifted = analysis::LiftCoroutineInstruction(
            site.decoded, frame.registers);
        const auto* transfer = std::get_if<analysis::CoroutineTransfer>(&lifted);
        return transfer && transfer->kind == analysis::CoroutineTransferKind::kAwait;
      });
  if (has_await) {
    auto found = analysis::FindAwaitRegions(body, profile, frame.registers, bytes);
    if (const auto* failure = std::get_if<analysis::AwaitRegionFailure>(&found)) {
      AsyncCompletionFailure result{Check::kAwaitRegion, failure->pc}; result.cause = failure->kind;
      if (failure->kind == analysis::AwaitRegionError::kResourceLimit) result.kind = FunctionEmitError::kResourceLimit;
      return result;
    }
    regions = std::get<std::vector<analysis::AwaitRegion>>(std::move(found));
  }
  const auto region_bytes = regions.capacity() * sizeof(analysis::AwaitRegion);
  if (region_bytes > bytes) return FunctionEmitError::kResourceLimit;
  bytes -= region_bytes;
  auto catch_inputs = analysis::ProveAwaitCatchInputs(
      body, regions, profile, frame.registers, sites[handler].offset, {bytes});
  bool catch_flow_proved = false;
  if (const auto* failure = std::get_if<analysis::AwaitCatchFailure>(&catch_inputs)) {
    AsyncCompletionFailure result{Check::kCatchInput, failure->pc}; result.cause = failure->kind;
    result.async.catch_input = failure->observation;
    // Supplement only fast-proof unknowns. Already accepted normalizations keep
    // their original path; raw-flow proof never authorizes close projection.
    if (failure->kind == analysis::AwaitCatchError::kObservedRegister) {
      constexpr auto overhead = kCatchDiagnosticBytes;
      if (bytes >= overhead) {
        const auto available = bytes - overhead;
        auto proven = analysis::ProveAwaitCatchFlow(body, regions, profile, frame.registers,
            sites[handler].offset, {available});
        auto report = std::allocate_shared<analysis::AwaitCatchFlowDiagnostic>(
            CatchDiagnosticAllocator<analysis::AwaitCatchFlowDiagnostic>{overhead});
        if (const auto* error = std::get_if<analysis::CatchFlowFailure>(&proven)) report->failure = *error;
        else {
          auto& proof = std::get<analysis::AwaitCatchFlowProof>(proven);
          const auto held = proof.states.capacity() * sizeof(analysis::CatchFlowState) +
              proof.queries.capacity() * sizeof(analysis::CatchFlowEvidence);
          if (held > available) report->failure = analysis::CatchFlowFailure{analysis::CatchFlowError::kResourceLimit, {}};
          else {
            const auto checked = analysis::VerifyAwaitCatchFlow(body, regions, profile,
                frame.registers, sites[handler].offset, proof, {available - held});
            if (const auto* error = std::get_if<analysis::CatchFlowFailure>(&checked)) report->failure = *error;
            else {
              report->verified = true;
              if (failure->observation && failure->observation->prefix == analysis::CatchInputPrefix::kUnprovedPaths) {
                auto supplemented = analysis::ProveAwaitCatchInputsWithFlow(body, regions, profile,
                    frame.registers, sites[handler].offset, proof, {available - held});
                // Failed supplementary proofs cannot erase the original witness.
                // Only successful proof composition authorizes normalization.
                if (std::holds_alternative<analysis::AwaitCatchProof>(supplemented))
                  catch_flow_proved = true;
              }
              report->queries = std::move(proof.queries);
            }
          }
        }
        result.async.catch_flow = std::move(report);
      }
    }
    if (failure->kind == analysis::AwaitCatchError::kResourceLimit) result.kind = FunctionEmitError::kResourceLimit;
    if (!catch_flow_proved) return result;
  }
  std::size_t next_region = 0;
  for (std::size_t i = enter + 2; i < handler - 2; ++i) {
    const auto& site = sites[i];
    if (next_region < regions.size() && site.offset == regions[next_region].await) {
      const auto& region = regions[next_region++];
      if (region.state != promise) return fail(Check::kAwaitState, i);
      if (region.continuation >= sites[handler - 2].offset) return fail(Check::kAwaitBounds, i);
      while (i + 1 < handler - 2 && sites[i + 1].offset < region.continuation) {
        ++i;
        if (sites[i].offset != region.result_store &&
            sites[i].offset != region.mode && sites[i].offset != region.mode_store)
          --retained;
      }
      continue;
    }
    abc::RegisterId settled;
    if (IsRegister(site, Opcode::kASYNCFUNCTIONRESOLVE_V8, &settled)) {
      if (settled != promise) return fail(Check::kResolveIdentity, i);
      if (i + 1 >= handler - 2 || !IsReturn(sites[i + 1])) return fail(Check::kResolveSuccessor, i);
      --retained;
      continue;
    }
    const auto control = abc::GetControlTransfer(site.decoded.opcode);
    if (!control || *control == abc::ControlTransfer::kSuspend)
      return fail(Check::kBodyControl, i);
    if (*control == abc::ControlTransfer::kReturn) {
      if (!IsReturn(site) || i == enter + 2 ||
          !IsRegister(sites[i - 1], Opcode::kASYNCFUNCTIONRESOLVE_V8, &settled) ||
          settled != promise)
        return fail(Check::kReturnProtocol, i);
    }
    if (site.branch_target &&
        (*site.branch_target < sites[enter + 2].offset ||
         *site.branch_target > sites[handler - 1].offset))
      return fail(Check::kBranchBounds, i);
    if (TouchesRegister(site, promise, frame.registers))
      return fail(Check::kPromiseUse, i);
  }
    if (next_region != regions.size()) return fail(Check::kAwaitCoverage);
    if (regions.size() > bytes / sizeof(analysis::AwaitSite))
      return FunctionEmitError::kResourceLimit;
    std::vector<analysis::AwaitSite> await_sites;
    await_sites.reserve(regions.size());
    if (await_sites.capacity() > bytes / sizeof(analysis::AwaitSite))
      return FunctionEmitError::kResourceLimit;
    bytes -= await_sites.capacity() * sizeof(analysis::AwaitSite);
    if (retained > bytes / site_bytes) return FunctionEmitError::kResourceLimit;
    abc::MethodBody source;
    source.instructions.reserve(retained);
    if (source.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return FunctionEmitError::kResourceLimit;
    source.instructions.insert(source.instructions.end(), sites.begin(),
                               sites.begin() + enter);
    next_region = 0;
    for (std::size_t i = enter + 2; i < handler - 2; ++i) {
      if (sites[i].decoded.opcode == Opcode::kASYNCFUNCTIONRESOLVE_V8) continue;
      source.instructions.push_back(sites[i]);
      if (next_region < regions.size() && sites[i].offset == regions[next_region].await) {
        const auto& region = regions[next_region++];
        await_sites.push_back({sites[i].offset, region, region.mode});
        while (i + 1 < handler - 2 && sites[i + 1].offset < region.continuation) {
          ++i;
          if (sites[i].offset == region.result_store ||
              sites[i].offset == region.mode || sites[i].offset == region.mode_store)
            source.instructions.push_back(sites[i]);
        }
      }
    }
    source.instructions.push_back(sites[handler - 1]);
    std::vector<abc::CodeOffset> origins;
    {
      const auto remaining = bytes - source.instructions.capacity() *
                                     sizeof(abc::BodyInstruction);
      if (retained > remaining / sizeof(abc::CodeOffset))
        return FunctionEmitError::kResourceLimit;
      origins.reserve(retained);
      if (origins.capacity() > remaining / sizeof(abc::CodeOffset))
        return FunctionEmitError::kResourceLimit;
      for (const auto& site : source.instructions) origins.push_back(site.offset);
    }
    std::uint32_t next_pc = 0;
    next_region = 0;
    std::size_t next_mode = 0;
    for (auto& site : source.instructions) {
      if (next_mode < await_sites.size() &&
          site.offset == await_sites[next_mode].origin.mode)
        await_sites[next_mode++].mode_pc = abc::CodeOffset{next_pc};
      if (next_region < await_sites.size() &&
          site.offset == await_sites[next_region].origin.await)
        await_sites[next_region++].pc = abc::CodeOffset{next_pc};
      site.offset = abc::CodeOffset{next_pc};
      next_pc += site.decoded.size;
    }
    AsyncCompletionEvidence rebasing;
    if (!RebaseBranches(body, origins, source, rebasing)) {
      auto failure = fail(Check::kBranchRebase); failure.async = rebasing; return failure;
    }
    const auto retained_bytes = source.instructions.capacity() *
        sizeof(abc::BodyInstruction) + origins.capacity() * sizeof(abc::CodeOffset);
    if (retained_bytes > bytes) return FunctionEmitError::kResourceLimit;
    if (const auto error = RebaseAsyncHandlers(body, origins, source,
                                                 bytes - retained_bytes, &rebasing)) {
      AsyncCompletionFailure failure{*error};
      if (*error == FunctionEmitError::kKind) failure.async = rebasing;
      return failure;
    }
    if (source_pcs) *source_pcs = std::move(origins);
    if (awaits) *awaits = std::move(await_sites);
    return source;
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
}
}  // namespace abcd::source
