#include "value_rewrite.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "value_edits.h"

namespace abcd::analysis {
namespace {
using Error = ValueRewriteError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class RewriteQuota {
 public:
  explicit RewriteQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::size_t count = 1) {
    Require(count <= remaining_.work, Error::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= remaining_.bytes / width, Error::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <typename T> void Size(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T));
    Work(count);
    items.resize(count);
    if (items.capacity() > count) Admit(items.capacity() - count, sizeof(T));
  }
  void Uses(const ValueProgram& graph) {
    Work(graph.values.size());
    Work(graph.ops.size());
    Work(graph.uses.size());
    Work(graph.uses.size());
    Work(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      throw *error == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kShape;
  }
  std::uint64_t RemainingWork() const { return remaining_.work; }
 private:
  SemanticBudget remaining_;
};
bool SameEntry(const EntryDefinition& a, const EntryDefinition& b) {
  return std::visit([&](const auto& entry) {
    using T = std::decay_t<decltype(entry)>;
    const auto* other = std::get_if<T>(&b);
    if (!other) return false;
    if constexpr (std::is_same_v<T, ParameterEntry>) return entry.index == other->index;
    else if constexpr (std::is_same_v<T, UnknownEntry>) {
      if (entry.slot.index() != other->slot.index()) return false;
      const auto* reg = std::get_if<abc::RegisterId>(&entry.slot);
      return !reg || *reg == std::get<abc::RegisterId>(other->slot);
    } else {
      static_assert(std::is_same_v<T, ReceiverEntry> || std::is_same_v<T, CallableEntry> ||
                    std::is_same_v<T, NewTargetEntry>);
      return true;
    }
  }, a);
}
bool SameDefinition(const ValueDefinition& a, const ValueDefinition& b) {
  if (a.domain != b.domain) return false;
  return std::visit([&](const auto& definition) {
    using T = std::decay_t<decltype(definition)>;
    const auto* other = std::get_if<T>(&b.origin);
    if (!other) return false;
    if constexpr (std::is_same_v<T, EntryDefinition>) return SameEntry(definition, *other);
    else if constexpr (std::is_same_v<T, OpResult>) return definition.op == other->op;
    else {
      static_assert(std::is_same_v<T, PhiDefinition>);
      return definition.block == other->block;
    }
  }, a.origin);
}
bool SameLiteral(const ValueOperand& a, const ValueOperand& b) {
  return std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    const auto* other = std::get_if<T>(&b);
    if (!other) return false;
    if constexpr (std::is_same_v<T, SpecialValue>) return value == *other;
    else {
      static_assert(std::is_same_v<T, ValueId> || std::is_same_v<T, IntegerLiteral> ||
                    std::is_same_v<T, Float64Bits>);
      return value.value == other->value;
    }
  }, a);
}
bool SameIncoming(const std::optional<IncomingKey>& a, const std::optional<IncomingKey>& b) {
  if (a.has_value() != b.has_value()) return false;
  if (!a) return true;
  return std::visit([&](const auto& key) {
    using T = std::decay_t<decltype(key)>;
    const auto* other = std::get_if<T>(&*b);
    if (!other) return false;
    if constexpr (std::is_same_v<T, EdgeId>) return key == *other;
    else { static_assert(std::is_same_v<T, MethodEntryKey>); return true; }
  }, *a);
}
bool SameRead(const std::optional<InputRead>& a, const std::optional<InputRead>& b) {
  if (a.has_value() != b.has_value()) return false;
  return !a || (a->site == b->site && a->ordinal == b->ordinal);
}
enum class OriginUse : std::uint8_t { kUnused, kRetained, kElided };
}  // namespace

std::variant<ValueRewriteProof, ValueRewriteError> VerifyValueRewrite(
    const ValueProgram& before, const ValueProgram& after,
    std::span<const ValueId> representatives, SemanticBudget budget) {
  try {
    Require(before.arguments == after.arguments, Error::kEffect);
    Require(before.revision == after.revision && before.cfg_revision == after.cfg_revision,
            Error::kRevision);
    Require(before.blocks.size() == after.blocks.size() && before.edges.size() == after.edges.size() &&
        before.ops.size() == after.ops.size() && before.values.size() == after.values.size() &&
        before.uses.size() == after.uses.size() && before.origins.size() == after.origins.size() &&
        before.exceptional_exits.size() == after.exceptional_exits.size() &&
        before.input.sites.size() == after.input.sites.size() &&
        before.input.values.size() == after.input.values.size() &&
        representatives.size() == before.values.size(), Error::kShape);
    RewriteQuota quota{budget};
    quota.Admit(before.uses.size(), sizeof(std::uint8_t));
    quota.Uses(before);
    quota.Uses(after);
    std::vector<std::uint8_t> anchored;
    std::vector<ValueId> pending;
    std::vector<OriginUse> origins;
    quota.Size(anchored, before.values.size());
    quota.Size(pending, before.values.size());
    quota.Size(origins, before.origins.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < before.blocks.size(); ++i) {
      quota.Work();
      const auto& a = before.blocks[i]; const auto& b = after.blocks[i];
      Require(a.first_op == b.first_op && a.op_count == b.op_count &&
              a.first_edge == b.first_edge && a.edge_count == b.edge_count, Error::kShape);
    }
    for (std::size_t i = 0; i < before.edges.size(); ++i) {
      quota.Work();
      const auto& a = before.edges[i]; const auto& b = after.edges[i];
      Require(a.source == b.source && a.target == b.target && a.kind == b.kind &&
              a.successor == b.successor, Error::kShape);
    }
    for (std::size_t i = 0; i < before.values.size(); ++i) {
      quota.Work();
      const auto& a = before.values[i]; const auto& b = after.values[i];
      Require(SameDefinition(a, b) &&
              (a.life != ValueLife::kRemoved || b.life == ValueLife::kRemoved), Error::kDefinition);
      const auto root = representatives[i];
      Require(root.value < representatives.size() && representatives[root.value] == root, Error::kAlias);
      if (root.value == i) { anchored[i] = 1; pending[count++] = root; continue; }
      const auto domain = before.values[root.value].domain;
      Require(a.life == ValueLife::kLive && !domain.IsEmpty() &&
              domain.Join(a.domain) == a.domain, Error::kAlias);
      if (const auto* result = std::get_if<OpResult>(&a.origin)) {
        const auto& op = before.ops[result->op.value];
        Require(std::holds_alternative<ValueCopy>(op.operation) && CanErasePureOp(op) &&
                op.use_count == 1, Error::kAlias);
        const auto& use = before.uses[op.first_use.value];
        const auto* input = std::get_if<ValueId>(&use.operand);
        Require(input && use.role == OperandRole::kInput && use.purpose == ReadPurpose::kOperand &&
                representatives[input->value] == root, Error::kAlias);
      } else Require(std::holds_alternative<PhiDefinition>(a.origin), Error::kAlias);
    }
    for (std::size_t i = 0; i < before.ops.size(); ++i) {
      quota.Work();
      const auto& a = before.ops[i]; const auto& b = after.ops[i];
      Require(a.operation == b.operation && a.block == b.block && a.order == b.order &&
          a.first_use == b.first_use && a.use_count == b.use_count && a.result == b.result &&
          a.normal == b.normal && a.before_throw == b.before_throw &&
          a.exceptional == b.exceptional && a.origin == b.origin, Error::kEffect);
      Require(a.life != ValueLife::kRemoved || b.life == ValueLife::kRemoved, Error::kEffect);
      if (a.life == ValueLife::kLive && b.life == ValueLife::kRemoved)
        Require(CanErasePureOp(a), Error::kEffect);
      Require(a.origin.value < origins.size() && origins[a.origin.value] == OriginUse::kUnused,
              Error::kOrigin);
      origins[a.origin.value] = b.life == ValueLife::kRemoved ? OriginUse::kElided : OriginUse::kRetained;
    }
    for (std::size_t i = 0; i < before.uses.size(); ++i) {
      quota.Work();
      const auto& a = before.uses[i]; const auto& b = after.uses[i];
      Require(a.owner == b.owner && a.role == b.role && a.purpose == b.purpose &&
          SameIncoming(a.incoming, b.incoming) && SameRead(a.input, b.input) &&
          (a.life != ValueLife::kRemoved || b.life == ValueLife::kRemoved), Error::kOperand);
      if (a.life == ValueLife::kRemoved) {
        Require(SameLiteral(a.operand, b.operand), Error::kOperand);
        continue;
      }
      if (const auto* value = std::get_if<ValueId>(&a.operand)) {
        const auto* rewritten = std::get_if<ValueId>(&b.operand);
        Require(rewritten && *rewritten == representatives[value->value], Error::kOperand);
        if (const auto* phi = std::get_if<ValueId>(&a.owner);
            phi && representatives[phi->value] != *phi)
          Require(representatives[value->value] == representatives[phi->value], Error::kAlias);
      } else Require(SameLiteral(a.operand, b.operand), Error::kOperand);
    }
    // Every substituted equation must be grounded. A queue over original use
    // lists proves reachability from fixed representatives in O(V+U), even for
    // phi cycles; it does not rerun the optimizer's SCC selection algorithm.
    for (std::size_t i = 0; i < count; ++i) {
      quota.Work();
      const auto value = pending[i];
      for (auto use = before.values[value.value].first_use; use; use = before.uses[use->value].next) {
        quota.Work();
        const auto& occurrence = before.uses[use->value];
        std::optional<ValueId> dependent;
        if (const auto* op = std::get_if<OpId>(&occurrence.owner)) dependent = before.ops[op->value].result;
        else dependent = std::get<ValueId>(occurrence.owner);
        if (!dependent || anchored[dependent->value] ||
            representatives[dependent->value] != representatives[value.value]) continue;
        anchored[dependent->value] = 1;
        pending[count++] = *dependent;
      }
    }
    for (const auto reached : anchored) { quota.Work(); Require(reached != 0, Error::kUnanchored); }
    for (std::size_t i = 0; i < before.origins.size(); ++i) {
      quota.Work();
      const auto& a = before.origins[i]; const auto& b = after.origins[i];
      const auto disposition = origins[i] == OriginUse::kElided ? OriginDisposition::kElidedPure : a.disposition;
      Require(a.site == b.site && a.pc == b.pc && a.original == b.original &&
              b.disposition == disposition, Error::kOrigin);
    }
    for (std::size_t i = 0; i < before.exceptional_exits.size(); ++i) {
      quota.Work();
      const auto& a = before.exceptional_exits[i]; const auto& b = after.exceptional_exits[i];
      Require(a.op == b.op && a.origin == b.origin && a.before_throw == b.before_throw,
              Error::kEffect);
    }
    quota.Work(before.input.sites.size());
    quota.Work(before.input.values.size());
    Require(before.input.sites == after.input.sites && before.input.values == after.input.values,
            Error::kOrigin);
    return ValueRewriteProof{before.revision, before.cfg_revision,
                              budget.work - quota.RemainingWork()};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::analysis
