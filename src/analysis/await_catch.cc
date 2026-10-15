#include "abcd/analysis/await_catch.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/analysis/exception_dispatch.h"
#include "abcd/analysis/coroutine.h"

namespace abcd::analysis {
namespace {
enum class FirstUse : std::uint8_t { kUnknown, kRead, kWrite, kExit, kSplit };
struct WorkLimit {};
void Charge(std::uint64_t& remaining, std::uint64_t units = 1) {
  if (units > remaining) throw WorkLimit{};
  remaining -= units;
}
struct CatchProbe {
  abc::CodeOffset handler;
  abc::RegisterId slot;
  abc::CodeOffset suspension;
};
constexpr std::size_t kResumeRegisters = 2;
// Standard lazy binary segment tree uses fewer than 4*N nodes for N leaves.
constexpr std::size_t kTreeNodesPerSlot = 4;
class FirstUseIndex {
 public:
  FirstUseIndex(std::span<FirstUse> nodes, std::size_t slots, std::uint64_t& work)
      : nodes_(nodes), slots_(slots), work_(work) {}
  void Reset(FirstUse use) { nodes_[1] = use; }
  void Assign(std::size_t begin, std::size_t end, FirstUse use) {
    if (begin != end) Assign(1, 0, slots_, begin, end, use);
  }
  FirstUse At(std::size_t slot) {
    std::size_t node = 1, begin = 0, end = slots_;
    while (true) {
      Charge(work_);
      if (nodes_[node] != FirstUse::kSplit) return nodes_[node];
      const auto middle = begin + (end - begin) / 2;
      node *= 2;
      if (slot < middle) end = middle;
      else { ++node; begin = middle; }
    }
  }

 private:
  void Assign(std::size_t node, std::size_t begin, std::size_t end,
              std::size_t first, std::size_t last, FirstUse use) {
    Charge(work_);
    if (first <= begin && end <= last) { nodes_[node] = use; return; }
    if (nodes_[node] != FirstUse::kSplit) {
      nodes_[node * 2] = nodes_[node];
      nodes_[node * 2 + 1] = nodes_[node];
      nodes_[node] = FirstUse::kSplit;
    }
    const auto middle = begin + (end - begin) / 2;
    if (first < middle) Assign(node * 2, begin, middle, first, last, use);
    if (last > middle) Assign(node * 2 + 1, middle, end, first, last, use);
  }
  std::span<FirstUse> nodes_;
  std::size_t slots_;
  std::uint64_t& work_;
};
const AwaitRegion& Region(const AwaitRegion& region) { return region; }
const YieldRegion& Region(const YieldSite& site) { return site.origin; }
abc::CodeOffset Start(const AwaitRegion& region) { return region.await; }
abc::CodeOffset Start(const YieldRegion& region) { return region.create; }

template <class Certificate>
std::variant<AwaitCatchProof, AwaitCatchFailure> ProveCatchInputs(
    const abc::MethodBody& body, std::span<const Certificate> regions,
    abc::ProfileId profile, std::uint64_t frame_size,
    abc::CodeOffset completion_handler, SemanticBudget budget,
    std::optional<abc::CodeOffset> implicit_exit = std::nullopt,
    std::span<const AwaitRegion> awaits = {},
    std::span<const CatchFlowEvidence> flow = {}) {
  const auto fail = [](AwaitCatchError kind, std::optional<abc::CodeOffset> pc = {}) {
    return AwaitCatchFailure{kind, pc};
  };
  if (regions.empty()) return AwaitCatchProof{};
  if (implicit_exit) {
    if constexpr (std::is_same_v<Certificate, YieldSite>) {
      for (const auto& region : regions)
        if (region.origin.execution != YieldExecution::kAsynchronous)
          return fail(AwaitCatchError::kRegion, *implicit_exit);
    }
    if (*implicit_exit >= completion_handler) return fail(AwaitCatchError::kRegion, *implicit_exit);
  }
  try {
    auto work = budget.work;
    const auto charge = [&] { Charge(work); };
    const auto selected = BuildExceptionDispatch(body, profile, {budget.bytes});
    const auto* dispatch = std::get_if<ExceptionDispatch>(&selected);
    if (!dispatch) {
      const auto& error = std::get<DispatchFailure>(selected);
      // Legacy dispatch failures do not distinguish a missing offset from PC0
      // for malformed bodies. Only boundary failures guarantee a real site.
      const auto pc = error.kind == DispatchError::kInvalidBoundary
          ? std::optional{error.offset} : std::nullopt;
      return fail(error.kind == DispatchError::kResourceLimit
          ? AwaitCatchError::kResourceLimit : AwaitCatchError::kRegion, pc);
    }
    const auto retain = [&](std::size_t bytes) {
      if (bytes > budget.bytes) throw std::bad_alloc{};
      budget.bytes -= bytes;
    };
    retain(dispatch->spans.capacity() * sizeof(DispatchSpan));
    std::size_t probe_count = 0;
    for (const auto& certificate : regions) {
      auto count = kResumeRegisters;
      if constexpr (std::is_same_v<Certificate, YieldSite>) {
        if (Region(certificate).private_registers) count = 2 * std::size_t(Region(certificate).private_registers->count);
      }
      if (count > budget.bytes / sizeof(CatchProbe) - probe_count) throw std::bad_alloc{};
      probe_count += count;
    }
    std::vector<CatchProbe> probes; probes.reserve(probe_count);
    retain(probes.capacity() * sizeof(CatchProbe));
    const auto target_at = [&](abc::CodeOffset pc) {
      Charge(work, 1 + std::bit_width(dispatch->spans.size()));
      return dispatch->Find(pc);
    };
    const auto local = [&](abc::CodeOffset pc) {
      const auto target = target_at(pc);
      return target && target->handler_pc != completion_handler;
    };
    for (const auto& certificate : regions) {
      const auto& region = Region(certificate);
      charge();
      const auto target = target_at(Start(region));
      if (target != target_at(region.reject) ||
          region.result.value >= frame_size || region.resume_mode.value >= frame_size)
        return fail(AwaitCatchError::kRegion, Start(region));
      if constexpr (std::is_same_v<Certificate, YieldSite>) {
        if (region.awaited_rejections &&
            (target != target_at(region.awaited_rejections->value) || target != target_at(region.awaited_rejections->returned)))
          return fail(AwaitCatchError::kRegion, Start(region));
        if (region.private_registers) {
          const auto& window = *region.private_registers;
          if (window.first.value >= frame_size || window.count > frame_size - window.first.value)
            return fail(AwaitCatchError::kRegion, Start(region));
          for (std::uint32_t i = 0; i < window.count; ++i) {
            charge(); const abc::RegisterId slot{window.first.value + i};
            // Async NEXT commits result/mode in retained operations. The
            // other three recycled slots have no source definitions. THROW
            // and return rejection bypass all five VM writes at user catch.
            if (region.execution != YieldExecution::kAsynchronous || (slot != region.result && slot != region.resume_mode))
              probes.push_back({region.continuation, slot, Start(region)});
            if (target && target->handler_pc != completion_handler) probes.push_back({target->handler_pc, slot, Start(region)});
          }
          continue;
        }
      }
      if (!target || target->handler_pc == completion_handler) continue;
      probes.push_back({target->handler_pc, region.result, Start(region)});
      probes.push_back({target->handler_pc, region.resume_mode, Start(region)});
    }
    if (probes.empty()) return AwaitCatchProof{budget.work - work};
    std::sort(probes.begin(), probes.end(), [&](const auto& a, const auto& b) {
      charge();
      return a.handler > b.handler;
    });
    if (probes.size() > budget.bytes / sizeof(abc::RegisterId)) throw std::bad_alloc{};
    std::vector<abc::RegisterId> slots;
    slots.reserve(probes.size());
    retain(slots.capacity() * sizeof(abc::RegisterId));
    for (const auto& probe : probes) { charge(); slots.push_back(probe.slot); }
    std::sort(slots.begin(), slots.end(), [&](auto a, auto b) { charge(); return a < b; });
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    if (slots.size() > budget.bytes / kTreeNodesPerSlot / sizeof(FirstUse))
      throw std::bad_alloc{};
    std::vector<FirstUse> nodes(slots.size() * kTreeNodesPerSlot, FirstUse::kUnknown);
    retain(nodes.capacity() * sizeof(FirstUse));
    FirstUseIndex uses{nodes, slots.size(), work};
    if (slots.size() > budget.bytes / kTreeNodesPerSlot / sizeof(FirstUse))
      throw std::bad_alloc{};
    std::vector<FirstUse> suffix_nodes(slots.size() * kTreeNodesPerSlot, FirstUse::kExit);
    retain(suffix_nodes.capacity() * sizeof(FirstUse));
    FirstUseIndex suffix_reads{suffix_nodes, slots.size(), work};
    // FindAwaitRegions forbids interior entries but deliberately allows entry
    // at the retained continuation load. Such an entry may observe the old
    // result; only protocol-owned continuation edges make that read local.
    // Creation order differs from continuation order for a yield inside
    // another yield's RETURN cleanup. Index continuation PCs independently;
    // lower_bound on creation-ordered certificates would miss external entries
    // and could incorrectly treat an incoming private result as overwritten.
    struct Continuation { abc::CodeOffset pc; std::size_t region; };
    constexpr auto kContinuationStorage = sizeof(Continuation) + sizeof(std::uint8_t);
    if (regions.size() > budget.bytes / kContinuationStorage) throw std::bad_alloc{};
    std::vector<std::uint8_t> continuation_entries(regions.size());
    std::vector<Continuation> continuations;
    continuations.reserve(regions.size());
    retain(continuation_entries.capacity());
    retain(continuations.capacity() * sizeof(Continuation));
    for (std::size_t i = 0; i < regions.size(); ++i) {
      charge(); continuations.push_back({Region(regions[i]).continuation, i});
    }
    std::sort(continuations.begin(), continuations.end(), [&](const auto& a, const auto& b) {
      charge(); return a.pc < b.pc;
    });
    const auto entry = [&](abc::CodeOffset pc, std::optional<abc::CodeOffset> source) {
      const auto found = std::lower_bound(continuations.begin(), continuations.end(), pc,
          [&](const Continuation& continuation, abc::CodeOffset target) {
            charge(); return continuation.pc < target;
          });
      if (found != continuations.end() && found->pc == pc && source != Region(regions[found->region]).branch)
        continuation_entries[found->region] = 1;
    };
    for (const auto& site : body.instructions) {
      charge();
      if (site.branch_target) entry(*site.branch_target, site.offset);
    }
    for (const auto& handler : body.handlers) { charge(); entry(handler.offset, std::nullopt); }
    bool suffix_known = true;
    std::optional<abc::CodeOffset> suffix_target;
    const auto position = [&](std::uint64_t reg) {
      return static_cast<std::size_t>(std::lower_bound(slots.begin(), slots.end(), reg,
          [&](abc::RegisterId slot, std::uint64_t value) {
            charge(); return slot.value < value;
          }) - slots.begin());
    };
    const auto mark = [&](const ScalarSlot& slot, FirstUse use) {
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        uses.Assign(position(reg->value), position(std::uint64_t(reg->value) + 1), use);
    };
    std::size_t next_probe = 0, suffix_region = regions.size(), suffix_await = awaits.size();
    for (auto site = body.instructions.rbegin(); site != body.instructions.rend(); ++site) {
      charge();
      while (suffix_region && site->offset < Start(Region(regions[suffix_region - 1]))) {
        charge(); --suffix_region;
      }
      const auto* region = suffix_region ? &Region(regions[suffix_region - 1]) : nullptr;
      while (suffix_await && site->offset < awaits[suffix_await - 1].await) { charge(); --suffix_await; }
      const auto* awaited = suffix_await ? &awaits[suffix_await - 1] : nullptr;
      const auto lifted = LiftSemanticInstruction(*site, profile, frame_size);
      const auto* instruction = std::get_if<SemanticInstruction>(&lifted);
      // A closed suffix with no possible reads proves a register dead even
      // through branches/loops. Keep the straight-prefix overwrite proof too;
      // neither proof treats a branch or an unknown register effect as an exit.
      // Reads are accumulated, including unreachable sites; writes never erase
      // them. Certified protocol-local reads refer to new definitions, not the
      // incoming value. Every encoded/exception target must stay in the suffix.
      const auto target = [&](abc::CodeOffset pc) {
        if (!suffix_target || pc < *suffix_target) suffix_target = pc;
      };
      bool folded_protocol = awaited && site->offset <= awaited->continuation;
      const bool native_exit = implicit_exit && site->offset >= *implicit_exit;
      if constexpr (std::is_same_v<Certificate, YieldSite>)
        folded_protocol |= region && (region->form == YieldForm::kDelegate || region->execution == YieldExecution::kAsynchronous) &&
            site->offset < region->continuation;
      // Exact YieldStar/Iterator.Close, async yield and shared awaits prove
      // private reads reached from their own definitions. Their internal
      // control cannot enter an earlier suspension's continuation.
      // External entries and user handler boundaries are checked by the owner.
      // Only the retained native await/yield anchor's exception edge survives.
      if (!folded_protocol && !native_exit) {
        if (site->branch_target) target(*site->branch_target);
        if (const auto dispatch_target = target_at(site->offset)) target(dispatch_target->handler_pc);
      } else if (folded_protocol && ((region && site->offset == Start(*region)) || (awaited && site->offset == awaited->await))) {
        if (const auto dispatch_target = target_at(site->offset)) target(dispatch_target->handler_pc);
      }
      // Raw operand effects can omit hidden frame windows (arguments). Only
      // full semantic lifting may declare the suffix's read set complete.
      if (folded_protocol || native_exit) {
        // Native await/yield observes the incoming accumulator and private state,
        // neither of which is a probed local. Do not count recycled registers
        // in later exact delegates as reads of an earlier delegate's values.
      } else if (instruction) {
        const auto& effect = instruction->effects.normal_registers;
        for (std::size_t i = 0; i < effect.read_count; ++i) {
          if (const auto* reg = std::get_if<abc::RegisterId>(&effect.reads[i])) {
            // A certified single-entry resume protocol defines result/mode before
            // reading them. This is a local def-use fact, not a kill of reads
            // elsewhere in the suffix. External continuation entries disable it.
            const bool local_read = region && site->offset <= region->continuation &&
                (site->offset != region->continuation || !continuation_entries[suffix_region - 1]) &&
                ((*reg == region->result && site->offset > region->result_store) ||
                 (*reg == region->resume_mode && site->offset > region->mode_store));
            if (local_read) continue;
            suffix_reads.Assign(position(reg->value), position(std::uint64_t(reg->value) + 1), FirstUse::kRead);
          }
        }
        if (effect.range)
          suffix_reads.Assign(position(effect.range->first.value),
              position(std::uint64_t(effect.range->first.value) + effect.range->count), FirstUse::kRead);
      } else {
        // Only the exact resume protocol certified on this unchanged body has
        // complete coroutine reads here. Suspend preserves frame values but
        // does not observe arbitrary locals; subsequent reads still accumulate.
        // Never use generic raw effects: arguments can hide frame-wide reads.
        const bool protocol = region && (site->offset == Start(*region) ||
            site->offset == region->suspend || site->offset == region->resume ||
            site->offset == region->mode);
        const auto coroutine = LiftCoroutineInstruction(site->decoded, frame_size);
        const auto read = [&](const ScalarSlot& slot) {
          if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
            suffix_reads.Assign(position(reg->value), position(std::uint64_t(reg->value) + 1), FirstUse::kRead);
        };
        if (const auto* transfer = std::get_if<CoroutineTransfer>(&coroutine); protocol && transfer) {
          read(transfer->state); read(transfer->value);
        } else if (const auto* value = std::get_if<CoroutineRead>(&coroutine); protocol && value) {
          read(value->state);
        } else if constexpr (std::is_same_v<Certificate, YieldSite>) {
          // The owner has checked both explicit iterator-result operands.
          // This local certificate does not authorize generic raw effects.
          if (region && site->offset == region->create) {
            for (std::size_t operand = 0; operand < site->decoded.operand_count; ++operand)
              read(abc::RegisterId{static_cast<std::uint32_t>(site->decoded.operands[operand].raw_bits)});
          } else suffix_known = false;
        } else suffix_known = false;
      }
      if (native_exit) {
        // Exact implicit undefined completion and outer rejection wrapper:
        // neither observes an incoming local. VM value/flag/exception loads
        // only observe this tail's own definitions; state is owner-private.
        uses.Reset(FirstUse::kExit);
      } else if (!instruction) {
        const auto coroutine = LiftCoroutineInstruction(site->decoded, frame_size);
        const auto* transfer = std::get_if<CoroutineTransfer>(&coroutine);
        const auto next = site.base();
        // The owner proved this final settlement/return tail and private state.
        // It exits without observing other registers. Do not treat arbitrary
        // coroutine operations or settlements under a user catch as exits.
        const bool final_resolve = transfer &&
            transfer->kind == CoroutineTransferKind::kResolve &&
            next != body.instructions.end() &&
            next->decoded.opcode == abc::Opcode::kRETURN &&
            next->decoded.operand_count == 0 &&
            std::uint64_t(site->offset.value) + site->decoded.size == next->offset.value &&
            std::uint64_t(next->offset.value) + next->decoded.size ==
                completion_handler.value;
        if (final_resolve && !local(site->offset)) {
          uses.Reset(FirstUse::kExit);
          mark(transfer->state, FirstUse::kRead);
          mark(transfer->value, FirstUse::kRead);
        } else {
          uses.Reset(FirstUse::kUnknown);
        }
      } else {
        const bool throws_locally = instruction->effects.exceptional !=
            ExceptionalCompletion::kImpossible && local(site->offset);
        const bool branch = std::holds_alternative<BranchOperation>(instruction->operation) ||
                            std::holds_alternative<JumpOperation>(instruction->operation);
        const bool exits = std::holds_alternative<ReturnOperation>(instruction->operation) ||
            (std::holds_alternative<ThrowOperation>(instruction->operation) && !throws_locally);
        if (throws_locally || branch) uses.Reset(FirstUse::kUnknown);
        else if (exits) uses.Reset(FirstUse::kExit);
        const auto& registers = instruction->effects.normal_registers;
        // A normal-only write cannot kill the incoming value on a local
        // exception edge. Reads precede writes and therefore win below.
        if (registers.write && !throws_locally && !branch)
          mark(*registers.write, FirstUse::kWrite);
        for (std::size_t i = 0; i < registers.read_count; ++i)
          mark(registers.reads[i], FirstUse::kRead);
        if (registers.range)
          uses.Assign(position(registers.range->first.value),
              position(std::uint64_t(registers.range->first.value) + registers.range->count),
              FirstUse::kRead);
      }
      if (next_probe < probes.size() && probes[next_probe].handler > site->offset)
        return fail(AwaitCatchError::kRegion, probes[next_probe].suspension);
      while (next_probe < probes.size() && probes[next_probe].handler == site->offset) {
        const auto& probe = probes[next_probe++];
        const auto use = uses.At(position(probe.slot.value));
        const bool unread_suffix = suffix_known && (!suffix_target || *suffix_target >= probe.handler) &&
            suffix_reads.At(position(probe.slot.value)) == FirstUse::kExit;
        if (use != FirstUse::kWrite && use != FirstUse::kExit && !unread_suffix) {
          // The raw proof is validated by the public wrapper before this scan.
          // Its queries are in original await order, with two storage roles.
          if (use != FirstUse::kRead && !flow.empty()) {
            auto at = std::lower_bound(flow.begin(), flow.end(), probe.suspension,
                [&](const CatchFlowEvidence& evidence, abc::CodeOffset pc) {
                  charge(); return evidence.query.await < pc;
                });
            bool covered = false;
            for (; at != flow.end() && at->query.await == probe.suspension; ++at) {
              charge();
              if (at->query.handler == probe.handler && at->query.slot == probe.slot)
                switch (at->outcome) {
                  case CatchFlowOutcome::kUnobserved:
                  case CatchFlowOutcome::kCovered:
                  case CatchFlowOutcome::kTerminal:
                  case CatchFlowOutcome::kCoveredAndTerminal: covered = true; break;
                  case CatchFlowOutcome::kObservedRead:
                  case CatchFlowOutcome::kUnknownEffect: covered = false; break;
                }
            }
            if (covered) continue;
          }
          auto failure = fail(AwaitCatchError::kObservedRegister, probe.suspension);
          failure.observation = AwaitCatchObservation{probe.handler, probe.slot,
              use == FirstUse::kRead ? CatchInputPrefix::kRead : CatchInputPrefix::kUnprovedPaths,
              suffix_known, !suffix_target || *suffix_target >= probe.handler};
          return failure;
        }
      }
      if (next_probe == probes.size()) return AwaitCatchProof{budget.work - work};
    }
    return fail(AwaitCatchError::kRegion);
  } catch (const WorkLimit&) { return fail(AwaitCatchError::kResourceLimit);
  } catch (const std::bad_alloc&) { return fail(AwaitCatchError::kResourceLimit);
  } catch (const std::length_error&) { return fail(AwaitCatchError::kResourceLimit); }
}
}  // namespace

std::variant<AwaitCatchProof, AwaitCatchFailure> ProveAwaitCatchInputs(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame_size,
    abc::CodeOffset completion_handler, SemanticBudget budget, std::optional<abc::CodeOffset> implicit_exit) {
  return ProveCatchInputs(body, regions, profile, frame_size, completion_handler, budget, implicit_exit);
}
std::variant<AwaitCatchProof, AwaitCatchFailure> ProveAwaitCatchInputsWithFlow(
    const abc::MethodBody& body, std::span<const AwaitRegion> regions,
    abc::ProfileId profile, std::uint64_t frame_size, abc::CodeOffset completion_handler,
    const AwaitCatchFlowProof& proof, SemanticBudget budget) {
  const auto verified = VerifyAwaitCatchFlow(body, regions, profile, frame_size, completion_handler, proof, budget);
  if (const auto* error = std::get_if<CatchFlowFailure>(&verified))
    return AwaitCatchFailure{error->kind == CatchFlowError::kResourceLimit || error->kind == CatchFlowError::kWorkLimit
        ? AwaitCatchError::kResourceLimit : AwaitCatchError::kRegion, error->pc};
  const auto cost = std::get<CatchFlowCost>(verified);
  if (cost.work > budget.work) return AwaitCatchFailure{AwaitCatchError::kResourceLimit, {}};
  budget.work -= cost.work;
  auto result = ProveCatchInputs(body, regions, profile, frame_size, completion_handler,
      budget, std::nullopt, {}, proof.queries);
  if (auto* accepted = std::get_if<AwaitCatchProof>(&result)) accepted->work_units += cost.work;
  return result;
}
std::variant<AwaitCatchProof, AwaitCatchFailure> ProveYieldCatchInputs(
    const abc::MethodBody& body, std::span<const YieldSite> regions,
    abc::ProfileId profile, std::uint64_t frame_size,
    abc::CodeOffset completion_handler, SemanticBudget budget, std::optional<abc::CodeOffset> implicit_exit,
    std::span<const AwaitRegion> awaits) {
  return ProveCatchInputs(body, regions, profile, frame_size, completion_handler, budget, implicit_exit, awaits);
}
}  // namespace abcd::analysis
