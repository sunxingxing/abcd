#include "abcd/analysis/value_simplify.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "value_edits.h"
#include "value_flow.h"
#include "value_phi.h"
#include "value_rewrite.h"

namespace abcd::analysis {
namespace {
using Error = ValueSimplifyError;
void Require(bool condition, Error error) {
  if (!condition) throw error;
}
class BatchBudget {
 public:
  explicit BatchBudget(SemanticBudget budget)
      : bytes_(budget.bytes), work_(budget.work) {}
  void Work(std::size_t count = 1) {
    Require(count <= work_, Error::kWorkLimit);
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit);
    bytes_ -= count * width;
  }
  SemanticBudget Remaining() const { return {bytes_, work_}; }
  template <typename T> void Reserve(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T));
    items.reserve(count);
    if (items.capacity() > count) Admit(items.capacity() - count, sizeof(T));
  }
  void CheckUses(const ValueProgram& graph) {
    Work(graph.values.size());
    Work(graph.ops.size());
    // Linked traversal, owner ranges and occurrence census are separate passes.
    Work(graph.uses.size());
    Work(graph.uses.size());
    Work(graph.uses.size());
    if (const auto error = VerifyValueUses(graph, graph.uses.size()))
      throw *error == ValueGraphError::kResourceLimit
          ? Error::kResourceLimit : Error::kInvalidGraph;
  }
  void CheckFlow(const ValueProgram& graph) {
    const auto checked = VerifyValueFlow(graph, {bytes_, work_});
    if (const auto* proof = std::get_if<ValueFlowProof>(&checked)) {
      Work(proof->work);
      return;  // All verifier storage is released before returning.
    }
    switch (std::get<ValueAvailabilityError>(checked)) {
      case ValueAvailabilityError::kWorkLimit: throw Error::kWorkLimit;
      case ValueAvailabilityError::kResourceLimit: throw Error::kResourceLimit;
      case ValueAvailabilityError::kUnreachableBlock:
      case ValueAvailabilityError::kResumeFlow: throw Error::kUnsupportedFlow;
      case ValueAvailabilityError::kInvalidGraph:
      case ValueAvailabilityError::kInvalidTerminator:
      case ValueAvailabilityError::kInvalidPhi:
      case ValueAvailabilityError::kUnavailableValue: throw Error::kInvalidGraph;
    }
  }
 private:
  std::size_t bytes_;
  std::uint64_t work_;
};

template <typename Graph, typename F> void GraphArrays(Graph& graph, F visit) {
  visit(graph.blocks);
  visit(graph.edges);
  visit(graph.ops);
  visit(graph.values);
  visit(graph.uses);
  visit(graph.origins);
  visit(graph.exceptional_exits);
  visit(graph.input.sites);
  visit(graph.input.values);
}

ValueProgram Clone(const ValueProgram& input, BatchBudget& quota) {
  ValueProgram candidate;
  candidate.revision = input.revision;
  candidate.cfg_revision = input.cfg_revision;
  candidate.arguments = input.arguments;
  const auto copy = [&](const auto& from, auto& to) {
    quota.Reserve(to, from.size());
    quota.Work(from.size());
    to.assign(from.begin(), from.end());
  };
  copy(input.blocks, candidate.blocks);
  copy(input.edges, candidate.edges);
  copy(input.ops, candidate.ops);
  copy(input.values, candidate.values);
  copy(input.uses, candidate.uses);
  copy(input.origins, candidate.origins);
  copy(input.exceptional_exits, candidate.exceptional_exits);
  copy(input.input.sites, candidate.input.sites);
  copy(input.input.values, candidate.input.values);
  return candidate;
}

enum class AliasVisit : std::uint8_t { kUnseen, kActive, kResolved };
struct BatchScratch {
  std::vector<ValueId> canonical;
  std::vector<AliasVisit> visit;
  std::vector<ValueId> pending;
  std::vector<std::uint8_t> live_ops;
  std::vector<std::uint8_t> live_values;
  std::vector<std::optional<UseId>> phi_head;
  std::vector<std::optional<UseId>> phi_next;
  BatchScratch(const ValueProgram& graph, BatchBudget& quota) {
    const auto sized = [&](auto& items, std::size_t size) {
      quota.Reserve(items, size);
      quota.Work(size);
      items.resize(size);
    };
    sized(canonical, graph.values.size());
    sized(visit, graph.values.size());
    quota.Reserve(pending, graph.values.size());
    sized(live_ops, graph.ops.size());
    sized(live_values, graph.values.size());
    sized(phi_head, graph.values.size());
    sized(phi_next, graph.uses.size());
  }
};

void Canonicalize(ValueProgram& graph, BatchScratch& scratch,
                  BatchBudget& quota, ValueSimplifyStats& stats) {
  auto& aliases = scratch.canonical;
  for (std::size_t i = 0; i < aliases.size(); ++i) {
    quota.Work();
    aliases[i] = ValueId{static_cast<std::uint32_t>(i)};
  }
  for (const auto& op : graph.ops) {
    quota.Work();
    if (op.life != ValueLife::kLive || !std::holds_alternative<ValueCopy>(op.operation) ||
        !CanErasePureOp(op) || !op.result || op.use_count != 1) continue;
    const auto& use = graph.uses[op.first_use.value];
    const auto* source = std::get_if<ValueId>(&use.operand);
    if (!source || use.role != OperandRole::kInput ||
        use.purpose != ReadPurpose::kOperand) continue;
    Require(*source != *op.result, Error::kCopyCycle);
    // Do not broaden a refined input certificate by replacing it with a value
    // whose facts are weaker. Constants are not materialized as fake ValueIds.
    const auto domain = graph.values[source->value].domain;
    const auto admitted = graph.values[op.result->value].domain;
    if (domain.IsEmpty() || domain.Join(admitted) != admitted)
      continue;
    aliases[op.result->value] = *source;
  }
  // Functional-graph walk with path compression: each unresolved vertex is
  // entered once, including chains whose definition IDs are not topological.
  for (std::size_t i = 0; i < aliases.size(); ++i) {
    quota.Work();
    if (scratch.visit[i] == AliasVisit::kResolved) continue;
    scratch.pending.clear();
    ValueId cursor{static_cast<std::uint32_t>(i)};
    while (scratch.visit[cursor.value] == AliasVisit::kUnseen &&
           aliases[cursor.value] != cursor) {
      quota.Work();
      scratch.visit[cursor.value] = AliasVisit::kActive;
      scratch.pending.push_back(cursor);
      cursor = aliases[cursor.value];
    }
    Require(scratch.visit[cursor.value] != AliasVisit::kActive, Error::kCopyCycle);
    const auto root = aliases[cursor.value];
    scratch.visit[cursor.value] = AliasVisit::kResolved;
    for (const auto value : scratch.pending) {
      quota.Work();
      aliases[value.value] = root;
      scratch.visit[value.value] = AliasVisit::kResolved;
    }
  }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work();
    const auto& use = graph.uses[i];
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (use.life != ValueLife::kLive || !value || aliases[value->value] == *value)
      continue;
    Require(!ReplaceUse(graph, UseId{static_cast<std::uint32_t>(i)},
                        aliases[value->value]), Error::kInvalidGraph);
    ++stats.replaced_uses;
  }
}

// Copy canonicalization already compressed all alias paths. A single use scan
// can now substitute primitive roots, without per-use walks or moving effects.
void PropagateLiterals(ValueProgram& graph, BatchBudget& quota, ValueSimplifyStats& stats) {
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work();
    const auto& use = graph.uses[i];
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (use.life != ValueLife::kLive || !value) continue;
    // Phi selection/default recovery and conditional argument snapshots retain
    // SSA inputs. Keep their defining copy alive; substitute only ordinary
    // operation operands, not merge inputs or protocol reads.
    if (!std::holds_alternative<OpId>(use.owner) || use.purpose != ReadPurpose::kOperand) continue;
    const auto* result = std::get_if<OpResult>(&graph.values[value->value].origin);
    if (!result) continue;
    const auto& root = graph.ops[result->op.value];
    if (root.life != ValueLife::kLive || !CanErasePureOp(root) ||
        !std::holds_alternative<ValueCopy>(root.operation) || root.use_count != 1) continue;
    const auto& input = graph.uses[root.first_use.value];
    if (input.role != OperandRole::kInput || input.purpose != ReadPurpose::kOperand ||
        std::holds_alternative<ValueId>(input.operand)) continue;
    Require(!ReplaceLiteralUse(graph, UseId{static_cast<std::uint32_t>(i)}, input.operand), Error::kInvalidGraph);
    ++stats.replaced_uses;
  }
}

void MarkLive(const ValueProgram& graph, BatchScratch& scratch, BatchBudget& quota) {
  scratch.pending.clear();
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work();
    const auto& use = graph.uses[i];
    const auto* phi = std::get_if<ValueId>(&use.owner);
    if (!phi || use.life != ValueLife::kLive) continue;
    scratch.phi_next[i] = scratch.phi_head[phi->value];
    scratch.phi_head[phi->value] = UseId{static_cast<std::uint32_t>(i)};
  }
  const auto mark_value = [&](const ValueOperand& operand) {
    const auto* value = std::get_if<ValueId>(&operand);
    if (!value || scratch.live_values[value->value]) return;
    scratch.live_values[value->value] = 1;
    scratch.pending.push_back(*value);
  };
  const auto mark_op = [&](OpId id) {
    if (scratch.live_ops[id.value]) return;
    scratch.live_ops[id.value] = 1;
    const auto& op = graph.ops[id.value];
    for (std::uint32_t ordinal = 0; ordinal < op.use_count; ++ordinal) {
      quota.Work();
      mark_value(graph.uses[op.first_use.value + ordinal].operand);
    }
  };
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    quota.Work();
    const auto& op = graph.ops[i];
    if (op.life == ValueLife::kLive && !CanErasePureOp(op))
      mark_op(OpId{static_cast<std::uint32_t>(i)});
  }
  for (std::size_t i = 0; i < scratch.pending.size(); ++i) {
    quota.Work();
    const auto id = scratch.pending[i];
    const auto& value = graph.values[id.value];
    if (const auto* result = std::get_if<OpResult>(&value.origin)) {
      mark_op(result->op);
    } else if (std::holds_alternative<PhiDefinition>(value.origin)) {
      for (auto use = scratch.phi_head[id.value]; use;
           use = scratch.phi_next[use->value]) {
        quota.Work();
        mark_value(graph.uses[use->value].operand);
      }
    }
  }
}

// Selective tail duplication for shared pure exits. Unlike general
// node splitting, each admitted block has one jump and at most one primitive
// copy, no phi/environment/effect, and any result only feeds the outgoing phi.
// Dead literal elimination can leave a jump-only tail; preserve its outgoing
// phi operands on every copied edge without inventing a new value.
// Every original incoming path still executes any retained literal once. This
// removes cross-entry diamonds without moving or duplicating property reads.
void SpecializePureTails(ValueProgram& graph, BatchBudget& quota, ValueSimplifyStats& stats) {
  struct Candidate { std::optional<OpId> literal; OpId jump; };
  std::vector<std::optional<Candidate>> candidates;
  std::vector<std::optional<EdgeId>> first, next;
  std::vector<std::optional<UseId>> phi_first, phi_next;
  std::vector<bool> phi_blocks;
  const auto size = [&](auto& values, std::size_t count) {
    quota.Reserve(values, count); quota.Work(count); values.resize(count);
  };
  const auto blocks = graph.blocks.size(), edges = graph.edges.size(), uses = graph.uses.size();
  size(candidates, blocks); size(first, blocks); size(next, edges);
  size(phi_first, edges); size(phi_next, uses); size(phi_blocks, blocks);
  for (const auto& value : graph.values) {
    quota.Work();
    if (value.life == ValueLife::kLive)
      if (const auto* phi = std::get_if<PhiDefinition>(&value.origin)) phi_blocks[phi->block.value] = true;
  }
  for (std::size_t i = 0; i < edges; ++i) {
    quota.Work(); const auto target = graph.edges[i].target.value;
    next[i] = first[target]; first[target] = EdgeId{static_cast<std::uint32_t>(i)};
  }
  for (std::size_t i = 0; i < uses; ++i) {
    quota.Work(); const auto& use = graph.uses[i];
    if (use.life != ValueLife::kLive || !std::holds_alternative<ValueId>(use.owner) || !use.incoming) continue;
    const auto* edge = std::get_if<EdgeId>(&*use.incoming);
    if (!edge) continue;
    phi_next[i] = phi_first[edge->value]; phi_first[edge->value] = UseId{static_cast<std::uint32_t>(i)};
  }
  std::size_t copies = 0, extra_uses = 0, extra_ops = 0, extra_values = 0;
  const auto add = [&](std::size_t& count, std::size_t amount) {
    Require(amount <= std::numeric_limits<std::uint32_t>::max() - count, Error::kResourceLimit);
    count += amount;
  };
  for (std::size_t b = 1; b < blocks; ++b) {
    quota.Work(); const auto& block = graph.blocks[b];
    if (phi_blocks[b] || block.edge_count != 1) continue;
    std::optional<OpId> literal, jump;
    bool valid = true;
    for (std::size_t i = block.first_op.value; i < block.first_op.value + block.op_count; ++i) {
      quota.Work(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive) continue;
      if (!literal && !jump && std::holds_alternative<ValueCopy>(op.operation) && CanErasePureOp(op) && op.result && op.use_count == 1 &&
          !std::holds_alternative<ValueId>(graph.uses[op.first_use.value].operand) &&
          graph.values[op.result->value].domain.Join(ValueKinds::Language()) == ValueKinds::Language()) {
        literal = OpId{static_cast<std::uint32_t>(i)};
      } else if (!jump && std::holds_alternative<ValueJump>(op.operation) && op.use_count == 0 && !op.result &&
          op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone &&
          op.exceptional == ExceptionalCompletion::kImpossible) jump = OpId{static_cast<std::uint32_t>(i)};
      else { valid = false; break; }
    }
    if (!valid || !jump) continue;
    const auto value = literal ? graph.ops[literal->value].result : std::nullopt;
    for (auto use = value ? graph.values[value->value].first_use : std::nullopt; use; use = graph.uses[use->value].next) {
      quota.Work(); const auto& input = graph.uses[use->value];
      if (!std::holds_alternative<ValueId>(input.owner) || !input.incoming ||
          !std::holds_alternative<EdgeId>(*input.incoming) || std::get<EdgeId>(*input.incoming) != block.first_edge) {
        valid = false; break;
      }
    }
    if (valid) candidates[b] = Candidate{literal, *jump};
  }
  // A tail must eventually leave the copy/jump chain. In particular, do not
  // manufacture more blocks each time simplification sees a pure jump cycle.
  enum class TailState : std::uint8_t { kUnknown, kVisiting, kExit, kCycle };
  std::vector<TailState> states; std::vector<std::size_t> path;
  size(states, blocks); quota.Reserve(path, blocks);
  for (std::size_t b = 1; b < blocks; ++b) {
    quota.Work(); if (!candidates[b] || states[b] != TailState::kUnknown) continue;
    auto cursor = b; path.clear();
    while (candidates[cursor] && states[cursor] == TailState::kUnknown) {
      quota.Work(); states[cursor] = TailState::kVisiting; path.push_back(cursor);
      cursor = graph.edges[graph.blocks[cursor].first_edge.value].target.value;
    }
    const auto terminal = !candidates[cursor] || states[cursor] == TailState::kExit ? TailState::kExit : TailState::kCycle;
    for (const auto item : path) { quota.Work(); states[item] = terminal; }
  }
  for (std::size_t b = 1; b < blocks; ++b) {
    quota.Work();
    if (!candidates[b]) continue;
    if (states[b] != TailState::kExit || !first[b] || !next[first[b]->value]) { candidates[b].reset(); continue; }
    const auto& block = graph.blocks[b];
    std::size_t count = 0, phi_count = 0;
    for (auto edge = first[b]; edge; edge = next[edge->value]) { quota.Work(); ++count; }
    for (auto use = phi_first[block.first_edge.value]; use; use = phi_next[use->value]) { quota.Work(); ++phi_count; }
    const auto duplicates = count - 1;
    Require(phi_count < std::numeric_limits<std::uint32_t>::max() &&
        duplicates <= std::numeric_limits<std::uint32_t>::max() / (phi_count + 1), Error::kResourceLimit);
    add(copies, duplicates);
    const auto has_literal = candidates[b]->literal.has_value();
    add(extra_values, has_literal ? duplicates : 0);
    add(extra_ops, duplicates); if (has_literal) add(extra_ops, duplicates);
    add(extra_uses, duplicates * (phi_count + (has_literal ? 1U : 0U)));
  }
  if (!copies) return;
  const auto grow = [&](auto& values, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max() - values.size(), Error::kResourceLimit);
    quota.Reserve(values, values.size() + count);
  };
  grow(graph.blocks, copies); grow(graph.edges, copies); grow(graph.ops, extra_ops);
  grow(graph.origins, extra_ops); grow(graph.values, extra_values); grow(graph.uses, extra_uses);
  for (std::size_t b = 1; b < blocks; ++b) {
    quota.Work(); if (!candidates[b]) continue;
    const auto candidate = *candidates[b];
    const auto original_edge = graph.blocks[b].first_edge;
    const auto original_value = candidate.literal ? graph.ops[candidate.literal->value].result : std::nullopt;
    // Keep one incoming edge on the original block, so no original block or
    // source PC becomes unreachable. Each remaining path gets a private copy.
    for (auto incoming = next[first[b]->value]; incoming; incoming = next[incoming->value]) {
      quota.Work();
      const WorkBlockId block{static_cast<std::uint32_t>(graph.blocks.size())};
      const EdgeId edge{static_cast<std::uint32_t>(graph.edges.size())};
      const OpId op{static_cast<std::uint32_t>(graph.ops.size())};
      const ValueId value{static_cast<std::uint32_t>(graph.values.size())};
      if (original_value) {
        auto definition = graph.values[original_value->value];
        definition.origin = OpResult{op, {}}; definition.first_use.reset(); graph.values.push_back(definition);
        auto constant = graph.ops[candidate.literal->value];
        auto input = graph.uses[constant.first_use.value];
        input.owner = op; input.previous.reset(); input.next.reset();
        constant.block = block; constant.order = 0; constant.result = value;
        constant.first_use = UseId{static_cast<std::uint32_t>(graph.uses.size())};
        graph.uses.push_back(input);
        const auto constant_origin = graph.origins[constant.origin.value];
        constant.origin = OriginId{static_cast<std::uint32_t>(graph.origins.size())};
        graph.origins.push_back(constant_origin); graph.ops.push_back(constant);
      }
      auto jump = graph.ops[candidate.jump.value];
      jump.block = block; jump.order = original_value ? 1 : 0; jump.first_use = UseId{static_cast<std::uint32_t>(graph.uses.size())};
      const auto jump_origin = graph.origins[jump.origin.value];
      jump.origin = OriginId{static_cast<std::uint32_t>(graph.origins.size())};
      graph.origins.push_back(jump_origin); graph.ops.push_back(jump);
      auto outgoing = graph.edges[original_edge.value]; outgoing.source = block;
      graph.edges.push_back(outgoing); graph.blocks.push_back({op, original_value ? 2U : 1U, edge, 1});
      graph.edges[incoming->value].target = block;
      for (auto use = phi_first[original_edge.value]; use; use = phi_next[use->value]) {
        quota.Work(); auto copied = graph.uses[use->value];
        copied.incoming = IncomingKey{edge}; copied.previous.reset(); copied.next.reset();
        if (const auto* operand = std::get_if<ValueId>(&copied.operand); original_value && operand && *operand == *original_value)
          copied.operand = value;
        graph.uses.push_back(copied);
      }
    }
  }
  // Rebuild occurrence links once after the batch, never per new phi input.
  for (auto& value : graph.values) { quota.Work(); value.first_use.reset(); }
  for (std::size_t i = 0; i < graph.uses.size(); ++i) {
    quota.Work(); auto& use = graph.uses[i]; use.previous.reset(); use.next.reset();
    const auto* id = std::get_if<ValueId>(&use.operand);
    if (use.life != ValueLife::kLive || !id) continue;
    auto& value = graph.values[id->value]; use.next = value.first_use;
    if (use.next) graph.uses[use.next->value].previous = UseId{static_cast<std::uint32_t>(i)};
    value.first_use = UseId{static_cast<std::uint32_t>(i)};
  }
  quota.CheckUses(graph); quota.CheckFlow(graph);
  stats.specialized_edges = copies;
}

}  // namespace

std::variant<ValueSimplifyStats, ValueSimplifyError> SimplifyValues(
    ValueProgram& graph, IrRevision expected, SemanticBudget budget) {
  if (graph.revision != expected) return Error::kStaleRevision;
  if (!graph.phases.empty()) return Error::kUnsupportedFlow;
  try {
    BatchBudget quota{budget};
    quota.Admit(1, sizeof(ValueProgram));
    quota.Admit(1, sizeof(ValueProgram));
    GraphArrays(graph, [&](const auto& items) {
      using T = typename std::decay_t<decltype(items)>::value_type;
      quota.Admit(items.capacity(), sizeof(T));
    });
    // Admit the complete original and verifier before allocating a candidate.
    // Original capacities, not sizes, remain charged while both owners coexist.
    quota.Admit(graph.uses.size(), sizeof(std::uint8_t));
    auto candidate = Clone(graph, quota);
    quota.CheckUses(candidate);
    quota.CheckFlow(candidate);
    BatchScratch scratch{candidate, quota};
    ValueSimplifyStats stats{expected};
    Canonicalize(candidate, scratch, quota, stats);
    const auto phis = SimplifyPhis(candidate, quota.Remaining(), scratch.canonical);
    if (const auto* error = std::get_if<ValueSimplifyError>(&phis)) throw *error;
    const auto& collapsed = std::get<PhiSimplifyStats>(phis);
    quota.Work(collapsed.work);
    stats.replaced_uses += collapsed.replaced_uses;
    PropagateLiterals(candidate, quota, stats);
    MarkLive(candidate, scratch, quota);
    for (std::size_t i = 0; i < candidate.ops.size(); ++i) {
      quota.Work();
      if (candidate.ops[i].life == ValueLife::kLive && !scratch.live_ops[i])
        ++stats.removed_ops;
    }
    for (std::size_t i = 0; i < candidate.values.size(); ++i) {
      quota.Work();
      const auto& value = candidate.values[i];
      if (value.life == ValueLife::kLive && !scratch.live_values[i] &&
          std::holds_alternative<PhiDefinition>(value.origin)) ++stats.removed_phis;
    }
    // Bound each pass inside EraseUnmarkedValues, including pure-op input
    // unlinking. This is a single batch, not repeated erase-to-fixpoint scans.
    quota.Work(candidate.ops.size());
    quota.Work(candidate.ops.size());
    quota.Work(candidate.values.size());
    quota.Work(candidate.uses.size());
    quota.Work(candidate.uses.size());
    quota.Work(candidate.uses.size());
    Require(!EraseUnmarkedValues(candidate, scratch.live_ops, scratch.live_values),
            Error::kInvalidGraph);
    quota.CheckUses(candidate);
    quota.CheckFlow(candidate);
    const auto rewritten = VerifyValueRewrite(graph, candidate, scratch.canonical, quota.Remaining());
    if (const auto* error = std::get_if<ValueRewriteError>(&rewritten)) {
      if (*error == ValueRewriteError::kWorkLimit) throw Error::kWorkLimit;
      if (*error == ValueRewriteError::kResourceLimit) throw Error::kResourceLimit;
      throw Error::kInvalidGraph;
    }
    quota.Work(std::get<ValueRewriteProof>(rewritten).work);
    SpecializePureTails(candidate, quota, stats);
    if (stats.specialized_edges) {
      Require(candidate.cfg_revision.value != std::numeric_limits<std::uint64_t>::max(), Error::kRevisionLimit);
      ++candidate.cfg_revision.value;
    }
    if (stats.replaced_uses == 0 && stats.removed_ops == 0 && stats.removed_phis == 0 && stats.specialized_edges == 0)
      return stats;
    Require(expected.value != std::numeric_limits<std::uint64_t>::max(),
            Error::kRevisionLimit);
    candidate.revision.value = expected.value + 1;
    stats.revision = candidate.revision;
    static_assert(std::is_nothrow_move_assignable_v<ValueProgram>);
    graph = std::move(candidate);
    return stats;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::analysis
