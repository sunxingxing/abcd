#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
abc::MethodBody Body(unsigned count, bool arm, bool leading, bool object, bool logical, bool barrier, bool retained) {
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> bytes;
  const auto read = [&](std::uint8_t name) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, name, 0});
  };
  const auto jump = [&](abc::Opcode opcode) {
    const auto offset = bytes.size(); bytes.insert(bytes.end(), {op(opcode), 0}); return offset;
  };
  const auto patch = [&](std::size_t offset) { bytes[offset + 1] = static_cast<std::uint8_t>(bytes.size() - offset); };
  for (unsigned i = 0; i < count; ++i) {
    // target.method(target.probe ||
    //     ((target.flag ? target.left : target.right) === target.test)
    //     ? target.yes : target.no)
    read(0); bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 0});
    if (retained) {
      read(1); bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),1,op(abc::Opcode::kLDUNDEFINED),
          op(abc::Opcode::kSTRICTEQ_IMM8_V8),0,1});
      const auto missing = jump(abc::Opcode::kJNEZ_IMM8);
      bytes.insert(bytes.end(),{op(abc::Opcode::kLDNULL),op(abc::Opcode::kSTRICTEQ_IMM8_V8),0,1});
      const auto nullish = jump(abc::Opcode::kJNEZ_IMM8);
      if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
      bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),1});
      const auto join = jump(abc::Opcode::kJMP_IMM8);
      patch(missing); patch(nullish); read(6); patch(join);
      bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),1,op(abc::Opcode::kLDA_V8),0,
          op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8),0,3,1});
      continue;
    }
    if (object) bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
        op(abc::Opcode::kSTA_V8), 2});
    if (leading) {
      read(8); bytes.push_back(op(abc::Opcode::kISTRUE));
      const auto fallback = jump(abc::Opcode::kJEQZ_IMM8);
      read(1); const auto end = jump(abc::Opcode::kJMP_IMM8);
      patch(fallback); read(9); patch(end);
    } else read(1);
    if (logical) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1});
    bytes.push_back(op(abc::Opcode::kISTRUE));
    const auto outer = jump(arm ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8);
    read(2); bytes.push_back(op(abc::Opcode::kISTRUE)); const auto inner = jump(abc::Opcode::kJEQZ_IMM8);
    read(3); const auto inner_join = jump(abc::Opcode::kJMP_IMM8);
    patch(inner); read(4); patch(inner_join);
    if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1}); read(5);
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, 1});
    if (logical) {
      const auto join = jump(abc::Opcode::kJMP_IMM8);
      patch(outer); bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1}); patch(join);
    } else if (arm) {
      const auto join = jump(abc::Opcode::kJMP_IMM8);
      patch(outer); read(7); patch(join);
    } else {
      const auto final = jump(abc::Opcode::kJNEZ_IMM8);
      read(7); const auto join = jump(abc::Opcode::kJMP_IMM8);
      patch(outer); patch(final); read(6); patch(join);
    }
    if (object) bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 10, 0, 2,
        op(abc::Opcode::kLDA_V8), 2});
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 0,
        op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  return test::Body(bytes);
}
std::uint64_t Run(abc::ProfileId profile, unsigned count, bool print, bool arm = false, bool leading = false, bool object = false,
    bool logical = false, bool barrier = false, bool retained = false) {
  const auto body = Body(count, arm, leading, object, logical, barrier, retained);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto planned = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"target", u"method", u"probe", u"flag", u"left", u"right", u"test", u"yes", u"no", u"leadingFlag", u"leadingOther", u"item"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (std::holds_alternative<ValueObjectAllocation>(graph.ops[i].operation)) constants[i] = AggregateConstant{0, 1};
    if (std::holds_alternative<ValueOwnWrite>(graph.ops[i].operation)) constants[i] = NameId{11};
  }
  const std::vector<ConstantProperty> properties{{NameId{11}, NullLiteral{}}};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  if (barrier) {
    const auto* error = std::get_if<FunctionTreeFailure>(&built);
    test::Check(error && (*error == FunctionTreeError::kExpression || *error == FunctionTreeError::kControl ||
        *error == FunctionTreeError::kOwnership));
    return evaluation.work;
  }
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  if (count == 1) {
    for (std::size_t i = 0; i < evaluation.logical_values.size(); ++i) {
      if (!evaluation.logical_values[i]) continue;
      auto forged = evaluation; forged.logical_values[i].reset();
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged))); break;
    }
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      if (!std::holds_alternative<ConditionalExpr>(tree.expressions[i].expression)) continue;
      if (retained) {
        auto wrong_snapshot = tree;
        const auto& conditional = std::get<ConditionalExpr>(wrong_snapshot.expressions[i].expression);
        auto* symbol = std::get_if<SymbolExpr>(&wrong_snapshot.expressions[conditional.alternate.value].expression);
        test::Check(symbol != nullptr); symbol->symbol = tree.parameters[0].symbol;
        test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph,control,evaluation,wrong_snapshot,resolved)));
      }
      auto forged = tree; auto& expression = std::get<ConditionalExpr>(forged.expressions[i].expression);
      std::swap(expression.consequent, expression.alternate);
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, forged, resolved))); break;
    }
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    test::Check(option == "--source" || option == "--arm-source" || option == "--leading-source" || option == "--object-source" ||
        option == "--logical-source" || option == "--logical-object-source" || option == "--retained-source");
    Run(abc::ProfileId::kDynamic1206, 1, true, option == "--arm-source",
        option == "--leading-source" || option == "--object-source" || option == "--logical-object-source",
        option == "--object-source" || option == "--logical-object-source",
        option == "--logical-source" || option == "--logical-object-source", false, option == "--retained-source"); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,2U,200U,201U,402U}) {
      const auto work = Run(profile,count,false,false,false,false,false,false,true);
      if (count == 402) test::Check(work < 3 * previous);
      previous = work;
    }
    Run(profile,1,false,false,false,false,false,true,true);
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool arm : {false, true}) for (const bool leading : {false, true}) for (const bool object : {false, true}) {
      std::uint64_t previous = 0, logical_previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, count, false, arm, leading, object);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
        if (!arm) {
          const auto logical_work = Run(profile, count, false, false, leading, object, true);
          if (count == 402) test::Check(logical_work < 3 * logical_previous);
          logical_previous = logical_work;
        }
      }
      Run(profile, 1, false, arm, leading, object, false, true);
    }
}
