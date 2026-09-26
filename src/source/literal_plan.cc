#include "literal_plan.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

#include "../analysis/value_flow.h"

namespace abcd::source {
namespace {
using Error = LiteralError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class LiteralBudget {
 public:
  explicit LiteralBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit); remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit); remaining_.bytes -= count * width;
  }
  template <class T> void Size(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return remaining_; }
  std::uint64_t Work() const { return initial_.work - remaining_.work; }
 private:
  analysis::SemanticBudget initial_, remaining_;
};
void Flow(const analysis::ValueProgram& graph, LiteralBudget& budget) {
  const auto proof = analysis::VerifyValueFlow(graph, budget.Remaining());
  if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&proof)) {
    if (*error == analysis::ValueAvailabilityError::kResourceLimit) throw Error::kResourceLimit;
    if (*error == analysis::ValueAvailabilityError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kFlow;
  }
  budget.Step(std::get<analysis::ValueFlowProof>(proof).work);
}
analysis::OpId Allocation(const analysis::ValueProgram& graph, analysis::OpId id) {
  Require(id.value < graph.ops.size(), Error::kDefinition);
  const auto& op = graph.ops[id.value];
  const auto* write = std::get_if<analysis::ValueOwnWrite>(&op.operation);
  Require(op.life == analysis::ValueLife::kLive && write && !op.result && op.use_count == 2,
      Error::kDefinition);
  Require(write->kind == analysis::OwnWriteKind::kStore ||
      write->kind == analysis::OwnWriteKind::kDefineField, Error::kDefinition);
  const auto& base = graph.uses[op.first_use.value];
  const auto& stored = graph.uses[op.first_use.value + 1];
  Require(base.role == analysis::OperandRole::kBase && stored.role == analysis::OperandRole::kStoredValue &&
      base.purpose == analysis::ReadPurpose::kOperand && stored.purpose == analysis::ReadPurpose::kOperand,
      Error::kDefinition);
  const auto* value = std::get_if<analysis::ValueId>(&base.operand);
  Require(value && value->value < graph.values.size(), Error::kReceiver);
  const auto& definition = graph.values[value->value];
  Require(definition.domain.IsLanguage(), Error::kDomain);
  const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
  Require(result && result->op.value < graph.ops.size(), Error::kReceiver);
  const auto& allocation = graph.ops[result->op.value];
  Require(allocation.life == analysis::ValueLife::kLive &&
      std::holds_alternative<analysis::ValueObjectAllocation>(allocation.operation) &&
      !std::get<analysis::ValueObjectAllocation>(allocation.operation).literal &&
      allocation.result == *value && allocation.use_count == 0 &&
      allocation.block == op.block && allocation.order < op.order, Error::kReceiver);
  return result->op;
}
void EarlyValue(const analysis::ValueProgram& graph, const analysis::ValueOp& allocation,
    const analysis::ValueOperand& operand) {
  const auto* value = std::get_if<analysis::ValueId>(&operand);
  if (!value) return;
  const auto& definition = graph.values[value->value];
  Require(definition.domain.IsLanguage(), Error::kDomain);
  if (const auto* result = std::get_if<analysis::OpResult>(&definition.origin)) {
    const auto& producer = graph.ops[result->op.value];
    Require(producer.block != allocation.block || producer.order < allocation.order, Error::kLateValue);
  }
  // Fresh flow proof supplies dominance. A different-block producer available
  // at the same-block own-write is also available at the allocation block entry.
}
struct FieldOwner { analysis::OpId allocation; analysis::OpId definition; };
}  // namespace

std::variant<std::uint64_t, LiteralError> VerifyLiteralPlan(
    const analysis::ValueProgram& graph, const LiteralPlan& plan, analysis::SemanticBudget allowance) {
  try {
    LiteralBudget budget(allowance);
    Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision, Error::kRevision);
    Flow(graph, budget);
    Require(plan.objects.size() <= graph.ops.size() && plan.definitions.size() <= graph.ops.size(), Error::kCoverage);
    std::vector<std::optional<analysis::OpId>> owners;
    std::vector<std::uint8_t> allocations;
    budget.Admit(1, sizeof(owners) + sizeof(allocations));
    budget.Size(owners, graph.ops.size()); budget.Size(allocations, graph.ops.size());
    std::size_t next = 0;
    for (const auto& object : plan.objects) {
      budget.Step();
      Require(object.allocation.value < graph.ops.size() && !allocations[object.allocation.value] &&
          object.first == next && object.count != 0 && object.count <= plan.definitions.size() - next,
          Error::kCoverage);
      allocations[object.allocation.value] = 1;
      const auto& allocation = graph.ops[object.allocation.value];
      auto order = allocation.order;
      for (std::uint32_t i = 0; i < object.count; ++i) {
        budget.Step();
        const auto id = plan.definitions[next++];
        Require(Allocation(graph, id) == object.allocation && !owners[id.value], Error::kCoverage);
        const auto& op = graph.ops[id.value];
        Require(op.order > order, Error::kDefinition); order = op.order;
        EarlyValue(graph, allocation, graph.uses[op.first_use.value + 1].operand);
        owners[id.value] = object.allocation;
      }
    }
    Require(next == plan.definitions.size(), Error::kCoverage);
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const bool own = graph.ops[i].life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueOwnWrite>(graph.ops[i].operation);
      Require(owners[i].has_value() == own, Error::kCoverage);
    }
    for (const auto& object : plan.objects) {
      budget.Step();
      const auto& allocation = graph.ops[object.allocation.value];
      const auto last = graph.ops[plan.definitions[object.first + static_cast<std::size_t>(object.count) - 1].value].order;
      auto use = graph.values[allocation.result->value].first_use;
      while (use) {
        budget.Step();
        const auto& occurrence = graph.uses[use->value];
        if (occurrence.life == analysis::ValueLife::kLive) {
          const auto* op = std::get_if<analysis::OpId>(&occurrence.owner);
          if (op && graph.ops[op->value].block == allocation.block) {
            const bool own_base = owners[op->value] == object.allocation &&
                *use == graph.ops[op->value].first_use;
            Require(own_base || graph.ops[op->value].order > last, Error::kObserved);
          }
        }
        use = occurrence.next;
      }
    }
    return budget.Work();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<LiteralPlan, LiteralError> BuildLiteralPlan(
    const analysis::ValueProgram& graph, analysis::SemanticBudget allowance) {
  try {
    LiteralBudget budget(allowance);
    Flow(graph, budget);
    Require(graph.ops.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    std::size_t count = 0;
    for (const auto& op : graph.ops) {
      budget.Step();
      count += op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueOwnWrite>(op.operation);
    }
    std::vector<FieldOwner> fields;
    LiteralPlan plan{graph.revision, graph.cfg_revision};
    budget.Admit(1, sizeof(fields) + sizeof(plan)); budget.Size(fields, count);
    std::size_t next = 0;
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive || !std::holds_alternative<analysis::ValueOwnWrite>(op.operation)) continue;
      const analysis::OpId id{static_cast<std::uint32_t>(i)};
      fields[next++] = {Allocation(graph, id), id};
    }
    std::sort(fields.begin(), fields.end(), [&](const auto& a, const auto& b) {
      budget.Step();
      return a.allocation != b.allocation ? a.allocation < b.allocation : a.definition < b.definition;
    });
    std::size_t objects = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); objects += i == 0 || fields[i].allocation != fields[i - 1].allocation;
    }
    budget.Size(plan.objects, objects); budget.Size(plan.definitions, count);
    next = 0;
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step();
      if (i == 0 || fields[i].allocation != fields[i - 1].allocation)
        plan.objects[next++] = {fields[i].allocation, static_cast<std::uint32_t>(i), 0};
      ++plan.objects[next - 1].count;
      plan.definitions[i] = fields[i].definition;
    }
    const auto verified = VerifyLiteralPlan(graph, plan, budget.Remaining());
    if (const auto* error = std::get_if<Error>(&verified)) return *error;
    budget.Step(std::get<std::uint64_t>(verified));
    plan.allocated_bytes = sizeof(plan) + plan.objects.capacity() * sizeof(ObjectInit) +
        plan.definitions.capacity() * sizeof(analysis::OpId);
    plan.work = budget.Work();
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
