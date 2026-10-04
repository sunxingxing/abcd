#include "../../../src/source/class_initializers.h"
#include "../../../src/source/class_self.h"
#include "../../../src/source/class_definitions.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/analysis/value_edits.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"
#include <array>
namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class AccessorCase { kValid, kWrongHome, kEscape, kNameFlag, kClassEscape, kPrototypeEscape, kBarrier, kUsedResult, kLaterClassUse, kLaterPrototypeUse, kPair, kEnvironment, kLaterBlock, kLaterPhi, kLaterLoop, kSplitInstall, kPureDiamond, kEffectDiamond, kPureLoop, kRepeatedInstall, kConditionalInstall, kBalancedEffects };
void Accessors(abc::ProfileId profile, unsigned count, bool prototype, bool setter, AccessorCase kind = AccessorCase::kValid, bool ordinary = false) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t n) { bytes.push_back(n); bytes.push_back(n >> 8); };
  const auto load = [&](std::uint8_t r) { op(abc::Opcode::kLDA_V8); bytes.push_back(r); };
  const auto save = [&](std::uint8_t r) { op(abc::Opcode::kSTA_V8); bytes.push_back(r); };
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0); save(1);
  for (unsigned i = 0; i < count; ++i) {
    const auto conditional = bytes.size();
    if (kind == AccessorCase::kConditionalInstall) { load(6); op(abc::Opcode::kJEQZ_IMM8); bytes.push_back(0); }
    load(1);
    if (prototype) { op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.push_back(0); word(13); }
    save(2);
    if (kind == AccessorCase::kPrototypeEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == AccessorCase::kClassEscape) { load(1); op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == AccessorCase::kBarrier) op(abc::Opcode::kDEBUGGER);
    if (kind == AccessorCase::kEnvironment) { op(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(0); }
    op(abc::Opcode::kLDA_STR_ID16); word(15); save(3);
    if (kind == AccessorCase::kPureDiamond || kind == AccessorCase::kEffectDiamond || kind == AccessorCase::kBalancedEffects) {
      load(6); op(abc::Opcode::kJEQZ_IMM8); bytes.push_back(5);
      op(kind == AccessorCase::kEffectDiamond || kind == AccessorCase::kBalancedEffects ? abc::Opcode::kDEBUGGER : abc::Opcode::kNOP);
      op(abc::Opcode::kJMP_IMM8); bytes.push_back(3); op(kind == AccessorCase::kBalancedEffects ? abc::Opcode::kDEBUGGER : abc::Opcode::kNOP);
    }
    if (kind == AccessorCase::kPureLoop) { load(6); op(abc::Opcode::kJNEZ_IMM8); bytes.push_back(static_cast<std::uint8_t>(-2)); }
    const auto repeated = bytes.size();
    load(kind == AccessorCase::kWrongHome ? 3 : 2);
    op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(0); word(11); bytes.push_back(setter ? 1 : 0);
    save(setter ? 5 : 4);
    if (kind == AccessorCase::kSplitInstall) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (kind == AccessorCase::kEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (ordinary) {
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); bytes.push_back(0); word(15); bytes.push_back(2);
      continue;
    }
    if (kind == AccessorCase::kPair) {
      load(2); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(0); word(11); bytes.push_back(1); save(5);
    } else { op(abc::Opcode::kLDUNDEFINED); save(setter ? 4 : 5); }
    op(kind == AccessorCase::kNameFlag ? abc::Opcode::kLDTRUE : abc::Opcode::kLDFALSE);
    op(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8);
    bytes.insert(bytes.end(), {2, 3, 4, 5});
    if (kind == AccessorCase::kRepeatedInstall) {
      load(6); const auto branch = bytes.size(); op(abc::Opcode::kJNEZ_IMM8);
      bytes.push_back(static_cast<std::uint8_t>(static_cast<int>(repeated) - static_cast<int>(branch)));
    }
    if (kind == AccessorCase::kConditionalInstall) bytes[conditional + 3] = bytes.size() - (conditional + 2);
  }
  const bool later_flow = kind == AccessorCase::kLaterBlock || kind == AccessorCase::kLaterPhi || kind == AccessorCase::kLaterLoop;
  const bool installs = kind == AccessorCase::kSplitInstall || kind == AccessorCase::kPureDiamond || kind == AccessorCase::kPureLoop;
  if (kind == AccessorCase::kLaterBlock) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
  if (kind == AccessorCase::kLaterLoop) {
    load(6); op(abc::Opcode::kJNEZ_IMM8); bytes.push_back(static_cast<std::uint8_t>(-2));
  }
  if (kind == AccessorCase::kLaterPhi) {
    load(6); op(abc::Opcode::kJEQZ_IMM8); bytes.push_back(8);
    load(1); save(0); op(abc::Opcode::kJMP_IMM8); bytes.push_back(6);
    load(2); save(0); load(0); op(abc::Opcode::kRETURN);
  } else if (later_flow || kind == AccessorCase::kLaterClassUse || kind == AccessorCase::kLaterPrototypeUse) {
    load(kind == AccessorCase::kLaterClassUse ? 1 : 2);
    op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0);
  }
  if (installs || kind == AccessorCase::kConditionalInstall || kind == AccessorCase::kEffectDiamond || kind == AccessorCase::kRepeatedInstall) load(1);
  if (kind != AccessorCase::kLaterPhi) op(kind == AccessorCase::kUsedResult || installs || kind == AccessorCase::kConditionalInstall || kind == AccessorCase::kEffectDiamond || kind == AccessorCase::kRepeatedInstall ? abc::Opcode::kRETURN : abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(bytes);
  const auto args = later_flow || installs || kind == AccessorCase::kConditionalInstall || kind == AccessorCase::kEffectDiamond || kind == AccessorCase::kRepeatedInstall || kind == AccessorCase::kBalancedEffects ? 1U : 0U;
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 6 + args));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 6, args));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(6, args, {false, false, false, false}));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto result = std::get<ClassDefinitionSites>(FindClassDefinitions(graph));
  const bool candidate = kind != AccessorCase::kWrongHome && kind != AccessorCase::kEscape && kind != AccessorCase::kNameFlag && !(ordinary && kind == AccessorCase::kUsedResult);
  test::Check(result.sites.size() == (candidate ? count : 0));
  for (const auto& site : result.sites) {
    test::Check(site.creation < site.definition && site.prototype.has_value() == prototype && site.key.value == 15);
    test::Check(site.Method(abc::ClassMethodKind::kOrdinary).has_value() == ordinary);
    test::Check(site.Method(abc::ClassMethodKind::kGetter).has_value() == (!ordinary && !setter));
    test::Check(site.Method(abc::ClassMethodKind::kSetter).has_value() == (!ordinary && (setter || kind == AccessorCase::kPair)));
  }
  if (candidate) {
    const auto projected = ProjectClassDefinitions(graph, result.sites);
    if (installs || later_flow || kind == AccessorCase::kValid || kind == AccessorCase::kLaterClassUse || kind == AccessorCase::kLaterPrototypeUse || kind == AccessorCase::kPair) {
      test::Check(std::holds_alternative<ClassDefinitionProjection>(projected));
      const auto& projection = std::get<ClassDefinitionProjection>(projected);
      test::Check(projection.prototypes.size() == (prototype ? count : 0));
      for (const auto& site : result.sites) {
        test::Check(projection.owners[site.definition.value] == site.creation);
        for (const auto& method : site.Methods())
          if (method.operation) test::Check(projection.owners[method.operation->value] == site.creation);
        if (site.prototype) {
          const bool retain = (kind == AccessorCase::kLaterPrototypeUse || later_flow) && site == result.sites.back();
          test::Check(projection.owners[site.prototype->value] == (retain ? std::optional<OpId>{} : site.creation));
        }
      }
      auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, result.sites);
      test::Check(std::holds_alternative<EvaluationPlan>(evaluated));
      auto& evaluation = std::get<EvaluationPlan>(evaluated);
      test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, evaluation)));
      evaluation.class_projection.owners[result.sites.front().definition.value].reset();
      test::Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
      auto forged = result.sites; ++forged.front().key.value;
      test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(graph, forged)) == ClassDefinitionError::kInput);
      forged = result.sites; ++forged.front().order.value;
      test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(graph, forged)) == ClassDefinitionError::kInput);
      if (count > 1) {
        forged = result.sites; std::swap(forged[0], forged[1]);
        test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(graph, forged)) == ClassDefinitionError::kInput);
      }
      test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(graph, result.sites, {0})) == ClassDefinitionError::kResourceLimit);
      test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(graph, result.sites, {UINT32_MAX, 0})) == ClassDefinitionError::kWorkLimit);
      if (later_flow) {
        auto invalid = graph;
        invalid.edges.front().target = {static_cast<std::uint32_t>(invalid.blocks.size())};
        test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(invalid, result.sites)) == ClassDefinitionError::kFlow);
        if (count == 1) {
          invalid = graph;
          const auto& creation = invalid.ops[result.sites.front().creation.value];
          // Keep CFG and def-use chains valid while introducing an unavailable
          // class result at its own heritage read. No stale proof may admit it.
          test::Check(!ReplaceUse(invalid, creation.first_use, *creation.result));
          test::Check(std::get<ClassDefinitionError>(ProjectClassDefinitions(invalid, result.sites)) == ClassDefinitionError::kFlow);
        }
      }
    } else test::Check(std::get<ClassDefinitionError>(projected) == ClassDefinitionError::kFlow);
  }
  test::Check(std::get<ClassDefinitionError>(FindClassDefinitions(graph, {0})) == ClassDefinitionError::kResourceLimit);
  test::Check(std::get<ClassDefinitionError>(FindClassDefinitions(graph, {UINT32_MAX, 0})) == ClassDefinitionError::kWorkLimit);
}
enum class Case { kSingle, kRepeated, kEscape, kUsedResult, kWrongReceiver, kClassEscape, kBarrier, kPrototype, kLaterBlock, kSelfStore, kSelfOverwrite, kSelfExposed, kSplitCreation, kSplitCall, kDiamondCreation, kDiamondCall, kBypassCall, kDivergentCall, kRepeatedCall, kBranchEffects };
void Run(abc::ProfileId profile, unsigned count, Case kind, std::uint64_t* projection_work = nullptr) {
  const bool self = kind == Case::kSelfStore || kind == Case::kSelfOverwrite || kind == Case::kSelfExposed;
  constexpr std::uint8_t kSelfSlot = 2, kSlotShift = 4;
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) { bytes.push_back(static_cast<std::uint8_t>(code)); };
  const auto word = [&](std::uint16_t n) { bytes.push_back(n); bytes.push_back(n >> 8); };
  const bool cross = kind >= Case::kSplitCreation;
  const auto corridor = [&](bool effects = false) {
    op(abc::Opcode::kLDA_V8); bytes.push_back(3);
    op(abc::Opcode::kJEQZ_IMM8); bytes.push_back(5);
    op(effects ? abc::Opcode::kDEBUGGER : abc::Opcode::kNOP);
    op(abc::Opcode::kJMP_IMM8); bytes.push_back(3);
    op(effects ? abc::Opcode::kDEBUGGER : abc::Opcode::kNOP);
  };
  op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); bytes.push_back(0);
  for (unsigned i = 0; i < count; ++i) {
    if (self) {
      op(abc::Opcode::kNEWLEXENV_IMM8); bytes.push_back(kSelfSlot+1);
      if (kind == Case::kSelfExposed) op(abc::Opcode::kDEBUGGER);
    }
    op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
    bytes.push_back(0); word(7); word(9); word(0); bytes.push_back(0);
    op(abc::Opcode::kSTA_V8); bytes.push_back(1);
    if (self) {
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); bytes.push_back(kSelfSlot << kSlotShift);
      if (kind == Case::kSelfOverwrite) { op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); bytes.push_back(kSelfSlot << kSlotShift); }
    }
    if (kind == Case::kClassEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    if (kind == Case::kBarrier) op(abc::Opcode::kDEBUGGER);
    if (kind == Case::kPrototype) {
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.push_back(0); word(13);
      op(abc::Opcode::kLDA_V8); bytes.push_back(1);
    }
    if (kind == Case::kSplitCreation) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (kind == Case::kDiamondCreation) corridor();
    if (cross) { op(abc::Opcode::kLDA_V8); bytes.push_back(1); }
    op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8); bytes.push_back(1); word(11); bytes.push_back(0);
    op(abc::Opcode::kSTA_V8); bytes.push_back(2);
    if (kind == Case::kSplitCall) { op(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    if (kind == Case::kDiamondCall || kind == Case::kBranchEffects) corridor(kind == Case::kBranchEffects);
    if (kind == Case::kDivergentCall) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(3);
      op(abc::Opcode::kJNEZ_IMM8); bytes.push_back(static_cast<std::uint8_t>(-2));
    }
    std::optional<std::size_t> bypass;
    if (kind == Case::kBypassCall) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(3);
      op(abc::Opcode::kJEQZ_IMM8); bypass = bytes.size(); bytes.push_back(0);
      op(abc::Opcode::kLDA_V8); bytes.push_back(2);
    }
    if (kind == Case::kEscape) { op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0); }
    const auto call_pc = bytes.size();
    if (cross) { op(abc::Opcode::kLDA_V8); bytes.push_back(2); }
    op(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.push_back(2); bytes.push_back(kind == Case::kWrongReceiver ? 2 : 1);
    if (kind == Case::kRepeatedCall) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(3);
      const auto branch = bytes.size(); op(abc::Opcode::kJNEZ_IMM8);
      bytes.push_back(static_cast<std::uint8_t>(static_cast<int>(call_pc) - static_cast<int>(branch)));
    }
    if (bypass) bytes[*bypass] = bytes.size() - (*bypass - 1);
    if (kind == Case::kLaterBlock) {
      constexpr std::uint8_t kJumpSize = 2;
      op(abc::Opcode::kJMP_IMM8); bytes.push_back(kJumpSize);
      op(abc::Opcode::kLDA_V8); bytes.push_back(1);
      op(abc::Opcode::kSTMODULEVAR_IMM8); bytes.push_back(0);
    }
    if (kind == Case::kRepeated) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(2);
      op(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.push_back(3); bytes.push_back(1);
    }
  }
  op(kind == Case::kUsedResult ? abc::Opcode::kRETURN : abc::Opcode::kRETURNUNDEFINED);
  const auto body = test::Body(bytes);
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, profile, 3 + cross));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 3, cross));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, cross, {false, false, false, false}));
  auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph, graph.revision)));
  const auto found = FindClassInitializerCalls(graph);
  test::Check(std::holds_alternative<ClassInitializerCalls>(found));
  const auto& calls = std::get<ClassInitializerCalls>(found);
  test::Check(calls.revision == graph.revision);
  SemanticBudget exact; exact.work = calls.work;
  test::Check(std::holds_alternative<ClassInitializerCalls>(FindClassInitializerCalls(graph, exact)));
  --exact.work;
  test::Check(std::get<ClassInitializerError>(FindClassInitializerCalls(graph, exact)) == ClassInitializerError::kWorkLimit);
  test::Check(calls.calls.size() == ((kind == Case::kSingle || kind == Case::kClassEscape || kind == Case::kBarrier || kind == Case::kPrototype || kind == Case::kLaterBlock || self || cross) ? count : 0));
  for (const auto& call : calls.calls) {
    test::Check(call.creation < call.method && call.method < call.call);
    test::Check(graph.ops[call.creation.value].result == call.receiver);
    test::Check(std::holds_alternative<ValueClassCreation>(graph.ops[call.creation.value].operation));
  }
  if (calls.calls.empty()) return;
  if (self) {
    const auto environments = std::get<ValueEnvironments>(BuildValueEnvironments(graph));
    const auto cells = std::get<CellPlan>(BuildCellPlan(graph,environments));
    const auto result = std::get<ClassSelfStores>(FindClassSelfStores(graph,environments,cells,calls.calls));
    test::Check(result.stores.size() == (kind == Case::kSelfStore ? count : 0));
    for (const auto& binding : result.stores) {
      test::Check(std::get<AllocatedCell>(cells.cells[binding.cell.value].storage).slot.value == kSelfSlot);
      test::Check(binding.creation < binding.store && binding.store < binding.initializer);
    }
    if (!calls.calls.empty()) {
      auto wrong = calls.calls; ++wrong.front().call.value;
      test::Check(std::get<ClassSelfError>(FindClassSelfStores(graph,environments,cells,wrong)) == ClassSelfError::kInput);
    }
    SemanticBudget limited; limited.work = result.work;
    test::Check(std::holds_alternative<ClassSelfStores>(FindClassSelfStores(graph,environments,cells,calls.calls,limited)));
    --limited.work;
    test::Check(std::get<ClassSelfError>(FindClassSelfStores(graph,environments,cells,calls.calls,limited)) == ClassSelfError::kWorkLimit);
    auto stale = cells; ++stale.revision.value;
    test::Check(std::holds_alternative<ClassSelfError>(FindClassSelfStores(graph,environments,stale,calls.calls)));
    test::Check(std::holds_alternative<ClassInitializerError>(ProjectClassMembers(graph,calls.calls)));
    return;
  }
  const auto projected = ProjectClassMembers(graph, calls.calls);
  if (kind == Case::kClassEscape || kind == Case::kBarrier || kind == Case::kBypassCall || kind == Case::kDivergentCall || kind == Case::kRepeatedCall || kind == Case::kBranchEffects) {
    test::Check(std::get<ClassInitializerError>(projected) == ClassInitializerError::kFlow);
    return;
  }
  const auto& projection = std::get<ClassProjection>(projected);
  if (projection_work) *projection_work = projection.work;
  exact = {}; exact.work = projection.work;
  test::Check(std::holds_alternative<ClassProjection>(ProjectClassMembers(graph, calls.calls, exact)));
  --exact.work;
  test::Check(std::get<ClassInitializerError>(ProjectClassMembers(graph, calls.calls, exact)) == ClassInitializerError::kWorkLimit);
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
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--cross-install") {
    for (const auto kind : {AccessorCase::kSplitInstall, AccessorCase::kPureDiamond, AccessorCase::kPureLoop})
      Accessors(abc::ProfileId::kDynamic1206, 1, true, false, kind);
    return 0;
  }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    std::array<std::uint64_t, 4> previous{};
    for (const auto count : {1U, 200U, 201U, 402U}) {
      Accessors(profile, count, false, false); Accessors(profile, count, true, true);
      Accessors(profile, count, false, false, AccessorCase::kValid, true);
      Accessors(profile, count, true, true, AccessorCase::kValid, true);
      for (const auto kind : {AccessorCase::kLaterBlock, AccessorCase::kLaterPhi, AccessorCase::kLaterLoop}) {
        Accessors(profile, count, true, true, kind);
        Accessors(profile, count, true, false, kind, true);
      }
      for (const auto kind : {AccessorCase::kSplitInstall, AccessorCase::kPureDiamond, AccessorCase::kPureLoop})
        Accessors(profile, count, true, false, kind);
      Run(profile, count, Case::kSingle); Run(profile, count, Case::kPrototype); Run(profile,count,Case::kLaterBlock); Run(profile,count,Case::kSelfStore); Run(profile,count,Case::kSelfOverwrite); Run(profile,count,Case::kSelfExposed);
      std::size_t index = 0;
      for (const auto kind : {Case::kSplitCreation, Case::kSplitCall, Case::kDiamondCreation, Case::kDiamondCall}) {
        std::uint64_t work = 0; Run(profile, count, kind, &work);
        if (count == 201 || count == 402) test::Check(work < 3 * previous[index]);
        previous[index++] = work;
        if (profile == abc::ProfileId::kDynamic1206) std::cout << "class projection count=" << count << " kind=" << static_cast<unsigned>(kind) << " work=" << work << '\n';
      }
    }
    for (const auto kind : {AccessorCase::kWrongHome, AccessorCase::kEscape, AccessorCase::kNameFlag, AccessorCase::kClassEscape, AccessorCase::kPrototypeEscape, AccessorCase::kBarrier, AccessorCase::kUsedResult, AccessorCase::kLaterClassUse, AccessorCase::kLaterPrototypeUse, AccessorCase::kPair, AccessorCase::kEnvironment})
      Accessors(profile, 1, true, false, kind);
    for (const auto kind : {AccessorCase::kConditionalInstall, AccessorCase::kEffectDiamond, AccessorCase::kRepeatedInstall, AccessorCase::kBalancedEffects})
      Accessors(profile, 1, true, false, kind);
    for (const auto kind : {AccessorCase::kWrongHome, AccessorCase::kEscape, AccessorCase::kClassEscape,
        AccessorCase::kPrototypeEscape, AccessorCase::kBarrier, AccessorCase::kUsedResult,
        AccessorCase::kLaterClassUse, AccessorCase::kLaterPrototypeUse, AccessorCase::kEnvironment})
      Accessors(profile, 1, true, false, kind, true);
    Run(profile, 1, Case::kClassEscape); Run(profile, 1, Case::kBarrier);
    for (const auto kind : {Case::kRepeated, Case::kEscape, Case::kUsedResult, Case::kWrongReceiver}) Run(profile, 1, kind);
    for (const auto kind : {Case::kBypassCall, Case::kDivergentCall, Case::kRepeatedCall, Case::kBranchEffects}) Run(profile, 1, kind);
  }
}
