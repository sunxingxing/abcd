#include "derived_projection.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>

#include "abcd/source/capture_preservation.h"
#include "abcd/source/method_context.h"
#include "constructor_forwarding.h"
#include "derived_value_states.h"
#include "implicit_constructor.h"
namespace abcd::source {
namespace {
using Error = DerivedProjectionError;
void Require(bool condition, Error error) { if (!condition) throw error; }
using State = DerivedValueState;
}
std::variant<DerivedProjection, Error> ProjectDerivedConstructor(
    const FileFunction& function, const FunctionContext& owner, analysis::SemanticBudget budget,
    const DerivedCaptureInput* captures) {
  using namespace analysis;
  const auto initial = budget;
  try {
    Require(owner.Callable() == CallableKind::kDerivedConstructor && owner.ConstructorSite() &&
        function.input.MatchesOwner(owner) && function.frame == owner.Frame(), Error::kOwner);
    CapturePreservations preserved;
    if (captures) {
      Require(captures->file.Size() == owner.Identity().file_size && !captures->file.CheckUnchanged() &&
          captures->header.file_size == owner.Identity().file_size && captures->header.profile == owner.Identity().profile && captures->header.version == owner.Identity().abc_version &&
          captures->lookup.Find(owner.Target()) == owner.Position() && owner.Position().value < captures->index.methods.size() &&
          captures->index.methods[owner.Position().value].offset == owner.Target() &&
          captures->index.methods[owner.Position().value].owner_offset == function.input.Record(), Error::kOwner);
      MethodConstants constants(captures->file, captures->header, captures->index, owner.Target(), {});
      auto checked = ProveCapturePreservation(function.body, constants, &captures->lookup, budget);
      if (const auto* error = std::get_if<SourceFailure>(&checked)) {
        Require(error->kind != SourceError::kResourceLimit, Error::kResourceLimit);
        throw Error::kProtocol;
      }
      preserved = std::get<CapturePreservations>(std::move(checked));
      Require(preserved.AllocatedBytes() <= budget.bytes && preserved.work_units <= budget.work, Error::kResourceLimit);
      budget.bytes -= preserved.AllocatedBytes(); budget.work -= preserved.work_units;
    }
    bool forwarding = false;
    for (const auto& instruction : function.input.Input().instructions)
      forwarding |= std::holds_alternative<SuperForward>(instruction.operation);
    std::optional<ImplicitConstructorProof> implicit;
    std::optional<ConstructorForwardingProof> explicit_forward;
    if (forwarding) {
      Require(owner.Length() == 0, Error::kForwardingParameters);
      auto elision = ProveImplicitConstructor(function, budget);
      if (const auto* proof = std::get_if<ImplicitConstructorProof>(&elision)) {
        implicit = *proof;
        Require(proof->allocated_bytes <= budget.bytes, Error::kResourceLimit);
        budget.bytes -= proof->allocated_bytes;
        Require(proof->work <= budget.work, Error::kWorkLimit); budget.work -= proof->work;
      } else {
        const auto error = std::get<Error>(elision);
        if (error != Error::kProtocol && error != Error::kOperand) throw error;
        // Elision failure grants no body projection. Reprove the full explicit
        // protocol and frame independently; resource/input failures never retry.
        auto checked = ProveConstructorForwarding(function, owner, budget, preserved.sites);
        if (const auto* failure = std::get_if<Error>(&checked)) throw *failure;
        explicit_forward = std::get<ConstructorForwardingProof>(std::move(checked));
        Require(explicit_forward->allocated_bytes <= budget.bytes, Error::kResourceLimit);
        budget.bytes -= explicit_forward->allocated_bytes;
        Require(explicit_forward->work <= budget.work, Error::kWorkLimit); budget.work -= explicit_forward->work;
      }
    }
    bool spread = false;
    for (const auto& instruction : function.input.Input().instructions)
      spread |= std::holds_alternative<SuperSpread>(instruction.operation);
    // Select from the actual protocol, never retry a failed range proof under
    // a more permissive form. Both share normal-flow initialization analysis.
    std::optional<RangeDerivedConstructor> range_proof;
    const auto proof = [&]() -> std::variant<abc::CodeOffset, DerivedConstructorError> {
      if (implicit || explicit_forward) {
        const auto& graph = function.values.graph;
        const auto call = implicit ? implicit->first_call : explicit_forward->calls.front();
        return graph.origins[graph.ops[call.value].origin.value].pc;
      }
      if (spread) {
        const auto checked = ProveSpreadDerivedConstructor(function.body, function.frame,
            {budget.bytes, budget.work}, function.input.Identity().profile, preserved.sites);
        if (const auto* error = std::get_if<DerivedConstructorError>(&checked)) return *error;
        return std::get<SpreadDerivedConstructor>(checked).super_pc;
      }
      auto checked = ProveRangeDerivedConstructor(
          function.body, function.frame, {budget.bytes, budget.work},
          function.input.Identity().profile, preserved.sites);
      if (const auto* error = std::get_if<DerivedConstructorError>(&checked)) return *error;
      range_proof.emplace(
          std::get<RangeDerivedConstructor>(std::move(checked)));
      return range_proof->sites.front().pc;
    }();
    if (const auto* error = std::get_if<DerivedConstructorError>(&proof))
      throw *error == DerivedConstructorError::kResourceLimit ? Error::kResourceLimit : Error::kProtocol;
    Require(budget.bytes >= sizeof(DerivedProjection), Error::kResourceLimit);
    budget.bytes -= sizeof(DerivedProjection);
    const auto& graph = function.values.graph;
    const auto& input = function.input.Input();
    Require(graph.input.sites.size() == input.instructions.size(), Error::kOrigin);
    const auto step = [&](std::uint64_t count = 1) {
      Require(count <= budget.work, Error::kWorkLimit);
      budget.work -= count;
    };
    DerivedProjection result{graph.revision, graph.cfg_revision, {}};
    result.form = implicit ? DerivedConstructorForm::kImplicit : explicit_forward ?
        DerivedConstructorForm::kInvocationRest : DerivedConstructorForm::kExplicit;
    if (explicit_forward) result.forwarding_calls = std::move(explicit_forward->calls);
    bool found = false;
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      step();
      const auto& original = input.instructions[i];
      if (original.pc != std::get<abc::CodeOffset>(proof)) continue;
      Require(graph.input.sites[i].has_value(), Error::kOrigin);
      result.call = *graph.input.sites[i]; found = true; break;
    }
    Require(found && result.call.value < graph.ops.size(), Error::kOrigin);
    if (range_proof) {
      Require(range_proof->allocated_bytes <= budget.bytes,
              Error::kResourceLimit);
      budget.bytes -= range_proof->allocated_bytes;
      Require(range_proof->work <= budget.work, Error::kWorkLimit);
      budget.work -= range_proof->work;
      Require(range_proof->sites.size() <= budget.bytes / sizeof(OpId),
              Error::kResourceLimit);
      budget.bytes -= range_proof->sites.size() * sizeof(OpId);
      result.calls.reserve(range_proof->sites.size());
      for (const auto& site : range_proof->sites) {
        step();
        Require(site.instruction.value < graph.input.sites.size() &&
                    graph.input.sites[site.instruction.value],
                Error::kOrigin);
        result.calls.push_back(*graph.input.sites[site.instruction.value]);
      }
      step(result.calls.size() * (1 + std::bit_width(result.calls.size())));
      std::sort(result.calls.begin(), result.calls.end());
      const bool native =
          std::any_of(range_proof->coverage.begin(),
                      range_proof->coverage.end(), [](const auto& check) {
                        return check.action != DerivedCheckAction::kNoop;
                      });
      if (native) {
        result.native.emplace();
        auto& projected = *result.native;
        const auto reserve = [&](auto& values, std::size_t count) {
          using T = typename std::decay_t<decltype(values)>::value_type;
          Require(count <= budget.bytes / sizeof(T), Error::kResourceLimit);
          budget.bytes -= count * sizeof(T);
          values.reserve(count);
          Require(values.capacity() - count <= budget.bytes / sizeof(T),
                  Error::kResourceLimit);
          budget.bytes -= (values.capacity() - count) * sizeof(T);
        };
        reserve(projected.calls, result.calls.size());
        projected.calls.assign(result.calls.begin(), result.calls.end());
        reserve(projected.returns, range_proof->returns.size());
        reserve(projected.reads, range_proof->coverage.size());
        reserve(projected.bindings, range_proof->bindings.size());
        for (const auto& returned : range_proof->returns) {
          step();
          Require(returned.instruction.value < graph.input.sites.size() &&
                      graph.input.sites[returned.instruction.value],
                  Error::kOrigin);
          projected.returns.push_back(
              *graph.input.sites[returned.instruction.value]);
        }
        for (const auto& covered : range_proof->coverage) {
          step();
          const auto guard = graph.input.sites[covered.instruction.value];
          Require(guard.has_value(), Error::kOrigin);
          if (covered.action == DerivedCheckAction::kRead)
            projected.reads.push_back(*guard);
        }
        for (const auto& binding : range_proof->bindings) {
          step();
          Require(graph.input.sites[binding.guard.value] &&
                      graph.input.sites[binding.construction.value],
                  Error::kOrigin);
          projected.bindings.push_back(
              {*graph.input.sites[binding.guard.value],
               *graph.input.sites[binding.construction.value]});
        }
        step(projected.bindings.size() *
             (1 + std::bit_width(projected.bindings.size())));
        std::sort(projected.bindings.begin(), projected.bindings.end(),
                  [](auto a, auto b) {
                    return a.guard != b.guard ? a.guard < b.guard
                                              : a.construction < b.construction;
                  });
      }
    }
    auto classified = range_proof
                          ? BuildDerivedValueStates(
                                function, std::span<const OpId>(result.calls),
                                preserved.sites, budget)
                          : BuildDerivedValueStates(function, result.call,
                                                    preserved.sites, budget);
    if (const auto* error = std::get_if<Error>(&classified)) throw *error;
    const auto& states = std::get<DerivedValueStates>(classified);
    Require(states.allocated_bytes <= budget.bytes && states.work <= budget.work, Error::kResourceLimit);
    budget.bytes -= states.allocated_bytes; budget.work -= states.work;
    const auto classify = [&](const ValueOperand& operand) {
      step();
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        Require(value->value < states.values.size() && graph.values[value->value].life == ValueLife::kLive, Error::kOperand);
        return states.values[value->value];
      }
      const auto* special = std::get_if<SpecialValue>(&operand);
      return special && *special == SpecialValue::kUndefined ? State::kUndefined : State::kOther;
    };
    std::size_t guard_count = 0;
    for (const auto& original : input.instructions) { step(); guard_count += std::holds_alternative<SuperCheck>(original.operation); }
    Require(guard_count <= budget.bytes / sizeof(OpId), Error::kResourceLimit);
    result.guards.reserve(guard_count);
    Require(result.guards.capacity() <= budget.bytes / sizeof(OpId), Error::kResourceLimit);
    budget.bytes -= result.guards.capacity() * sizeof(OpId);
    if (captures && states.captures_preserved && function.values.cells) {
      const auto& cells = *function.values.cells;
      Require(function.values.environments.has_value(), Error::kOrigin);
      const auto verified = VerifyCellPlan(graph, *function.values.environments, cells, budget);
      if (const auto* error = std::get_if<CellError>(&verified))
        throw *error == CellError::kResourceLimit ? Error::kResourceLimit :
            *error == CellError::kWorkLimit ? Error::kWorkLimit : Error::kOrigin;
      const auto work = std::get<std::uint64_t>(verified);
      Require(work <= budget.work, Error::kWorkLimit); budget.work -= work;
      Require(cells.cells.size() <= budget.bytes / sizeof(CellId), Error::kResourceLimit);
      result.receiver_cells.reserve(cells.cells.size());
      Require(result.receiver_cells.capacity() <= budget.bytes / sizeof(CellId), Error::kResourceLimit);
      budget.bytes -= result.receiver_cells.capacity() * sizeof(CellId);
      Require(cells.cells.size() <= budget.bytes / sizeof(ReceiverInitialization), Error::kResourceLimit);
      result.receiver_initializations.reserve(cells.cells.size());
      Require(result.receiver_initializations.capacity() <= budget.bytes / sizeof(ReceiverInitialization), Error::kResourceLimit);
      budget.bytes -= result.receiver_initializations.capacity() * sizeof(ReceiverInitialization);
      // One pass over indexed accesses, not one graph walk per captured cell.
      // Language initialization alone includes undefined. Only actual super
      // operands plus transitive preservation can strengthen that domain.
      const auto rank = [&](const ValueOp& op) {
        Require(op.origin.value < graph.origins.size(), Error::kOrigin);
        const auto site = graph.origins[op.origin.value].site.value;
        Require(site < states.site_order.size(), Error::kOrigin);
        return states.site_order[site];
      };
      for (std::size_t c = 0; c < cells.cells.size(); ++c) {
        step(); const auto& cell = cells.cells[c];
        if (!std::holds_alternative<AllocatedCell>(cell.storage)) continue;
        bool stored = false, receiver = true;
        std::optional<InstructionRank> last_other;
        for (const auto& access : std::span(cells.accesses).subspan(cell.first, cell.count)) {
          step(); if (access.kind != CellAccessKind::kStore) continue;
          const auto& op = graph.ops[access.operation.value];
          const auto position = rank(op);
          if (!position) continue;
          Require(op.use_count == 1 && op.first_use.value < graph.uses.size(), Error::kOperand);
          const auto& use = graph.uses[op.first_use.value];
          Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){access.operation} &&
              use.role == OperandRole::kStoredValue && use.purpose == ReadPurpose::kOperand, Error::kOperand);
          const bool actual_receiver = classify(use.operand) == State::kSuper;
          stored = true; receiver &= actual_receiver;
          if (!actual_receiver) {
            if (!last_other || *last_other < *position) last_other = *position;
          }
        }
        if (stored && receiver) result.receiver_cells.push_back({static_cast<std::uint32_t>(c)});
        else if (last_other) {
          // The shared DAG rank proves no non-receiver write can follow this
          // store, independently of physical PCs. It is a sufficient immutable
          // suffix proof; incomparable writes may need finer SSA snapshots.
          // Keep the first candidate: dominance at each real creation is a
          // separate obligation; a prior Language/undefined store cannot pass it.
          std::optional<InstructionId> initialized;
          std::optional<InstructionRank> first_receiver;
          for (const auto& access : std::span(cells.accesses).subspan(cell.first, cell.count)) {
            step(); if (access.kind != CellAccessKind::kStore) continue;
            const auto& op = graph.ops[access.operation.value];
            const auto position = rank(op);
            if (!position) continue;
            Require(op.origin.value < graph.origins.size(), Error::kOrigin);
            const InstructionId site{graph.origins[op.origin.value].site.value};
            if (*position > *last_other && (!first_receiver || *position < *first_receiver)) {
              initialized = site; first_receiver = *position;
            }
          }
          if (initialized) result.receiver_initializations.push_back({{static_cast<std::uint32_t>(c)}, *initialized});
        }
      }
    }
    std::vector<SsaValueId> original_roots;
    if (result.native) {
      const auto count = input.ssa.Values().size();
      Require(count <= budget.bytes /
                           (sizeof(SsaValueId) * 2 + sizeof(std::uint8_t)),
              Error::kResourceLimit);
      budget.bytes -= count * (sizeof(SsaValueId) * 2 + sizeof(std::uint8_t));
      original_roots.resize(count);
      std::vector<std::uint8_t> done(count);
      std::vector<SsaValueId> path;
      path.reserve(count);
      for (std::uint32_t v = 0; v < count; ++v) original_roots[v] = {v};
      for (std::uint32_t v = 0; v < count; ++v) {
        if (done[v]) continue;
        path.clear();
        auto at = SsaValueId{v};
        while (!done[at.value]) {
          step();
          done[at.value] = 1;
          path.push_back(at);
          const auto* origin = std::get_if<InstructionValue>(
              &input.ssa.Values()[at.value].origin);
          if (!origin) break;
          const auto& instruction =
              input.instructions[origin->instruction.value];
          const auto* assign =
              std::get_if<ScalarAssignment>(&instruction.operation);
          const auto* scalar =
              assign ? std::get_if<ScalarValue>(&assign->expression) : nullptr;
          if (!scalar ||
              (!std::holds_alternative<Accumulator>(*scalar) &&
               !std::holds_alternative<abc::RegisterId>(*scalar)) ||
              instruction.effects.normal != LanguageEffect::kNone ||
              instruction.effects.exceptional !=
                  ExceptionalCompletion::kImpossible)
            break;
          const auto reads = input.ssa.Reads(origin->instruction);
          Require(reads.size() == 1, Error::kOperand);
          at = reads[0].value;
        }
        const auto root = original_roots[at.value];
        while (!path.empty()) {
          const auto x = path.back();
          path.pop_back();
          original_roots[x.value] = root;
          done[x.value] = 2;
        }
      }
    }
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      step(); const auto& original = input.instructions[i];
      const auto* check = std::get_if<SuperCheck>(&original.operation);
      const auto* call = std::get_if<SuperRange>(&original.operation);
      const auto* spread_call = std::get_if<SuperSpread>(&original.operation);
      const auto* frame_call = std::get_if<SuperForward>(&original.operation);
      if (!check && !call && !spread_call && !frame_call) continue;
      Require(graph.input.sites[i].has_value(), Error::kOrigin);
      const auto id = *graph.input.sites[i]; Require(id.value < graph.ops.size(), Error::kOrigin);
      const auto& op = graph.ops[id.value];
      Require(op.life == ValueLife::kLive && op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto& origin = graph.origins[op.origin.value];
      Require(origin.site.value == i && origin.pc == original.pc && origin.original == original.origin &&
          origin.disposition == OriginDisposition::kRetained && op.normal == original.effects.normal &&
          op.before_throw == original.effects.before_throw && op.exceptional == original.effects.exceptional &&
          op.exception_cause == original.effects.exception_cause,
          Error::kOrigin);
      if (frame_call) {
        const auto* current = std::get_if<ValueSuperCall>(&op.operation);
        Require(result.form != DerivedConstructorForm::kExplicit && current && current->source == SuperArgumentSource::kInvocationFrame &&
            !current->arguments && !current->cache_slot && op.result && op.use_count == 1, Error::kOrigin);
        const auto& use = graph.uses[op.first_use.value];
        Require(use.role == OperandRole::kConstructor && use.purpose == ReadPurpose::kProtocol &&
            classify(use.operand) == State::kConstructor, Error::kOperand);
      } else if (call) {
        const auto* current = std::get_if<ValueSuperCall>(&op.operation);
        Require(call->callee == SuperCalleeSource::kInvocationFunction &&
                    (range_proof ? std::binary_search(result.calls.begin(),
                                                      result.calls.end(), id)
                                 : id == result.call) &&
                    current &&
                    current->callee == SuperCalleeSource::kInvocationFunction &&
                    current->source == SuperArgumentSource::kExplicit &&
                    current->arguments == call->count &&
                    current->cache_slot == call->cache_slot && op.result &&
                    op.use_count == call->count,
                Error::kOrigin);
      } else if (spread_call) {
        const auto* current = std::get_if<ValueSuperSpread>(&op.operation);
        Require(id == result.call && current && current->cache_slot == spread_call->cache_slot &&
            op.result && op.use_count == 2 && op.first_use.value <= graph.uses.size() &&
            graph.uses.size() - op.first_use.value >= 2, Error::kOrigin);
        const auto& constructor = graph.uses[op.first_use.value];
        const auto& array = graph.uses[op.first_use.value + 1];
        Require(constructor.life == ValueLife::kLive && array.life == ValueLife::kLive &&
            constructor.owner == decltype(constructor.owner){id} &&
            array.owner == decltype(array.owner){id} &&
            constructor.role == OperandRole::kConstructor && array.role == OperandRole::kInput &&
            constructor.purpose == ReadPurpose::kOperand && array.purpose == ReadPurpose::kOperand &&
            classify(constructor.operand) == State::kConstructor, Error::kOperand);
      } else {
        const auto* current = std::get_if<ValueSuperCheck>(&op.operation);
        Require(current && current->kind == check->kind && !op.result && op.use_count == 1 &&
            op.first_use.value < graph.uses.size(), Error::kOrigin);
        const auto& use = graph.uses[op.first_use.value];
        Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){id} &&
            use.role == OperandRole::kGuardValue && use.purpose == ReadPurpose::kProtocol, Error::kOperand);
        if (result.native) {
          // Original and working operands must still be the same SSA value.
          // The path proof alone is never authority for a mutated work graph.
          const auto reads =
              input.ssa.Reads(InstructionId{static_cast<std::uint32_t>(i)});
          Require(reads.size() == 1 &&
                      reads[0].value.value < graph.input.values.size(),
                  Error::kOperand);
          const auto expected =
              graph.input.values[original_roots[reads[0].value.value].value];
          const auto* actual = std::get_if<ValueId>(&use.operand);
          Require(actual && *actual == expected, Error::kOperand);
          if (std::binary_search(result.native->reads.begin(),
                                 result.native->reads.end(), id))
            continue;
          result.guards.push_back(id);
          continue;
        }
        const auto state = classify(use.operand);
        switch (check->kind) {
          case SuperCheckKind::kRequireInitialized: Require(state == State::kSuper, Error::kOperand); break;
          case SuperCheckKind::kRequireUninitialized: Require(state == State::kUndefined, Error::kOperand); break;
          case SuperCheckKind::kNoop: break;
        }
        result.guards.push_back(id);
      }
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::optional<DerivedProjectionError> VerifyDerivedProjection(
    const FileFunction& function, const FunctionContext& owner, const DerivedProjection& projection,
    analysis::SemanticBudget budget, const DerivedCaptureInput* captures) {
  if (projection.revision != function.values.graph.revision ||
      projection.cfg_revision != function.values.graph.cfg_revision) return Error::kRevision;
  const auto checked = ProjectDerivedConstructor(function, owner, budget, captures);
  if (const auto* error = std::get_if<Error>(&checked)) return *error;
  const auto& expected = std::get<DerivedProjection>(checked);
  if (expected.form != projection.form ||
      expected.forwarding_calls != projection.forwarding_calls ||
      expected.call != projection.call || expected.calls != projection.calls ||
      expected.native != projection.native ||
      expected.guards != projection.guards ||
      expected.receiver_cells != projection.receiver_cells ||
      expected.receiver_initializations != projection.receiver_initializations)
    return Error::kCoverage;
  return {};
}

}
