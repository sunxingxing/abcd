#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "abcd/analysis/isa_catalog.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr unsigned kByteBits = 8;
// Distinct operands catch swapped accumulator/register routing.
constexpr std::array kBinary{
  abc::Opcode::kADD2_IMM8_V8,
  abc::Opcode::kSUB2_IMM8_V8,
  abc::Opcode::kMUL2_IMM8_V8,
  abc::Opcode::kDIV2_IMM8_V8,
  abc::Opcode::kMOD2_IMM8_V8,
  abc::Opcode::kEQ_IMM8_V8,
  abc::Opcode::kNOTEQ_IMM8_V8,
  abc::Opcode::kLESS_IMM8_V8,
  abc::Opcode::kLESSEQ_IMM8_V8,
  abc::Opcode::kGREATER_IMM8_V8,
  abc::Opcode::kGREATEREQ_IMM8_V8,
  abc::Opcode::kSHL2_IMM8_V8,
  abc::Opcode::kSHR2_IMM8_V8,
  abc::Opcode::kASHR2_IMM8_V8,
  abc::Opcode::kAND2_IMM8_V8,
  abc::Opcode::kOR2_IMM8_V8,
  abc::Opcode::kXOR2_IMM8_V8,
  abc::Opcode::kEXP_IMM8_V8,
  abc::Opcode::kISIN_IMM8_V8,
  abc::Opcode::kINSTANCEOF_IMM8_V8,
  abc::Opcode::kSTRICTNOTEQ_IMM8_V8,
  abc::Opcode::kSTRICTEQ_IMM8_V8};
std::uint64_t Run(abc::Opcode binary, abc::ProfileId profile, unsigned count, bool print = false, bool negative = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const auto* spec = FindPinnedInstruction(binary); test::Check(spec && (spec->group == IsaGroup::kBinary || spec->group == IsaGroup::kComparison));
  const auto encoded = static_cast<std::uint16_t>(binary);
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  const auto integer = [&](std::int32_t value) {
    bytes.push_back(op(abc::Opcode::kLDAI_IMM32));
    const auto bits = static_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < sizeof(bits) * kByteBits; shift += kByteBits)
      bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  };
  for (unsigned i = 0; i < count; ++i) {
    integer(negative ? -7 : 7);
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1});
    integer(negative ? 3 : 9);
    if (binary == abc::Opcode::kISIN_IMM8_V8) bytes.push_back(op(abc::Opcode::kCREATEEMPTYOBJECT));
    if (binary == abc::Opcode::kINSTANCEOF_IMM8_V8) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2});
    test::Check(spec->size == 3);
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(encoded), 0, 1});
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> kByteBits), 0});
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto prepared = FunctionInput::Build(identity, {300}, {100}, body, frame, InputCaptureMode::kClosed);
  if (!std::holds_alternative<FunctionInput>(prepared)) std::cerr << "Input rejected opcode " << encoded << '\n';
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
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
  syntax.parameters = {{NameId{static_cast<std::uint32_t>(syntax.names.size())}, NameEvidence::kDebugRange}};
  syntax.names.push_back(u"Constructor");
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
  for (const auto& spec : PinnedIsaCatalog()) if (spec.group == IsaGroup::kBinary || spec.group == IsaGroup::kComparison)
    test::Check(std::find(kBinary.begin(), kBinary.end(), spec.opcode) != kBinary.end());
  if (argc == 3) {
    const auto option = std::string_view(argv[1]);
    test::Check(option == "--source" || option == "--negative-source");
    const auto index = std::stoul(argv[2]); test::Check(index < kBinary.size());
    Run(kBinary[index], abc::ProfileId::kDynamic1206, 2, true, option == "--negative-source"); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto binary : kBinary) {
      Run(binary, profile, 2, false, true);
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(binary, profile, count);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
}
