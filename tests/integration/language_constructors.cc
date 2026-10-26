#include "../../src/source/function_file.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/derived_value_states.h"
#include "../../src/source/instruction_sources.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <iostream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
std::uint64_t Run(abc::ProfileId profile, SourceContract contract, unsigned copies, bool split, bool rest, bool getter = false) {
  const auto op = [](abc::Opcode code) {return static_cast<std::uint8_t>(code);};
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 4, {true,true,true,true}));
  std::vector<std::uint8_t> bytes;
  if (rest) bytes.insert(bytes.end(), {op(abc::Opcode::kCOPYRESTARGS_IMM8), 0, op(abc::Opcode::kSTA_V8), 0});
  else if (getter) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 6,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0});
  else bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 0, 6});
  bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 1, 3});
  for (unsigned i = 0; i < copies; ++i) bytes.insert(bytes.end(), {op(abc::Opcode::kMOV_V8_V8), 2, 1, op(abc::Opcode::kMOV_V8_V8), 1, 2});
  if (split) bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 2});
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1, op(abc::Opcode::kSUPERCALLSPREAD_IMM8_V8), 0, 0, op(abc::Opcode::kRETURN)});
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  RouteInputIdentity identity; identity.profile = profile; identity.contract = contract;
  auto input = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  test::Check(std::holds_alternative<FunctionInput>(input));
  auto values = BuildFunctionValues(std::get<FunctionInput>(input), body, frame);
  test::Check(std::holds_alternative<FunctionValues>(values));
  FileFunction function{std::move(body), frame, std::get<FunctionInput>(std::move(input)), std::get<FunctionValues>(std::move(values))};
  auto& graph = function.values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {300}; syntax.owner = {0};
  syntax.callable = CallableKind::kDerivedConstructor; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"args", u"array"}; syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange, rest}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) if (std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) {
    constants[i] = NameId{1};

  }
  ExpressionConstants resolved{constants, syntax.names.size()}; resolved.derived_constructor = &function;
  const auto built = BuildFunctionTree(graph, control, evaluation, syntax, resolved);
  if (Constructors(contract) == ConstructorSemantics::kPinnedRuntime) {
    test::Check(std::holds_alternative<FunctionTreeFailure>(built)); return 0;
  }
  if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) std::cerr << "tree error " << unsigned(error->kind) << '\n';
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  const auto verified = VerifyTreeSemantics(graph, control, evaluation, tree, resolved);
  test::Check(std::holds_alternative<TreeSemantics>(verified));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(function.input, graph, tree)));
  auto stale = tree; stale.identity.contract = SourceContract::kLanguageSourceV9;
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, stale, resolved)));
  auto missing = resolved; missing.derived_constructor = nullptr;
  test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, tree, missing)));
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (!std::holds_alternative<ValueSuperSpread>(graph.ops[i].operation)) continue;
    auto& use = graph.uses[graph.ops[i].first_use.value]; const auto original = use.operand;
    use.operand = graph.uses[graph.ops[i].first_use.value + 1].operand;
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
    use.operand = original;
  }
  return std::get<ConstructedFunction>(built).work + std::get<TreeSemantics>(verified).work;
}
}
int main() {
  unsigned cases = 0;
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (auto contract : {SourceContract::kLanguageConstructorsV1, SourceContract::kLanguageArithmeticConstructorsV1, SourceContract::kLanguageNumericConstructorsV1})
      for (bool rest : {false, true}) for (bool split : {false, true}) {
        if (!rest) { Run(profile, contract, 201, split, false, true); ++cases; }
        std::uint64_t previous = 0;
        for (unsigned count : {0U, 1U, 200U, 201U, 402U}) {
          const auto work = Run(profile, contract, count, split, rest);
          if (count == 402) test::Check(work < 3 * previous);
          if (count == 201) previous = work;
          ++cases;
        }
      }
  for (auto contract : {SourceContract::kLanguageAndHostV3, SourceContract::kLanguageArithmeticV1, SourceContract::kLanguageNumericV1}) Run(abc::ProfileId::kDynamic1206, contract, 1, true, true);
  std::cout << cases << " complete decoded language constructors; old contracts refused\n";
}
