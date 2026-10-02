#include "../../src/source/function_values.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/source/call_plan.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kFieldNameBase = 6;
constexpr unsigned kByteBits = 8;
abc::MethodBody Siblings(unsigned count, bool barrier) {
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kLDA_V8), 3,
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kSTA_V8), 1,
      op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0};
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 1, 0,
        op(abc::Opcode::kSTA_V8), 2, op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0, op(abc::Opcode::kISTRUE)});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 4, 0});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    const auto name = kFieldNameBase + i;
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0, 2,
        op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(name), static_cast<std::uint8_t>(name >> kByteBits), 0});
    if (barrier && i == 0) bytes.push_back(op(abc::Opcode::kDEBUGGER));
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1,
      op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 0, op(abc::Opcode::kRETURN)});
  return test::Body(bytes);
}
abc::MethodBody Body(unsigned depth, unsigned count, bool barrier, bool shared) {
  // Named property/call operands use V8 registers. Wider scaling is exercised
  // by many independent object trees; depth tests stay within the real ISA.
  test::Check(depth && depth + 1 <= UINT8_MAX);
  const auto callee = static_cast<std::uint8_t>(depth), target = static_cast<std::uint8_t>(depth + 1);
  const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), target,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kSTA_V8), callee});
    for (unsigned level = 0; level < depth; ++level)
      bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16), 0, 0, 0,
          op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(level)});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), target,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0, op(abc::Opcode::kISTRUE)});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), target,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), target,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 4, 0});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    for (unsigned level = depth; level != 0; --level) {
      if (level != depth) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(level)});
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, 0, 0,
          static_cast<std::uint8_t>(level - 1)});
      if (barrier && level == depth) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    }
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), callee,
        op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, target, 0});
  }
  if (shared) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(depth - 1)});
  bytes.push_back(op(abc::Opcode::kRETURN));
  return test::Body(bytes);
}
std::uint64_t Run(abc::ProfileId profile, unsigned depth, unsigned count, bool print = false, bool barrier = false,
    bool shared = false, bool siblings = false) {
  test::Check(!siblings || (depth == 2 && !shared));
  const auto body = siblings ? Siblings(count, barrier) : Body(depth, count, barrier, shared);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(depth + 1, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  const auto planned = BuildEvaluationPlan(graph);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto calls = BuildCallPlan(graph, evaluation);
  if (barrier || shared) { test::Check(std::get<CallPlanError>(calls) == CallPlanError::kReference); return 0; }
  test::Check(std::holds_alternative<CallPlan>(calls));
  unsigned objects = 0;
  for (const auto& object : evaluation.logical_objects) objects += object.has_value();
  test::Check(objects == (siblings ? count + 1 : depth * count));
  if (depth == 2 && count == 1) {
    const auto outer = evaluation.literals->objects[0].allocation;
    const auto inner = evaluation.literals->objects[1].allocation;
    auto forged = evaluation; forged.logical_objects[inner.value].reset();
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[inner.value].consumer = evaluation.operations[outer.value].consumer;
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.logical_objects[inner.value] = evaluation.logical_objects[outer.value];
    test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, forged)));
  }
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"item", u"method", u"flag", u"left", u"right", u"target"};
  syntax.parameters = {{NameId{5}, NameEvidence::kDebugRange}};
  std::vector<ConstantProperty> properties;
  if (siblings) for (unsigned i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
    properties.push_back({NameId{kFieldNameBase + i}, NullLiteral{}});
  }
  properties.push_back({NameId{0}, NullLiteral{}});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value};
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation)) constants[i] = NameId{field->name.value};
    if (const auto* object = std::get_if<ValueObjectAllocation>(&graph.ops[i].operation))
      constants[i] = !siblings ? AggregateConstant{0, 1} : object->literal->value == 0 ?
          AggregateConstant{0, count} : AggregateConstant{count, 1};
  }
  const ExpressionConstants resolved{constants, syntax.names.size(), 0, {}, properties};
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
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
    test::Check(option == "--source" || option == "--deep-source" || option == "--siblings-source");
    Run(abc::ProfileId::kDynamic1206, option == "--deep-source" ? 7 : 2, 2, true, false, false,
        option == "--siblings-source"); return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const auto depth : {1U, 2U, 7U, 32U, 64U, 128U, 200U, 201U}) Run(profile, depth, 1);
    for (const bool siblings : {false, true}) {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
        const auto work = Run(profile, 2, count, false, false, false, siblings);
        if (count == 402) test::Check(work < 3 * previous);
        previous = work;
      }
    }
    Run(profile, 2, 1, false, true);
    Run(profile, 2, 1, false, false, true);
    Run(profile, 2, 2, false, true, false, true);
  }
}
