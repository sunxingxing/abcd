#include "parameter_expression.h"

#include <algorithm>
#include <new>

#include "cell_plan.h"
#include "tree_checks.h"
namespace abcd::source {
namespace {
using namespace analysis;
using E = ParameterExpressionError;
struct Check {
  const ValueProgram& g;
  const ParameterPlan& parameters;
  const ParameterExpressionCertificate& c;
  TreeCheckBudget q;
  ParameterExpressionFailure failure{E::kCorrespondence};
  void Need(bool ok, E e = E::kCorrespondence) {
    if (!ok) {
      failure.kind = e;
      throw failure;
    }
  }
  const OperandUse& Use(const ValueOp& op, std::uint32_t n) {
    Need(n < op.use_count && op.first_use.value + n < g.uses.size());
    const auto& use = g.uses[op.first_use.value + n];
    Need(use.life == ValueLife::kLive && use.purpose == ReadPurpose::kOperand &&
         use.owner == decltype(use.owner){OpId{
                          static_cast<std::uint32_t>(&op - g.ops.data())}} &&
         !use.incoming);
    const auto role = std::visit(
        [&](const auto& kind) -> OperandRole {
          using T = std::decay_t<decltype(kind)>;
          if constexpr (std::is_same_v<T, ValueCopy> ||
                        std::is_same_v<T, ValueUnary>)
            return OperandRole::kInput;
          else if constexpr (std::is_same_v<T, ValueBinary>)
            return n == 0 ? OperandRole::kLeft : OperandRole::kRight;
          else if constexpr (std::is_same_v<T, ValueNamedRead> ||
                             std::is_same_v<T, ValuePropertyRead>)
            return n == 0 ? OperandRole::kBase : OperandRole::kKey;
          else if constexpr (std::is_same_v<T, ValueBranch>)
            return OperandRole::kCondition;
          else if constexpr (std::is_same_v<T, ValueGuard>)
            return OperandRole::kGuardValue;
          else {
            Need(false);
            return OperandRole::kInput;
          }
        },
        op.operation);
    Need(use.role == role);
    return use;
  }
  bool Equal(const ValueOperand& a, const ValueOperand& b) {
    if (a.index() != b.index()) return false;
    return std::visit(
        [&](const auto& x) {
          using T = std::decay_t<decltype(x)>;
          const auto& y = std::get<T>(b);
          if constexpr (std::is_same_v<T, SpecialValue>)
            return x == y;
          else
            return x.value == y.value;
        },
        a);
  }
  std::vector<std::optional<std::uint32_t>> blocks, owners, entries, merged;
  std::vector<std::uint32_t> pred, uses, guards, exits, ranks;
  std::vector<std::optional<OpId>> guard_ops;
  std::vector<ParameterRegionRole> roles;
  ParameterExpressionCost Run() {
    Need(g.phases.empty(), E::kControl);
    Need(parameters.revision == g.revision &&
             parameters.cfg_revision == g.cfg_revision &&
             c.revision == g.revision && c.cfg_revision == g.cfg_revision,
         E::kRevision);
    Need(c.formals == parameters.formals.size() && !c.defaults.empty() &&
             c.length < c.formals && c.defaults.front().parameter == c.length,
         E::kParameter);
    q.Array(blocks, g.blocks.size());
    q.Array(owners, g.ops.size());
    q.Array(entries, g.values.size());
    q.Array(merged, g.values.size());
    q.Array(pred, g.blocks.size());
    q.Array(uses, g.values.size());
    q.Array(guards, g.values.size());
    q.Array(exits, g.ops.size());
    q.Array(ranks, g.ops.size());
    q.Array(roles, g.blocks.size());
    q.Array(guard_ops, g.values.size());
    for (const auto& e : g.edges) {
      q.Step();
      Need(e.source.value < blocks.size() && e.target.value < blocks.size());
      ++pred[e.target.value];
    }
    auto result = DeriveParameterRegions(g, c.defaults, q.Remaining());
    if (const auto* e = std::get_if<ParameterExpressionFailure>(&result))
      throw *e;
    const auto& regions = std::get<ParameterRegions>(result);
    q.Admit(regions.allocated_bytes, 1);
    q.Step(regions.work);
    for (const auto& r : regions.blocks) {
      q.Step();
      blocks[r.block.value] = r.parameter;
      roles[r.block.value] = r.role;
    }
    std::optional<std::uint32_t> previous;
    for (std::uint32_t i = 0; i < c.defaults.size(); ++i) {
      q.Step();
      const auto& d = c.defaults[i];
      failure.parameter = d.parameter;
      failure.operation = d.branch;
      Need(d.parameter < c.formals && (!previous || *previous < d.parameter) &&
               parameters.formals[d.parameter].entry == d.entry,
           E::kParameter);
      previous = d.parameter;
      Need(d.header.value < blocks.size() && d.arm.value < blocks.size() &&
               d.join.value < blocks.size(),
           E::kControl);
      Need(d.entry.value < entries.size() && !entries[d.entry.value]);
      entries[d.entry.value] = i;
      const auto* entry =
          std::get_if<EntryDefinition>(&g.values[d.entry.value].origin);
      const auto* p = entry ? std::get_if<ParameterEntry>(entry) : nullptr;
      Need(p && p->index == d.parameter, E::kParameter);
      if (d.merged) {
        Need(d.merged->value < merged.size() && !merged[d.merged->value]);
        merged[d.merged->value] = i;
        const auto* phi =
            std::get_if<PhiDefinition>(&g.values[d.merged->value].origin);
        Need(phi && phi->block == d.join, E::kControl);
      }
      Need(d.comparison.value < g.ops.size() && d.branch.value < g.ops.size() &&
           d.initializer.value < g.ops.size());
      const auto& test = g.ops[d.comparison.value];
      const auto& branch = g.ops[d.branch.value];
      const auto* binary = std::get_if<ValueBinary>(&test.operation);
      const auto* b = std::get_if<ValueBranch>(&branch.operation);
      Need(blocks[test.block.value] == i &&
               roles[test.block.value] == ParameterRegionRole::kTest &&
               test.result && test.use_count == 2 && binary &&
               (binary->op == BinaryOperator::kStrictEqual ||
                binary->op == BinaryOperator::kStrictNotEqual) &&
               test.normal == LanguageEffect::kNone &&
               test.before_throw == LanguageEffect::kNone &&
               test.exceptional == ExceptionalCompletion::kImpossible,
           E::kControl);
      ValueOperand undefined = SpecialValue::kUndefined;
      if (d.undefined) {
        Need(d.undefined->value < g.ops.size());
        const auto& op = g.ops[d.undefined->value];
        Need(blocks[op.block.value] == i &&
                 roles[op.block.value] == ParameterRegionRole::kTest &&
                 op.result && op.use_count == 1 &&
                 std::holds_alternative<ValueCopy>(op.operation) &&
                 Equal(Use(op, 0).operand, undefined),
             E::kControl);
        undefined = *op.result;
      }
      Need((Equal(Use(test, 0).operand, d.entry) &&
            Equal(Use(test, 1).operand, undefined)) ||
               (Equal(Use(test, 1).operand, d.entry) &&
                Equal(Use(test, 0).operand, undefined)),
           E::kControl);
      Need(branch.block == d.header && branch.use_count == 1 && b &&
               (b->predicate == UnaryOperator::kTrueOrNonzeroNumber ||
                b->predicate == UnaryOperator::kFalseOrZero) &&
               Equal(Use(branch, 0).operand, *test.result) &&
               branch.normal == LanguageEffect::kNone &&
               branch.before_throw == LanguageEffect::kNone &&
               branch.exceptional == ExceptionalCompletion::kImpossible,
           E::kControl);
      const bool taken = (binary->op == BinaryOperator::kStrictEqual) ==
                         (b->predicate == UnaryOperator::kTrueOrNonzeroNumber);
      Need(
          g.edges[d.missing.value].kind ==
                  (taken ? FlowEdgeKind::kTaken : FlowEdgeKind::kFallthrough) &&
              g.edges[d.present.value].kind ==
                  (taken ? FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken),
          E::kControl);
      const auto& root = g.ops[d.initializer.value];
      if (d.input) {
        Need(d.initializer == d.branch && d.merged &&
                 d.input->value < g.uses.size(),
             E::kExpression);
        const auto& u = g.uses[d.input->value];
        const auto* edge =
            u.incoming ? std::get_if<EdgeId>(&*u.incoming) : nullptr;
        Need(u.owner == decltype(u.owner){*d.merged} &&
                 u.role == OperandRole::kPhiInput && edge && *edge == d.merge &&
                 Equal(u.operand, d.fallback),
             E::kExpression);
        const auto* v = std::get_if<ValueId>(&d.fallback);
        Need(v && v->value < g.values.size(), E::kScope);
        std::optional<std::uint32_t> ordinal;
        if (const auto* e =
                std::get_if<EntryDefinition>(&g.values[v->value].origin))
          if (const auto* p = std::get_if<ParameterEntry>(e))
            ordinal = p->index;
        if (merged[v->value]) ordinal = c.defaults[*merged[v->value]].parameter;
        Need(ordinal && *ordinal < d.parameter && !entries[v->value],
             E::kScope);
      } else
        Need(blocks[root.block.value] == i &&
                 roles[root.block.value] == ParameterRegionRole::kInitializer &&
                 root.result && Equal(d.fallback, *root.result),
             E::kExpression);
    }
    // One operation census reconstructs the entire owned set; certificate flags
    // cannot hide an observable prefix or an unrelated arm event.
    std::size_t event = 0;
    for (const auto& r : regions.blocks)
      for (std::uint32_t n = 0; n < g.blocks[r.block.value].op_count; ++n) {
        q.Step();
        const auto i = g.blocks[r.block.value].first_op.value + n;
        const auto& op = g.ops[i];
        if (op.life != ValueLife::kLive ||
            std::holds_alternative<ValueNop>(op.operation))
          continue;
        const auto owner = *blocks[op.block.value];
        const auto& d = c.defaults[owner];
        failure.parameter = d.parameter;
        failure.operation = OpId{i};
        Need(event < c.events.size() && c.events[event].operation == OpId{i} &&
                 c.events[event].parameter == d.parameter,
             E::kCorrespondence);
        ranks[i] = event++;
        owners[i] = owner;
        if (r.role == ParameterRegionRole::kTest) {
          Need(d.comparison == OpId{i} || d.branch == OpId{i} ||
                   d.undefined == OpId{i} ||
                   (std::holds_alternative<ValueJump>(op.operation) &&
                    op.use_count == 0 && op.normal == LanguageEffect::kNone &&
                    op.exceptional == ExceptionalCompletion::kImpossible),
               E::kOrder);
          continue;
        }
        if (d.input)
          Need(std::holds_alternative<ValueJump>(op.operation), E::kExpression);
        const bool pure = op.normal == LanguageEffect::kNone &&
                          op.before_throw == LanguageEffect::kNone &&
                          op.exceptional == ExceptionalCompletion::kImpossible;
        if (std::holds_alternative<ValueJump>(op.operation)) {
          Need(!op.result && op.use_count == 0 && pure, E::kExpression);
          continue;
        }
        if (std::holds_alternative<ValueGuard>(op.operation)) {
          Need(!op.result && op.use_count == 1 &&
                   op.normal == LanguageEffect::kGuard &&
                   op.before_throw == LanguageEffect::kGuard &&
                   op.exceptional == ExceptionalCompletion::kMayThrow,
               E::kExpression);
          const auto* v = std::get_if<ValueId>(&Use(op, 0).operand);
          Need(v && v->value < guards.size(), E::kExpression);
          ++guards[v->value];
          Need(!guard_ops[v->value], E::kExpression);
          guard_ops[v->value] = OpId{i};
          continue;
        }
        Need(op.result && op.result->value < g.values.size(), E::kExpression);
        if (std::holds_alternative<ValueCopy>(op.operation) ||
            std::holds_alternative<ValueString>(op.operation) ||
            std::holds_alternative<ValueGlobalObject>(op.operation))
          Need(pure, E::kExpression);
        else if (const auto* n = std::get_if<ValueNamedRead>(&op.operation))
          Need(n->lookup == PropertyLookup::kObject && op.use_count == 1 &&
                   op.normal == (LanguageEffect::kHeapRead |
                                 LanguageEffect::kUserCode) &&
                   op.before_throw == op.normal &&
                   op.exceptional == ExceptionalCompletion::kMayThrow,
               E::kExpression);
        else if (std::holds_alternative<ValuePropertyRead>(op.operation))
          Need(op.use_count == 2 &&
                   op.normal == (LanguageEffect::kHeapRead |
                                 LanguageEffect::kUserCode) &&
                   op.before_throw == op.normal &&
                   op.exceptional == ExceptionalCompletion::kMayThrow,
               E::kExpression);
        else if (const auto* global =
                     std::get_if<ValueGlobalRead>(&op.operation))
          Need(global->lookup == GlobalLookup::kName && op.use_count == 0 &&
                   op.normal == (LanguageEffect::kGlobalAccess |
                                 LanguageEffect::kUserCode) &&
                   op.before_throw == op.normal &&
                   op.exceptional == ExceptionalCompletion::kMayThrow,
               E::kExpression);
        else if (const auto* m = std::get_if<ValueModuleRead>(&op.operation))
          Need(op.use_count == 0 &&
                   op.normal == ModuleReadEffects(m->loading) &&
                   op.before_throw == op.normal &&
                   op.exceptional == ExceptionalCompletion::kMayThrow,
               E::kExpression);
        else if (std::holds_alternative<ValueLexicalLoad>(op.operation))
          Need(op.use_count == 0 &&
                   op.normal == (LanguageEffect::kLexicalRead |
                                 LanguageEffect::kEnvironmentRead) &&
                   op.before_throw == LanguageEffect::kNone &&
                   op.exceptional == ExceptionalCompletion::kImpossible,
               E::kExpression);
        else if (const auto* u = std::get_if<ValueUnary>(&op.operation))
          Need(op.use_count == 1 && (u->op == UnaryOperator::kNegate ||
                                     u->op == UnaryOperator::kBitNot ||
                                     u->op == UnaryOperator::kToNumber ||
                                     u->op == UnaryOperator::kLogicalNot ||
                                     u->op == UnaryOperator::kTypeOf),
               E::kExpression);
        else if (std::holds_alternative<ValueBinary>(op.operation))
          Need(op.use_count == 2, E::kExpression);
        else
          Need(false, E::kExpression);
      }
    Need(event == c.events.size());
    std::vector<std::uint32_t> phi_inputs;
    q.Array(phi_inputs, c.defaults.size());
    for (const auto& u : g.uses) {
      q.Step();
      if (u.life != ValueLife::kLive) continue;
      const auto* v = std::get_if<ValueId>(&u.operand);
      if (!v) continue;
      Need(v->value < g.values.size());
      const auto* to = std::get_if<OpId>(&u.owner);
      const auto* from = std::get_if<OpResult>(&g.values[v->value].origin);
      const bool owned =
          from && from->op.value < owners.size() && owners[from->op.value];
      const auto* phi = std::get_if<ValueId>(&u.owner);
      bool own_phi = phi && phi->value < merged.size() && merged[phi->value];
      if (own_phi) {
        const auto i = *merged[phi->value];
        const auto& d = c.defaults[i];
        const auto* edge =
            u.incoming ? std::get_if<EdgeId>(&*u.incoming) : nullptr;
        Need(edge && u.role == OperandRole::kPhiInput, E::kControl);
        const auto bit = *edge == d.present ? 1U : 2U;
        Need(!(phi_inputs[i] & bit), E::kControl);
        Need((bit == 1 && Equal(u.operand, d.entry)) ||
                 (bit == 2 && *edge == d.merge && Equal(u.operand, d.fallback)),
             E::kControl);
        phi_inputs[i] |= bit;
      }
      if (owned) {
        const auto i = *owners[from->op.value];
        const auto& d = c.defaults[i];
        const auto& producer = g.ops[from->op.value];
        if (own_phi) {
          Need(*merged[phi->value] == i && Equal(u.operand, d.fallback),
               E::kEscape);
        } else {
          Need(to && to->value < owners.size() && owners[to->value] == i,
               E::kEscape);
          const auto& target = g.ops[to->value];
          Need(ranks[from->op.value] < ranks[to->value], E::kOrder);
        }
        if (!to ||
            !std::holds_alternative<ValueGuard>(g.ops[to->value].operation))
          ++uses[v->value];
      }
      if (to && to->value < owners.size() && owners[to->value]) {
        const auto i = *owners[to->value];
        const auto& d = c.defaults[i];
        const auto& target = g.ops[to->value];
        if (roles[target.block.value] != ParameterRegionRole::kInitializer)
          continue;
        if (owned) {
          Need(roles[g.ops[from->op.value].block.value] ==
                   ParameterRegionRole::kInitializer,
               E::kScope);
          continue;
        }
        std::optional<std::uint32_t> p;
        if (const auto* entry =
                std::get_if<EntryDefinition>(&g.values[v->value].origin))
          if (const auto* parameter = std::get_if<ParameterEntry>(entry))
            p = parameter->index;
        if (merged[v->value]) p = c.defaults[*merged[v->value]].parameter;
        Need(p && *p < d.parameter, E::kScope);
        Need(!entries[v->value],
             E::kScope);  // an earlier default's raw argument is not its
                          // initialized value
      }
      if (entries[v->value]) {
        const auto i = *entries[v->value];
        Need((to && c.defaults[i].comparison == *to) ||
                 (own_phi && *merged[phi->value] == i),
             E::kEscape);
      }
    }
    for (std::size_t i = 0; i < c.defaults.size(); ++i) {
      q.Step();
      Need(phi_inputs[i] == (c.defaults[i].merged ? 3U : 0U), E::kControl);
      const auto& d = c.defaults[i];
      Need(ranks[d.comparison.value] < ranks[d.branch.value], E::kOrder);
      if (d.undefined)
        Need(ranks[d.undefined->value] < ranks[d.comparison.value], E::kOrder);
    }
    for (std::uint32_t id = 0; id < g.ops.size(); ++id) {
      q.Step();
      const auto& op = g.ops[id];
      if (!owners[id] ||
          roles[op.block.value] != ParameterRegionRole::kInitializer ||
          !op.result)
        continue;
      const auto& d = c.defaults[*owners[id]];
      failure.parameter = d.parameter;
      failure.operation = OpId{id};
      Need(uses[op.result->value] ==
               (d.merged || id != d.initializer.value ? 1U : 0U),
           E::kEscape);
      // Migration must not hide an internal/unproved arithmetic result from
      // the ordinary semantic gate. Binding loads are qualified separately by
      // their guard, and pure copies retain the operand identity.
      if(!std::holds_alternative<ValueModuleRead>(op.operation) &&
         !std::holds_alternative<ValueLexicalLoad>(op.operation) &&
         !std::holds_alternative<ValueCopy>(op.operation))
        Need(g.values[op.result->value].domain.IsLanguage(), E::kExpression);
      if (guards[op.result->value])
        Need(guards[op.result->value] == 1 &&
                 (std::holds_alternative<ValueModuleRead>(op.operation) ||
                  std::holds_alternative<ValueLexicalLoad>(op.operation)),
             E::kExpression);
    }

    // Re-derive the source evaluation order from operand edges. Each expression
    // result has one consumer; this explicit postorder stack is linear even on
    // deep or wide initializers. A reordered sibling/guard cannot borrow the
    // builder's event list merely because all definitions dominate their uses.
    struct Task {
      ValueOperand value;
      bool exit;
      std::uint32_t parameter;
    };
    std::vector<Task> tasks;
    std::vector<OpId> ordered;
    std::vector<std::uint8_t> visited;
    Need(g.ops.size() <= SIZE_MAX - g.uses.size() &&
             g.ops.size() + g.uses.size() <= SIZE_MAX - c.defaults.size(),
         E::kResourceLimit);
    const auto maximum = g.ops.size() + g.uses.size() + c.defaults.size();
    q.Admit(maximum, sizeof(Task));
    tasks.reserve(maximum);
    q.Admit(tasks.capacity() - maximum, sizeof(Task));
    q.Admit(c.events.size(), sizeof(OpId));
    ordered.reserve(c.events.size());
    q.Admit(ordered.capacity() - c.events.size(), sizeof(OpId));
    q.Array(visited, g.ops.size());
    for (auto i = c.defaults.size(); i-- > 0;) {
      q.Step();
      tasks.push_back(
          {c.defaults[i].fallback, false, static_cast<std::uint32_t>(i)});
    }
    while (!tasks.empty()) {
      q.Step();
      const auto task = tasks.back();
      tasks.pop_back();
      const auto* v = std::get_if<ValueId>(&task.value);
      if (!v) continue;
      const auto* r = std::get_if<OpResult>(&g.values[v->value].origin);
      if (!r || !owners[r->op.value] ||
          roles[g.ops[r->op.value].block.value] !=
              ParameterRegionRole::kInitializer)
        continue;
      Need(*owners[r->op.value] == task.parameter, E::kScope);
      const auto& op = g.ops[r->op.value];
      if (task.exit) {
        ordered.push_back(r->op);
        if (guard_ops[v->value]) ordered.push_back(*guard_ops[v->value]);
        continue;
      }
      Need(!visited[r->op.value], E::kEscape);
      visited[r->op.value] = 1;
      Need(tasks.size() < tasks.capacity(), E::kResourceLimit);
      tasks.push_back({task.value, true, task.parameter});
      for (auto n = op.use_count; n-- > 0;) {
        Need(tasks.size() < tasks.capacity(), E::kResourceLimit);
        tasks.push_back({Use(op, n).operand, false, task.parameter});
      }
    }
    std::size_t ordered_index = 0;
    for (const auto& e : c.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      if (roles[op.block.value] != ParameterRegionRole::kInitializer ||
          std::holds_alternative<ValueJump>(op.operation))
        continue;
      Need(ordered_index < ordered.size() &&
               ordered[ordered_index++] == e.operation,
           E::kOrder);
    }
    Need(ordered_index == ordered.size(), E::kOrder);
    bool lexical = false;
    for (const auto& e : c.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      lexical |= std::holds_alternative<ValueLexicalLoad>(op.operation);
      if (op.result &&
          (std::holds_alternative<ValueLexicalLoad>(op.operation) ||
           std::holds_alternative<ValueModuleRead>(op.operation)))
        Need(g.values[op.result->value].domain.IsLanguage() ||
                 guards[op.result->value] == 1,
             E::kScope);
    }
    if (lexical) {
      auto env = BuildValueEnvironments(g, q.Remaining());
      if (const auto* e = std::get_if<ValueEnvironmentError>(&env)) {
        Need(*e != ValueEnvironmentError::kResourceLimit, E::kResourceLimit);
        Need(*e != ValueEnvironmentError::kWorkLimit, E::kWorkLimit);
        Need(false, E::kScope);
      }
      const auto& environments = std::get<ValueEnvironments>(env);
      q.Admit(environments.allocated_bytes, 1);
      q.Step(environments.work);
      auto plan = BuildCellPlan(g, environments, q.Remaining());
      if (const auto* e = std::get_if<CellError>(&plan)) {
        Need(*e != CellError::kResourceLimit, E::kResourceLimit);
        Need(*e != CellError::kWorkLimit, E::kWorkLimit);
        Need(false, E::kScope);
      }
      const auto& cells = std::get<CellPlan>(plan);
      q.Admit(cells.allocated_bytes, 1);
      q.Step(cells.work);
      for (const auto& e : c.events) {
        q.Step();
        if (std::holds_alternative<ValueLexicalLoad>(
                g.ops[e.operation.value].operation)) {
          Need(cells.operations[e.operation.value] &&
                   std::holds_alternative<IncomingCell>(
                       cells.cells[cells.operations[e.operation.value]->value]
                           .storage),
               E::kScope);
        }
      }
    }
    std::size_t exception = 0;
    for (const auto& e : g.exceptional_exits) {
      q.Step();
      Need(e.op.value < owners.size());
      if (!owners[e.op.value]) continue;
      failure.operation = e.op;
      Need(!e.handler && e.origin == g.ops[e.op.value].origin &&
               e.before_throw == g.ops[e.op.value].before_throw,
           E::kCatchState);
      Need(exception < c.exceptions.size());
      const auto& expected = c.exceptions[exception++];
      Need(expected.op == e.op && expected.origin == e.origin &&
               expected.before_throw == e.before_throw &&
               expected.handler == e.handler,
           E::kCatchState);
      ++exits[e.op.value];
    }
    Need(exception == c.exceptions.size(), E::kCatchState);
    for (const auto& e : c.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      Need(exits[e.operation.value] ==
               (op.exceptional == ExceptionalCompletion::kImpossible ? 0U : 1U),
           E::kCatchState);
    }
    return {q.Bytes(), q.Work()};
  }
};
}  // namespace
std::variant<ParameterExpressionCost, ParameterExpressionFailure>
VerifyParameterExpressions(const analysis::ValueProgram& g,
                           const ParameterPlan& p,
                           const ParameterExpressionCertificate& c,
                           analysis::SemanticBudget b) {
  try {
    return Check{g, p, c, TreeCheckBudget{b}}.Run();
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
std::variant<ParameterExpressionProof, ParameterExpressionFailure>
ParameterExpressionProof::Build(const ValueProgram& g,
                                const ParameterPlan& parameters,
                                const DefaultCandidates& candidates,
                                std::uint32_t length, SemanticBudget b) {
  try {
    TreeCheckBudget q(b);
    ParameterExpressionProof p;
    p.evidence_.revision = g.revision;
    p.evidence_.cfg_revision = g.cfg_revision;
    p.evidence_.formals = parameters.formals.size();
    p.evidence_.length = length;
    q.Array(p.evidence_.defaults, candidates.values.size());
    std::copy(candidates.values.begin(), candidates.values.end(),
              p.evidence_.defaults.begin());
    auto regions_result =
        DeriveParameterRegions(g, candidates.values, q.Remaining());
    if (const auto* e =
            std::get_if<ParameterExpressionFailure>(&regions_result))
      return *e;
    const auto& regions = std::get<ParameterRegions>(regions_result);
    q.Admit(regions.allocated_bytes, 1);
    q.Step(regions.work);
    std::vector<std::uint8_t> moved;
    q.Array(moved, g.ops.size());
    std::size_t count = 0;
    for (const auto& r : regions.blocks)
      for (std::uint32_t n = 0; n < g.blocks[r.block.value].op_count; ++n) {
        q.Step();
        const auto& op = g.ops[g.blocks[r.block.value].first_op.value + n];
        if (op.life == ValueLife::kLive &&
            !std::holds_alternative<ValueNop>(op.operation))
          ++count;
      }
    q.Array(p.evidence_.events, count);
    count = 0;
    for (const auto& r : regions.blocks)
      for (std::uint32_t n = 0; n < g.blocks[r.block.value].op_count; ++n) {
        q.Step();
        const auto i = g.blocks[r.block.value].first_op.value + n;
        const auto& op = g.ops[i];
        if (op.life == ValueLife::kLive &&
            !std::holds_alternative<ValueNop>(op.operation)) {
          moved[i] = 1;
          p.evidence_.events[count++] = {
              OpId{i}, candidates.values[r.parameter].parameter};
        }
      }
    count = 0;
    for (const auto& e : g.exceptional_exits) {
      q.Step();
      if (moved[e.op.value]) ++count;
    }
    q.Array(p.evidence_.exceptions, count);
    count = 0;
    for (const auto& e : g.exceptional_exits) {
      q.Step();
      if (moved[e.op.value]) p.evidence_.exceptions[count++] = e;
    }

    auto checked =
        VerifyParameterExpressions(g, parameters, p.evidence_, q.Remaining());
    if (auto* e = std::get_if<ParameterExpressionFailure>(&checked)) return *e;
    auto cost = std::get<ParameterExpressionCost>(checked);
    q.Admit(cost.allocated_bytes, 1);
    q.Step(cost.work);
    p.cost_ = {q.Bytes(), q.Work()};
    return p;
  } catch (TreeStructureError e) {
    return ParameterExpressionFailure{e == TreeStructureError::kWorkLimit
                                          ? E::kWorkLimit
                                          : E::kResourceLimit};
  } catch (const std::bad_alloc&) {
    return ParameterExpressionFailure{E::kResourceLimit};
  }
}
}  // namespace abcd::source
