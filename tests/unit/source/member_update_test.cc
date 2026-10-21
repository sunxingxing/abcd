#include "../../../src/source/member_update.h"

#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/tree_semantics.h"
#include "abcd/analysis/exception_captures.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
template <class F>
auto Graph(unsigned count, bool postfix, bool computed, bool copies, bool split,
           bool rename, F&& inspect) {
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode o) {
    const auto n = static_cast<std::uint16_t>(o);
    code.push_back(n);
    if (n > UINT8_MAX) code.push_back(n >> 8);
  };
  const auto reg = [&](unsigned r) {
    constexpr unsigned permutation[]{4, 2, 5, 0, 3, 1};
    return static_cast<std::uint8_t>(rename ? permutation[r] : r);
  };
  const auto load = [&](unsigned r) {
    op(abc::Opcode::kLDA_V8);
    code.push_back(reg(r));
  };
  const auto save = [&](unsigned r) {
    op(abc::Opcode::kSTA_V8);
    code.push_back(reg(r));
  };
  for (unsigned n = 0; n < count; ++n) {
    op(abc::Opcode::kLDGLOBAL);
    save(0);
    op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    code.insert(code.end(), {0, 0, 0});
    save(1);
    op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16);
    code.insert(code.end(), {0, 1, 0});
    save(2);
    if (copies) {
      op(abc::Opcode::kMOV_V8_V8);
      code.insert(code.end(), {reg(2), reg(2)});
    }
    if (split) {
      op(abc::Opcode::kJMP_IMM8);
      code.push_back(2);
    }
    if (computed) {
      op(abc::Opcode::kLDA_STR_ID16);
      code.insert(code.end(), {2, 0});
      save(5);
      op(abc::Opcode::kLDOBJBYVALUE_IMM8_V8);
      code.insert(code.end(), {0, reg(2)});
    } else {
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
      code.insert(code.end(), {0, 2, 0});
    }
    if (postfix) {
      op(abc::Opcode::kTONUMERIC_IMM8);
      code.push_back(0);
      save(4);
    }
    op(abc::Opcode::kINC_IMM8);
    code.push_back(0);
    save(3);
    if (computed) {
      op(abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8);
      code.insert(code.end(), {0, reg(2), reg(5)});
    } else {
      op(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8);
      code.insert(code.end(), {0, 2, 0, reg(2)});
    }
    load(1);
    op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);
    code.insert(code.end(), {0, reg(0), reg(postfix ? 4 : 3)});
  }
  op(abc::Opcode::kRETURN);
  auto body = test::Body(code);
  const auto frame =
      std::get<abc::FrameLayout>(abc::BuildFrameLayout(6, 0, {}));
  const auto flow = std::get<ExceptionFlow>(
      BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa =
      std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto environments =
      std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto captures =
      std::get<ExceptionCaptures>(PlanExceptionCaptures(flow, environments));
  IdfQuota work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow, ssa, {}, work, {}, PendingExceptionValues::kLanguage,
      captures.loads, {}, CallResultValues::kLanguage, FrameReceiver::kUnknown,
      ArithmeticSemantics::kSourceNumeric));
  auto checked = VerifyExceptionInputs(flow, ssa, environments, values);
  if (auto* e = std::get_if<ExceptionInputFailure>(&checked))
    std::cerr << "input error " << unsigned(e->kind) << " pc " << e->pc.value
              << "\n";
  const auto proof = std::get<ExceptionInputProof>(checked);
  const PhasedValueInput input{flow, ssa, environments, values, proof, frame};
  auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  return inspect(graph, body, input);
}
// A single large-arity call result stored repeatedly is not a mutation.
// Discovery must not rescan that call's operand list once per store.
void CallResultFanout(unsigned count) {
  ValueProgram graph;
  const auto add = [&](ValueOperation operation,
                       std::vector<ValueOperand> operands,
                       bool result = false) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    const auto shape = DescribeEvaluation(operation);
    Check(shape && shape->Count() == operands.size());
    const UseId first{static_cast<std::uint32_t>(graph.uses.size())};
    for (std::uint32_t i = 0; i < operands.size(); ++i)
      graph.uses.push_back({id, *shape->Role(i), ReadPurpose::kOperand,
                            operands[i]});
    std::optional<ValueId> value;
    if (result) {
      value = ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({OpResult{id, {}}, ValueKinds::Language(), {}});
    }
    graph.ops.push_back({std::move(operation), {0}, id.value, first,
                         static_cast<std::uint32_t>(operands.size()), value,
                         LanguageEffect::kNone, LanguageEffect::kNone,
                         ExceptionalCompletion::kMayThrow, {0}});
    return value;
  };
  const auto callee = *add(ValueNamedRead{{0}}, {SpecialValue::kUndefined}, true);
  std::vector<ValueOperand> arguments(count + 1, analysis::IntegerLiteral{0});
  const auto value = *add(ValueInvoke{InvokeReceiver::kUndefined, count},
                           std::move(arguments), true);
  for (unsigned i = 0; i < count; ++i)
    add(ValueNamedWrite{{i}}, {SpecialValue::kUndefined, value});
  add(ValueInvoke{InvokeReceiver::kExplicit, 1},
      {callee, SpecialValue::kUndefined, value});
  EvaluationPlan pending{};
  pending.operations.resize(graph.ops.size(),
      {EvaluationKind::kRoot, {}, TemporaryReason::kCrossRegionValue});
  TreeCheckBudget budget({0, graph.ops.size() + graph.uses.size()});
  Check(DiscoverMemberUpdates(graph, pending, budget).updates.empty());
  Check(budget.Bytes() == 0);
  std::cout << "call-fanout " << count << " bytes=" << budget.Bytes()
            << " work=" << budget.Work() << '\n';
}
void Run(unsigned count, bool postfix = false, bool computed = false,
         bool copies = false, bool split = false, bool rename = false) {
  Graph(
      count, postfix, computed, copies, split, rename,
      [&](auto& graph, const auto& body, const auto& input) {
        const auto built = BuildPhasedEvaluationPlan(graph);
        if (const auto* e = std::get_if<EvaluationFailure>(&built))
          std::cerr << "evaluation " << unsigned(e->kind) << "\n";
        Check(std::holds_alternative<EvaluationPlan>(built));
        const auto& plan = std::get<EvaluationPlan>(built);
        Check(MemberUpdates(plan).updates.size() == count);
        Check(
            std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, plan)));
        TreeCheckBudget budget({});
        VerifyMemberUpdates(graph, plan, budget);
        const SemanticBudget exact{budget.Bytes(), budget.Work()};
        for (const auto limit :
             {exact, SemanticBudget{exact.bytes - 1, exact.work},
              SemanticBudget{exact.bytes, exact.work - 1}}) {
          bool pass = false;
          try {
            TreeCheckBudget b(limit);
            VerifyMemberUpdates(graph, plan, b);
            pass = true;
          } catch (TreeStructureError e) {
            Check(e == TreeStructureError::kResourceLimit ||
                  e == TreeStructureError::kWorkLimit);
          }
          Check(pass ==
                (limit.bytes == exact.bytes && limit.work == exact.work));
        }
        std::cout << count << " " << exact.bytes << " " << exact.work << "\n";
        auto rebuilt = plan;
        Check(rebuilt.member_updates == plan.member_updates);
        TreeCheckBudget rebuilding({});
        BuildMemberUpdates(graph, rebuilt, rebuilding);
        Check(MemberUpdates(rebuilt).updates == MemberUpdates(plan).updates);
        const SemanticBudget rebuild_cost{rebuilding.Bytes(), rebuilding.Work()};
        for (const auto limit : {rebuild_cost,
             SemanticBudget{rebuild_cost.bytes - 1, rebuild_cost.work},
             SemanticBudget{rebuild_cost.bytes, rebuild_cost.work - 1}}) {
          auto trial = plan;
          bool passed = false;
          try {
            TreeCheckBudget low(limit);
            BuildMemberUpdates(graph, trial, low);
            passed = true;
          } catch (TreeStructureError error) {
            Check(error == TreeStructureError::kResourceLimit ||
                  error == TreeStructureError::kWorkLimit);
          }
          Check(passed == (limit.bytes == rebuild_cost.bytes &&
                           limit.work == rebuild_cost.work));
        }
        std::cout << "build " << count << " bytes=" << rebuild_cost.bytes
                  << " work=" << rebuild_cost.work << '\n';
        // An unmet member reference without a matching Get/Set pair owns no
        // per-value index or certificate allocation, including in large families.
        auto no_pair = graph;
        if (!computed) for (const auto& update : MemberUpdates(plan).updates) {
          auto& write = std::get<ValueNamedWrite>(no_pair.ops[update.write.value].operation);
          ++write.name.value;
        }
        if (!computed) {
          auto pending = plan;
          pending.member_updates.reset();
          for (const auto& update : MemberUpdates(plan).updates)
            pending.operations[update.callee.value] = {
                EvaluationKind::kRoot, {}, TemporaryReason::kCrossRegionValue};
          TreeCheckBudget empty({0, kDefaultSemanticWork});
          Check(DiscoverMemberUpdates(no_pair, pending, empty).updates.empty());
          Check(empty.Bytes() == 0 && empty.Work() > graph.ops.size());
          TreeCheckBudget exact_empty({0, empty.Work()});
          Check(DiscoverMemberUpdates(no_pair, pending, exact_empty).updates.empty());
          if (empty.Work()) {
            bool exhausted = false;
            try {
              TreeCheckBudget low({0, empty.Work() - 1});
              (void)DiscoverMemberUpdates(no_pair, pending, low);
            } catch (TreeStructureError e) {
              exhausted = e == TreeStructureError::kWorkLimit;
            }
            Check(exhausted);
          }
          std::cout << "no-pair " << count << " bytes=" << empty.Bytes()
                    << " work=" << empty.Work() << '\n';
        }
        auto changed = plan;
        auto mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
        const auto rejected = [&] {
          TreeCheckBudget b({});
          bool fail = false;
          try {
            VerifyMemberUpdates(graph, changed, b);
          } catch (MemberUpdateError) {
            fail = true;
          }
          Check(fail);
        };
        mutation->updates.front().base =
            graph.ops[MemberUpdates(plan).updates.front().write.value]
                .first_use;
        rejected();
        changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
        mutation->updates.front().ports.front().exception.reset();
        rejected();
        changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
        mutation->updates.front().ports.back().phase =
            MemberUpdatePhase::kGet;
        rejected();
        changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
        mutation->updates.front().value = {0};
        rejected();
        changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
        mutation->wires[graph.ops[mutation->updates.front().write.value]
                       .first_use.value] = 0;
        rejected();
        if (count == 1) {
          const auto& certificate = MemberUpdates(plan).updates.front();
          const auto rawRejected = [&](const auto& raw) {
            TreeCheckBudget b({});
            bool fail = false;
            try {
              VerifyMemberUpdates(raw, plan, b);
            } catch (MemberUpdateError) {
              fail = true;
            }
            Check(fail);
          };
          auto raw = graph;
          raw.uses[graph.ops[certificate.write.value].first_use.value].operand =
              SpecialValue::kNull;
          rawRejected(raw);
          raw = graph;
          raw.ops[certificate.calculation.value].operation =
              ValueUnary{UnaryOperator::kDecrement};
          rawRejected(raw);
          raw = graph;
          raw.exceptional_exits.erase(raw.exceptional_exits.begin() +
                                      *certificate.ports.front().exception);
          rawRejected(raw);
          changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
          mutation->updates.clear();
          rejected();
          changed = plan;
        mutation = std::make_shared<MemberUpdatePlan>(MemberUpdates(plan));
        changed.member_updates = mutation;
          changed.operations[certificate.read.value].kind =
              EvaluationKind::kRoot;
          rejected();
          const auto regions =
              std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
          const auto control = std::get<ExceptionControl>(
              BuildExceptionControl(body, graph, input, regions));
          const ExceptionTreeInput exceptions{body, input, regions, control};
          FunctionSyntax syntax{};
          syntax.owner = {2};
          syntax.callable = CallableKind::kFunction;
          syntax.strictness = Strictness::kStrict;
          syntax.identity.contract = SourceContract::kLanguageSourceV5;
          syntax.names = {u"m", u"state", u"p"};
          syntax.texts = {u"p"};
          std::vector<ExpressionConstant> constants(graph.ops.size());
          for (std::size_t i = 0; i < graph.ops.size(); ++i) {
            if (const auto* r =
                    std::get_if<ValueNamedRead>(&graph.ops[i].operation))
              constants[i] = NameId{r->name.value};
            if (std::holds_alternative<ValueNamedWrite>(graph.ops[i].operation))
              constants[i] = NameId{2};
            if (std::holds_alternative<ValueGlobalRead>(graph.ops[i].operation))
              constants[i] = NameId{1};
            if (std::holds_alternative<ValueString>(graph.ops[i].operation))
              constants[i] = TextId{0};
          }
          const auto tree = BuildExceptionFunctionDraft(
              graph, exceptions, plan, syntax, {constants, 3, 1});
          Check(std::holds_alternative<ExceptionFunctionDraft>(tree));
          const auto& draft = std::get<ExceptionFunctionDraft>(tree);
          Check(std::holds_alternative<TreeSemantics>(
              VerifyExceptionTreeSemantics(graph, exceptions, plan, draft.tree,
                                           {constants, 3, 1})));
          const auto& original = *draft.flow->original;
          const auto prepared = std::get<SourceVariables>(
              PrepareSourceVariables(draft.tree, {}, &original));
          const auto& bindings = prepared.bindings;
          std::vector<FlowValueEquation> equations(
              draft.tree.expressions.size());
          std::vector<ExprId> order;
          for (unsigned n = 0; n < draft.tree.expressions.size(); ++n) {
            order.push_back({n});
            if (const auto* symbol = std::get_if<SymbolExpr>(
                    &draft.tree.expressions[n].expression))
              equations[n] = FlowBindingValue{symbol->symbol};
          }
          const auto project = [&](const BindingInitializers& candidate,
                                   const OriginalFlow& raw) {
            const LegacyFlowProjection projection{
                candidate.aliases,
                candidate.literals,
                candidate.replacements,
                candidate.omitted,
                {},
                {},
                candidate.statements,
                {},
                candidate.storage,
                candidate.undefined_stores,
                candidate.declaration_values,
                candidate.declaration_completions};
            return PrepareRecoveryProjection(
                draft.tree, projection, {candidate, equations, order, &raw});
          };
          const auto recovered =
              std::get<RecoveryProjection>(project(bindings, original));
          Check(recovered.flow.member_updates.size() == 1);
          for (const auto& port : certificate.ports) {
            Check(recovered.flow.original_actions[port.operation.value]
                      .has_value());
            if (port.exception)
              Check(recovered.flow.original_exceptions[*port.exception]
                        .has_value());
          }
          auto corrupted = original;
          corrupted.member_updates.front().syntax.target.base = {0};
          if (corrupted.member_updates.front().syntax.target.base ==
              original.member_updates.front().syntax.target.base)
            corrupted.member_updates.front().syntax.target.base = {1};
          Check(std::holds_alternative<TreeStructureError>(
              project(bindings, corrupted)));
          auto wrong = draft.tree;
          const auto expr =
              *draft.flow->expressions[MemberUpdates(plan).updates.front()
                                           .write.value];
          auto& node = std::get<MemberMutationExpr>(
              wrong.expressions[expr.value].expression);
          node.operation = MemberIncrement{UpdateDirection::kDecrement,
                                           UpdateResult::kPrefix};
          Check(std::holds_alternative<TreeSemanticError>(
              VerifyExceptionTreeSemantics(graph, exceptions, plan, wrong,
                                           {constants, 3, 1})));
        }
      });
}
}  // namespace
int main() {
  static_assert(sizeof(MemberMutationExpr) <= sizeof(CallExpr));
  for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
    Run(count);
    CallResultFanout(count);
  }
  Run(1, true);
  Run(1, false, true);
  Run(1, false, false, true, true);
  Run(1, true, true, true, true, true);
}
