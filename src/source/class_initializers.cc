#include "class_initializers.h"
#include "exception_forwarding.h"
#include "../analysis/value_edits.h"
#include "../analysis/value_flow.h"
#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <type_traits>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ClassInitializerError;
void Require(bool condition, Error error = Error::kInput) { if (!condition) throw error; }
// The local handler an op may throw to (exits are recorded in op order).
std::optional<ValueHandler> Handler(const ValueProgram& graph, OpId op) {
  const auto exit = std::lower_bound(graph.exceptional_exits.begin(), graph.exceptional_exits.end(), op,
      [](const auto& item, OpId wanted) { return item.op < wanted; });
  if (exit == graph.exceptional_exits.end() || exit->op != op) return std::nullopt;
  return exit->handler;
}
}
std::variant<ClassInitializerCalls, ClassInitializerError> FindClassInitializerCalls(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  const auto initial = budget;
  try {
    Require(graph.ops.size() <= UINT32_MAX && graph.values.size() <= UINT32_MAX, Error::kResourceLimit);
    const auto step = [&](std::size_t count = 1) {
      Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
    };
    const auto admit = [&](std::size_t count, std::size_t width) {
      Require(count <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= count * width;
    };
    step(graph.ops.size()); step(graph.values.size()); step(graph.uses.size());
    admit(graph.uses.size(), sizeof(std::uint8_t));
    const auto links = VerifyValueUses(graph, graph.uses.size());
    if (links) return *links == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kInput;
    ClassInitializerCalls result{graph.revision};
    std::size_t methods = 0;
    for (const auto& op : graph.ops) {
      step(); const auto* method = std::get_if<ValueFunctionCreation>(&op.operation);
      methods += op.life == ValueLife::kLive && method && method->kind == CreationKind::kMethod;
    }
    admit(methods, sizeof(ClassInitializerCall)); result.calls.reserve(methods);
    admit(result.calls.capacity() - methods, sizeof(ClassInitializerCall));
    if (methods) {
      // Common SSA availability validates every actual home/callee occurrence.
      // Numeric operation positions and block membership grant no extra proof.
      // Phased graphs prove the same availability over Before/Commit/Catch.
      const auto proved = [&](const auto& checked) -> std::optional<Error> {
        if (const auto* error = std::get_if<ValueAvailabilityError>(&checked)) {
          if (*error == ValueAvailabilityError::kResourceLimit) return Error::kResourceLimit;
          if (*error == ValueAvailabilityError::kWorkLimit) return Error::kWorkLimit;
          return Error::kFlow;
        }
        std::visit([&](const auto& flow) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(flow)>, ValueAvailabilityError>) {
            admit(flow.allocated_bytes, 1); step(flow.work);
          }
        }, checked);
        return std::nullopt;
      };
      if (const auto error = graph.phases.empty() ? proved(VerifyValueFlow(graph, budget))
                                                  : proved(VerifyPhasedValueFlow(graph, budget)))
        return *error;
    }
    const auto operand = [&](OpId id, std::uint32_t ordinal, OperandRole role, ReadPurpose purpose) -> const ValueOperand& {
      Require(id.value < graph.ops.size());
      const auto& op = graph.ops[id.value];
      Require(ordinal < op.use_count && op.first_use.value < graph.uses.size() &&
          op.use_count <= graph.uses.size() - op.first_use.value);
      const auto& use = graph.uses[op.first_use.value + ordinal];
      Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){id} &&
          use.role == role && use.purpose == purpose);
      return use.operand;
    };
    // Forwarding summaries and catch values, computed once on first need.
    std::optional<ExceptionForwarding> forwarding;
    std::vector<std::optional<ValueId>> catches;
    const auto CallerExit = [&](OpId op) {
      step(std::bit_width(graph.exceptional_exits.size()) + 1);
      const auto handler = Handler(graph, op);
      if (!handler) return true;
      if (!forwarding) {
        auto built = AnalyzeExceptionForwarding(graph, budget);
        if (const auto* failure = std::get_if<ForInFailure>(&built))
          throw failure->kind == ForInError::kResourceLimit ? Error::kResourceLimit :
              failure->kind == ForInError::kWorkLimit ? Error::kWorkLimit : Error::kFlow;
        forwarding = std::get<ExceptionForwarding>(std::move(built));
        admit(forwarding->allocated_bytes, 1); step(forwarding->work);
        admit(graph.blocks.size(), sizeof(std::optional<ValueId>)); catches.resize(graph.blocks.size());
        for (std::uint32_t v = 0; v < graph.values.size(); ++v) {
          step();
          if (const auto* entry = std::get_if<CatchDefinition>(&graph.values[v].origin);
              entry && entry->block.value < catches.size()) catches[entry->block.value] = ValueId{v};
        }
      }
      const auto entry = handler->entry.value;
      if (entry >= catches.size() || !catches[entry]) return false;
      const auto& summary = forwarding->blocks[entry];
      return summary && !summary->destination && summary->payload == *catches[entry];
    };
    for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
      step(); const OpId id{i}; const auto& op = graph.ops[i];
      const auto* method = std::get_if<ValueFunctionCreation>(&op.operation);
      if (op.life != ValueLife::kLive || !method || method->kind != CreationKind::kMethod) continue;
      Require(method->home_object == HomeObjectInput::kAccumulator && op.result &&
          op.result->value < graph.values.size() && op.use_count == 1);
      const auto* home = std::get_if<ValueId>(&operand(id, 0, OperandRole::kHomeObject, ReadPurpose::kProtocol));
      if (method->length || !home) continue;
      Require(home->value < graph.values.size());
      // Identity is the copy root: phased graphs keep pure register copies.
      std::uint64_t hops = 0;
      const auto root = [&](ValueId value) { const auto found = CopyRoot(graph, value, hops); step(hops); hops = 0; return found; };
      const auto class_value = root(*home);
      const auto* origin = std::get_if<OpResult>(&graph.values[class_value.value].origin);
      if (!origin) continue;
      Require(origin->op.value < graph.ops.size());
      const auto& owner = graph.ops[origin->op.value];
      if (owner.life != ValueLife::kLive || !std::holds_alternative<ValueClassCreation>(owner.operation)) continue;
      Require(owner.result == class_value);
      auto first = graph.values[op.result->value].first_use;
      // Phased graphs may route the method through single-use pure copies.
      for (std::size_t hops = 0; first && hops < graph.ops.size() && !graph.phases.empty(); ++hops) {
        step(); const auto& through = graph.uses[first->value];
        const auto* copy_id = std::get_if<OpId>(&through.owner);
        if (through.next || !copy_id) break;
        const auto& copy = graph.ops[copy_id->value];
        if (copy.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(copy.operation) ||
            !CanErasePureOp(copy) || !copy.result) break;
        first = graph.values[copy.result->value].first_use;
      }
      if (!first) continue;
      const auto& use = graph.uses[first->value];
      if (use.next || use.role != OperandRole::kCallee || use.purpose != ReadPurpose::kOperand) continue;
      const auto* call_id = std::get_if<OpId>(&use.owner);
      if (!call_id) continue;
      Require(call_id->value < graph.ops.size());
      const auto& call = graph.ops[call_id->value];
      const auto* invoke = std::get_if<ValueInvoke>(&call.operation);
      if (call.life != ValueLife::kLive || !invoke || invoke->receiver != InvokeReceiver::kExplicit ||
          invoke->arguments) continue;
      Require(call.use_count == 2 && call.result && call.result->value < graph.values.size());
      const auto* callee = std::get_if<ValueId>(&operand(*call_id, 0, OperandRole::kCallee, ReadPurpose::kOperand));
      const auto* receiver = std::get_if<ValueId>(&operand(*call_id, 1, OperandRole::kReceiver, ReadPurpose::kOperand));
      if (!callee || root(*callee) != *op.result || !receiver || root(*receiver) != class_value ||
          graph.values[call.result->value].first_use) continue;
      // A phased projection moves the method creation and call into class
      // syntax; admit it only when neither may throw to a local handler other
      // than one that just rethrows its catch value to the caller (e.g. an
      // environment cleanup `poplexenv; throw`), which is a caller exit.
      if (!graph.phases.empty() && (!CallerExit(id) || !CallerExit(*call_id))) continue;
      result.calls.push_back({origin->op, id, *call_id, *receiver});
    }
    // Stored instance initializers: home is an actual prototype read of a
    // class creation, and the method's only use is one lexical store. Their
    // constructor projection has no phased exception-port proof yet.
    if (methods && graph.phases.empty()) {
      auto reads = FindClassPrototypeReads(graph, budget);
      if (const auto* error = std::get_if<ClassPrototypeError>(&reads))
        return *error == ClassPrototypeError::kResourceLimit ? Error::kResourceLimit :
            *error == ClassPrototypeError::kWorkLimit ? Error::kWorkLimit : Error::kFlow;
      const auto& prototypes = std::get<ClassPrototypeReads>(reads);
      admit(prototypes.allocated_bytes, 1); step(prototypes.work);
      std::size_t count = 0;
      const auto candidate = [&](std::uint32_t i) -> std::optional<ClassInstanceInitializer> {
        const OpId id{i}; const auto& op = graph.ops[i];
        const auto* method = std::get_if<ValueFunctionCreation>(&op.operation);
        if (op.life != ValueLife::kLive || !method || method->kind != CreationKind::kMethod ||
            method->home_object != HomeObjectInput::kAccumulator || method->length || !op.result) return {};
        const auto* home = std::get_if<ValueId>(&operand(id, 0, OperandRole::kHomeObject, ReadPurpose::kProtocol));
        if (!home) return {};
        const auto* origin = std::get_if<OpResult>(&graph.values[home->value].origin);
        // v7 RuntimeDefineGetterSetterByValue returns its object: a home that is
        // an accessor definition's result is that definition's base object.
        // es2abc may also leave a preceding member method in acc; that home is
        // not the prototype and is admitted only for super-free bodies.
        bool prototype_home = true;
        for (std::size_t hops = 0; origin && hops < graph.ops.size(); ++hops) {
          step(); const auto& definer = graph.ops[origin->op.value];
          if (definer.life != ValueLife::kLive) break;
          const ValueId* base = nullptr;
          if (std::holds_alternative<ValueDefineAccessor>(definer.operation)) {
            base = std::get_if<ValueId>(&operand(origin->op, 0, OperandRole::kBase, ReadPurpose::kOperand));
          } else if (const auto* member = std::get_if<ValueFunctionCreation>(&definer.operation);
                     member && member->kind == CreationKind::kMethod && member->home_object == HomeObjectInput::kAccumulator) {
            base = std::get_if<ValueId>(&operand(origin->op, 0, OperandRole::kHomeObject, ReadPurpose::kProtocol));
            prototype_home = false;
          } else break;
          origin = base ? std::get_if<OpResult>(&graph.values[base->value].origin) : nullptr;
        }
        if (!origin) return {};
        step(std::bit_width(prototypes.reads.size()) + 1);
        const auto read = std::lower_bound(prototypes.reads.begin(), prototypes.reads.end(), origin->op,
            [](const auto& item, OpId wanted) { return item.read < wanted; });
        if (read == prototypes.reads.end() || read->read != origin->op) return {};
        const auto first = graph.values[op.result->value].first_use;
        if (!first) return {};
        const auto& use = graph.uses[first->value];
        const auto* store_id = std::get_if<OpId>(&use.owner);
        if (use.next || !store_id || use.role != OperandRole::kStoredValue) return {};
        const auto& store = graph.ops[store_id->value];
        const auto* lexical = std::get_if<ValueLexicalStore>(&store.operation);
        if (store.life != ValueLife::kLive || !lexical) return {};
        return ClassInstanceInitializer{read->creation, read->read, id, *store_id, lexical->address, prototype_home};
      };
      for (std::uint32_t i = 0; i < graph.ops.size(); ++i) { step(); count += candidate(i).has_value(); }
      admit(count, sizeof(ClassInstanceInitializer)); result.instances.reserve(count);
      admit(result.instances.capacity() - count, sizeof(ClassInstanceInitializer));
      for (std::uint32_t i = 0; i < graph.ops.size(); ++i) { step(); if (auto found = candidate(i)) result.instances.push_back(*found); }
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
