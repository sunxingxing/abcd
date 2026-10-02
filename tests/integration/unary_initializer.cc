#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/analysis/isa_catalog.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr unsigned kByteBits = 8;
// Deliberately distinct acc/register seeds expose deprecated operand routing.
constexpr std::array kUnary{
  abc::Opcode::kTYPEOF_IMM8, abc::Opcode::kTYPEOF_IMM16,
  abc::Opcode::kTONUMBER_IMM8, abc::Opcode::kTONUMERIC_IMM8,
  abc::Opcode::kNEG_IMM8, abc::Opcode::kNOT_IMM8,
  abc::Opcode::kINC_IMM8, abc::Opcode::kDEC_IMM8,
  abc::Opcode::kISTRUE, abc::Opcode::kISFALSE,
  abc::Opcode::kDEPRECATED_TONUMBER_PREF_V8, abc::Opcode::kDEPRECATED_TONUMERIC_PREF_V8,
  abc::Opcode::kDEPRECATED_NOT_PREF_V8, abc::Opcode::kDEPRECATED_INC_PREF_V8,
  abc::Opcode::kDEPRECATED_DEC_PREF_V8};
std::uint64_t Run(abc::Opcode unary, abc::ProfileId profile, unsigned count, bool print = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const auto* spec = FindPinnedInstruction(unary); test::Check(spec && spec->group == IsaGroup::kUnary);
  const auto encoded = static_cast<std::uint16_t>(unary);
  const bool prefixed = encoded > UINT8_MAX;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0,
        op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDAI_IMM32), 9, 0, 0, 0});
    const auto first = bytes.size(); bytes.resize(first + spec->size);
    bytes[first] = static_cast<std::uint8_t>(encoded);
    if (prefixed) { bytes[first + 1] = static_cast<std::uint8_t>(encoded >> kByteBits); bytes[first + 2] = 1; }
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> kByteBits), 0});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 0, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (unsigned i = 0; i < count; ++i) {
    const auto name = std::string("field") + std::to_string(i); syntax.names.emplace_back(name.begin(), name.end());
  }
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
  const ExpressionConstants resolved{constants, syntax.names.size()};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream output; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return evaluation.work;
}
}
int main(int argc, char** argv) {
  if (argc == 3) {
    test::Check(std::string_view(argv[1]) == "--source");
    const auto index = std::stoul(argv[2]); test::Check(index < kUnary.size());
    Run(kUnary[index], abc::ProfileId::kDynamic1206, 2, true); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto unary : kUnary) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(unary, profile, count);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
}
