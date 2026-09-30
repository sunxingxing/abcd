#include "../../../src/source/class_initializers.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Case { kSingle, kRepeated, kEscape, kUsedResult, kWrongReceiver };
void Run(abc::ProfileId profile, unsigned count, Case kind) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t n) { bytes.push_back(n); bytes.push_back(n >> 8); };
  op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); bytes.push_back(0);
  for (unsigned i = 0; i < count; ++i) {
    op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
    bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0);
    op(abc::Opcode::kSTA_V8); bytes.push_back(1);
    op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(1); word(11); bytes.push_back(0);
    op(abc::Opcode::kSTA_V8); bytes.push_back(2);
    if (kind == Case::kEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    op(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.push_back(2); bytes.push_back(kind == Case::kWrongReceiver ? 2 : 1);
    if (kind == Case::kRepeated) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(2);
      op(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.push_back(3); bytes.push_back(1);
    }
  }
  op(kind == Case::kUsedResult ? abc::Opcode::kRETURN : abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(bytes);
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 3));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 3, 0));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 0, {false, false, false, false}));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto found = FindClassInitializerCalls(graph);
  test::Check(std::holds_alternative<ClassInitializerCalls>(found));
  const auto& calls = std::get<ClassInitializerCalls>(found);
  test::Check(calls.revision == graph.revision);
  test::Check(calls.calls.size() == (kind == Case::kSingle ? count : 0));
  for (const auto& call : calls.calls) {
    test::Check(call.creation < call.method && call.method < call.call);
    test::Check(graph.ops[call.creation.value].result == call.receiver);
    test::Check(std::holds_alternative<ValueClassCreation>(graph.ops[call.creation.value].operation));
  }
  if (calls.calls.empty()) return;
  auto& creation = std::get<ValueFunctionCreation>(graph.ops[calls.calls[0].method.value].operation);
  creation.length = 1;
  test::Check(std::get<ClassInitializerCalls>(FindClassInitializerCalls(graph)).calls.size() == count - 1);
  creation.length = 0;
  const auto use = graph.ops[calls.calls[0].method.value].first_use;
  graph.uses[use.value].purpose = ReadPurpose::kOperand;
  test::Check(std::get<ClassInitializerError>(FindClassInitializerCalls(graph)) == ClassInitializerError::kInput);
}
}
int main() {
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const auto count : {1U, 200U, 201U, 402U}) Run(profile, count, Case::kSingle);
    for (const auto kind : {Case::kRepeated, Case::kEscape, Case::kUsedResult, Case::kWrongReceiver}) Run(profile, 1, kind);
  }
}
