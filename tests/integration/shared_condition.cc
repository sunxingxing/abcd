#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/function_abi.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
constexpr std::uint8_t kArgument = 1, kSaved = 0;
abc::MethodBody Body(abc::ProfileId profile, std::uint32_t count, bool last_positive,
    bool extra_root, bool abrupt) {
  const auto op = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  std::vector<std::uint8_t> code;
  std::vector<std::uint32_t> branches;
  const auto read = [&](std::uint32_t name) {
    code.insert(code.end(), {op(abc::Opcode::kLDA_V8), kArgument,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0,
        static_cast<std::uint8_t>(name), static_cast<std::uint8_t>(name >> kByteBits)});
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    // A discarded getter is an independent effect, not a condition operand.
    if (extra_root && i == 1) read(count + 2);
    read(i);
    code.insert(code.end(), {op(abc::Opcode::kSTA_V8), kSaved, op(abc::Opcode::kLDUNDEFINED),
        op(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, kSaved});
    branches.push_back(static_cast<std::uint32_t>(code.size()));
    code.insert(code.end(), {op(i + 1 == count && !last_positive ? abc::Opcode::kJNEZ_IMM32 :
        abc::Opcode::kJEQZ_IMM32), 0, 0, 0, 0});
  }
  const auto shared = static_cast<std::uint32_t>(code.size());
  read(count);
  code.insert(code.end(), {op(abc::Opcode::kCALLTHIS0_IMM8_V8), 0, kArgument});
  if (abrupt) code.insert(code.end(), {op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kRETURN)});
  const auto join = static_cast<std::uint32_t>(code.size());
  read(count + 1);
  code.insert(code.end(), {op(abc::Opcode::kCALLTHIS0_IMM8_V8), 0, kArgument});
  code.insert(code.end(), {op(abc::Opcode::kLDUNDEFINED), op(abc::Opcode::kRETURN)});
  for (std::size_t i = 0; i < branches.size(); ++i) {
    const auto target = i + 1 == branches.size() ? join : shared;
    const auto delta = target - branches[i];
    for (unsigned b = 0; b < sizeof(delta); ++b)
      code[branches[i] + 1 + b] = static_cast<std::uint8_t>(delta >> (kByteBits * b));
  }
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < code.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(code).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
std::uint64_t Run(abc::ProfileId profile, std::uint32_t count, bool last_positive,
    bool print, bool extra_root = false, bool abrupt = false) {
  const auto body = Body(profile, count, last_positive, extra_root, abrupt);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(1, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"object"}; syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  syntax.names.push_back(u"action"); syntax.names.push_back(u"after"); syntax.names.push_back(u"discarded");
  std::vector<ExpressionConstant> constants(graph.ops.size());
  std::size_t decisions = 0;
  for (const auto& node : control.nodes) decisions += node.decision.has_value();
  test::Check(decisions + 1 == count);
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}};
  auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  if (extra_root) {
    test::Check(std::holds_alternative<FunctionTreeFailure>(built)); return control.work;
  }
  if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) std::cerr << "tree error " << static_cast<unsigned>(error->kind) << '\n';
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(graph, tree, frame, 1)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << nlohmann::json{{"count", count}, {"positive", last_positive}, {"abrupt", abrupt}, {"source", output.str()}}.dump() << '\n';
  return control.work + evaluation.work + std::get<ConstructedFunction>(built).work;
}
}
int main(int argc, char** argv) {
  const bool print = argc == 2 && std::string_view{argv[1]} == "--source";
  test::Check(argc == 1 || print);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    if (print) {
      for (const auto count : {2U, 3U}) for (const auto positive : {false, true})
        for (const auto abrupt : {false, true}) Run(profile, count, positive, true, false, abrupt);
    } else {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
        const auto work = Run(profile, count, false, false);
        Run(profile, count, false, false, false, true);
        if (count == 400 || count == 800) test::Check(work < previous * 3);
        previous = work;
      }
      Run(profile, 2, false, false, true);
    }
  }
}
