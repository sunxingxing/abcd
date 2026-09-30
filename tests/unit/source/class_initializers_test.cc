#include "../../../src/source/class_initializers.h"
#include "../../../src/source/class_accessors.h"
#include "../../../src/source/evaluation_plan.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class AccessorCase { kValid, kWrongHome, kEscape, kNameFlag, kClassEscape, kPrototypeEscape, kBarrier, kUsedResult, kLaterClassUse, kLaterPrototypeUse, kPair, kEnvironment };
void Accessors(abc::ProfileId profile, unsigned count, bool prototype, bool setter, AccessorCase kind = AccessorCase::kValid) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t n) { bytes.push_back(n); bytes.push_back(n >> 8); };
  const auto load = [&](std::uint8_t r) { op(abc::Opcode::kLDA_V8); bytes.push_back(r); };
  const auto save = [&](std::uint8_t r) { op(abc::Opcode::kSTA_V8); bytes.push_back(r); };
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0); save(1);
  for (unsigned i = 0; i < count; ++i) {
    load(1);
    if (prototype) { op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.push_back(0); word(13); }
    save(2);
    if (kind == AccessorCase::kPrototypeEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == AccessorCase::kClassEscape) { load(1); op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == AccessorCase::kBarrier) op(abc::Opcode::kDEBUGGER);
    if (kind == AccessorCase::kEnvironment) { op(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(0); }
    op(abc::Opcode::kLDA_STR_ID16); word(15); save(3);
    load(kind == AccessorCase::kWrongHome ? 3 : 2);
    op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(0); word(11); bytes.push_back(setter ? 1 : 0);
    save(setter ? 5 : 4);
    if (kind == AccessorCase::kEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == AccessorCase::kPair) {
      load(2); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(0); word(11); bytes.push_back(1); save(5);
    } else { op(abc::Opcode::kLDUNDEFINED); save(setter ? 4 : 5); }
    op(kind == AccessorCase::kNameFlag ? abc::Opcode::kLDTRUE : abc::Opcode::kLDFALSE);
    op(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8);
    bytes.insert(bytes.end(), {2, 3, 4, 5});
  }
  if (kind == AccessorCase::kLaterClassUse || kind == AccessorCase::kLaterPrototypeUse) {
    load(kind == AccessorCase::kLaterClassUse ? 1 : 2);
    op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0);
  }
  op(kind == AccessorCase::kUsedResult ? abc::Opcode::kRETURN : abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(bytes);
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 6));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 6, 0));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(6, 0, {false, false, false, false}));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto result = std::get<ClassAccessorSites>(FindClassAccessors(graph));
  const bool candidate = kind != AccessorCase::kWrongHome && kind != AccessorCase::kEscape && kind != AccessorCase::kNameFlag;
  test::Check(result.sites.size() == (candidate ? count : 0));
  for (const auto& site : result.sites) {
    test::Check(site.creation < site.definition && site.prototype.has_value() == prototype && site.key.value == 15);
    test::Check(site.getter.has_value() == !setter && site.setter.has_value() == (setter || kind == AccessorCase::kPair));
  }
  if (candidate) {
    const auto projected = ProjectClassAccessors(graph, result.sites);
    if (kind == AccessorCase::kValid || kind == AccessorCase::kLaterClassUse || kind == AccessorCase::kLaterPrototypeUse || kind == AccessorCase::kPair) {
      const auto& projection = std::get<ClassAccessorProjection>(projected);
      test::Check(projection.prototypes.size() == (prototype ? count : 0));
      for (const auto& site : result.sites) {
        test::Check(projection.owners[site.definition.value] == site.creation);
        test::Check(projection.owners[(setter ? *site.setter : *site.getter).value] == site.creation);
        if (site.prototype) test::Check(projection.owners[site.prototype->value] == (kind == AccessorCase::kLaterPrototypeUse ? std::optional<OpId>{} : site.creation));
      }
      auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, result.sites);
      test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
      auto& evaluation = std::get<EvaluationPlan>(evaluated);
      test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
      evaluation.class_projection.owners[result.sites.front().definition.value].reset();
      test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
      auto forged = result.sites; ++forged.front().key.value;
      test::Check(std::get<ClassAccessorError>(ProjectClassAccessors(graph, forged)) == ClassAccessorError::kInput);
      if (count > 1) {
        forged = result.sites; std::swap(forged[0], forged[1]);
        test::Check(std::get<ClassAccessorError>(ProjectClassAccessors(graph, forged)) == ClassAccessorError::kInput);
      }
      test::Check(std::get<ClassAccessorError>(ProjectClassAccessors(graph, result.sites, {0})) == ClassAccessorError::kResourceLimit);
      test::Check(std::get<ClassAccessorError>(ProjectClassAccessors(graph, result.sites, {UINT32_MAX, 0})) == ClassAccessorError::kWorkLimit);
    } else test::Check(std::get<ClassAccessorError>(projected) == ClassAccessorError::kFlow);
  }
  test::Check(std::get<ClassAccessorError>(FindClassAccessors(graph, {0})) == ClassAccessorError::kResourceLimit);
  test::Check(std::get<ClassAccessorError>(FindClassAccessors(graph, {UINT32_MAX, 0})) == ClassAccessorError::kWorkLimit);
}
enum class Case { kSingle, kRepeated, kEscape, kUsedResult, kWrongReceiver, kClassEscape, kBarrier, kPrototype };
void Run(abc::ProfileId profile, unsigned count, Case kind) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t n) { bytes.push_back(n); bytes.push_back(n >> 8); };
  op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); bytes.push_back(0);
  for (unsigned i = 0; i < count; ++i) {
    op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
    bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0);
    op(abc::Opcode::kSTA_V8); bytes.push_back(1);
    if (kind == Case::kClassEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == Case::kBarrier) op(abc::Opcode::kDEBUGGER);
    if (kind == Case::kPrototype) {
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.push_back(0); word(13);
      op(abc::Opcode::kLDA_V8); bytes.push_back(1);
    }
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
  test::Check(calls.calls.size() == ((kind == Case::kSingle || kind == Case::kClassEscape || kind == Case::kBarrier || kind == Case::kPrototype) ? count : 0));
  for (const auto& call : calls.calls) {
    test::Check(call.creation < call.method && call.method < call.call);
    test::Check(graph.ops[call.creation.value].result == call.receiver);
    test::Check(std::holds_alternative<ValueClassCreation>(graph.ops[call.creation.value].operation));
  }
  if (calls.calls.empty()) return;
  const auto projected = ProjectClassMembers(graph, calls.calls);
  if (kind == Case::kClassEscape || kind == Case::kBarrier) {
    test::Check(std::get<ClassInitializerError>(projected) == ClassInitializerError::kFlow);
    return;
  }
  const auto& projection = std::get<ClassProjection>(projected);
  test::Check(projection.prototypes.size() == (kind == Case::kPrototype ? count : 0));
  for (const auto& call : calls.calls) {
    test::Check(projection.owners[call.method.value] == call.creation);
    test::Check(projection.owners[call.call.value] == call.creation);
  }
  auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, calls.calls));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
  evaluation.class_projection.owners[calls.calls.front().call.value].reset();
  test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  auto forged = calls.calls; ++forged.front().call.value;
  test::Check(std::holds_alternative<ClassInitializerError>(ProjectClassMembers(graph, forged)));

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
    for (const auto count : {1U, 200U, 201U, 402U}) {
      Accessors(profile, count, false, false); Accessors(profile, count, true, true);
      Run(profile, count, Case::kSingle); Run(profile, count, Case::kPrototype);
    }
    for (const auto kind : {AccessorCase::kWrongHome, AccessorCase::kEscape, AccessorCase::kNameFlag, AccessorCase::kClassEscape, AccessorCase::kPrototypeEscape, AccessorCase::kBarrier, AccessorCase::kUsedResult, AccessorCase::kLaterClassUse, AccessorCase::kLaterPrototypeUse, AccessorCase::kPair, AccessorCase::kEnvironment})
      Accessors(profile, 1, true, false, kind);
    Run(profile, 1, Case::kClassEscape); Run(profile, 1, Case::kBarrier);
    for (const auto kind : {Case::kRepeated, Case::kEscape, Case::kUsedResult, Case::kWrongReceiver}) Run(profile, 1, kind);
  }
}
