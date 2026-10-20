#include "../../../src/source/parameter_expression.h"

#include <cstdlib>
#include <iostream>
#include <source_location>

#include "../../../src/analysis/value_flow.h"
#include "../../../src/source/default_body.h"
#include "../../../src/source/default_signature.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/parameter_recovery.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/tree_semantics.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool x, std::source_location at = std::source_location::current()) {
  if (!x) {
    std::cerr << at.line() << " failed\n";
    std::abort();
  }
}
template <class T, class... E>
T Take(std::variant<T, E...> x,
       std::source_location at = std::source_location::current()) {
  Check(std::holds_alternative<T>(x), at);
  return std::get<T>(std::move(x));
}
void Relink(ValueProgram& g) {
  for (auto& v : g.values) v.first_use.reset();
  std::vector<std::optional<UseId>> last(g.values.size());
  for (std::uint32_t i = 0; i < g.uses.size(); ++i) {
    auto& u = g.uses[i];
    u.previous.reset();
    u.next.reset();
    if (u.life != ValueLife::kLive) continue;
    if (auto* v = std::get_if<ValueId>(&u.operand)) {
      u.previous = last[v->value];
      if (u.previous)
        g.uses[u.previous->value].next = UseId{i};
      else
        g.values[v->value].first_use = UseId{i};
      last[v->value] = UseId{i};
    }
  }
}
ValueProgram Graph(std::uint32_t count, std::uint32_t depth = 1,
                   bool earlier = false) {
  ValueProgram g;
  for (std::uint32_t i = 0; i < count; ++i)
    g.values.push_back(
        {EntryDefinition{ParameterEntry{i}}, ValueKinds::Language()});
  ValueId last{};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto h = WorkBlockId{2 * i}, a = WorkBlockId{2 * i + 1},
               j = WorkBlockId{2 * i + 2};
    const auto first = OpId{static_cast<std::uint32_t>(g.ops.size())};
    const auto edge = EdgeId{static_cast<std::uint32_t>(g.edges.size())};
    auto op = [&](ValueOperation kind, WorkBlockId block,
                  std::initializer_list<std::pair<OperandRole, ValueOperand>>
                      operands,
                  bool result, LanguageEffect effect,
                  ExceptionalCompletion throws) {
      const auto id = OpId{static_cast<std::uint32_t>(g.ops.size())};
      const auto use = UseId{static_cast<std::uint32_t>(g.uses.size())};
      for (const auto& [r, v] : operands)
        g.uses.push_back({id, r, ReadPurpose::kOperand, v});
      std::optional<ValueId> value;
      if (result) {
        value = ValueId{static_cast<std::uint32_t>(g.values.size())};
        g.values.push_back({OpResult{id}, ValueKinds::Language()});
      }
      const auto origin =
          OriginId{static_cast<std::uint32_t>(g.origins.size())};
      g.origins.push_back({{origin.value}, {origin.value}, {}});
      g.ops.push_back({std::move(kind), block,
                       id.value - first.value - (block == a ? 2 : 0), use,
                       static_cast<std::uint32_t>(operands.size()), value,
                       effect, effect, throws, origin});
      if (throws != ExceptionalCompletion::kImpossible)
        g.exceptional_exits.push_back({id, origin, effect, {}});
      return value;
    };
    auto condition =
        op(ValueBinary{BinaryOperator::kStrictNotEqual}, h,
           {{OperandRole::kLeft, ValueId{i}},
            {OperandRole::kRight, SpecialValue::kUndefined}},
           true, LanguageEffect::kNone, ExceptionalCompletion::kImpossible);
    op(ValueBranch{UnaryOperator::kTrueOrNonzeroNumber}, h,
       {{OperandRole::kCondition, *condition}}, false, LanguageEffect::kNone,
       ExceptionalCompletion::kImpossible);
    std::optional<ValueId> value = last;
    if (!earlier || i == 0) {
      auto base = op(ValueGlobalRead{GlobalLookup::kName, {0}}, a, {}, true,
                     LanguageEffect::kGlobalAccess | LanguageEffect::kUserCode,
                     ExceptionalCompletion::kMayThrow);
      value = base;
      for (std::uint32_t k = 0; k < depth; ++k)
        value = op(ValueNamedRead{{1}}, a, {{OperandRole::kBase, *value}}, true,
                   LanguageEffect::kHeapRead | LanguageEffect::kUserCode,
                   ExceptionalCompletion::kMayThrow);
    }
    last = {static_cast<std::uint32_t>(g.values.size())};
    g.values.push_back({PhiDefinition{j}, ValueKinds::Language()});
    g.uses.push_back({last, OperandRole::kPhiInput, ReadPurpose::kOperand,
                      ValueId{i}, IncomingKey{edge}});
    g.uses.push_back({last, OperandRole::kPhiInput, ReadPurpose::kOperand,
                      *value, IncomingKey{EdgeId{edge.value + 2}}});
    g.blocks.push_back({first, 2, edge, 2});
    g.blocks.push_back({{first.value + 2},
                        earlier && i ? 0U : depth + 1,
                        {edge.value + 2},
                        1});
    g.edges.push_back({h, j, FlowEdgeKind::kTaken, 0});
    g.edges.push_back({h, a, FlowEdgeKind::kFallthrough, 1});
    g.edges.push_back({a, j, FlowEdgeKind::kFallthrough, 0});
  }
  const OpId ret{static_cast<std::uint32_t>(g.ops.size())};
  const UseId use{static_cast<std::uint32_t>(g.uses.size())};
  g.uses.push_back(
      {ret, OperandRole::kReturnValue, ReadPurpose::kOperand, last});
  g.origins.push_back({{ret.value}, {ret.value}, {}});
  g.ops.push_back({ValueReturn{},
                   {2 * count},
                   0,
                   use,
                   1,
                   {},
                   LanguageEffect::kNone,
                   LanguageEffect::kNone,
                   ExceptionalCompletion::kImpossible,
                   {ret.value}});
  g.blocks.push_back({ret, 1, {static_cast<std::uint32_t>(g.edges.size())}, 0});
  Relink(g);
  return g;
}
void Mutations() {
  auto g = Graph(2);
  auto p = Take<ParameterPlan>(BuildParameterPlan(g, 2));
  auto d = Take<DefaultCandidates>(FindParameterExpressionCandidates(g, p, 0));
  auto proof = Take<ParameterExpressionProof>(
      ParameterExpressionProof::Build(g, p, d, 0));
  const auto c = proof.Evidence();
  auto reject = [&](const auto& raw, const auto& cert) {
    Check(std::holds_alternative<ParameterExpressionFailure>(
        VerifyParameterExpressions(raw, p, cert)));
  };
  {
    auto x = c;
    x.events.pop_back();
    reject(g, x);
  }
  {
    auto x = c;
    x.events.push_back(x.events.back());
    reject(g, x);
  }
  {
    auto x = c;
    std::swap(x.events[2], x.events[3]);
    reject(g, x);
  }
  {
    auto x = c;
    x.defaults[0].parameter = 1;
    reject(g, x);
  }
  {
    auto x = c;
    x.defaults[0].fallback = c.defaults[1].fallback;
    reject(g, x);
  }
  {
    auto x = g;
    x.exceptional_exits[0].handler = ValueHandler{{0}, {4}};
    reject(x, c);
  }
  {
    auto x = g;
    x.exceptional_exits.erase(x.exceptional_exits.begin());
    reject(x, c);
  }
  {
    auto x = g;
    x.ops[3].operation = ValueNamedWrite{{1}};
    reject(x, c);
  }
  {
    auto x=g;x.values[x.ops[3].result->value].domain={};reject(x,c);
  }
  {
    auto x = g;
    x.uses[x.ops[3].first_use.value].operand = ValueId{1};
    reject(x, c);
  }
  {
    auto x = g;
    x.uses[x.ops[3].first_use.value].owner = OpId{7};
    reject(x, c);
  }
  {
    auto x = c;
    x.defaults[0].present = x.defaults[0].missing;
    reject(g, x);
  }
  {
    auto x = g;
    const auto old = x.blocks[1];
    for (auto& op : x.ops)
      if (op.block.value >= 2) ++op.block.value;
    for (auto& v : x.values) {
      if (auto* phi = std::get_if<PhiDefinition>(&v.origin))
        if (phi->block.value >= 2) ++phi->block.value;
    }
    for (auto& e : x.edges) {
      if (e.source.value >= 2) ++e.source.value;
      if (e.target.value >= 2) ++e.target.value;
    }
    for (auto& u : x.uses)
      if (u.incoming)
        if (auto* e = std::get_if<EdgeId>(&*u.incoming))
          if (e->value >= old.first_edge.value) ++e->value;
    for (auto& block : x.blocks)
      if (block.first_edge.value >= old.first_edge.value)
        ++block.first_edge.value;
    x.edges.insert(x.edges.begin() + old.first_edge.value,
                   {{1}, {2}, FlowEdgeKind::kFallthrough, 0});
    x.edges[old.first_edge.value + 1].source = {2};
    x.blocks.insert(
        x.blocks.begin() + 2,
        {{old.first_op.value + 1}, 1, {old.first_edge.value + 1}, 1});
    x.blocks[1].op_count = 1;
    x.blocks[1].first_edge = old.first_edge;
    x.ops[old.first_op.value + 1].block = {2};
    x.ops[old.first_op.value + 1].order = 0;
    auto split =
        Take<DefaultCandidates>(FindParameterExpressionCandidates(x, p, 0));
    Check(split.values.size() == 2);
    Check(std::holds_alternative<ParameterExpressionProof>(
        ParameterExpressionProof::Build(x, p, split, 0)));
    Check(std::holds_alternative<DefaultBody>(
        DefaultBody::BuildExpressions(x, p, split, 0)));
  }
}
void Recovery(bool earlier = false) {
  auto g = Graph(2, 1, earlier);
  auto p = Take<ParameterPlan>(BuildParameterPlan(g, 2));
  auto candidates =
      Take<DefaultCandidates>(FindParameterExpressionCandidates(g, p, 0));
  auto body =
      Take<DefaultBody>(DefaultBody::BuildExpressions(g, p, candidates, 0));
  auto control = Take<ControlPlan>(BuildControlPlan(body.Graph()));
  auto evaluation = Take<EvaluationPlan>(BuildEvaluationPlan(body.Graph()));
  FunctionSyntax syntax{{},
                        {1},
                        {0},
                        CallableKind::kMethod,
                        Strictness::kStrict,
                        {{}, {}},
                        {u"input", u"x"},
                        {}};
  syntax.identity.contract = SourceContract::kLanguageSourceV5;
  std::vector<ExpressionConstant> constants(g.ops.size());
  for (std::uint32_t i = 0; i < g.ops.size(); ++i) {
    if (std::holds_alternative<ValueGlobalRead>(g.ops[i].operation))
      constants[i] = NameId{0};
    if (std::holds_alternative<ValueNamedRead>(g.ops[i].operation))
      constants[i] = NameId{1};
  }
  ExpressionConstants names{constants};
  names.name_count = 2;
  auto built = Take<ConstructedFunction>(BuildFunctionTree(
      body.Graph(), control, evaluation, std::move(syntax), names));
  auto& tree = built.tree;
  DefaultSignatureContext context{control, evaluation, &names};
  Take<DefaultSignatureProof>(BuildDefaultSignature(body, tree, {}, &context));
  {
    auto other =
        Take<DefaultBody>(DefaultBody::BuildExpressions(g, p, candidates, 0));
    Check(std::get<DefaultSignatureError>(VerifyDefaultSignature(
              other, tree, {}, &context)) == DefaultSignatureError::kContext);
  }
  auto semantics = Take<TreeSemantics>(
      VerifyTreeSemantics(body.Graph(), control, evaluation, tree, names));
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.expressions.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),
      normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),
                                            ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases, literals, replacements,
                                  omitted, effects,  normal};
  projection.original = semantics.flow.original.get();
  auto flow = Take<RecoveryFlow>(BuildRecoveryFlow(tree, projection));
  {
    auto reused =
        Take<RecoveryFlow>(BuildRecoveryFlow(tree, projection, {}, flow));
    Check(reused.parameter_expression_events ==
          flow.parameter_expression_events);
  }
  const auto rejects = [&](const auto& t, const auto& f,
                           std::source_location at =
                               std::source_location::current()) {
    bool rejected = false;
    try {
      TreeCheckBudget q({});
      VerifyParameterExpressionFlow(t, f, q);
    } catch (TreeStructureError) {
      rejected = true;
    }
    Check(rejected, at);
  };
  {
    auto x = flow;
    x.parameter_expression_events.pop_back();
    rejects(tree, x);
  }
  {
    auto x = flow;
    x.parameter_expression_events.push_back(
        x.parameter_expression_events.back());
    rejects(tree, x);
  }
  {
    auto x = flow;
    x.parameter_defaults[0].initialized = x.parameter_defaults[0].provided;
    rejects(tree, x);
  }
  {
    auto x = flow;
    const auto decision = x.parameter_defaults[0].decision;
    auto edge = x.events[decision.value].outgoing;
    Check(edge.has_value());
    x.edges[*edge].to = x.parameter_defaults[0].initialized;
    rejects(tree, x);
  }
  {
    auto x = tree;
    const auto root = *x.parameters[0].initializer;
    std::get<MemberExpr>(x.expressions[root.value].expression).key = NameId{0};
    rejects(x, flow);
  }
  {
    auto x = flow;
    bool changed = false;
    for (const auto& port : x.parameter_expression_events) {
      if (port.exception_kind != ParameterExceptionPort::kCaller) continue;
      for (auto at = x.events[port.before.value].outgoing; at;
           at = x.edges[*at].outgoing)
        if (x.edges[*at].completion == FlowCompletion::kThrow) {
          x.edges[*at].to = x.parameter_defaults[0].initialized;
          changed = true;
          break;
        }
      if (changed) break;
    }
    Check(changed);
    rejects(tree, x);
  }
  if (earlier) {
    auto x = flow;
    const auto root = *tree.parameters[1].initializer;
    const auto before = x.actions[root.value]->before;
    x.events[before.value].read = tree.parameters[1].symbol;
    rejects(tree, x);
  }
  Check(std::holds_alternative<RecoveryFlow>(
      BuildRecoveryFlow(tree, projection, {flow.allocated_bytes, flow.work})));
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(
            tree, projection, {flow.allocated_bytes - 1, flow.work})) ==
        TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildRecoveryFlow(
            tree, projection, {flow.allocated_bytes, flow.work - 1})) ==
        TreeStructureError::kWorkLimit);
}
void Scale() {
  std::uint64_t previous = 0;
  for (auto n : {200U, 201U, 400U, 800U}) {
    auto g = Graph(n);
    auto p = Take<ParameterPlan>(BuildParameterPlan(g, n));
    auto d =
        Take<DefaultCandidates>(FindParameterExpressionCandidates(g, p, 0));
    Check(d.values.size() == n);
    Check(std::holds_alternative<DefaultCandidates>(
        FindParameterExpressionCandidates(g, p, 0,
                                          {d.allocated_bytes, d.work})));
    Check(std::get<DefaultCandidateError>(FindParameterExpressionCandidates(
              g, p, 0, {d.allocated_bytes - 1, d.work})) ==
          DefaultCandidateError::kResourceLimit);
    Check(std::get<DefaultCandidateError>(FindParameterExpressionCandidates(
              g, p, 0, {d.allocated_bytes, d.work - 1})) ==
          DefaultCandidateError::kWorkLimit);
    auto proof = Take<ParameterExpressionProof>(
        ParameterExpressionProof::Build(g, p, d, 0));
    auto cost = Take<ParameterExpressionCost>(
        VerifyParameterExpressions(g, p, proof.Evidence()));
    Check(std::holds_alternative<ParameterExpressionCost>(
        VerifyParameterExpressions(g, p, proof.Evidence(),
                                   {cost.allocated_bytes, cost.work})));
    Check(std::get<ParameterExpressionFailure>(
              VerifyParameterExpressions(g, p, proof.Evidence(),
                                         {cost.allocated_bytes - 1, cost.work}))
              .kind == ParameterExpressionError::kResourceLimit);
    Check(std::get<ParameterExpressionFailure>(
              VerifyParameterExpressions(g, p, proof.Evidence(),
                                         {cost.allocated_bytes, cost.work - 1}))
              .kind == ParameterExpressionError::kWorkLimit);
    Check(!previous || cost.work < previous * 3);
    previous = cost.work;
    std::cout << n << " defaults bytes=" << cost.allocated_bytes
              << " work=" << cost.work << '\n';
  }
}
void Depth() {
  for (auto n : {200U, 201U, 400U, 800U}) {
    auto g = Graph(1, n);
    auto p = Take<ParameterPlan>(BuildParameterPlan(g, 1));
    auto d =
        Take<DefaultCandidates>(FindParameterExpressionCandidates(g, p, 0));
    auto proof = Take<ParameterExpressionProof>(
        ParameterExpressionProof::Build(g, p, d, 0));
    auto cost = Take<ParameterExpressionCost>(
        VerifyParameterExpressions(g, p, proof.Evidence()));
    Check(std::holds_alternative<ParameterExpressionCost>(
        VerifyParameterExpressions(g, p, proof.Evidence(),
                                   {cost.allocated_bytes, cost.work})));
    Check(std::get<ParameterExpressionFailure>(
              VerifyParameterExpressions(g, p, proof.Evidence(),
                                         {cost.allocated_bytes - 1, cost.work}))
              .kind == ParameterExpressionError::kResourceLimit);
    Check(std::get<ParameterExpressionFailure>(
              VerifyParameterExpressions(g, p, proof.Evidence(),
                                         {cost.allocated_bytes, cost.work - 1}))
              .kind == ParameterExpressionError::kWorkLimit);
    std::cout << "depth " << n << " bytes=" << cost.allocated_bytes
              << " work=" << cost.work << '\n';
  }
}

}  // namespace
int main() {
  Mutations();
  Recovery();
  Recovery(true);
  Scale();
  Depth();
}
