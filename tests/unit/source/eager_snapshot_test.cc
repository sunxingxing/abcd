#include <iostream>
#include <sstream>

#include "../../../src/source/function_tree.h"
#include "../../../src/source/instruction_sources.h"
#include "../../../src/source/tree_semantics.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "abcd/source/tree_printer.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
enum class Consumer {
  kWrite,
  kTypeof,
  kComputedRead,
  kEqual,
  kComputedWrite,
  kCall,
  kRepeatedCall
};
struct Fixture {
  ValueProgram graph;
  FunctionInput input;
};
Fixture Read(Consumer consumer, abc::ProfileId profile, unsigned copies = 0,
             bool swapped = false, bool barrier = false, bool split = false) {
  const auto wire = [](abc::Opcode opcode) {
    return static_cast<std::uint8_t>(opcode);
  };
  constexpr std::uint8_t kParameter = 3, kKey = 2;
  const std::uint8_t saved = swapped ? 1 : 0, alias = swapped ? 0 : 1;
  std::vector<std::uint8_t> bytes;
  if (consumer == Consumer::kComputedWrite)
    bytes = {wire(abc::Opcode::kLDA_V8),
             kParameter,
             wire(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),
             0,
             2,
             0,
             wire(abc::Opcode::kSTA_V8),
             kKey};
  bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8), kParameter,
                             wire(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0,
                             wire(abc::Opcode::kSTA_V8), saved});
  for (unsigned i = 0; i < copies; ++i)
    bytes.insert(bytes.end(), {wire(abc::Opcode::kMOV_V8_V8), alias, saved,
                               wire(abc::Opcode::kMOV_V8_V8), saved, alias});
  if (barrier)
    bytes.insert(bytes.end(),
                 {wire(abc::Opcode::kLDA_V8), kParameter,
                  wire(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
  if (split) bytes.insert(bytes.end(), {wire(abc::Opcode::kJMP_IMM8), 2});
  bytes.insert(bytes.end(), {wire(abc::Opcode::kLDA_V8), saved});
  switch (consumer) {
    case Consumer::kWrite:
      break;
    case Consumer::kTypeof:
      bytes.insert(bytes.end(), {wire(abc::Opcode::kTYPEOF_IMM8), 0});
      break;
    case Consumer::kComputedRead:
      bytes.insert(bytes.end(),
                   {wire(abc::Opcode::kLDOBJBYVALUE_IMM8_V8), 0, kParameter});
      break;
    case Consumer::kEqual:
      bytes.insert(bytes.end(), {wire(abc::Opcode::kLDNULL),
                                 wire(abc::Opcode::kEQ_IMM8_V8), 0, saved});
      break;
    case Consumer::kComputedWrite:
      break;
    case Consumer::kCall:
      bytes.insert(bytes.end(),
                   {wire(abc::Opcode::kLDA_V8), kParameter,
                    wire(abc::Opcode::kCALLARG1_IMM8_V8), 0, saved});
      break;
    case Consumer::kRepeatedCall:
      bytes.insert(bytes.end(),
                   {wire(abc::Opcode::kLDA_V8), kParameter,
                    wire(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, saved, saved});
      break;
  }
  if (consumer == Consumer::kComputedWrite)
    bytes.insert(bytes.end(), {wire(abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8), 0,
                               kParameter, kKey});
  else if (consumer != Consumer::kCall && consumer != Consumer::kRepeatedCall)
    bytes.insert(bytes.end(), {wire(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0,
                               1, 0, kParameter});
  bytes.insert(bytes.end(),
               {wire(abc::Opcode::kLDA_V8), saved, wire(abc::Opcode::kRETURN)});
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta)
      instruction.branch_target = abc::CodeOffset{
          static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  const auto method = std::get<SemanticMethod>(
      BuildSemanticMethod(body, profile, kParameter + 1));
  const auto checked = VerifySemanticSource(body, method, kParameter, 1);
  Check(std::holds_alternative<SemanticProof>(checked));
  const auto frame =
      std::get<abc::FrameLayout>(abc::BuildFrameLayout(kParameter, 1, {}));
  RouteInputIdentity identity{};
  identity.profile = profile;
  auto input = std::get<FunctionInput>(FunctionInput::Build(
      identity, {}, {}, body, frame, InputCaptureMode::kClosed));
  auto graph = std::get<ValueProgram>(ImportNormalValues(
      body, method, std::get<SemanticProof>(checked), frame));
  Check(!std::holds_alternative<ValueSimplifyError>(
      SimplifyValues(graph, graph.revision)));
  return {std::move(graph), std::move(input)};
}
std::string Source(const Fixture& fixture, const EvaluationPlan& evaluation) {
  const auto& graph = fixture.graph;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = fixture.input.Identity();
  syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"probe", u"result", u"key", u"barrier"};
  syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation))
      constants[i] = NameId{read->name.value + 1};
    if (const auto* write =
            std::get_if<ValueNamedWrite>(&graph.ops[i].operation))
      constants[i] = NameId{write->name.value + 1};
  }
  const ExpressionConstants view{constants, 5, 0, {}};
  const auto built =
      BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  Check(tree.recovery_issues.empty());
  Check(std::holds_alternative<TreeSemantics>(
      VerifyTreeSemantics(graph, control, evaluation, tree, view)));
  const auto sources = BuildInstructionSources(fixture.input, graph, tree);
  Check(std::holds_alternative<InstructionSources>(sources));
  for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
    const auto* assignment =
        std::get_if<AssignmentExpr>(&tree.expressions[i].expression);
    if (!assignment ||
        !std::holds_alternative<SymbolTarget>(assignment->target))
      continue;
    auto wrong = tree;
    std::get<AssignmentExpr>(wrong.expressions[i].expression).target =
        SymbolTarget{tree.parameters.front().symbol};
    Check(std::holds_alternative<TreeSemanticError>(
        VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
    break;
  }
  std::ostringstream output;
  Check(
      std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  return output.str();
}
}  // namespace
int main(int argc, char**) {
  const bool emit = argc == 2;
  if (emit) std::cout << '[';
  bool first = true;
  for (const auto profile :
       {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
        abc::ProfileId::kDynamic1301})
    for (const auto consumer :
         {Consumer::kWrite, Consumer::kTypeof, Consumer::kComputedRead,
          Consumer::kEqual, Consumer::kComputedWrite, Consumer::kCall,
          Consumer::kRepeatedCall})
      for (unsigned copies : {0U, 1U, 200U, 201U, 402U})
        for (bool swapped : {false, true})
          for (bool barrier : {false, true})
            for (bool split : {false, true}) {
              const auto fixture =
                  Read(consumer, profile, copies, swapped, barrier, split);
              const auto& graph = fixture.graph;
              const auto evaluation =
                  std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
              Check(std::holds_alternative<std::uint64_t>(
                  VerifyEvaluationPlan(graph, evaluation)));
              unsigned snapshots = 0;
              for (std::size_t i = 0; i < graph.ops.size(); ++i) {
                if (evaluation.operations[i].kind !=
                    EvaluationKind::kPredicateSnapshot)
                  continue;
                ++snapshots;
                auto wrong = evaluation;
                wrong.operations[i].consumer.reset();
                Check(std::holds_alternative<EvaluationError>(
                    VerifyEvaluationPlan(graph, wrong)));
              }
              const unsigned expected =
                  !barrier && !split && consumer != Consumer::kComputedWrite
                      ? 1U
                      : 0U;
              if (snapshots != expected)
                std::cerr << "consumer " << static_cast<unsigned>(consumer)
                          << " copies " << copies << " swapped " << swapped
                          << " barrier " << barrier << " split " << split
                          << " snapshots " << snapshots << " expected "
                          << expected << '\n';
              Check(snapshots == expected);
              if (!expected) {
                bool forged = false;
                for (std::size_t i = 0; i < graph.ops.size(); ++i) {
                  const auto* read =
                      std::get_if<ValueNamedRead>(&graph.ops[i].operation);
                  if (!read || read->name.value != 0) continue;
                  const auto value = *graph.ops[i].result;
                  std::optional<UseId> first_use;
                  for (auto use = graph.values[value.value].first_use; use;
                       use = graph.uses[use->value].next) {
                    const auto& occurrence = graph.uses[use->value];
                    const auto* owner = std::get_if<OpId>(&occurrence.owner);
                    if (occurrence.life != ValueLife::kLive || !owner ||
                        graph.ops[owner->value].life != ValueLife::kLive)
                      continue;
                    if (!first_use || *use < *first_use) first_use = use;
                  }
                  Check(first_use.has_value());
                  auto wrong = evaluation;
                  wrong.operations[i] = {EvaluationKind::kPredicateSnapshot,
                                         first_use,
                                         TemporaryReason::kMultipleUses};
                  Check(std::holds_alternative<EvaluationError>(
                      VerifyEvaluationPlan(graph, wrong)));
                  forged = true;
                }
                Check(forged);
              }
              constexpr std::uint64_t kWorkPerOperation = 256;
              Check(evaluation.work <=
                    kWorkPerOperation *
                        (graph.ops.size() + graph.blocks.size()));
              if (copies == 0 && !swapped && !barrier && !split) {
                SemanticBudget exact{};
                exact.work = evaluation.work;
                Check(std::holds_alternative<EvaluationPlan>(
                    BuildEvaluationPlan(graph, exact)));
                --exact.work;
                Check(std::get<EvaluationError>(BuildEvaluationPlan(
                          graph, exact)) == EvaluationError::kWorkLimit);
              }
              const auto text = Source(fixture, evaluation);
              if (emit) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"consumer\":" << static_cast<unsigned>(consumer)
                          << ",\"barrier\":" << std::boolalpha << barrier
                          << ",\"source\":(" << text << ")}";
              }
            }
  if (emit) std::cout << "]\n";
}
