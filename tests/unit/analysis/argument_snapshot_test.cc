#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/semantic_validation.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "../../../src/analysis/value_edits.h"
#include "../../../src/analysis/value_rewrite.h"

#include "analysis/ssa_fixture.h"

namespace {
void CheckValues(const abcd::abc::MethodBody& body,
    const abcd::analysis::SemanticMethod& method,
    const abcd::analysis::SemanticProof& proof,
    const abcd::abc::FrameLayout& frame,
    abcd::analysis::SemanticBudget budget) {
  using namespace abcd;
  using namespace abcd::analysis;
  const auto imported = ImportNormalValues(body, method, proof, frame, budget);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  const auto& graph = std::get<ValueProgram>(imported);
  test::Check(graph.arguments == proof.arguments);
  test::Check(!VerifyImportedValues(graph, method, proof, frame, budget));
  std::vector<ValueId> representatives;
  for (std::uint32_t i = 0; i < graph.values.size(); ++i) representatives.push_back({i});
  auto changed = graph;
  changed.arguments.reset();
  test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
  test::Check(std::get<ValueRewriteError>(VerifyValueRewrite(
      graph, changed, representatives, budget)) == ValueRewriteError::kEffect);
  auto unowned = std::get<SemanticProof>(VerifySemanticSource(body, method,
      frame.registers, frame.locals, budget, CapturedValues::kReject, {},
      ClosureWrites::kUnknown, {}, CallResultValues::kUnknown,
      FrameReceiver::kUnknown, &*proof.arguments));
  unowned.arguments.reset();
  test::Check(std::holds_alternative<ValueImportFailure>(
      ImportNormalValues(body, method, unowned, frame, budget)));
  changed = graph;
  changed.arguments->frame.extra_arguments = false;
  test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
  auto exhausted = budget;
  exhausted.work = 0;
  test::Check(std::get<ValueImportFailure>(ImportNormalValues(
      body, method, proof, frame, exhausted)).kind == ValueImportError::kWorkLimit);
  std::size_t snapshots = 0;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (!std::holds_alternative<ValueArgumentSnapshot>(op.operation)) continue;
    ++snapshots;
    test::Check(std::get<ValueArgumentSnapshot>(op.operation).slots == frame.parameters);
    test::Check(op.use_count == frame.parameters);
    test::Check(graph.origins[op.origin.value].pc == body.instructions[i].offset);
    test::Check(!CanErasePureOp(op));
    auto cleared = op;
    cleared.normal = cleared.before_throw = LanguageEffect::kNone;
    cleared.exceptional = ExceptionalCompletion::kImpossible;
    test::Check(!CanErasePureOp(cleared));
    changed = graph;
    ++std::get<ValueArgumentSnapshot>(changed.ops[i].operation).slots;
    test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
    changed = graph;
    changed.ops[i].before_throw = LanguageEffect::kNone;
    test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
    for (std::uint32_t j = 0; j < op.use_count; ++j) {
      const auto& use = graph.uses[op.first_use.value + j];
      test::Check(use.purpose == ReadPurpose::kProtocol && use.input->ordinal == j);
    }
    if (op.use_count != 0) {
      changed = graph;
      changed.uses[op.first_use.value].purpose = ReadPurpose::kOperand;
      test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
      changed = graph;
      ++changed.uses[op.first_use.value].input->ordinal;
      test::Check(VerifyImportedValues(changed, method, proof, frame, budget).has_value());
    }
  }
  test::Check(snapshots != 0);
  changed = graph;
  test::Check(std::holds_alternative<ValueSimplifyStats>(
      SimplifyValues(changed, changed.revision, budget)));
  test::Check(changed.arguments == graph.arguments);
  std::size_t retained = 0;
  for (const auto& op : changed.ops) {
    if (std::holds_alternative<ValueArgumentSnapshot>(op.operation)) {
      test::Check(op.life == ValueLife::kLive);
      ++retained;
    }
  }
  test::Check(retained == snapshots);
}
}  // namespace

int main() {
  using namespace abcd;
  using namespace abcd::analysis;
  const auto body = test::Body({
      static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
      static_cast<std::uint8_t>(abc::Opcode::kRETURN)});
  for (const auto profile : {abc::ProfileId::kDynamic1202,
                            abc::ProfileId::kDynamic1206,
                            abc::ProfileId::kDynamic1301}) {
    for (const std::uint32_t count : {0u, 1u, 200u, 201u, 65536u}) {
      const auto frame = std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, count, {false, false, true, false}));
      auto budget = SemanticBudget{};
      budget.work = UINT64_MAX;
      auto built = BuildSemanticMethod(body, profile, frame.registers,
          budget, {}, &frame);
      test::Check(std::holds_alternative<SemanticMethod>(built));
      auto& method = std::get<SemanticMethod>(built);
      test::Check(std::holds_alternative<ArgumentSnapshot>(method.instructions[0].operation));
      const auto& effects = method.instructions[0].effects;
      test::Check(effects.normal ==
          (LanguageEffect::kArgumentsRead | LanguageEffect::kAllocation));
      test::Check(effects.before_throw == effects.normal &&
          effects.exceptional == ExceptionalCompletion::kMayThrow);
      const auto reads = method.ssa.Reads({0});
      test::Check(reads.size() == count);
      for (std::uint32_t i = 0; i < count; ++i) {
        test::Check(reads[i].ordinal == i);
        const auto& value = method.ssa.Values()[reads[i].value.value];
        test::Check(std::get<abc::RegisterId>(value.slot).value == i);
        test::Check(std::holds_alternative<MethodEntry>(value.origin));
      }
      test::Check(method.ssa.Reads({1})[0].value == *method.ssa.Write({0}));
      // A layout is not an actual-count/tail ownership or source certificate.
      test::Check(std::get<SemanticProofFailure>(VerifySemanticSource(
          body, method, frame.registers, 0)).kind ==
          SemanticProofError::kArgumentOwner);
      ArgumentFrameFact invocation{frame};
      const auto prove = [&] {
        return VerifySemanticSource(body, method, frame.registers, frame.locals,
            budget, CapturedValues::kReject, {}, ClosureWrites::kUnknown, {},
            CallResultValues::kUnknown, FrameReceiver::kUnknown, &invocation);
      };
      const auto proven = prove();
      test::Check(std::holds_alternative<SemanticProof>(proven));
      test::Check(std::get<SemanticProof>(proven).arguments->frame == frame);
      CheckValues(body, method, std::get<SemanticProof>(proven), frame, budget);
      invocation.frame.extra_arguments = false;
      test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kArgumentOwner);
      invocation.frame = frame;
      const auto original = method.instructions[0];
      method.instructions[0].effects.normal = LanguageEffect::kNone;
      test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
      method.instructions[0] = original;
      ++method.instructions[0].effects.normal_registers.range->count;
      test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
      method.instructions[0] = original;
      method.instructions[0].operation = NoOperation{};
      test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kSsaMismatch);
      method.instructions[0] = original;
      const auto work = budget.work;
      budget.work = 0;
      test::Check(std::get<SemanticProofFailure>(prove()).kind == SemanticProofError::kResourceLimit);
      budget.work = work;
    }
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 0)));
    auto frame = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 1, {false, false, true, false}));
    const auto internal = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
        0x61, 0, static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x64});
    const auto internal_method = std::get<SemanticMethod>(BuildSemanticMethod(
        internal, profile, frame.registers, {}, {}, &frame));
    const ArgumentFrameFact invocation{frame};
    const auto prove_invocation = [](const abc::MethodBody& code,
        const SemanticMethod& input, const abc::FrameLayout& layout) {
      const ArgumentFrameFact fact{layout};
      return VerifySemanticSource(code, input, layout.registers, layout.locals,
          {}, CapturedValues::kReject, {}, ClosureWrites::kUnknown, {},
          CallResultValues::kUnknown, FrameReceiver::kUnknown, &fact);
    };
    test::Check(std::get<SemanticProofFailure>(VerifySemanticSource(internal,
        internal_method, frame.registers, frame.locals, {}, CapturedValues::kReject,
        {}, ClosureWrites::kUnknown, {}, CallResultValues::kUnknown,
        FrameReceiver::kUnknown, &invocation)).kind == SemanticProofError::kInternalValue);
    const auto overwritten = test::Body({0x62, 9, 0, 0, 0, 0x61, 0,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x61, 0,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x64});
    const auto built = BuildSemanticMethod(overwritten, profile, 1, {}, {}, &frame);
    const auto& method = std::get<SemanticMethod>(built);
    test::Check(method.ssa.Reads({2})[0].value == *method.ssa.Write({1}));
    test::Check(method.ssa.Reads({4})[0].value == *method.ssa.Write({3}));
    test::Check(method.ssa.Write({2}) != method.ssa.Write({4}));
    test::Check(std::holds_alternative<SemanticProof>(prove_invocation(overwritten, method, frame)));
    CheckValues(overwritten, method, std::get<SemanticProof>(
        prove_invocation(overwritten, method, frame)), frame, {});
    const auto loop = test::Body({
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS),
        0x61, 0, 0x4d, 0xfd});
    const auto looping = BuildSemanticMethod(loop, profile, 1, {}, {}, &frame);
    const auto& loop_ssa = std::get<SemanticMethod>(looping).ssa;
    const auto carried = loop_ssa.Reads({0})[0].value;
    test::Check(std::holds_alternative<PhiValue>(loop_ssa.Values()[carried.value].origin));
    test::Check(loop_ssa.Inputs(carried).size() == 2);
    test::Check(loop_ssa.Inputs(carried)[1].value == *loop_ssa.Write({1}));
    test::Check(std::holds_alternative<SemanticProof>(
        prove_invocation(loop, std::get<SemanticMethod>(looping), frame)));
    const auto branch = test::Body({0x60, 0, 0x4f, 11,
        0x62, 11, 0, 0, 0, 0x61, 1, 0x4d, 9,
        0x62, 22, 0, 0, 0, 0x61, 1,
        static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS), 0x64});
    const auto pair = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(0, 2, {false, false, true, false}));
    const auto branched = BuildSemanticMethod(branch, profile, 2, {}, {}, &pair);
    const auto& branch_ssa = std::get<SemanticMethod>(branched).ssa;
    const auto joined = branch_ssa.Reads({7})[1].value;
    test::Check(std::holds_alternative<PhiValue>(branch_ssa.Values()[joined.value].origin));
    test::Check(branch_ssa.Inputs(joined).size() == 2);
    test::Check(branch_ssa.Inputs(joined)[0].value == *branch_ssa.Write({3}));
    test::Check(branch_ssa.Inputs(joined)[1].value == *branch_ssa.Write({6}));
    test::Check(std::holds_alternative<SemanticProof>(
        prove_invocation(branch, std::get<SemanticMethod>(branched), pair)));
    auto mismatched = std::get<SemanticMethod>(BuildSemanticMethod(
        body, profile, pair.registers, {}, {}, &pair));
    auto single = std::get<SemanticMethod>(BuildSemanticMethod(
        body, profile, frame.registers, {}, {}, &frame));
    mismatched.ssa = std::move(single.ssa);
    test::Check(std::get<SemanticProofFailure>(prove_invocation(body, mismatched, pair)).kind ==
        SemanticProofError::kSsaMismatch);
    const auto hidden = std::get<abc::FrameLayout>(
        abc::BuildFrameLayout(2, 4, {true, true, true, true}));
    const auto with_hidden = BuildSemanticMethod(body, profile,
        hidden.registers, {}, {}, &hidden);
    const auto& hidden_ssa = std::get<SemanticMethod>(with_hidden).ssa;
    test::Check(hidden_ssa.Reads({0}).size() == hidden.parameters);
    test::Check(std::get<abc::RegisterId>(hidden_ssa.Values()[
        hidden_ssa.Reads({0})[0].value.value].slot) == hidden.first_parameter);
    frame.extra_arguments = false;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 1, {}, {}, &frame)));
    frame.extra_arguments = true;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 2, {}, {}, &frame)));
    auto malformed = body.instructions[0];
    malformed.decoded.operand_count = 1;
    test::Check(std::holds_alternative<SemanticFailure>(
        LiftSemanticInstruction(malformed, profile, 1, &frame)));
    frame.first_parameter.value = 1;
    test::Check(std::holds_alternative<SemanticFailure>(
        BuildSemanticMethod(body, profile, 1, {}, {}, &frame)));
  }
  constexpr std::uint32_t kRuntimeCountMax = (1u << 28) - 1;
  const auto huge = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      0, kRuntimeCountMax, {false, false, true, false}));
  auto bounded = SemanticBudget{};
  bounded.work = 100;
  test::Check(std::holds_alternative<SemanticFailure>(BuildSemanticMethod(
      body, abc::ProfileId::kDynamic1206, huge.registers, bounded, {}, &huge)));
  test::Check(std::get<SemanticFailure>(BuildSemanticMethod(body,
      abc::ProfileId::kUnknown, huge.registers, {}, {}, &huge)).kind ==
      SemanticError::kUnsupportedProfile);
}
