#include <iostream>

#include "../../../src/source/function_tree.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_structure.h"
#include "../../support/analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
template <class T, class V>
T Take(V v, std::source_location at = std::source_location::current()) {
  if (!std::holds_alternative<T>(v)) {
    std::cerr << at.line() << " variant=" << v.index() << '\n';
    std::visit(
        [](const auto& x) {
          if constexpr (requires { x.kind; })
            std::cerr << unsigned(x.kind) << '\n';
        },
        v);
    std::abort();
  }
  return std::get<T>(std::move(v));
}
abc::MethodBody Body(unsigned count, bool copies, bool split,
                     bool conjunction) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode op) {
    bytes.push_back(static_cast<std::uint8_t>(op));
  };
  const auto load = [&](unsigned r) {
    emit(abc::Opcode::kLDA_V8);
    bytes.push_back(r);
  };
  const auto save = [&](unsigned r) {
    emit(abc::Opcode::kSTA_V8);
    bytes.push_back(r);
  };
  const auto read = [&](unsigned key) {
    load(4);
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(key), 0});
  };
  const auto jump = [&](abc::Opcode op) {
    const auto pc = bytes.size();
    emit(op);
    bytes.push_back(0);
    return pc;
  };
  const auto bind = [&](std::size_t pc) {
    Check(bytes.size() - pc < 128);
    bytes[pc + 1] = bytes.size() - pc;
  };
  const auto number = [&](unsigned n) {
    emit(abc::Opcode::kLDAI_IMM32);
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(n), 0, 0, 0});
  };
  for (unsigned n = 0; n < count; ++n) {
    read(0);
    emit(abc::Opcode::kISTRUE);
    const auto first =
        jump(conjunction ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8);
    if (split) {
      emit(abc::Opcode::kJMP_IMM8);
      bytes.push_back(2);
    }
    read(1);
    save(0);
    emit(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16);
    bytes.insert(bytes.end(), {0, 0, 0});
    save(1);
    if (copies) {
      emit(abc::Opcode::kMOV_V8_V8);
      bytes.insert(bytes.end(), {2, 1});
      emit(abc::Opcode::kMOV_V8_V8);
      bytes.insert(bytes.end(), {1, 2});
    }
    emit(abc::Opcode::kCREATEEMPTYARRAY_IMM8);
    bytes.push_back(0);
    emit(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
    bytes.insert(bytes.end(), {0, 2, 0, 1});
    load(1);
    save(2);
    load(0);
    emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);
    bytes.insert(bytes.end(), {0, 4, 2});
    emit(abc::Opcode::kISTRUE);
    const auto failed = jump(abc::Opcode::kJEQZ_IMM8);
    if (!conjunction) bind(first);
    number(11);
    const auto join = jump(abc::Opcode::kJMP_IMM8);
    bind(failed);
    if (conjunction) bind(first);
    number(22);
    bind(join);
    save(3);
  }
  load(3);
  emit(abc::Opcode::kRETURN);
  return test::Body(bytes);
}
std::pair<std::size_t, std::uint64_t> Run(unsigned count, bool copies = false,
                                          bool split = false,
                                          bool conjunction = false) {
  const auto body = Body(count, copies, split, conjunction);
  const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(4, 1, {}));
  RouteInputIdentity identity;
  identity.profile = abc::ProfileId::kDynamic1206;
  const auto input = Take<FunctionInput>(FunctionInput::Build(
      identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values =
      Take<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = Take<ControlPlan>(BuildControlPlan(graph));
  const auto plan = Take<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = identity;
  syntax.method = input.Method();
  syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"};
  syntax.names = {u"input", u"flag", u"test", u"params", u"id"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (unsigned i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i].operation;
    if (const auto* read = std::get_if<ValueNamedRead>(&op))
      constants[i] = NameId{read->name.value + 1};
    if (const auto* write = std::get_if<ValueOwnWrite>(&op))
      constants[i] = NameId{write->name.value + 1};
    if (std::holds_alternative<ValueObjectAllocation>(op))
      constants[i] = AggregateConstant{0, 2};
  }
  const std::vector<ConstantProperty> properties{{NameId{4}, NullLiteral{}},
                                                 {NameId{3}, NullLiteral{}}};
  const ExpressionConstants resolved{constants, syntax.names.size(), 1,
                                     TextId{0}, properties};
  const auto result = BuildFunctionTree(graph, control, plan, syntax, resolved);
  if (const auto* f = std::get_if<FunctionTreeFailure>(&result)) {
    if (const auto* e = std::get_if<ExpressionFailure>(&f->cause))
      std::cerr << "expression=" << unsigned(e->kind)
                << " cause=" << e->cause.index() << '\n';
    if (f->tree)
      std::cerr << "obligation=" << unsigned(f->tree->obligation) << '\n';
  }
  const auto built = Take<ConstructedFunction>(result);
  const auto semantics = Take<TreeSemantics>(
      VerifyTreeSemantics(graph, control, plan, built.tree, resolved));
  Check(semantics.flow.original != nullptr);
  Take<SourceVariables>(
      PrepareSourceVariables(built.tree, {}, semantics.flow.original.get()));
  unsigned initializers = 0;
  for (const auto& placement : built.tree.operations)
    if (const auto* property = std::get_if<PropertyId>(&placement.node)) {
      ++initializers;
      if (count != 1) continue;
      const auto rejects = [&](const FunctionTree& wrong) {
        Check(!std::holds_alternative<TreeSemantics>(
            VerifyTreeSemantics(graph, control, plan, wrong, resolved)));
      };
      auto wrong = built.tree;
      // Still a well-formed tree: the original commit is attributed to a
      // buffered property instead of its actual initialized property.
      auto p = std::find_if(
          wrong.operations.begin(), wrong.operations.end(),
          [&](const auto& x) { return x.operation == placement.operation; });
      p->node = PropertyId{property->value - 1};
      rejects(wrong);
      wrong = built.tree;
      wrong.operations.push_back(placement);
      rejects(wrong);
      wrong = built.tree;
      wrong.operations.erase(std::find_if(
          wrong.operations.begin(), wrong.operations.end(),
          [&](const auto& x) { return x.operation == placement.operation; }));
      rejects(wrong);
      wrong = built.tree;
      wrong.properties[property->value].key = {4};
      rejects(wrong);
      wrong = built.tree;
      std::swap(wrong.properties[property->value],
                wrong.properties[property->value - 1]);
      rejects(wrong);
      wrong = built.tree;
      wrong.properties[property->value].origin =
          wrong.properties[property->value - 1].origin;
      rejects(wrong);
      // Change the branch execution edge without invalid ownership/sharing.
      wrong = built.tree;
      bool selected = false;
      for (auto& node : wrong.statements)
        if (auto* branch = std::get_if<IfStmt>(&node.statement);
            branch && branch->alternate) {
          std::swap(branch->consequent, *branch->alternate);
          selected = true;
          break;
        }
      if (!selected)
        for (auto& node : wrong.expressions)
          if (auto* c = std::get_if<ConditionalExpr>(&node.expression)) {
            std::swap(c->consequent, c->alternate);
            selected = true;
            break;
          }
      Check(selected);
      rejects(wrong);
      auto wrong_plan = plan;
      wrong_plan.operations[placement.operation.value].kind =
          EvaluationKind::kRoot;
      Check(std::holds_alternative<EvaluationFailure>(
          VerifyEvaluationPlan(graph, wrong_plan)));
    }
  Check(initializers == count);
  const auto flow =
      Take<TreeFlow>(VerifyTreeFlow(graph, control, plan, built.tree));
  Take<TreeFlow>(VerifyTreeFlow(graph, control, plan, built.tree,
                                {flow.allocated_bytes, flow.work}));
  Check(std::get<TreeFlowError>(
            VerifyTreeFlow(graph, control, plan, built.tree,
                           {flow.allocated_bytes - 1, flow.work})) ==
        TreeFlowError::kResourceLimit);
  Check(std::get<TreeFlowError>(
            VerifyTreeFlow(graph, control, plan, built.tree,
                           {flow.allocated_bytes, flow.work - 1})) ==
        TreeFlowError::kWorkLimit);
  return {flow.allocated_bytes, flow.work};
}
}  // namespace
int main() {
  Run(1, false, false, true);
  Run(1);
  Run(1, true);
  Run(1, true, true, true);
  for (unsigned n : {200U, 201U, 400U, 800U}) {
    const auto [bytes, work] = Run(n, false, false, true);
    std::cout << n << " bytes=" << bytes << " work=" << work << '\n';
  }
}
