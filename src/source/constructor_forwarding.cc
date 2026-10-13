#include "constructor_forwarding.h"
#include "derived_value_states.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = DerivedProjectionError;
void Require(bool ok, Error error) { if (!ok) throw error; }
class Quota {
 public:
  explicit Quota(SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::size_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width = 1) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template<class T> void Array(std::vector<T>& out, std::size_t count) {
    Admit(count, sizeof(T)); out.resize(count);
    Admit(out.capacity() - count, sizeof(T)); Step(count);
  }
  template<class T> void Reserve(std::vector<T>& out, std::size_t count) {
    Admit(count, sizeof(T)); out.reserve(count); Admit(out.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return remaining_; }
  std::size_t Bytes() const { return initial_.bytes - remaining_.bytes; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  SemanticBudget initial_, remaining_;
};
enum class EntryKind : std::uint8_t { kBottom, kRegister, kOther };
struct FrameOrigin {
  EntryKind kind = EntryKind::kBottom;
  abc::RegisterId slot{};
  bool parameter = false;
  bool new_target = false;
  bool operator==(const FrameOrigin&) const = default;
};
FrameOrigin Join(FrameOrigin a, FrameOrigin b) {
  if (a.kind == EntryKind::kBottom) return b;
  if (b.kind == EntryKind::kBottom) return a;
  const bool same = a.kind == b.kind && a.slot == b.slot;
  return {same ? a.kind : EntryKind::kOther, same ? a.slot : abc::RegisterId{},
      a.parameter || b.parameter, a.new_target || b.new_target};
}
bool Copy(const SemanticInstruction& instruction) {
  const auto* assignment = std::get_if<ScalarAssignment>(&instruction.operation);
  const auto* value = assignment ? std::get_if<ScalarValue>(&assignment->expression) : nullptr;
  return value && (std::holds_alternative<Accumulator>(*value) || std::holds_alternative<abc::RegisterId>(*value)) &&
      instruction.effects.normal == LanguageEffect::kNone &&
      instruction.effects.before_throw == LanguageEffect::kNone &&
      instruction.effects.exceptional == ExceptionalCompletion::kImpossible;
}
// Flat entry-identity lattice over existing register SSA. Copy/phi propagation
// has two identity and two observation-bit transitions, including cycles;
// no per-slot CFG walks.
std::vector<FrameOrigin> FrameOrigins(const SemanticMethod& input, const abc::FrameLayout& frame, Quota& quota) {
  const auto values = input.ssa.Values();
  struct Edge { SsaValueId source, target; };
  std::vector<FrameOrigin> origins; quota.Array(origins, values.size());
  Require(input.ssa.AllInputs().size() <= SIZE_MAX - values.size(), Error::kResourceLimit);
  std::vector<Edge> edges; quota.Reserve(edges, values.size() + input.ssa.AllInputs().size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    quota.Step(); const auto& value = values[i]; const SsaValueId target{static_cast<std::uint32_t>(i)};
    if (std::holds_alternative<MethodEntry>(value.origin)) {
      const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      origins[i] = reg ? FrameOrigin{EntryKind::kRegister, *reg,
          reg->value >= frame.first_parameter.value && reg->value - frame.first_parameter.value < frame.parameters,
          frame.new_target == *reg} : FrameOrigin{EntryKind::kOther, {}};
    } else if (const auto* definition = std::get_if<InstructionValue>(&value.origin)) {
      Require(definition->instruction.value < input.instructions.size(), Error::kOrigin);
      if (Copy(input.instructions[definition->instruction.value])) {
        quota.Step(std::bit_width(input.ssa.AllReads().size()) + 1);
        const auto reads = input.ssa.Reads(definition->instruction);
        Require(reads.size() == 1, Error::kOrigin);
        edges.push_back({reads.front().value, target});
      } else origins[i] = {EntryKind::kOther, {}};
    }
  }
  for (const auto& incoming : input.ssa.AllInputs()) {
    quota.Step(); edges.push_back({incoming.value, incoming.phi});
  }
  std::vector<std::size_t> offsets; quota.Array(offsets, values.size() + 1);
  for (const auto& edge : edges) {
    quota.Step(); Require(edge.source.value < values.size() && edge.target.value < values.size(), Error::kOrigin);
    ++offsets[edge.source.value + 1];
  }
  for (std::size_t i = 1; i < offsets.size(); ++i) { quota.Step(); offsets[i] += offsets[i - 1]; }
  std::vector<std::size_t> cursor; quota.Array(cursor, values.size());
  for (std::size_t i = 0; i < cursor.size(); ++i) { quota.Step(); cursor[i] = offsets[i]; }
  std::vector<SsaValueId> targets; quota.Array(targets, edges.size());
  for (const auto& edge : edges) { quota.Step(); targets[cursor[edge.source.value]++] = edge.target; }
  constexpr std::size_t kTransitions = 4; // Two identity and two observation-bit transitions.
  Require(values.size() <= SIZE_MAX / kTransitions, Error::kResourceLimit);
  std::vector<SsaValueId> pending; quota.Reserve(pending, values.size() * kTransitions);
  for (std::size_t i = 0; i < values.size(); ++i)
    if (origins[i].kind != EntryKind::kBottom) pending.push_back({static_cast<std::uint32_t>(i)});
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto source = pending[head]; quota.Step();
    for (auto edge = offsets[source.value]; edge < offsets[source.value + 1]; ++edge) {
      quota.Step(); const auto target = targets[edge];
      const auto joined = Join(origins[target.value], origins[source.value]);
      if (joined == origins[target.value]) continue;
      origins[target.value] = joined;
      Require(pending.size() < pending.capacity(), Error::kResourceLimit); pending.push_back(target);
    }
  }
  return origins;
}
enum class ForwardState : std::uint8_t { kUnreachable, kBefore, kAfter, kMixed };
ForwardState Join(ForwardState a, ForwardState b) {
  if (a == ForwardState::kUnreachable) return b;
  if (b == ForwardState::kUnreachable || a == b) return a;
  return ForwardState::kMixed;
}
}

std::variant<ConstructorForwardingProof, DerivedProjectionError> ProveConstructorForwarding(
    const FileFunction& function, const FunctionContext& owner, SemanticBudget budget,
    std::span<const CapturePreservation> preserved) {
  try {
    Quota quota(budget); quota.Admit(sizeof(ConstructorForwardingProof));
    const auto& graph = function.values.graph;
    const auto& input = function.input.Input();
    const auto& frame = function.frame;
    Require(owner.Callable() == CallableKind::kDerivedConstructor && owner.ConstructorSite() &&
        function.input.MatchesOwner(owner) && owner.Frame() == frame &&
        function.input.Identity() == owner.Identity(), Error::kOwner);
    Require(ConstructorForwarding(owner.Identity().contract) == ConstructorForwardingSemantics::kSourceRestSpread,
        Error::kForwardingContract);
    Require(function.body.tries.empty() && function.body.handlers.empty() && graph.phases.empty(), Error::kForwardingHandlers);
    Require(owner.Length() == 0, Error::kForwardingParameters);
    Require(graph.arguments && graph.arguments->frame == frame && frame.function && frame.new_target && frame.this_value &&
        std::holds_alternative<ArgumentWindow>(ArgumentWindow::Build(frame)), Error::kForwardingFrame);
    Require(input.instructions.size() == function.body.instructions.size() &&
        graph.input.sites.size() == input.instructions.size(), Error::kOrigin);
    ConstructorForwardingProof proof{owner.Identity(), owner.Target(), frame, graph.revision, graph.cfg_revision};
    std::size_t count = 0;
    for (const auto& instruction : input.instructions) {
      quota.Step(); count += std::holds_alternative<SuperForward>(instruction.operation);
      Require(!std::holds_alternative<ArgumentSnapshot>(instruction.operation), Error::kForwardingArguments);
      Require(!std::holds_alternative<RestCopy>(instruction.operation), Error::kForwardingParameters);
      Require(!std::holds_alternative<SuperRange>(instruction.operation) &&
          !std::holds_alternative<SuperSpread>(instruction.operation), Error::kProtocol);
    }
    Require(count != 0, Error::kForwardingMissing); quota.Reserve(proof.calls, count);
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      quota.Step(); if (!std::holds_alternative<SuperForward>(input.instructions[i].operation)) continue;
      const auto id = graph.input.sites[i]; Require(id && id->value < graph.ops.size(), Error::kOrigin);
      const auto& op = graph.ops[id->value];
      const auto* call = std::get_if<ValueSuperCall>(&op.operation);
      Require(op.life == ValueLife::kLive && call && call->source == SuperArgumentSource::kInvocationFrame &&
          !call->arguments && !call->cache_slot && op.result && op.use_count == 1 &&
          op.first_use.value < graph.uses.size() && op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto& origin = graph.origins[op.origin.value]; const auto& original = input.instructions[i];
      Require(origin.site.value == i && origin.pc == original.pc && origin.original == original.origin &&
          origin.disposition == OriginDisposition::kRetained && op.normal == original.effects.normal &&
          op.before_throw == original.effects.before_throw && op.exceptional == original.effects.exceptional &&
          op.exception_cause == original.effects.exception_cause, Error::kOrigin);
      proof.calls.push_back(*id);
    }
    std::size_t live_calls = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      quota.Step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueSuperCall>(op.operation)) continue;
      Require(op.origin.value < graph.origins.size(), Error::kOrigin);
      const auto site = graph.origins[op.origin.value].site.value;
      Require(site < input.instructions.size() && std::holds_alternative<SuperForward>(input.instructions[site].operation) &&
          graph.input.sites[site] == OpId{static_cast<std::uint32_t>(i)}, Error::kOrigin);
      ++live_calls;
    }
    Require(live_calls == proof.calls.size(), Error::kCoverage);
    auto built = BuildDerivedValueStates(function, proof.calls.front(), preserved, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&built)) throw *error;
    const auto& states = std::get<DerivedValueStates>(built); quota.Admit(states.allocated_bytes); quota.Step(states.work);
    const auto state = [&](const ValueOperand& operand) {
      if (const auto* id = std::get_if<ValueId>(&operand)) {
        Require(id->value < states.values.size(), Error::kOperand); return states.values[id->value];
      }
      const auto* value = std::get_if<SpecialValue>(&operand);
      return value && *value == SpecialValue::kUndefined ? DerivedValueState::kUndefined : DerivedValueState::kOther;
    };
    for (const auto id : proof.calls) {
      quota.Step(); const auto& use = graph.uses[graph.ops[id.value].first_use.value];
      Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){id} &&
          use.role == OperandRole::kConstructor && use.purpose == ReadPurpose::kProtocol &&
          state(use.operand) == DerivedValueState::kConstructor, Error::kOperand);
    }
    const auto entries = FrameOrigins(input, frame, quota);
    const auto parameter = [&](abc::RegisterId slot) {
      return slot.value >= frame.first_parameter.value && slot.value - frame.first_parameter.value < frame.parameters;
    };
    // Only copies may transport frame entries without observing them. A later
    // non-copy use, including a cell store/capture, is checked at its own read.
    for (const auto& read : input.ssa.AllReads()) {
      quota.Step(); Require(read.value.value < entries.size() && read.instruction.value < input.instructions.size(), Error::kOrigin);
      const auto origin = entries[read.value.value];
      if (Copy(input.instructions[read.instruction.value])) continue;
      Require(!origin.new_target, Error::kForwardingNewTarget);
      Require(!origin.parameter, Error::kForwardingParameters);
    }
    std::vector<bool> changed; quota.Array(changed, input.instructions.size());
    const auto registers = input.ssa.Values();
    for (std::size_t i = 0; i < registers.size(); ++i) {
      quota.Step(); const auto* definition = std::get_if<InstructionValue>(&registers[i].origin);
      const auto* slot = std::get_if<abc::RegisterId>(&registers[i].slot);
      if (!definition || !slot || (!parameter(*slot) && *slot != *frame.new_target)) continue;
      Require(definition->instruction.value < changed.size(), Error::kOrigin);
      changed[definition->instruction.value] = changed[definition->instruction.value] || (entries[i].kind != EntryKind::kRegister || entries[i].slot != *slot);
    }
    const auto blocks = input.flow.blocks.size(); Require(blocks != 0, Error::kOrigin);
    std::vector<ForwardState> incoming; quota.Array(incoming, blocks);
    std::vector<std::uint8_t> dirty, queued; quota.Array(dirty, blocks); quota.Array(queued, blocks);
    // Each input can grow from bottom to a concrete state to mixed, and the
    // frame-change bit grows once. Reserve a bounded append-only work queue.
    constexpr std::size_t kBlockTransitions = 3;
    Require(blocks <= SIZE_MAX / kBlockTransitions, Error::kResourceLimit);
    std::vector<std::size_t> pending; quota.Reserve(pending, blocks * kBlockTransitions);
    incoming[0] = ForwardState::kBefore; pending.push_back(0); queued[0] = 1;
    for (std::size_t head = 0; head < pending.size(); ++head) {
      const auto b = pending[head]; queued[b] = 0; quota.Step(); const auto& block = input.flow.blocks[b];
      auto paths = incoming[b]; bool modified = dirty[b];
      const auto end = std::size_t(block.first_instruction) + block.instruction_count;
      Require(end <= input.instructions.size(), Error::kOrigin);
      for (std::size_t i = block.first_instruction; i < end; ++i) {
        quota.Step(); const auto& original = input.instructions[i]; const auto mapped = graph.input.sites[i];
        Require(!mapped || mapped->value < graph.ops.size(), Error::kOrigin);
        const auto* op = mapped ? &graph.ops[mapped->value] : nullptr;
        if (std::holds_alternative<SuperForward>(original.operation)) {
          Require(paths == ForwardState::kBefore, Error::kForwardingRepeated);
          Require(!modified, Error::kForwardingFrameChanged); paths = ForwardState::kAfter;
        } else if (const auto* guard = std::get_if<SuperCheck>(&original.operation)) {
          Require(op && op->use_count == 1 && op->first_use.value < graph.uses.size(), Error::kOrigin);
          const auto* current = std::get_if<ValueSuperCheck>(&op->operation);
          Require(current && current->kind == guard->kind, Error::kOrigin);
          const auto actual = state(graph.uses[op->first_use.value].operand);
          Require(guard->kind == SuperCheckKind::kNoop ||
              (guard->kind == SuperCheckKind::kRequireInitialized && actual == DerivedValueState::kSuper) ||
              (guard->kind == SuperCheckKind::kRequireUninitialized && actual == DerivedValueState::kUndefined), Error::kOperand);
        }
        if (std::holds_alternative<ReturnOperation>(original.operation)) {
          Require(paths == ForwardState::kAfter, Error::kForwardingMissing);
          Require(op && std::holds_alternative<ValueReturn>(op->operation) && op->use_count == 1 &&
              op->first_use.value < graph.uses.size() && state(graph.uses[op->first_use.value].operand) == DerivedValueState::kSuper,
              Error::kForwardingReturn);
        }
        modified |= changed[i];
      }
      Require(block.successor_count || block.transfer == abc::ControlTransfer::kReturn ||
          block.transfer == abc::ControlTransfer::kThrow, Error::kForwardingReturn);
      for (std::size_t e = 0; e < block.successor_count; ++e) {
        quota.Step(); const auto edge = block.successors[e];
        Require(edge.target.value < blocks && (edge.kind == FlowEdgeKind::kTaken || edge.kind == FlowEdgeKind::kFallthrough), Error::kProtocol);
        const auto next = edge.target.value; const auto joined = Join(incoming[next], paths);
        if (joined == incoming[next] && (!modified || dirty[next])) continue;
        incoming[next] = joined; dirty[next] |= modified;
        if (!queued[next]) {
          Require(pending.size() < pending.capacity(), Error::kResourceLimit);
          pending.push_back(next); queued[next] = 1;
        }
      }
    }
    proof.allocated_bytes = quota.Bytes(); proof.work = quota.Work(); return proof;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::optional<DerivedProjectionError> VerifyConstructorForwarding(
    const FileFunction& function, const FunctionContext& owner, const ConstructorForwardingProof& proof,
    SemanticBudget budget, std::span<const CapturePreservation> preserved) {
  if (proof.revision != function.values.graph.revision || proof.cfg_revision != function.values.graph.cfg_revision)
    return Error::kRevision;
  const auto checked = ProveConstructorForwarding(function, owner, budget, preserved);
  if (const auto* error = std::get_if<Error>(&checked)) return *error;
  const auto& expected = std::get<ConstructorForwardingProof>(checked);
  if (proof.identity != expected.identity || proof.method != expected.method || proof.frame != expected.frame ||
      proof.calls != expected.calls) return Error::kCoverage;
  return {};
}
}
