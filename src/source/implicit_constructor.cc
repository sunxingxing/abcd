#include "implicit_constructor.h"
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = DerivedProjectionError;
void Require(bool ok, Error error = Error::kProtocol) { if (!ok) throw error; }
enum class Forwarding : std::uint8_t { kUnreachable, kBefore, kAfter, kMixed };
Forwarding Join(Forwarding left, Forwarding right) {
  if (left == Forwarding::kUnreachable) return right;
  if (right == Forwarding::kUnreachable || left == right) return left;
  return Forwarding::kMixed;
}
}
std::variant<ImplicitConstructorProof, Error> ProveImplicitConstructor(
    const FileFunction& function, SemanticBudget budget) {
  const auto initial = budget;
  try {
    const auto step = [&](std::size_t count = 1) {
      Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
    };
    const auto array = [&]<class T>(std::vector<T>& out, std::size_t count) {
      Require(count <= budget.bytes / sizeof(T), Error::kResourceLimit);
      out.resize(count); Require(out.capacity() <= budget.bytes / sizeof(T), Error::kResourceLimit);
      budget.bytes -= out.capacity() * sizeof(T); step(count);
    };
    const auto& graph = function.values.graph;
    const auto& input = function.input.Input();
    const auto& frame = function.frame;
    Require(graph.arguments && graph.arguments->frame == frame, Error::kOwner);
    Require(frame.function && frame.new_target && frame.this_value && frame.extra_arguments &&
        function.body.tries.empty() && function.body.handlers.empty() && graph.phases.empty());
    std::optional<OpId> first_call;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive) continue;
      const auto* call = std::get_if<ValueSuperCall>(&op.operation);
      if (call && call->source == SuperArgumentSource::kInvocationFrame) {
        if (!first_call) first_call = OpId{static_cast<std::uint32_t>(i)};
      } else if (!std::holds_alternative<ValueSuperCheck>(op.operation)) {
        Require(op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
            op.exceptional == ExceptionalCompletion::kImpossible);
      }
    }
    Require(first_call.has_value());
    auto classified = BuildDerivedValueStates(function, *first_call, {}, budget);
    if (const auto* error = std::get_if<Error>(&classified)) throw *error;
    const auto& states = std::get<DerivedValueStates>(classified);
    Require(states.allocated_bytes <= budget.bytes, Error::kResourceLimit);
    budget.bytes -= states.allocated_bytes; step(states.work);
    const auto state = [&](const ValueOperand& operand) {
      const auto* value = std::get_if<ValueId>(&operand);
      if (value) { Require(value->value < states.values.size(), Error::kOperand); return states.values[value->value]; }
      const auto* literal = std::get_if<SpecialValue>(&operand);
      return literal && *literal == SpecialValue::kUndefined ? DerivedValueState::kUndefined : DerivedValueState::kOther;
    };
    Require(input.instructions.size() == function.body.instructions.size() &&
        graph.input.sites.size() == input.instructions.size(), Error::kOrigin);
    std::vector<Forwarding> forwarded;
    std::vector<std::uint8_t> dirty, terminal;
    array(forwarded, input.flow.blocks.size()); array(dirty, forwarded.size()); array(terminal, forwarded.size());
    std::vector<std::size_t> indegrees, pending;
    array(indegrees, forwarded.size()); array(pending, forwarded.size());
    for (const auto& block : input.flow.blocks) {
      step(); for (std::size_t e = 0; e < block.successor_count; ++e) {
        step(); const auto target = block.successors[e].target.value;
        Require(target < forwarded.size(), Error::kOrigin); ++indegrees[target];
      }
    }
    // Kahn scheduling proves finite normal execution; a residual cycle carries
    // an unresolved termination obligation, never a guessed default body.
    std::size_t count = 0;
    for (std::size_t b = 0; b < forwarded.size(); ++b) if (!indegrees[b]) pending[count++] = b;
    Require(!forwarded.empty(), Error::kOrigin);
    forwarded[0] = Forwarding::kBefore;
    for (std::size_t head = 0; head < count; ++head) {
      step(); const auto b = pending[head]; const auto& block = input.flow.blocks[b];
      auto paths = forwarded[b]; bool arguments_changed = dirty[b];
      const auto end = std::size_t(block.first_instruction) + block.instruction_count;
      Require(end <= input.instructions.size(), Error::kOrigin);
      for (std::size_t i = block.first_instruction; paths != Forwarding::kUnreachable && i < end; ++i) {
        step(); const auto& original = input.instructions[i];
        const auto site = graph.input.sites[i];
        Require(!site || site->value < graph.ops.size(), Error::kOrigin);
        const ValueOp* op = site ? &graph.ops[site->value] : nullptr;
        const auto* forward = std::get_if<SuperForward>(&original.operation);
        if (forward) {
          Require(!arguments_changed && paths == Forwarding::kBefore && op && op->life == ValueLife::kLive &&
              op->use_count == 1 && op->result, Error::kOperand);
          const auto* call = std::get_if<ValueSuperCall>(&op->operation);
          Require(call && call->source == SuperArgumentSource::kInvocationFrame && !call->arguments && !call->cache_slot);
          const auto& use = graph.uses[op->first_use.value];
          Require(use.role == OperandRole::kConstructor && use.purpose == ReadPurpose::kProtocol &&
              state(use.operand) == DerivedValueState::kConstructor, Error::kOperand);
          paths = Forwarding::kAfter;
        } else if (const auto* guard = std::get_if<SuperCheck>(&original.operation)) {
          Require(op && op->use_count == 1, Error::kOrigin);
          const auto actual = state(graph.uses[op->first_use.value].operand);
          Require(guard->kind == SuperCheckKind::kNoop ||
              (guard->kind == SuperCheckKind::kRequireInitialized && actual == DerivedValueState::kSuper) ||
              (guard->kind == SuperCheckKind::kRequireUninitialized && actual == DerivedValueState::kUndefined), Error::kOperand);
        } else {
          Require(original.effects.normal == LanguageEffect::kNone && original.effects.before_throw == LanguageEffect::kNone &&
              original.effects.exceptional == ExceptionalCompletion::kImpossible);
        }
        if (op && op->life == ValueLife::kLive && std::holds_alternative<ValueReturn>(op->operation)) {
          Require(paths == Forwarding::kAfter && op->use_count == 1 && state(graph.uses[op->first_use.value].operand) == DerivedValueState::kSuper);
          terminal[b] = 1;
        }
        const auto access = abc::GetRegisterAccesses(function.body.instructions[i].decoded, frame.registers);
        const auto* registers = std::get_if<abc::RegisterAccesses>(&access); Require(registers, Error::kOrigin);
        for (std::size_t w = 0; w < registers->count; ++w) {
          step(); const auto& window = registers->windows[w];
          if (window.access == abc::OperandAccess::kRead) continue;
          const auto contains = [&](abc::RegisterId id) { return id.value >= window.first.value && id.value - window.first.value < window.count; };
          const auto parameter_end = std::uint64_t(frame.first_parameter.value) + frame.parameters;
          arguments_changed |= contains(*frame.new_target) ||
              (window.first.value < parameter_end && std::uint64_t(window.first.value) + window.count > frame.first_parameter.value);
        }
      }
      Require(paths == Forwarding::kUnreachable || block.successor_count || terminal[b]);
      for (std::size_t e = 0; e < block.successor_count; ++e) {
        step(); const auto target = block.successors[e].target.value;
        forwarded[target] = Join(forwarded[target], paths);
        dirty[target] |= paths != Forwarding::kUnreachable && arguments_changed;
        if (!--indegrees[target]) pending[count++] = target;
      }
    }
    Require(count == forwarded.size());
    return ImplicitConstructorProof{*first_call, initial.work - budget.work, initial.bytes - budget.bytes};
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
