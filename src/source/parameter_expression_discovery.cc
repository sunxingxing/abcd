#include <algorithm>

#include "parameter_expression.h"
#include "tree_checks.h"
namespace abcd::source {
namespace {
using namespace analysis;
using E = ParameterExpressionError;
void Need(bool ok, E kind = E::kControl) {
  if (!ok) throw ParameterExpressionFailure{kind};
}
struct Discovery {
  const ValueProgram& g;
  const ParameterPlan& p;
  TreeCheckBudget q;
  std::vector<std::uint32_t> pred, arm_generation;
  std::vector<std::uint8_t> seen;
  std::vector<std::optional<UseId>> phi_heads, phi_next;
  std::uint32_t generation = 0;
  const ValueOperand& Operand(const ValueOp& op, std::uint32_t n) {
    Need(n < op.use_count && op.first_use.value + n < g.uses.size());
    return g.uses[op.first_use.value + n].operand;
  }
  const ValueOp* Definition(const ValueOperand& value) {
    const auto* v = std::get_if<ValueId>(&value);
    if (!v || v->value >= g.values.size()) return nullptr;
    const auto* op = std::get_if<OpResult>(&g.values[v->value].origin);
    return op && op->op.value < g.ops.size() ? &g.ops[op->op.value] : nullptr;
  }
  std::optional<DefaultCandidate> Match(WorkBlockId entry) {
    Need(generation < UINT32_MAX);
    ++generation;
    auto header = entry;
    const ValueOp* branch = nullptr;
    while (true) {
      q.Step();
      if (header.value >= g.blocks.size() || seen[header.value]) return {};
      seen[header.value] = 1;
      const auto& block = g.blocks[header.value];
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        q.Step();
        const auto& op = g.ops[block.first_op.value + n];
        if (op.life != ValueLife::kLive ||
            std::holds_alternative<ValueNop>(op.operation))
          continue;
        if (!std::holds_alternative<ValueCopy>(op.operation) &&
            !std::holds_alternative<ValueBinary>(op.operation) &&
            !std::holds_alternative<ValueJump>(op.operation) &&
            !std::holds_alternative<ValueBranch>(op.operation))
          return {};
        branch = &op;
      }
      if (block.edge_count == 2) break;
      if (block.edge_count != 1) return {};
      header = g.edges[block.first_edge.value].target;
      if (header.value >= pred.size() || pred[header.value] != 1) return {};
    }
    const auto* b =
        branch ? std::get_if<ValueBranch>(&branch->operation) : nullptr;
    if (!b || branch->use_count != 1) return {};
    if (b->predicate != UnaryOperator::kTrueOrNonzeroNumber &&
        b->predicate != UnaryOperator::kFalseOrZero)
      return {};
    const auto* comparison = Definition(Operand(*branch, 0));
    const auto* binary =
        comparison ? std::get_if<ValueBinary>(&comparison->operation) : nullptr;
    if (!binary || comparison->use_count != 2 ||
        (binary->op != BinaryOperator::kStrictEqual &&
         binary->op != BinaryOperator::kStrictNotEqual))
      return {};
    std::optional<OpId> undefined;
    const auto is_undefined = [&](const ValueOperand& value) {
      if (const auto* v = std::get_if<SpecialValue>(&value))
        return *v == SpecialValue::kUndefined;
      const auto* op = Definition(value);
      if (!op || op->use_count != 1 ||
          !std::holds_alternative<ValueCopy>(op->operation))
        return false;
      const auto* literal = std::get_if<SpecialValue>(&Operand(*op, 0));
      if (!literal || *literal != SpecialValue::kUndefined) return false;
      undefined = OpId{static_cast<std::uint32_t>(op - g.ops.data())};
      return true;
    };
    const auto& lhs = Operand(*comparison, 0);
    const auto& rhs = Operand(*comparison, 1);
    const auto* value = std::get_if<ValueId>(is_undefined(lhs)   ? &rhs
                                             : is_undefined(rhs) ? &lhs
                                                                 : nullptr);
    if (!value || value->value >= g.values.size()) return {};
    const auto* origin =
        std::get_if<EntryDefinition>(&g.values[value->value].origin);
    const auto* parameter =
        origin ? std::get_if<ParameterEntry>(origin) : nullptr;
    if (!parameter || parameter->index >= p.formals.size() ||
        p.formals[parameter->index].entry != *value)
      return {};
    const auto& block = g.blocks[header.value];
    const bool taken = (binary->op == BinaryOperator::kStrictEqual) ==
                       (b->predicate == UnaryOperator::kTrueOrNonzeroNumber);
    const EdgeId first = block.first_edge, second{first.value + 1};
    const bool first_taken = g.edges[first.value].kind == FlowEdgeKind::kTaken;
    const auto missing = first_taken == taken ? first : second;
    const auto present = first_taken == taken ? second : first;
    const auto arm = g.edges[missing.value].target,
               join = g.edges[present.value].target;
    if (arm == join || join.value >= g.blocks.size() || pred[join.value] != 2)
      return {};
    auto at = arm;
    std::optional<EdgeId> merge;
    std::optional<OpId> last_result;
    while (at != join) {
      q.Step();
      if (at.value >= g.blocks.size() || seen[at.value] || pred[at.value] != 1)
        return {};
      seen[at.value] = 1;
      arm_generation[at.value] = generation;
      const auto& current = g.blocks[at.value];
      if (current.edge_count != 1) return {};
      for (std::uint32_t n = 0; n < current.op_count; ++n) {
        q.Step();
        const auto id = OpId{current.first_op.value + n};
        const auto& op = g.ops[id.value];
        if (op.life == ValueLife::kLive && op.result) last_result = id;
      }
      merge = current.first_edge;
      at = g.edges[merge->value].target;
    }
    if (!merge) return {};
    const auto compare =
                   OpId{static_cast<std::uint32_t>(comparison - g.ops.data())},
               test = OpId{static_cast<std::uint32_t>(branch - g.ops.data())};
    // Resolve the parameter's commit phi by its provided input, not by a VM
    // register or by requiring the join to have a particular physical layout.
    std::optional<ValueId> merged;
    std::optional<UseId> fallback;
    for (auto at = phi_heads[present.value]; at; at = phi_next[at->value]) {
      q.Step();
      const auto& use = g.uses[at->value];
      const auto* owner = std::get_if<ValueId>(&use.owner);
      const auto* input = std::get_if<ValueId>(&use.operand);
      if (!input || *input != *value) continue;
      if (merged) return {};
      merged = *owner;
    }
    if (merged)
      for (auto at = phi_heads[merge->value]; at; at = phi_next[at->value]) {
        q.Step();
        const auto& use = g.uses[at->value];
        const auto* owner = std::get_if<ValueId>(&use.owner);
        if (owner && *owner == *merged) {
          if (fallback) return {};
          fallback = *at;
        }
      }
    if (merged && !fallback) return {};
    ValueOperand result = merged ? g.uses[fallback->value].operand
                          : last_result
                              ? ValueOperand{*g.ops[last_result->value].result}
                              : ValueOperand{SpecialValue::kUndefined};
    const auto* root = Definition(result);
    const bool local = root && root->block.value < arm_generation.size() &&
                       arm_generation[root->block.value] == generation;
    if (!local && !fallback) return {};
    DefaultCandidate candidate{
        parameter->index,
        *value,
        merged,
        result,
        local ? OpId{static_cast<std::uint32_t>(root - g.ops.data())} : test,
        compare,
        test,
        header,
        arm,
        join,
        present,
        missing,
        *merge,
        undefined,
        {}};
    if (!local) candidate.input = fallback;
    candidate.prologue = entry;
    return candidate;
  }
  DefaultCandidates Run(std::uint32_t length) {
    Need(p.revision == g.revision && p.cfg_revision == g.cfg_revision,
         E::kRevision);
    q.Array(pred, g.blocks.size());
    q.Array(seen, g.blocks.size());
    q.Array(arm_generation, g.blocks.size());
    q.Array(phi_heads, g.edges.size());
    q.Array(phi_next, g.uses.size());
    for (std::uint32_t i = 0; i < g.uses.size(); ++i) {
      q.Step();
      const auto& u = g.uses[i];
      const auto* edge =
          u.incoming ? std::get_if<EdgeId>(&*u.incoming) : nullptr;
      if (u.life == ValueLife::kLive &&
          std::holds_alternative<ValueId>(u.owner) && edge) {
        Need(edge->value < phi_heads.size());
        phi_next[i] = phi_heads[edge->value];
        phi_heads[edge->value] = UseId{i};
      }
    }
    for (const auto& e : g.edges) {
      q.Step();
      Need(e.target.value < pred.size());
      ++pred[e.target.value];
    }
    DefaultCandidates out;
    out.revision = g.revision;
    out.cfg_revision = g.cfg_revision;
    if (length >= p.formals.size() || g.blocks.empty()) return out;
    q.Admit(p.formals.size() - length, sizeof(DefaultCandidate));
    out.values.reserve(p.formals.size() - length);
    q.Admit(out.values.capacity() - (p.formals.size() - length),
            sizeof(DefaultCandidate));
    WorkBlockId at{0};
    std::uint32_t next = length;
    while (true) {
      auto candidate = Match(at);
      if (!candidate || candidate->parameter < next ||
          (out.values.empty() && candidate->parameter != length))
        break;
      out.values.push_back(*candidate);
      next = candidate->parameter + 1;
      at = candidate->join;
    }
    out.allocated_bytes = q.Bytes();
    out.work = q.Work();
    return out;
  }
};
}  // namespace
std::variant<DefaultCandidates, DefaultCandidateError>
FindParameterExpressionCandidates(const analysis::ValueProgram& g,
                                  const ParameterPlan& p, std::uint32_t length,
                                  analysis::SemanticBudget b) {
  try {
    return Discovery{g, p, TreeCheckBudget{b}}.Run(length);
  } catch (ParameterExpressionFailure e) {
    return e.kind == E::kRevision ? DefaultCandidateError::kRevision
                                  : DefaultCandidateError::kInvalidGraph;
  } catch (TreeStructureError e) {
    return e == TreeStructureError::kWorkLimit
               ? DefaultCandidateError::kWorkLimit
               : DefaultCandidateError::kResourceLimit;
  } catch (const std::bad_alloc&) {
    return DefaultCandidateError::kResourceLimit;
  }
}
std::variant<ParameterRegions, ParameterExpressionFailure>
DeriveParameterRegions(const ValueProgram& g,
                       std::span<const DefaultCandidate> defaults,
                       SemanticBudget b) {
  try {
    TreeCheckBudget q(b);
    std::vector<std::uint32_t> pred;
    std::vector<std::uint8_t> seen;
    q.Array(pred, g.blocks.size());
    q.Array(seen, g.blocks.size());
    for (const auto& e : g.edges) {
      q.Step();
      Need(e.target.value < pred.size());
      ++pred[e.target.value];
    }
    ParameterRegions result;
    q.Admit(g.blocks.size(), sizeof(ParameterRegionBlock));
    result.blocks.reserve(g.blocks.size());
    q.Admit(result.blocks.capacity() - g.blocks.size(),
            sizeof(ParameterRegionBlock));
    WorkBlockId expected{0};
    for (std::uint32_t i = 0; i < defaults.size(); ++i) {
      q.Step();
      const auto& d = defaults[i];
      auto at = d.prologue.value_or(d.header);
      Need(at == expected);
      bool first = true;
      while (true) {
        q.Step();
        Need(at.value < g.blocks.size() && !seen[at.value] &&
             pred[at.value] == (first ? (i ? 2U : 0U) : 1U));
        seen[at.value] = 1;
        result.blocks.push_back({at, i, ParameterRegionRole::kTest});
        first = false;
        const auto& block = g.blocks[at.value];
        if (at == d.header) {
          Need(block.edge_count == 2);
          break;
        }
        Need(block.edge_count == 1);
        at = g.edges[block.first_edge.value].target;
      }
      Need(d.present.value < g.edges.size() &&
           d.missing.value < g.edges.size() &&
           g.edges[d.present.value].source == d.header &&
           g.edges[d.present.value].target == d.join &&
           g.edges[d.missing.value].source == d.header &&
           g.edges[d.missing.value].target == d.arm);
      at = d.arm;
      std::optional<EdgeId> last;
      while (at != d.join) {
        q.Step();
        Need(at.value < g.blocks.size() && !seen[at.value] &&
             pred[at.value] == 1);
        seen[at.value] = 1;
        result.blocks.push_back({at, i, ParameterRegionRole::kInitializer});
        const auto& block = g.blocks[at.value];
        Need(block.edge_count == 1);
        last = block.first_edge;
        at = g.edges[last->value].target;
      }
      Need(last && *last == d.merge && d.join.value < pred.size() &&
           pred[d.join.value] == 2);
      expected = d.join;
    }
    result.allocated_bytes = q.Bytes();
    result.work = q.Work();
    return result;
  } catch (ParameterExpressionFailure e) {
    return e;
  } catch (TreeStructureError e) {
    return ParameterExpressionFailure{e == TreeStructureError::kWorkLimit
                                          ? E::kWorkLimit
                                          : E::kResourceLimit};
  } catch (const std::bad_alloc&) {
    return ParameterExpressionFailure{E::kResourceLimit};
  }
}
}  // namespace abcd::source
