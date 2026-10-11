#include "derived_value_states.h"
#include "abcd/analysis/local_ssa.h"
#include "../analysis/value_flow.h"
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = DerivedProjectionError;
using State = DerivedValueState;
void Require(bool ok, Error error) { if (!ok) throw error; }
struct StateId { std::size_t value; };
struct StateEdge { StateId source, target; };
State Join(State left, State right) {
  if (left == State::kBottom) return right;
  if (right == State::kBottom || left == right) return left;
  return State::kOther;
}
State Literal(const ValueOperand& value) {
  const auto* special = std::get_if<SpecialValue>(&value);
  return special && *special == SpecialValue::kUndefined ? State::kUndefined : State::kOther;
}
}
std::variant<DerivedValueStates, Error> BuildDerivedValueStates(
    const FileFunction& function, OpId call, std::span<const CapturePreservation> preserved,
    SemanticBudget budget) {
  const auto initial = budget;
  try {
    const auto step = [&](std::size_t count = 1) {
      Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= count * width;
    };
    const auto size = [&]<class T>(std::vector<T>& values, std::size_t count) {
      admit(count, sizeof(T)); values.resize(count);
      admit(values.capacity() - count, sizeof(T)); step(count);
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
      admit(count, sizeof(T)); values.reserve(count);
      admit(values.capacity() - count, sizeof(T));
    };
    const auto& graph = function.values.graph;
    const auto& input = function.input.Input();
    Require(graph.values.size() <= UINT32_MAX, Error::kResourceLimit);
    Require(call.value < graph.ops.size(), Error::kOrigin);
    // Local SSA describes original stores, but supplied working operands may
    // have changed. Reject unavailable feedback before solving origin cycles.
    const auto available = VerifyValueFlow(graph, budget);
    if (const auto* error = std::get_if<ValueAvailabilityError>(&available)) {
      if (*error == ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kOperand;
    }
    step(std::get<ValueFlowProof>(available).work); // Availability scratch is released here.

    bool lexical = false;
    for (const auto& op : graph.ops) { step(); lexical |= op.life == ValueLife::kLive &&
        (std::holds_alternative<ValueLexicalLoad>(op.operation) ||
         std::holds_alternative<ValueLexicalStore>(op.operation)); }
    std::optional<LocalSsa> locals;
    if (lexical) {
      auto built = BuildLocalSsa(function.body, input.flow, input.ssa, function.frame.registers,
          function.input.Identity().profile, {budget.bytes}, budget.work, preserved);
      if (auto* local = std::get_if<LocalSsa>(&built)) {
        admit(local->AllocatedBytes(), 1); step(local->work_units);
        locals.emplace(std::move(*local));
      } else if (std::get<LocalSsaError>(built) == LocalSsaError::kResourceLimit)
        throw Error::kResourceLimit;
      // Unsupported local effects grant no origin. Register-only constructors
      // retain their proof; a lexical guard input will remain kOther.
    }
    const auto values = graph.values.size(), cells = locals ? locals->values.Values().size() : 0;
    Require(cells <= std::numeric_limits<std::size_t>::max() - values, Error::kResourceLimit);
    const auto count = values + cells;
    Require(count < std::numeric_limits<std::size_t>::max(), Error::kResourceLimit);
    const auto phi_inputs = locals ? locals->values.AllInputs().size() : 0;
    Require(phi_inputs <= std::numeric_limits<std::size_t>::max() - count, Error::kResourceLimit);
    std::vector<State> states; size(states, count);
    Require(graph.uses.size() <= std::numeric_limits<std::size_t>::max() - count - phi_inputs, Error::kResourceLimit);
    std::vector<StateEdge> edges; reserve(edges, count + phi_inputs + graph.uses.size());
    std::vector<std::optional<SsaValueId>> reads;
    if (locals) {
      size(reads, input.instructions.size());
      for (const auto& read : locals->values.AllReads()) {
        step(); Require(read.instruction.value < reads.size() && read.ordinal == 0 &&
            read.value.value < cells && !reads[read.instruction.value], Error::kOrigin);
        reads[read.instruction.value] = read.value;
      }
    }
    const auto operation = [&](InstructionId site) -> const ValueOp* {
      Require(site.value < input.instructions.size() && site.value < graph.input.sites.size(), Error::kOrigin);
      const auto id = graph.input.sites[site.value];
      if (!id) return nullptr;
      Require(id->value < graph.ops.size(), Error::kOrigin);
      const auto& op = graph.ops[id->value];
      if (op.life != ValueLife::kLive) return nullptr;
      Require(op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto& origin = graph.origins[op.origin.value];
      const auto& original = input.instructions[site.value];
      Require(origin.site.value == site.value && origin.pc == original.pc && origin.original == original.origin &&
          origin.disposition == OriginDisposition::kRetained && op.normal == original.effects.normal &&
          op.before_throw == original.effects.before_throw && op.exceptional == original.effects.exceptional &&
          op.exception_cause == original.effects.exception_cause,
          Error::kOrigin);
      return &op;
    };
    const auto edge = [&](ValueOperand operand, StateId target) {
      if (const auto* value = std::get_if<ValueId>(&operand)) {
        Require(value->value < values && graph.values[value->value].life == ValueLife::kLive, Error::kOperand);
        Require(edges.size() < edges.capacity(), Error::kResourceLimit);
        edges.push_back({StateId{value->value}, target});
      } else states[target.value] = Join(states[target.value], Literal(operand));
    };
    for (std::size_t i = 0; i < values; ++i) {
      step(); const auto& value = graph.values[i]; const StateId target{i};
      if (value.life != ValueLife::kLive) { states[i] = State::kOther; continue; }
      if (const auto* entry = std::get_if<EntryDefinition>(&value.origin)) {
        states[i] = std::holds_alternative<ReceiverEntry>(*entry) ? State::kUndefined :
            std::holds_alternative<CallableEntry>(*entry) ? State::kConstructor : State::kOther;
        continue;
      }
      if (std::holds_alternative<PhiDefinition>(value.origin)) {
        continue;
      }
      const auto* result = std::get_if<OpResult>(&value.origin);
      if (!result) { states[i] = State::kOther; continue; }
      Require(result->op.value < graph.ops.size(), Error::kOrigin);
      const auto& op = graph.ops[result->op.value];
      Require(op.life == ValueLife::kLive && op.result == ValueId{static_cast<std::uint32_t>(i)}, Error::kOrigin);
      const auto* super = std::get_if<ValueSuperCall>(&op.operation);
      if (result->op == call || (super && super->source == SuperArgumentSource::kInvocationFrame)) {
        states[i] = State::kSuper; continue;
      }
      if (std::holds_alternative<ValueCopy>(op.operation)) {
        Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
            op.exceptional == ExceptionalCompletion::kImpossible && op.use_count == 1 &&
            op.first_use.value < graph.uses.size(), Error::kOperand);
        const auto& use = graph.uses[op.first_use.value];
        Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){result->op} &&
            use.role == OperandRole::kInput && use.purpose == ReadPurpose::kOperand, Error::kOperand);
        edge(use.operand, target); continue;
      }
      const auto* load = std::get_if<ValueLexicalLoad>(&op.operation);
      if (load && locals) {
        Require(op.origin.value < graph.origins.size(), Error::kOrigin);
        const InstructionId site{graph.origins[op.origin.value].site.value};
        const auto* actual = operation(site);
        const auto* original = std::get_if<LexicalLoad>(&input.instructions[site.value].operation);
        Require(actual == &op && original && original->address.depth.value == load->address.depth.value &&
            original->address.slot.value == load->address.slot.value && op.use_count == 0, Error::kOperand);
        if (locals->PreservesRead(site) && reads[site.value]) {
          Require(edges.size() < edges.capacity(), Error::kResourceLimit);
          edges.push_back({StateId{values + reads[site.value]->value}, target}); continue;
        }
      }
      states[i] = State::kOther;
    }
    for (std::size_t i = 0; i < cells; ++i) {
      step(); const auto& value = locals->values.Values()[i]; const StateId target{values + i};
      if (std::holds_alternative<PhiValue>(value.origin)) continue;
      const auto* store_site = std::get_if<InstructionValue>(&value.origin);
      if (!store_site) { states[target.value] = State::kOther; continue; }
      const auto* op = operation(store_site->instruction);
      if (!op) { states[target.value] = State::kOther; continue; }
      const auto* store = std::get_if<ValueLexicalStore>(&op->operation);
      const auto* original = std::get_if<LexicalStore>(&input.instructions[store_site->instruction.value].operation);
      Require(store && original && store->address.depth.value == original->address.depth.value &&
          store->address.slot.value == original->address.slot.value && !op->result && op->use_count == 1 &&
          op->first_use.value < graph.uses.size(), Error::kOperand);
      const auto& use = graph.uses[op->first_use.value];
      Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){*graph.input.sites[store_site->instruction.value]} &&
          use.role == OperandRole::kStoredValue && use.purpose == ReadPurpose::kOperand, Error::kOperand);
      edge(use.operand, target);
    }
    for (const auto& use : graph.uses) {
      step(); if (use.life != ValueLife::kLive) continue;
      const auto* target = std::get_if<ValueId>(&use.owner);
      if (!target || !std::holds_alternative<PhiDefinition>(graph.values[target->value].origin)) continue;
      Require(use.role == OperandRole::kPhiInput, Error::kOperand);
      edge(use.operand, StateId{target->value});
    }
    if (locals) for (const auto& input_edge : locals->values.AllInputs()) {
      step(); Require(input_edge.phi.value < cells && input_edge.value.value < cells &&
          std::holds_alternative<PhiValue>(locals->values.Values()[input_edge.phi.value].origin), Error::kOrigin);
      Require(edges.size() < edges.capacity(), Error::kResourceLimit);
      edges.push_back({StateId{values + input_edge.value.value}, StateId{values + input_edge.phi.value}});
    }
    std::vector<std::size_t> begin; size(begin, count + 1);
    for (const auto& dependency : edges) { step(); ++begin[dependency.source.value + 1]; }
    for (std::size_t i = 1; i < begin.size(); ++i) { step(); begin[i] += begin[i - 1]; }
    std::vector<StateId> targets; size(targets, edges.size());
    std::vector<std::size_t> cursor; size(cursor, count);
    for (std::size_t i = 0; i < count; ++i) { step(); cursor[i] = begin[i]; }
    for (const auto& dependency : edges) { step(); targets[cursor[dependency.source.value]++] = dependency.target; }
    // The flat lattice admits at most two transitions per node. Cycles with
    // no concrete origin stay bottom and never certify an initialized receiver.
    constexpr auto kTransitions = std::size_t{2};
    Require(count <= std::numeric_limits<std::size_t>::max() / kTransitions, Error::kResourceLimit);
    std::vector<StateId> pending; reserve(pending, count * kTransitions);
    for (std::size_t i = 0; i < count; ++i) { step(); if (states[i] != State::kBottom) pending.push_back({i}); }
    for (std::size_t head = 0; head < pending.size(); ++head) {
      step(); const auto source = pending[head];
      for (auto i = begin[source.value]; i < begin[source.value + 1]; ++i) {
        step(); auto& target = states[targets[i].value]; const auto joined = Join(target, states[source.value]);
        if (joined == target) continue;
        target = joined; Require(pending.size() < pending.capacity(), Error::kResourceLimit); pending.push_back(targets[i]);
      }
    }
    states.resize(values);
    return DerivedValueStates{std::move(states), initial.bytes - budget.bytes, initial.work - budget.work,
        locals && !locals->unpreserved_capture, locals ? std::move(locals->site_order) :
            std::vector<std::optional<InstructionRank>>{}};
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
