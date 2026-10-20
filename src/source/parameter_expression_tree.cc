#include "parameter_expression_tree.h"

#include <bit>

#include "default_signature.h"
#include "module_aliases.h"
#include "recovery_scratch.h"
#include "tree_checks.h"
#include "tree_flow.h"
namespace abcd::source {
namespace {
using namespace analysis;
void Need(bool ok) { RequireTree(ok, TreeStructureError::kOwnership); }
template <class T>
void Need(const std::optional<T>& v) {
  Need(v.has_value());
}
template <class T>
void Need(const std::shared_ptr<T>& v) {
  Need(static_cast<bool>(v));
}
template <class T, class E>
void Result(const std::variant<T, E>& value, E resource, E work) {
  if (const auto* e = std::get_if<E>(&value))
    throw *e == resource ? TreeStructureError::kResourceLimit
    : *e == work         ? TreeStructureError::kWorkLimit
                         : TreeStructureError::kOwnership;
}
TreeLiteral Literal(const ValueOperand& v) {
  if (const auto* n = std::get_if<analysis::IntegerLiteral>(&v))
    return IntegerLiteral{n->value};
  if (const auto* n = std::get_if<Float64Bits>(&v))
    return NumberLiteral{n->value};
  const auto* s = std::get_if<SpecialValue>(&v);
  Need(s);
  switch (*s) {
    case SpecialValue::kUndefined:
      return UndefinedLiteral{};
    case SpecialValue::kNull:
      return NullLiteral{};
    case SpecialValue::kTrue:
      return BooleanLiteral{true};
    case SpecialValue::kFalse:
      return BooleanLiteral{false};
    default:
      Need(false);
      return UndefinedLiteral{};
  }
}
bool SameLiteral(const TreeExpression& node, const ValueOperand& v) {
  const auto* actual = std::get_if<TreeLiteral>(&node);
  if (!actual) return false;
  const auto expected = Literal(v);
  if (actual->index() != expected.index()) return false;
  return std::visit(
      [&](const auto& a) {
        using T = std::decay_t<decltype(a)>;
        const auto& b = std::get<T>(expected);
        if constexpr (requires { a.value; })
          return a.value == b.value;
        else if constexpr (requires { a.bits; })
          return a.bits == b.bits;
        else
          return true;
      },
      *actual);
}
TreeUnary Unary(UnaryOperator u) {
  switch (u) {
    case UnaryOperator::kNegate:
      return TreeUnary::kNegate;
    case UnaryOperator::kBitNot:
      return TreeUnary::kBitNot;
    case UnaryOperator::kToNumber:
      return TreeUnary::kPlus;
    case UnaryOperator::kLogicalNot:
      return TreeUnary::kNot;
    case UnaryOperator::kTypeOf:
      return TreeUnary::kTypeof;
    default:
      Need(false);
      return TreeUnary::kNot;
  }
}
template <class T>
void Reserve(std::vector<T>& v, std::size_t count, TreeCheckBudget& q) {
  if (count <= v.capacity()) return;
  Need(v.capacity() <= SIZE_MAX / 2);
  count = std::max(count, v.capacity() * 2);
  q.Admit(count, sizeof(T));
  q.Step(v.size());
  v.reserve(count);
  q.Admit(v.capacity() - count, sizeof(T));
}
}  // namespace
class ParameterTreeBuilder {
  const DefaultBody& body;
  FunctionTree& tree;
  const DefaultSignatureContext& context;
  TreeCheckBudget& q;
  std::shared_ptr<ParameterExpressionSource> source;
  std::vector<std::optional<ExprId>> values;
  std::vector<std::optional<std::uint32_t>> formals;
  std::vector<ModuleEvaluation> modules;
  std::vector<std::pair<ModuleSymbol, SymbolId>> aliases;
  ExprId Add(TreeExpression expression, OriginId origin) {
    Need(tree.expressions.size() < UINT32_MAX);
    Reserve(tree.expressions, tree.expressions.size() + 1, q);
    ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {origin.value}});
    return id;
  }
  ExprId Operand(const ValueOperand& operand, OriginId origin) {
    if (const auto* v = std::get_if<ValueId>(&operand)) {
      Need(v->value < values.size());
      if (values[v->value]) return *values[v->value];
      Need(formals[v->value] && *formals[v->value] < tree.parameters.size());
      return Add(SymbolExpr{tree.parameters[*formals[v->value]].symbol},
                 origin);
    }
    return Add(Literal(operand), origin);
  }
  NameId Name(OpId op) {
    Need(context.constants && op.value < context.constants->operations.size());
    const auto* name =
        std::get_if<NameId>(&context.constants->operations[op.value]);
    Need(name && name->value < tree.names.size());
    q.Admit(tree.names[name->value].size() + 1, sizeof(char16_t));
    source->names_[op.value] = tree.names[name->value];
    return *name;
  }

 public:
  ParameterTreeBuilder(const DefaultBody& b, FunctionTree& t,
                       const DefaultSignatureContext& c, TreeCheckBudget& quota)
      : body(b), tree(t), context(c), q(quota) {}
  void Run() {
    Need(body.Expressions() && body.CellParameterOriginal() &&
         !tree.parameter_expressions && tree.signature.empty());
    q.Admit(1, sizeof(ParameterExpressionSource));
    source = std::shared_ptr<ParameterExpressionSource>(
        new ParameterExpressionSource,
        std::default_delete<ParameterExpressionSource>{},
        RecoveryAllocator<ParameterExpressionSource>{q});
    source->input_ = body.CellParameterOriginal();
    const auto& g = source->Input();
    const auto& proof = body.Expressions()->Evidence();
    auto parameter = BuildParameterPlan(g, proof.formals, q.Remaining());
    Result(parameter, ParameterPlanError::kResourceLimit,
           ParameterPlanError::kWorkLimit);
    const auto& p = std::get<ParameterPlan>(parameter);
    q.Admit(p.allocated_bytes, 1);
    q.Step(p.work);
    auto verified = VerifyParameterExpressions(g, p, proof, q.Remaining());
    if (const auto* e = std::get_if<ParameterExpressionFailure>(&verified))
      throw e->kind == ParameterExpressionError::kResourceLimit
          ? TreeStructureError::kResourceLimit
      : e->kind == ParameterExpressionError::kWorkLimit
          ? TreeStructureError::kWorkLimit
          : TreeStructureError::kOwnership;
    const auto cost = std::get<ParameterExpressionCost>(verified);
    q.Admit(cost.allocated_bytes, 1);
    q.Step(cost.work);
    auto& c = source->certificate_;
    c.revision = proof.revision;
    c.cfg_revision = proof.cfg_revision;
    c.length = proof.length;
    c.formals = proof.formals;
    const auto clone = [&](auto& to, const auto& from) {
      q.Array(to, from.size());
      std::copy(from.begin(), from.end(), to.begin());
    };
    clone(c.defaults, proof.defaults);
    clone(c.events, proof.events);
    clone(c.exceptions, proof.exceptions);
    q.Array(source->operations_, g.ops.size());
    q.Array(source->roots_, tree.parameters.size());
    q.Array(source->names_, g.ops.size());
    q.Array(source->bindings_, g.ops.size());
    q.Array(values, g.values.size());
    q.Array(formals, g.values.size());
    for (std::uint32_t i = 0; i < p.formals.size(); ++i) {
      q.Step();
      if (p.formals[i].entry) formals[p.formals[i].entry->value] = i;
    }
    for (const auto& d : proof.defaults) {
      q.Step();
      if (d.merged) formals[d.merged->value] = d.parameter;
    }
    std::vector<std::optional<SymbolId>> captures;
    if (!body.SignatureCaptures().empty()) {
      auto flow = VerifyTreeFlow(body.Graph(), context.control,
                                 context.evaluation, tree, q.Remaining());
      Result(flow, TreeFlowError::kResourceLimit, TreeFlowError::kWorkLimit);
      const auto& f = std::get<TreeFlow>(flow);
      q.Admit(f.allocated_bytes, 1);
      q.Step(f.work);
      q.Array(captures, g.ops.size());
      for (const auto& s : body.SignatureCaptures()) {
        q.Step();
        Need(context.evaluation.cells);
        const auto cell = FindCell(context.evaluation.cells->cells, s.storage);
        Need(cell && cell->value < f.cells.size() && f.cells[cell->value]);
        captures[s.read.value] = f.cells[cell->value];
      }
    }
    if (context.modules) {
      q.Array(modules, context.modules->signature_evaluations.size());
      std::copy(context.modules->signature_evaluations.begin(),
                context.modules->signature_evaluations.end(), modules.begin());
      std::sort(modules.begin(), modules.end(),
                [&](const auto& a, const auto& b) {
                  q.Step();
                  return a.pair.read < b.pair.read;
                });
    }
    q.Array(aliases, tree.modules.size());
    for (std::size_t i = 0; i < aliases.size(); ++i) {
      q.Step();
      aliases[i] = {tree.modules[i].target, tree.modules[i].symbol};
    }
    std::sort(aliases.begin(), aliases.end(),
              [&](const auto& a, const auto& b) {
                q.Step();
                return a.first < b.first;
              });
    // Already ordered original events are a topological expression schedule. No
    // recursive subtree copying: a result has one value consumer (guards
    // apart).
    for (const auto& e : proof.events) {
      q.Step();
      const auto& op = g.ops[e.operation.value];
      const auto& d =
          proof.defaults[std::lower_bound(proof.defaults.begin(),
                                          proof.defaults.end(), e.parameter,
                                          [&](const auto& a, auto p) {
                                            q.Step();
                                            return a.parameter < p;
                                          }) -
                         proof.defaults.begin()];
      if (e.operation == d.comparison || e.operation == d.branch ||
          e.operation == d.undefined ||
          std::holds_alternative<ValueJump>(op.operation))
        continue;
      const auto use = [&](std::uint32_t n) -> const ValueOperand& {
        Need(n < op.use_count);
        return g.uses[op.first_use.value + n].operand;
      };
      if (std::holds_alternative<ValueGuard>(op.operation)) {
        const auto* value = std::get_if<ValueId>(&use(0));
        Need(value && values[value->value]);
        source->operations_[e.operation.value] = values[value->value];
        continue;
      }
      TreeExpression node;
      if (std::holds_alternative<ValueCopy>(op.operation)) {
        source->operations_[e.operation.value] = Operand(use(0), op.origin);
        Need(op.result);
        values[op.result->value] = source->operations_[e.operation.value];
        continue;
      } else if (std::holds_alternative<ValueString>(op.operation)) {
        Need(context.constants);
        const auto* text = std::get_if<TextId>(
            &context.constants->operations[e.operation.value]);
        Need(text && text->value < tree.texts.size());
        node = TreeLiteral{*text};
        q.Admit(tree.texts[text->value].size() + 1, sizeof(char16_t));
        source->names_[e.operation.value] = tree.texts[text->value];
      } else if (const auto* read = std::get_if<ValueNamedRead>(&op.operation))
        node = MemberExpr{Operand(use(0), op.origin), Name(e.operation),
                          read->lookup};
      else if (std::holds_alternative<ValuePropertyRead>(op.operation))
        node = MemberExpr{Operand(use(0), op.origin),
                          ComputedKey{Operand(use(1), op.origin)}};
      else if (const auto* unary = std::get_if<ValueUnary>(&op.operation))
        node = UnaryExpr{Unary(unary->op), Operand(use(0), op.origin)};
      else if (const auto* binary = std::get_if<ValueBinary>(&op.operation))
        node = BinaryExpr{binary->op, Operand(use(0), op.origin),
                          Operand(use(1), op.origin)};
      else if (std::holds_alternative<ValueGlobalRead>(op.operation))
        node = GlobalNameExpr{Name(e.operation)};
      else if (std::holds_alternative<ValueGlobalObject>(op.operation))
        node = RealmObjectExpr{};
      else if (const auto* module =
                   std::get_if<ValueModuleRead>(&op.operation)) {
        Need(context.modules);
        const auto binding =
            std::lower_bound(modules.begin(), modules.end(), e.operation,
                             [&](const auto& m, auto op) {
                               q.Step();
                               return m.pair.read < op;
                             });
        Need(binding != modules.end() && binding->pair.read == e.operation);
        const auto symbol =
            std::lower_bound(aliases.begin(), aliases.end(), binding->target,
                             [&](const auto& m, auto target) {
                               q.Step();
                               return m.first < target;
                             });
        Need(symbol != aliases.end() && symbol->first == binding->target);
        source->bindings_[e.operation.value] = symbol->second;
        node = SymbolExpr{symbol->second, module->loading, module->domain};
      } else if (std::holds_alternative<ValueLexicalLoad>(op.operation)) {
        Need(e.operation.value < captures.size() &&
             captures[e.operation.value]);
        source->bindings_[e.operation.value] = captures[e.operation.value];
        node = SymbolExpr{*captures[e.operation.value]};
      } else {
        Need(false);
      }
      const auto id = Add(std::move(node), op.origin);
      source->operations_[e.operation.value] = id;
      Need(op.result);
      values[op.result->value] = id;
    }
    for (const auto& d : proof.defaults) {
      q.Step();
      if (d.input)
        source->roots_[d.parameter] =
            Operand(d.fallback, g.ops[d.branch.value].origin);
      else {
        Need(source->operations_[d.initializer.value]);
        source->roots_[d.parameter] = source->operations_[d.initializer.value];
      }
      tree.parameters[d.parameter].initializer = source->roots_[d.parameter];
    }
    Reserve(tree.signature, proof.events.size(), q);
    for (const auto& e : proof.events) {
      q.Step();
      tree.signature.push_back({e.operation,
                                {e.parameter},
                                {g.ops[e.operation.value].origin.value}});
    }
    tree.parameter_expressions = std::move(source);
    VerifyParameterExpressionTree(tree, q);
  }
};
void BuildParameterExpressionSignature(const DefaultBody& b, FunctionTree& t,
                                       const DefaultSignatureContext& c,
                                       TreeCheckBudget& q) {
  ParameterTreeBuilder{b, t, c, q}.Run();
}
void VerifyParameterExpressionTree(const FunctionTree& tree,
                                   TreeCheckBudget& q) {
  Need(tree.parameter_expressions);
  Need(tree.strictness == Strictness::kStrict &&
       !IsAsyncCallable(tree.callable) && !IsGeneratorCallable(tree.callable));
  const auto& s = *tree.parameter_expressions;
  const auto& g = s.Input();
  const auto& c = s.Certificate();
  Need(tree.value_revision.value == g.revision.value + 1 &&
       tree.cfg_revision.value == g.cfg_revision.value + 1 &&
       tree.parameters.size() == c.formals);
  auto parameters = BuildParameterPlan(g, c.formals, q.Remaining());
  Result(parameters, ParameterPlanError::kResourceLimit,
         ParameterPlanError::kWorkLimit);
  const auto& p = std::get<ParameterPlan>(parameters);
  q.Admit(p.allocated_bytes, 1);
  q.Step(p.work);
  auto raw = VerifyParameterExpressions(g, p, c, q.Remaining());
  if (auto* e = std::get_if<ParameterExpressionFailure>(&raw))
    throw e->kind == ParameterExpressionError::kResourceLimit
        ? TreeStructureError::kResourceLimit
    : e->kind == ParameterExpressionError::kWorkLimit
        ? TreeStructureError::kWorkLimit
        : TreeStructureError::kOwnership;
  const auto cost = std::get<ParameterExpressionCost>(raw);
  q.Admit(cost.allocated_bytes, 1);
  q.Step(cost.work);
  Need(s.Roots().size() == tree.parameters.size() &&
       s.Operations().size() == g.ops.size() &&
       s.Names().size() == g.ops.size() &&
       s.Bindings().size() == g.ops.size() &&
       tree.signature.size() == c.events.size());
  for (std::uint32_t i = 0; i < tree.parameters.size(); ++i) {
    q.Step();
    Need(!tree.parameters[i].rest &&
         tree.parameters[i].initializer == s.Roots()[i]);
  }
  std::vector<std::optional<std::uint32_t>> formals;
  std::vector<ModuleEvaluation> modules;
  std::vector<std::pair<ModuleSymbol, SymbolId>> aliases;
  q.Array(formals, g.values.size());
  for (std::uint32_t i = 0; i < p.formals.size(); ++i) {
    q.Step();
    if (p.formals[i].entry) formals[p.formals[i].entry->value] = i;
  }
  for (const auto& d : c.defaults) {
    q.Step();
    if (d.merged) formals[d.merged->value] = d.parameter;
  }
  struct Task {
    ValueOperand value;
    ExprId expression;
    std::uint32_t parameter;
    bool finish = false;
  };
  std::vector<Task> tasks;
  Need(c.defaults.size() <= SIZE_MAX &&
       tree.expressions.size() <= (SIZE_MAX - c.defaults.size()) / 2);
  Reserve(tasks, tree.expressions.size() * 2 + c.defaults.size(), q);
  std::vector<std::uint8_t> seen;
  q.Array(seen, tree.expressions.size());
  std::vector<std::optional<ExprId>> observed;
  q.Array(observed, g.ops.size());
  std::vector<OpId> order;
  Reserve(order, c.events.size(), q);
  std::vector<std::optional<OpId>> guard;
  q.Array(guard, g.ops.size());
  for (const auto& e : c.events) {
    q.Step();
    const auto& op = g.ops[e.operation.value];
    if (std::holds_alternative<ValueGuard>(op.operation)) {
      const auto* v = std::get_if<ValueId>(&g.uses[op.first_use.value].operand);
      Need(v);
      const auto* from = std::get_if<OpResult>(&g.values[v->value].origin);
      Need(from && !guard[from->op.value]);
      guard[from->op.value] = e.operation;
    }
  }
  for (auto i = c.defaults.size(); i-- > 0;) {
    const auto& d = c.defaults[i];
    q.Step();
    Need(s.Roots()[d.parameter]);
    tasks.push_back({d.fallback, *s.Roots()[d.parameter], d.parameter});
  }
  while (!tasks.empty()) {
    q.Step();
    const auto task = tasks.back();
    tasks.pop_back();
    Need(task.expression.value < tree.expressions.size());
    if (task.finish) {
      const auto& v = std::get<ValueId>(task.value);
      const auto op = std::get<OpResult>(g.values[v.value].origin).op;
      order.push_back(op);
      if (guard[op.value]) order.push_back(*guard[op.value]);
      continue;
    }
    Need(!seen[task.expression.value]);
    seen[task.expression.value] = 1;
    const auto& node = tree.expressions[task.expression.value];
    const auto& syntax = node.expression;
    const auto* id = std::get_if<ValueId>(&task.value);
    if (!id) {
      Need(SameLiteral(syntax, task.value));
      continue;
    }
    Need(id->value < g.values.size());
    if (formals[id->value]) {
      const auto ordinal = *formals[id->value];
      Need(ordinal < task.parameter);
      const auto* symbol = std::get_if<SymbolExpr>(&syntax);
      Need(symbol && symbol->symbol == tree.parameters[ordinal].symbol);
      continue;
    }
    const auto* result = std::get_if<OpResult>(&g.values[id->value].origin);
    Need(result && result->op.value < g.ops.size());
    const auto opid = result->op;
    const auto& op = g.ops[opid.value];
    Need(!observed[opid.value] &&
         s.Operations()[opid.value] == task.expression &&
         node.origin.value == op.origin.value);
    observed[opid.value] = task.expression;
    Need(tasks.size() < tasks.capacity());
    tasks.push_back({task.value, task.expression, task.parameter, true});
    const auto child = [&](std::uint32_t n, ExprId expr) {
      Need(n < op.use_count && tasks.size() < tasks.capacity());
      tasks.push_back(
          {g.uses[op.first_use.value + n].operand, expr, task.parameter});
    };
    const auto name = [&](NameId actual) {
      Need(s.Names()[opid.value] && actual.value < tree.names.size());
      q.Step(tree.names[actual.value].size());
      Need(tree.names[actual.value] == *s.Names()[opid.value]);
    };
    if (std::holds_alternative<ValueCopy>(op.operation)) {
      Need(op.use_count == 1);
      const auto& input = g.uses[op.first_use.value].operand;
      if (const auto* v = std::get_if<ValueId>(&input)) {
        Need(formals[v->value] && *formals[v->value] < task.parameter);
        const auto* actual = std::get_if<SymbolExpr>(&syntax);
        Need(actual &&
             actual->symbol == tree.parameters[*formals[v->value]].symbol);
      } else
        Need(SameLiteral(syntax, input));
    } else if (std::holds_alternative<ValueString>(op.operation)) {
      const auto* lit = std::get_if<TreeLiteral>(&syntax);
      const auto* text = lit ? std::get_if<TextId>(lit) : nullptr;
      Need(text && text->value < tree.texts.size() && s.Names()[opid.value] &&
           tree.texts[text->value] == *s.Names()[opid.value]);
    } else if (const auto* read = std::get_if<ValueNamedRead>(&op.operation)) {
      const auto* actual = std::get_if<MemberExpr>(&syntax);
      Need(actual && actual->lookup == read->lookup);
      const auto* key = std::get_if<NameId>(&actual->key);
      Need(key);
      name(*key);
      child(0, actual->base);
    } else if (std::holds_alternative<ValuePropertyRead>(op.operation)) {
      const auto* actual = std::get_if<MemberExpr>(&syntax);
      Need(actual && actual->lookup == PropertyLookup::kObject);
      const auto* key = std::get_if<ComputedKey>(&actual->key);
      Need(key);
      child(1, key->expression);
      child(0, actual->base);
    } else if (const auto* unary = std::get_if<ValueUnary>(&op.operation)) {
      const auto* actual = std::get_if<UnaryExpr>(&syntax);
      Need(actual && actual->operation == Unary(unary->op));
      child(0, actual->operand);
    } else if (const auto* binary = std::get_if<ValueBinary>(&op.operation)) {
      const auto* actual = std::get_if<BinaryExpr>(&syntax);
      Need(actual && actual->operation == binary->op);
      child(1, actual->right);
      child(0, actual->left);
    } else if (std::holds_alternative<ValueGlobalRead>(op.operation)) {
      const auto* actual = std::get_if<GlobalNameExpr>(&syntax);
      Need(actual);
      name(actual->name);
    } else if (std::holds_alternative<ValueGlobalObject>(op.operation))
      Need(std::holds_alternative<RealmObjectExpr>(syntax));
    else if (std::holds_alternative<ValueModuleRead>(op.operation) ||
             std::holds_alternative<ValueLexicalLoad>(op.operation)) {
      const auto* actual = std::get_if<SymbolExpr>(&syntax);
      Need(actual && s.Bindings()[opid.value] == actual->symbol &&
           actual->symbol.value < tree.symbols.size());
      const auto& binding = tree.symbols[actual->symbol.value];
      if (const auto* module = std::get_if<ValueModuleRead>(&op.operation))
        Need(binding.kind == BindingKind::kModule &&
             actual->loading == module->loading &&
             actual->domain == module->domain);
      else
        Need(binding.kind == BindingKind::kCapture);
    } else
      Need(false);
  }
  std::size_t next = 0;
  for (const auto& e : c.events) {
    q.Step();
    const auto& op = g.ops[e.operation.value];
    if (s.Operations()[e.operation.value]) {
      Need(next < order.size() && order[next++] == e.operation);
    }
  }
  Need(next == order.size());
  for (std::size_t i = 0; i < c.events.size(); ++i) {
    q.Step();
    const auto& e = c.events[i];
    const auto& placement = tree.signature[i];
    Need(placement.operation == e.operation &&
         placement.parameter.value == e.parameter &&
         placement.origin.value == g.ops[e.operation.value].origin.value);
    const auto& op = g.ops[e.operation.value];
    if (std::holds_alternative<ValueGuard>(op.operation)) {
      const auto* value =
          std::get_if<ValueId>(&g.uses[op.first_use.value].operand);
      Need(value);
      const auto* from = std::get_if<OpResult>(&g.values[value->value].origin);
      Need(from && observed[from->op.value] &&
           s.Operations()[e.operation.value] == observed[from->op.value]);
    } else if (s.Operations()[e.operation.value])
      Need(observed[e.operation.value] == s.Operations()[e.operation.value]);
  }
}
}  // namespace abcd::source
