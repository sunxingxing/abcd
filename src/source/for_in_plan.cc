#include "for_in_plan.h"
#include "for_in_quota.h"
#include "exception_forwarding.h"
#include "loop_forest.h"
#include "../analysis/value_phi.h"
#include "abcd/analysis/dominators.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ForInError;
// A next result is Undefined or a property key, which may include the pinned
// Proxy Symbol path. Key truthiness is deliberately unknown ("" is a key).
constexpr std::uint8_t kDone = 1, kKey = 2, kAll = kDone | kKey;
enum class FactKind : std::uint8_t { kUnknown, kConstant, kKey, kTest };
struct Fact {
  FactKind kind = FactKind::kUnknown;
  SpecialValue constant = SpecialValue::kUndefined;
  OpId advance;
  std::uint8_t truth = 0;
};
struct Event {
  bool resolved = false;
  std::optional<OpId> operation;
  bool operator==(const Event&) const = default;
};
class Proof {
 public:
  Proof(const ValueProgram& graph, detail::ForInQuota& quota)
      : g(graph), q(quota), vertices(&q.memory), arcs(&q.memory), originals(&q.memory),
        roots(&q.memory), exits(&q.memory), commits(&q.memory), facts(&q.memory), inert_tests(&q.memory),
        live(&q.memory), begin(&q.memory), end(&q.memory), acquired(&q.memory),
        advances(&q.memory), decisions(&q.memory), key_loop(&q.memory) {}
  void Index() {
    q.Require(!g.blocks.empty() && g.blocks.size() < UINT32_MAX && g.ops.size() < UINT32_MAX &&
        g.edges.size() < UINT32_MAX && g.uses.size() < UINT32_MAX && g.values.size() < UINT32_MAX);
    q.Require(g.phases.empty() || g.phases.size() == g.blocks.size());
    q.memory.Observe(g.uses.size());
    const auto uses = VerifyValueUses(g, q.memory.Available());
    q.Require(!uses, uses == ValueGraphError::kResourceLimit ? Error::kResourceLimit : Error::kCorrespondence); q.Step(g.uses.size() + g.values.size() + g.ops.size());
    auto identity = AnalyzeValueIdentities(g, q.Remaining());
    if (const auto* error = std::get_if<ValueSimplifyError>(&identity))
      q.Fail(*error == ValueSimplifyError::kResourceLimit ? Error::kResourceLimit :
          *error == ValueSimplifyError::kWorkLimit ? Error::kWorkLimit : Error::kIdentity);
    ids = std::get<ValueIdentities>(std::move(identity)); q.memory.Retain(ids.allocated_bytes); q.Step(ids.work);
    commits.resize(g.ops.size());
    for (std::uint32_t b = 0; b < g.phases.size(); ++b) {
      q.Step(); const auto& phase = g.phases[b];
      if (phase.phase == ExceptionPhase::kCommit && phase.operation) {
        q.Require(b < g.blocks.size() && phase.operation->value < commits.size());
        q.Require(!commits[phase.operation->value]);
        commits[phase.operation->value] = WorkBlockId{b};
      }
    }
    exits.resize(g.ops.size());
    for (std::uint32_t e = 0; e < g.exceptional_exits.size(); ++e) {
      q.Step(); const auto& exit = g.exceptional_exits[e];
      q.Require(exit.op.value < exits.size() && !exits[exit.op.value]); exits[exit.op.value] = e;
    }
    vertices.resize(g.blocks.size()); arcs.reserve(g.edges.size()); originals.reserve(g.edges.size());
    std::pmr::vector<std::uint32_t> incoming(g.blocks.size(), 0, &q.memory);
    for (std::uint32_t b = 0; b < g.blocks.size(); ++b) {
      q.Step(); const auto& block = g.blocks[b];
      q.Require(block.first_op.value <= g.ops.size() && block.op_count <= g.ops.size() - block.first_op.value);
      q.Require(block.first_edge.value <= g.edges.size() && block.edge_count <= g.edges.size() - block.first_edge.value);
      auto& node = vertices[b]; node.first_arc = {static_cast<std::uint32_t>(arcs.size())};
      for (std::uint32_t e = 0; e < block.edge_count; ++e) {
        q.Step(); const EdgeId id{block.first_edge.value + e}; const auto& edge = g.edges[id.value];
        q.Require(edge.source.value == b && edge.target.value < g.blocks.size());
        if (edge.kind == FlowEdgeKind::kException) continue;
        q.Require(edge.kind != FlowEdgeKind::kResume, Error::kActivation);
        arcs.push_back({{b}, {edge.target.value}, edge.kind, edge.successor}); originals.push_back(id);
        ++incoming[edge.target.value]; ++node.arc_count;
      }
    }
    roots.reserve(g.blocks.size()); roots.push_back({0});
    for (std::uint32_t b = 1; b < incoming.size(); ++b) { q.Step(); if (!incoming[b]) roots.push_back({b}); }
    auto discovered = BuildLoopForest(ControlGraphView{ControlProjection{g.revision, g.cfg_revision, vertices, arcs}}, roots, q.Remaining());
    if (const auto* error = std::get_if<LoopError>(&discovered))
      q.Fail(*error == LoopError::kResourceLimit ? Error::kResourceLimit :
          *error == LoopError::kWorkLimit ? Error::kWorkLimit : Error::kAdvance);
    loops = std::get<LoopForest>(std::move(discovered)); q.memory.Retain(loops.allocated_bytes); q.Step(loops.work);
    // Full exceptional execution dominance, distinct from natural normal loops.
    std::pmr::vector<std::uint32_t> offsets(g.blocks.size() + 1, 0, &q.memory);
    std::pmr::vector<BlockId> targets(g.edges.size(), {}, &q.memory);
    for (const auto& edge : g.edges) { q.Step(); ++offsets[edge.source.value + 1]; }
    for (std::size_t b = 1; b < offsets.size(); ++b) { q.Step(); offsets[b] += offsets[b - 1]; }
    auto cursor = std::pmr::vector<std::uint32_t>{offsets, &q.memory};
    for (const auto& edge : g.edges) { q.Step(); targets[cursor[edge.source.value]++] = {edge.target.value}; }
    auto dominated = BuildDominators({offsets, targets}, {}, {q.memory.Available(), q.remaining});
    if (const auto* error = std::get_if<DominatorError>(&dominated))
      q.Fail(*error == DominatorError::kResourceLimit ? Error::kResourceLimit :
          *error == DominatorError::kWorkLimit ? Error::kWorkLimit : Error::kCorrespondence);
    dom = std::get<DominatorTree>(std::move(dominated)); q.memory.Retain(dom.work.allocated_bytes); q.Step(dom.work.steps);
    LoopIntervals(); LiveValues(); Truths();
    acquired.resize(g.ops.size()); advances.resize(g.ops.size()); decisions.resize(g.ops.size()); key_loop.resize(g.ops.size());
  }
  ValueId Canon(ValueId value) const { return ids.canonical[value.value]; }
  const ValueExceptionalExit& Exit(OpId id) {
    q.witness.operation = id; q.Require(exits[id.value].has_value(), Error::kExceptionCorrespondence);
    const auto& exit = g.exceptional_exits[*exits[id.value]];
    q.Require(exit.origin == g.ops[id.value].origin && exit.before_throw == g.ops[id.value].before_throw,
              Error::kExceptionCorrespondence);
    return exit;
  }
  WorkBlockId Success(OpId id) const {
    if (commits[id.value]) return *commits[id.value];
    const auto& op = g.ops[id.value];
    if (op.result) if (const auto* result = std::get_if<OpResult>(&g.values[op.result->value].origin))
      if (result->commit) return *result->commit;
    return op.block;
  }
  bool Dominates(WorkBlockId a, WorkBlockId b) const { return dom.Dominates({a.value}, {b.value}); }
  bool Inside(LoopId loop, WorkBlockId b) const {
    const auto owner = loops.owners[b.value];
    return owner && begin[loop.value] <= begin[owner->value] && begin[owner->value] < end[loop.value];
  }
  void LoopIntervals() {
    const auto count = loops.loops.size();
    begin.resize(count); end.resize(count);
    std::pmr::vector<std::optional<LoopId>> first(count + 1, {}, &q.memory), next(count, {}, &q.memory);
    struct Visit { LoopId loop; bool leave; };
    std::pmr::vector<Visit> stack(&q.memory); stack.reserve(count * 2);
    for (std::uint32_t l = 0; l < count; ++l) {
      q.Step(); const auto p = loops.loops[l].parent; const auto slot = p ? p->value : count;
      next[l] = first[slot]; first[slot] = LoopId{l};
    }
    for (auto root = first[count]; root; root = next[root->value]) { q.Step(); stack.push_back({*root, false}); }
    std::uint32_t stamp = 0;
    while (!stack.empty()) {
      q.Step(); const auto item = stack.back(); stack.pop_back();
      if (item.leave) { end[item.loop.value] = stamp; continue; }
      begin[item.loop.value] = stamp++; stack.push_back({item.loop, true});
      for (auto child = first[item.loop.value]; child; child = next[child->value]) { q.Step(); stack.push_back({*child, false}); }
    }
    q.Require(stamp == count);
  }
  void LiveValues() {
    // Ignore unused SSA transport without confusing it with an observable read.
    live.resize(g.values.size());
    std::pmr::vector<std::optional<UseId>> heads(g.values.size(), {}, &q.memory), next(g.uses.size(), {}, &q.memory);
    std::pmr::vector<ValueId> queue(&q.memory); queue.reserve(g.values.size());
    const auto mark = [&](ValueId value) { if (!live[value.value]) { live[value.value] = 1; queue.push_back(value); } };
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      q.Step(); const auto& use = g.uses[u]; if (use.life != ValueLife::kLive) continue;
      const auto* value = std::get_if<ValueId>(&use.operand); if (!value) continue;
      std::optional<ValueId> transport;
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) transport = *phi;
      else {
        const auto& op = g.ops[std::get<OpId>(use.owner).value];
        if (std::holds_alternative<ValueCopy>(op.operation) && detail::Inert(op)) transport = op.result;
      }
      if (transport) { next[u] = heads[transport->value]; heads[transport->value] = UseId{u}; }
      else mark(*value);
    }
    for (std::size_t i = 0; i < queue.size(); ++i) {
      q.Step(); for (auto u = heads[queue[i].value]; u; u = next[u->value]) {
        q.Step(); mark(std::get<ValueId>(g.uses[u->value].operand));
      }
    }
  }
  Fact Operand(const ValueOperand& operand) const {
    if (const auto* value = std::get_if<ValueId>(&operand)) return facts[Canon(*value).value];
    if (const auto* special = std::get_if<SpecialValue>(&operand)) return {FactKind::kConstant, *special};
    return {};
  }
  Fact Evaluate(const ValueOp& op) const {
    if (std::holds_alternative<ValueCopy>(op.operation) && op.use_count == 1) return Operand(g.uses[op.first_use.value].operand);
    if (const auto* unary = std::get_if<ValueUnary>(&op.operation); unary && op.use_count == 1) {
      auto input = Operand(g.uses[op.first_use.value].operand);
      if (input.kind != FactKind::kTest) return {};
      switch (unary->op) {
        case UnaryOperator::kLogicalNot: case UnaryOperator::kFalseOrZero: input.truth ^= kAll; return input;
        case UnaryOperator::kToBoolean: case UnaryOperator::kTrueOrNonzeroNumber: return input;
        default: return {};
      }
    }
    const auto* binary = std::get_if<ValueBinary>(&op.operation);
    if (!binary || op.use_count != 2) return {};
    const bool equal = binary->op == BinaryOperator::kEqual || binary->op == BinaryOperator::kStrictEqual;
    const bool unequal = binary->op == BinaryOperator::kNotEqual || binary->op == BinaryOperator::kStrictNotEqual;
    if (!equal && !unequal) return {};
    auto a = Operand(g.uses[op.first_use.value].operand), b = Operand(g.uses[op.first_use.value + 1].operand);
    if (a.kind == FactKind::kConstant) std::swap(a, b);
    if (b.kind != FactKind::kConstant || (b.constant != SpecialValue::kUndefined && b.constant != SpecialValue::kNull)) return {};
    if (a.kind != FactKind::kKey && a.kind != FactKind::kTest) return {};
    const bool loose = binary->op == BinaryOperator::kEqual || binary->op == BinaryOperator::kNotEqual;
    a.truth = a.kind == FactKind::kKey && (b.constant == SpecialValue::kUndefined || loose) ? kDone : 0;
    a.kind = FactKind::kTest; if (unequal) a.truth ^= kAll; return a;
  }
  void Truths() {
    facts.resize(g.values.size()); inert_tests.resize(g.ops.size());
    std::pmr::vector<std::uint32_t> pending(g.ops.size(), 0, &q.memory);
    std::pmr::vector<std::optional<UseId>> heads(g.values.size(), {}, &q.memory), next(g.uses.size(), {}, &q.memory);
    std::pmr::vector<ValueId> queue(&q.memory); queue.reserve(g.values.size());
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      q.Step(); const auto& use = g.uses[u]; if (use.life != ValueLife::kLive) continue;
      const auto* owner = std::get_if<OpId>(&use.owner); const auto* value = std::get_if<ValueId>(&use.operand);
      if (!owner || !value) continue;
      const auto root = Canon(*value); next[u] = heads[root.value]; heads[root.value] = UseId{u}; ++pending[owner->value];
    }
    std::pmr::vector<std::uint8_t> waiting(g.ops.size(), 0, &q.memory);
    for (std::uint32_t v = 0; v < g.values.size(); ++v) {
      q.Step(); if (Canon({v}) != ValueId{v}) continue;
      const auto* result = std::get_if<OpResult>(&g.values[v].origin);
      if (result) {
        const auto& op = g.ops[result->op.value];
        if (const auto* e = std::get_if<ValueEnumeration>(&op.operation); e && e->action == EnumerationAction::kNext)
          facts[v] = {FactKind::kKey, SpecialValue::kUndefined, result->op};
        else if (std::holds_alternative<ValueCopy>(op.operation) || std::holds_alternative<ValueUnary>(op.operation) ||
                 std::holds_alternative<ValueBinary>(op.operation)) {
          if (pending[result->op.value]) { waiting[result->op.value] = 1; continue; }
          facts[v] = Evaluate(op);
        }
      }
      queue.push_back({v});
    }
    for (std::size_t i = 0; i < queue.size(); ++i) {
      q.Step(); for (auto u = heads[queue[i].value]; u; u = next[u->value]) {
        q.Step(); const auto owner = std::get<OpId>(g.uses[u->value].owner); if (!waiting[owner.value]) continue;
        q.Require(pending[owner.value] != 0);
        if (--pending[owner.value] == 0) {
          waiting[owner.value] = 0; const auto& op = g.ops[owner.value];
          facts[op.result->value] = Evaluate(op); queue.push_back(*op.result);
        }
      }
    }
    for (std::uint32_t o = 0; o < g.ops.size(); ++o) {
      q.Step(); const auto& op = g.ops[o];
      // This refinement is a primitive-domain theorem, not a raw effect erasure:
      // equality with null/undefined cannot coerce String/Symbol/Undefined.
      if (op.result && facts[Canon(*op.result).value].kind == FactKind::kTest &&
          (std::holds_alternative<ValueBinary>(op.operation) || std::holds_alternative<ValueUnary>(op.operation))) inert_tests[o] = 1;
    }
  }
  std::pmr::vector<Event> Events(bool branches, std::pmr::vector<Event>& after) {
    const auto count = g.blocks.size();
    std::pmr::vector<Event> first(count, {}, &q.memory);
    std::pmr::vector<std::uint8_t> cut(count, 0, &q.memory), conflict(count, 0, &q.memory);
    std::pmr::vector<std::uint32_t> pending(count, 0, &q.memory), offsets(count + 1, 0, &q.memory);
    std::pmr::vector<WorkBlockId> preds(arcs.size(), {}, &q.memory), queue(&q.memory); queue.reserve(count);
    const auto barrier = [&](OpId id) {
      const auto& op = g.ops[id.value];
      return op.life == ValueLife::kLive && ((branches && std::holds_alternative<ValueBranch>(op.operation)) ||
          (!detail::Inert(op) && !inert_tests[id.value]));
    };
    for (const auto& edge : arcs) { q.Step(); ++pending[edge.source.value]; ++offsets[edge.target.value + 1]; }
    for (std::size_t b = 1; b < offsets.size(); ++b) { q.Step(); offsets[b] += offsets[b - 1]; }
    auto cursor = std::pmr::vector<std::uint32_t>{offsets, &q.memory};
    for (const auto& edge : arcs) { q.Step(); preds[cursor[edge.target.value]++] = {edge.source.value}; }
    for (std::uint32_t b = 0; b < count; ++b) {
      q.Step(); const auto& block = g.blocks[b];
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        q.Step(); const OpId id{block.first_op.value + n}; if (barrier(id)) { first[b] = {true, id}; cut[b] = 1; break; }
      }
      if (cut[b] || !pending[b]) { if (!cut[b]) first[b].resolved = true; queue.push_back({b}); }
    }
    for (std::size_t at = 0; at < queue.size(); ++at) {
      q.Step(); const auto b = queue[at].value;
      for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
        q.Step(); const auto pred = preds[p].value; if (cut[pred]) continue;
        q.Require(pending[pred] != 0);
        if (!first[pred].resolved) first[pred] = first[b];
        else if (first[pred] != first[b]) conflict[pred] = 1;
        if (--pending[pred] == 0) {
          if (conflict[pred]) first[pred] = {true, {}};
          queue.push_back({pred});
        }
      }
    }
    for (std::uint32_t b = 0; b < count; ++b) {
      q.Step(); if (pending[b] && !cut[b]) first[b] = {};
    }
    for (std::uint32_t b = 0; b < count; ++b) {
      q.Step(); Event tail{true, {}}; bool seen = false;
      const auto& vertex = vertices[b];
      for (std::uint32_t e = 0; e < vertex.arc_count; ++e) {
        q.Step(); const auto value = first[arcs[vertex.first_arc.value + e].target.value];
        if (!seen) { tail = value; seen = true; } else if (tail != value) tail = {true, {}};
      }
      const auto& block = g.blocks[b];
      for (auto n = block.op_count; n != 0; --n) {
        q.Step(); const OpId id{block.first_op.value + n - 1}; after[id.value] = tail;
        if (barrier(id)) tail = {true, id};
      }
    }
    return first;
  }
  ForInPlan Build() {
    Index(); ForInPlan plan; plan.revision = g.revision; plan.cfg_revision = g.cfg_revision;
    q.Reserve(plan.loops, g.ops.size()); q.Reserve(plan.keys, g.uses.size()); q.Reserve(plan.forwarding, g.ops.size());
    for (std::uint32_t o = 0; o < g.ops.size(); ++o) {
      q.Step(); const OpId id{o}; const auto& op = g.ops[o]; if (op.life != ValueLife::kLive) continue;
      const auto* enumeration = std::get_if<ValueEnumeration>(&op.operation); if (!enumeration) continue;
      q.witness = {Error::kIdentity, {}, id}; q.Require(op.result && op.use_count == 1, Error::kIdentity);
      if (enumeration->action == EnumerationAction::kAcquire) {
        q.Require(g.values[op.result->value].domain == ValueKinds::PropertyEnumerator(), Error::kIdentity);
        acquired[o] = static_cast<std::uint32_t>(plan.loops.size());
        plan.loops.push_back({id, {}, {}, *op.result, {}, op.first_use, {}, {}, {}, {}, {}});
      }
    }
    for (std::uint32_t o = 0; o < g.ops.size(); ++o) {
      q.Step(); const auto& op = g.ops[o]; const auto* enumeration = std::get_if<ValueEnumeration>(&op.operation);
      if (op.life != ValueLife::kLive || !enumeration || enumeration->action != EnumerationAction::kNext) continue;
      q.witness = {Error::kIdentity, {}, OpId{o}};
      const auto* value = std::get_if<ValueId>(&g.uses[op.first_use.value].operand); q.Require(value, Error::kIdentity);
      const auto root = Canon(*value); const auto* producer = std::get_if<OpResult>(&g.values[root.value].origin);
      q.witness.value = root;
      q.Require(producer && acquired[producer->op.value].has_value(), Error::kIdentity);
      const auto slot = *acquired[producer->op.value]; auto& loop = plan.loops[slot]; q.witness.acquisition = loop.acquisition;
      q.Require(!advances[loop.acquisition.value], Error::kAdvance);
      advances[loop.acquisition.value] = OpId{o}; loop.advance = {o}; loop.key = *op.result; key_loop[o] = slot;
    }
    std::pmr::vector<Event> after(g.ops.size(), {}, &q.memory), control_after(g.ops.size(), {}, &q.memory);
    const auto event = Events(false, after); const auto control = Events(true, control_after);
    std::pmr::vector<std::optional<std::uint32_t>> owners(loops.loops.size(), {}, &q.memory);
    for (std::uint32_t l = 0; l < plan.loops.size(); ++l) {
      q.Step(); auto& loop = plan.loops[l]; q.witness = {Error::kAdvance, loop.acquisition, loop.advance};
      q.Require(advances[loop.acquisition.value].has_value(), Error::kAdvance);
      const auto natural = loops.owners[g.ops[loop.advance.value].block.value]; q.Require(natural.has_value(), Error::kAdvance);
      q.Require(!owners[natural->value], Error::kAdvance); owners[natural->value] = l;
      loop.header = {loops.loops[natural->value].header.value};
      q.Require(!Inside(*natural, g.ops[loop.acquisition.value].block) &&
          Dominates(Success(loop.acquisition), loop.header), Error::kIdentity);
      q.Require(after[loop.acquisition.value] == Event{true, loop.advance}, Error::kAcquisitionOrder);
      q.Require(event[loop.header.value] == Event{true, loop.advance}, Error::kAdvance);
      q.Require(control_after[loop.advance.value].resolved && control_after[loop.advance.value].operation, Error::kDone);
      loop.decision = *control_after[loop.advance.value].operation;
      const auto& branch_op = g.ops[loop.decision.value]; const auto* branch = std::get_if<ValueBranch>(&branch_op.operation);
      q.witness.operation = loop.decision; q.Require(branch && branch_op.use_count == 1, Error::kDone);
      auto test = Operand(g.uses[branch_op.first_use.value].operand);
      q.Require(test.kind == FactKind::kTest && test.advance == loop.advance, Error::kDone);
      switch (branch->predicate) {
        case UnaryOperator::kFalseOrZero: case UnaryOperator::kLogicalNot: test.truth ^= kAll; break;
        case UnaryOperator::kTrueOrNonzeroNumber: case UnaryOperator::kToBoolean: break;
        default: q.Fail(Error::kDone);
      }
      q.Require(test.truth == kDone || test.truth == kKey, Error::kDone);
      decisions[loop.decision.value] = l;
      const auto& vertex = vertices[Success(loop.decision).value]; q.Require(vertex.arc_count == 2, Error::kDone);
      bool saw_taken = false, saw_fallthrough = false;
      for (std::uint32_t e = 0; e < vertex.arc_count; ++e) {
        q.Step();
        const auto a = vertex.first_arc.value + e; const auto& arc = arcs[a];
        q.Require(arc.kind == FlowEdgeKind::kTaken || arc.kind == FlowEdgeKind::kFallthrough, Error::kDone);
        const bool taken = arc.kind == FlowEdgeKind::kTaken;
        auto& seen = taken ? saw_taken : saw_fallthrough;
        q.Require(!seen, Error::kDone); seen = true;
        const bool done = taken == (test.truth == kDone);
        if (done) { loop.done_edge = originals[a]; loop.exit = {arc.target.value}; }
        else { loop.body_edge = originals[a]; loop.body = {arc.target.value}; }
      }
      q.Require(Inside(*natural, loop.body) && !Inside(*natural, loop.exit), Error::kExit);
      q.Require(Dominates(Success(loop.advance), branch_op.block), Error::kAdvance);
    }
    for (std::uint32_t e = 0; e < arcs.size(); ++e) {
      q.Step(); if (!loops.backedges[e] || !owners[loops.backedges[e]->value]) continue;
      const auto& loop = plan.loops[*owners[loops.backedges[e]->value]];
      q.witness = {Error::kAdvance, loop.acquisition, loop.advance, {}, {}, {}, originals[e]};
      q.Require(Dominates(g.ops[loop.decision.value].block, {arcs[e].source.value}), Error::kAdvance);
    }
    Uses(plan); Catches(plan); Members(plan);
    plan.allocated_bytes = q.memory.Peak(); plan.work = q.initial - q.remaining; return plan;
  }
  void Uses(ForInPlan& plan) {
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      q.Step(); const auto& use = g.uses[u]; const auto* raw = std::get_if<ValueId>(&use.operand);
      if (use.life != ValueLife::kLive || !raw || !live[raw->value]) continue;
      const auto value = Canon(*raw); const auto* result = std::get_if<OpResult>(&g.values[value.value].origin);
      q.witness = {Error::kEscape, {}, {}, value, UseId{u}};
      if (const auto* phi = std::get_if<ValueId>(&use.owner)) {
        if (!live[phi->value]) continue;
        if (Canon(*phi) == value) continue;
        if (g.values[value.value].domain == ValueKinds::PropertyEnumerator()) q.Fail(Error::kIdentity);
        if (result && key_loop[result->op.value]) {
          const auto slot=*key_loop[result->op.value];const auto& loop=plan.loops[slot];
          q.witness.acquisition=loop.acquisition;
          const auto* edge=use.incoming?std::get_if<EdgeId>(&*use.incoming):nullptr;
          q.Require(edge && edge->value<g.edges.size(),Error::kKeyBinding);
          q.witness.edge=*edge;
          const auto& arc=g.edges[edge->value];
          const auto* target=std::get_if<PhiDefinition>(&g.values[phi->value].origin);
          // A live-out phi is an ordinary source assignment. Its key input
          // must come from the successful iteration, never the exhausted next.
          q.Require(target && arc.target==target->block && arc.kind!=FlowEdgeKind::kException &&
              Dominates(loop.body,arc.source),Error::kKeyBinding);
          plan.keys.push_back({slot,{u}});
        }
        continue;
      }
      const auto owner = std::get<OpId>(use.owner); const auto& op = g.ops[owner.value]; q.witness.operation = owner;
      if (std::holds_alternative<ValueCopy>(op.operation) && detail::Inert(op)) continue;
      if (g.values[value.value].domain == ValueKinds::PropertyEnumerator()) {
        q.Require(result && acquired[result->op.value], Error::kIdentity);
        const auto& loop = plan.loops[*acquired[result->op.value]]; q.witness.acquisition = loop.acquisition;
        q.Require(owner == loop.advance && use.role == OperandRole::kInput && use.purpose == ReadPurpose::kProtocol, Error::kEscape);
      } else if (result && key_loop[result->op.value]) {
        const auto slot = *key_loop[result->op.value]; const auto& loop = plan.loops[slot]; q.witness.acquisition = loop.acquisition;
        if (inert_tests[owner.value] && op.result && facts[Canon(*op.result).value].advance == loop.advance) continue;
        q.Require(Dominates(loop.body, op.block), Error::kKeyBinding);
        plan.keys.push_back({slot, {u}});
      } else if (facts[value.value].kind == FactKind::kTest && key_loop[facts[value.value].advance.value]) {
        const auto& loop = plan.loops[*key_loop[facts[value.value].advance.value]];
        q.witness.acquisition = loop.acquisition;
        q.Require(owner == loop.decision || inert_tests[owner.value], Error::kDone);
      }
    }
  }
  void Members(ForInPlan& plan) {
    q.Reserve(plan.operations, g.ops.size()); plan.operations.resize(g.ops.size());
    q.Reserve(plan.values, g.values.size()); plan.values.resize(g.values.size());
    q.Reserve(plan.uses, g.uses.size()); plan.uses.resize(g.uses.size());
    for (std::uint32_t v = 0; v < g.values.size(); ++v) {
      q.Step(); if (g.values[v].life != ValueLife::kLive) continue;
      const auto root = Canon({v});
      const auto* result = std::get_if<OpResult>(&g.values[root.value].origin);
      if (result && acquired[result->op.value])
        plan.values[v] = ForInMember{*acquired[result->op.value], ForInRole::kEnumerator};
      else if (result && key_loop[result->op.value])
        plan.values[v] = ForInMember{*key_loop[result->op.value], ForInRole::kNextResult};
      else if (facts[root.value].kind == FactKind::kTest && key_loop[facts[root.value].advance.value])
        plan.values[v] = ForInMember{*key_loop[facts[root.value].advance.value], ForInRole::kDoneTest};
    }
    for (std::uint32_t o = 0; o < g.ops.size(); ++o) {
      q.Step(); const auto& op = g.ops[o]; if (op.life != ValueLife::kLive) continue;
      if (decisions[o]) plan.operations[o] = ForInMember{*decisions[o], ForInRole::kDecision};
      else if (op.result && (std::holds_alternative<ValueEnumeration>(op.operation) ||
          (std::holds_alternative<ValueCopy>(op.operation) && detail::Inert(op)) || inert_tests[o]))
        plan.operations[o] = plan.values[op.result->value];
    }
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      q.Step(); const auto& use = g.uses[u]; if (use.life != ValueLife::kLive) continue;
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        const auto member = plan.operations[owner->value];
        // The RHS is an ordinary observation, evaluated before acquisition.
        if (member && *owner != plan.loops[member->loop].acquisition) plan.uses[u] = member;
      } else {
        const auto phi = std::get<ValueId>(use.owner);
        if (plan.values[phi.value]) plan.uses[u] = plan.values[phi.value];
      }
    }
  }
  void Catches(ForInPlan& plan) {
    bool needed = false;
    for (const auto& loop : plan.loops) { q.Step(); if (Exit(loop.acquisition).handler) needed = true; }
    std::optional<ExceptionForwarding> forwarding;
    if (needed) {
      auto built = AnalyzeExceptionForwarding(g, q.Remaining());
      if (const auto* failure = std::get_if<ForInFailure>(&built)) throw *failure;
      forwarding = std::get<ExceptionForwarding>(std::move(built));
      q.memory.Retain(forwarding->allocated_bytes); q.Step(forwarding->work);
    }
    std::pmr::vector<std::optional<ValueId>> catches(g.blocks.size(), {}, &q.memory);
    for (std::uint32_t v = 0; v < g.values.size(); ++v) {
      q.Step(); if (const auto* entry = std::get_if<CatchDefinition>(&g.values[v].origin)) catches[entry->block.value] = ValueId{v};
    }
    for (const auto& loop : plan.loops) {
      q.Step(); q.witness = {Error::kCatchState, loop.acquisition, loop.advance};
      // First increment has no new catch staging. Handled advance/RHS states
      // need a tree-state correspondence proof, not destination equality alone.
      q.Require(!Exit(loop.advance).handler, Error::kCatchState);
      const auto& exit = Exit(loop.acquisition); if (!exit.handler) continue;
      const auto entry = exit.handler->entry; q.witness.block = entry;
      q.Require(entry.value < catches.size() && catches[entry.value], Error::kExceptionCorrespondence);
      const auto& summary = forwarding->blocks[entry.value];
      q.Require(summary && summary->payload == Canon(*catches[entry.value]) && !summary->destination,
                Error::kExceptionCorrespondence);
      plan.forwarding.push_back({loop.acquisition, entry, *catches[entry.value]});
    }
  }
  const ValueProgram& g;
  detail::ForInQuota& q;
  ValueIdentities ids;
  LoopForest loops;
  DominatorTree dom;
  std::pmr::vector<ControlVertex> vertices;
  std::pmr::vector<ControlLink> arcs;
  std::pmr::vector<EdgeId> originals;
  std::pmr::vector<ControlNodeId> roots;
  std::pmr::vector<std::optional<std::uint32_t>> exits;
  std::pmr::vector<std::optional<WorkBlockId>> commits;
  std::pmr::vector<Fact> facts;
  std::pmr::vector<std::uint8_t> inert_tests, live;
  std::pmr::vector<std::uint32_t> begin, end;
  std::pmr::vector<std::optional<std::uint32_t>> acquired;
  std::pmr::vector<std::optional<OpId>> advances;
  std::pmr::vector<std::optional<std::uint32_t>> decisions, key_loop;
};
}
std::variant<ForInPlan, ForInFailure> BuildForInPlan(const ValueProgram& graph, SemanticBudget budget) {
  detail::ForInQuota quota{budget};
  try { return Proof(graph, quota).Build(); }
  catch (ForInFailure failure) { return failure; }
  catch (const std::bad_alloc&) { quota.witness.kind = Error::kResourceLimit; return quota.witness; }
  catch (const std::length_error&) { quota.witness.kind = Error::kResourceLimit; return quota.witness; }
}
std::variant<std::uint64_t, ForInFailure> VerifyForInPlan(
    const ValueProgram& graph, const ForInPlan& plan, SemanticBudget budget) {
  // Re-derive raw obligations from the immutable graph; no flags in the supplied
  // certificate grant identity, scope, termination or exception correspondence.
  // This raw-plan check does not replace independent tree/recovery verification.
  detail::ForInQuota quota{budget};
  try {
    quota.Require(plan.revision == graph.revision && plan.cfg_revision == graph.cfg_revision);
    auto checked = Proof(graph, quota).Build();
    quota.Step(plan.loops.size() + plan.keys.size() + plan.forwarding.size() + plan.operations.size() + plan.values.size() + plan.uses.size());
    quota.Require(plan.operations == checked.operations && plan.values == checked.values && plan.uses == checked.uses);
    quota.Require(plan.loops == checked.loops && plan.keys == checked.keys && plan.forwarding == checked.forwarding);
    return quota.initial - quota.remaining;
  } catch (ForInFailure failure) { return failure; }
  catch (const std::bad_alloc&) { quota.witness.kind = Error::kResourceLimit; return quota.witness; }
  catch (const std::length_error&) { quota.witness.kind = Error::kResourceLimit; return quota.witness; }
}
}  // namespace abcd::source
