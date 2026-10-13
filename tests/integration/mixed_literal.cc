#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/source/call_plan.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Name : std::uint8_t {
  kLeft, kRight, kBottom, kPayload, kMethod, kFlag, kFirst, kSecond,
  kThird, kGetValue, kArgument, kTail, kCtor, kValue
};
abc::MethodBody Body(unsigned count, bool aggregates, bool barrier, bool single_object) {
  // r0 = buffered result, r1 = original callee, r2..5 = constructor/args,
  // r6 = constructed receiver; parameters are r7 (Ctor), r8 (value).
  constexpr std::uint8_t kCtor = 7, kValue = 8;
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto read = [&](Name name) {
    emit(abc::Opcode::kLDA_V8); bytes.push_back(kValue);
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name), 0});
  };
  const auto field = [&](Name name, std::uint8_t base) {
    emit(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
    bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(name), 0, base});
  };
  const auto argument = [&](Name name) {
    if (!aggregates) { read(name); return; }
    emit(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16); bytes.insert(bytes.end(), {0, 1, 0});
    emit(abc::Opcode::kSTA_V8); bytes.push_back(6);
    read(name); field(Name::kPayload, 6);
    emit(abc::Opcode::kLDA_V8); bytes.push_back(6);
  };
  for (unsigned i = 0; i < count; ++i) {
    if (!single_object || i == 0) {
      read(Name::kMethod); emit(abc::Opcode::kSTA_V8); bytes.push_back(1);
      emit(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16); bytes.insert(bytes.end(), {0, 0, 0});
      emit(abc::Opcode::kSTA_V8); bytes.push_back(0);
    }
    argument(Name::kLeft); field(Name::kLeft, 0);
    emit(abc::Opcode::kLDA_V8); bytes.push_back(kCtor);
    emit(abc::Opcode::kSTA_V8); bytes.push_back(2);
    argument(Name::kFirst); emit(abc::Opcode::kSTA_V8); bytes.push_back(3);
    read(Name::kFlag); emit(abc::Opcode::kISTRUE);
    const auto branch = bytes.size(); emit(abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
    argument(Name::kSecond);
    const auto jump = bytes.size(); emit(abc::Opcode::kJMP_IMM8); bytes.push_back(0);
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    argument(Name::kThird);
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    emit(abc::Opcode::kSTA_V8); bytes.push_back(4);
    argument(Name::kTail); emit(abc::Opcode::kSTA_V8); bytes.push_back(5);
    emit(abc::Opcode::kNEWOBJRANGE_IMM8_IMM8_V8); bytes.insert(bytes.end(), {0, 4, 2});
    emit(abc::Opcode::kSTA_V8); bytes.push_back(6);
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    bytes.insert(bytes.end(), {0, static_cast<std::uint8_t>(Name::kGetValue), 0});
    emit(abc::Opcode::kSTA_V8); bytes.push_back(4);
    read(Name::kArgument); emit(abc::Opcode::kSTA_V8); bytes.push_back(5);
    emit(abc::Opcode::kLDA_V8); bytes.push_back(4);
    emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); bytes.insert(bytes.end(), {0, 6, 5});
    field(Name::kRight, 0);
    if (barrier) emit(abc::Opcode::kDEBUGGER);
    argument(Name::kBottom); field(Name::kBottom, 0);
    if (!single_object || i + 1 == count) {
      emit(abc::Opcode::kLDA_V8); bytes.push_back(1);
      emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); bytes.insert(bytes.end(), {0, kValue, 0});
    }
  }
  emit(abc::Opcode::kRETURN);
  return test::Body(bytes);
}
std::uint64_t Run(abc::ProfileId profile, unsigned count, bool aggregates, bool print, bool barrier = false,
    bool single_object = false) {
  const auto body = Body(count, aggregates, barrier, single_object);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(7, 2, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto planned = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto calls = BuildCallPlan(graph, evaluation);
  if (barrier) { test::Check(std::get<CallPlanFailure>(calls) == CallPlanError::kReference); return 0; }
  test::Check(std::holds_alternative<CallPlan>(calls));
  unsigned objects = 0;
  for (const auto& object : evaluation.logical_objects) objects += object.has_value();
  test::Check(objects == (single_object ? 1 : count));
  if (count == 1) {
    const auto allocation = evaluation.literals->objects.front().allocation;
    auto forged = evaluation;
    forged.logical_objects[allocation.value] = allocation;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    auto shared = graph;
    for (const auto& operation : graph.ops) if (std::holds_alternative<ValueReturn>(operation.operation))
      test::Check(!ReplaceUse(shared, operation.first_use, *graph.ops[allocation.value].result));
    test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
    const auto retained = std::get<EvaluationPlan>(BuildEvaluationPlan(shared));
    test::Check(std::get<CallPlanFailure>(BuildCallPlan(shared, retained)) == CallPlanError::kReference);
  }
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"left", u"right", u"bottom", u"payload", u"method", u"flag", u"first", u"second",
      u"third", u"getValue", u"argument", u"tail", u"Ctor", u"value"};
  syntax.parameters = {{NameId{static_cast<unsigned>(Name::kCtor)}, NameEvidence::kDebugRange},
      {NameId{static_cast<unsigned>(Name::kValue)}, NameEvidence::kDebugRange}};
  const std::vector<ConstantProperty> properties{{NameId{0}, NullLiteral{}}, {NameId{1}, NullLiteral{}},
      {NameId{2}, NullLiteral{}}, {NameId{3}, NullLiteral{}}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    if (const auto* allocation = std::get_if<ValueObjectAllocation>(&graph.ops[i].operation); allocation && allocation->literal)
      constants[i] = allocation->literal->value == 0 ? AggregateConstant{0, 3} : AggregateConstant{3, 1};
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  test::Check(output.str().find("if (") == std::string::npos);
  if (print) std::cout << output.str();
  return evaluation.work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    test::Check(option == "--source" || option == "--aggregate-source" ||
        option == "--sequence-source" || option == "--aggregate-sequence-source");
    Run(abc::ProfileId::kDynamic1206, 2, option == "--aggregate-source" || option == "--aggregate-sequence-source",
        true, false, option == "--sequence-source" || option == "--aggregate-sequence-source"); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool aggregates : {false, true}) for (const bool single : {false, true}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, count, aggregates, false, false, single);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
      Run(profile, 1, aggregates, false, true, single);
    }
}
