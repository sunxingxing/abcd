#include "../../../src/source/iterator_continuation.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>

#include "../../../src/source/exception_tree.h"
#include "../../../src/source/for_of_evaluation.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/tree_failure_json.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/abc/call_type.h"
#include "abcd/analysis/exception_captures.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/analysis/exception_modules.h"
#include "abcd/corpus/abc_file.h"

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok,
           std::source_location site = std::source_location::current()) {
  if (!ok) {
    std::cerr << site.line() << " assertion failed\n";
    std::abort();
  }
}
template <class T, class... U>
T Take(std::variant<U...> result,
       std::source_location at = std::source_location::current()) {
  if (auto* p = std::get_if<T>(&result)) return std::move(*p);
  std::cerr << "line=" << at.line() << " alternative=" << result.index();
  std::visit(
      [](const auto& f) {
        if constexpr (std::is_enum_v<std::decay_t<decltype(f)>>)
          std::cerr << " enum=" << unsigned(f);
        if constexpr (requires { f.kind; })
          std::cerr << " kind=" << unsigned(f.kind);
      },
      result);
  std::cerr << '\n';
  std::abort();
}
}  // namespace
int main(int argc, char** argv) {
  auto file = Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(
      argc > 1 ? argv[1] : ABCD_ITERATOR_CONTINUATION_FIXTURE));
  const auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index =
      Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned count = 0;
  for (const auto& method : index.methods) {
    bool selected_method = method.name.ends_with("#choose");
    if (argc > 2) {
      const std::string selected = argv[2];
      selected_method = std::all_of(selected.begin(), selected.end(),
                                    [](char c) { return c >= '0' && c <= '9'; })
                            ? method.offset.value == std::stoul(selected)
                            : method.name.ends_with("#" + selected);
    }
    if (!method.code || !selected_method) continue;
    const auto call = Take<abc::CallType>(
        abc::ReadCallType(*file, header, index, method.annotations));
    const auto frame = Take<abc::FrameLayout>(
        abc::BuildFrameLayout(method.code->registers, method.code->arguments,
                              abc::DecodeCallFlags(call.value)));
    auto body = Take<abc::MethodBody>(
        abc::DecodeMethodBody(*file, header.profile, *method.code));
    const auto padding = argc > 3 ? std::stoul(argv[3]) : 0;
    if (padding) {
      const auto nop = test::Body({0xd5}).instructions.front();
      for (auto& instruction : body.instructions) {
        instruction.offset.value += padding;
        if (instruction.branch_target)
          instruction.branch_target->value += padding;
      }
      for (auto& region : body.tries) region.offset.value += padding;
      for (auto& handler : body.handlers) handler.offset.value += padding;
      body.instructions.insert(body.instructions.begin(), padding, nop);
      for (std::uint32_t i = 0; i < padding; ++i)
        body.instructions[i].offset = {i};
    }
    if (argc > 4 && std::string_view(argv[4]) == "registers")
      for (auto& instruction : body.instructions)
        for (auto& operand : instruction.decoded.operands)
          if (operand.kind == abc::OperandKind::kRegister) {
            if (operand.raw_bits == 0)
              operand.raw_bits = 1;
            else if (operand.raw_bits == 1)
              operand.raw_bits = 0;
          }
    const auto mode = argc > 4 ? std::string_view(argv[4]) : std::string_view{};
    if (mode == "split") {
      const auto branch =
          std::find_if(body.instructions.begin(), body.instructions.end(),
                       [](const auto& instruction) {
                         const auto op = instruction.decoded.opcode;
                         return op == abc::Opcode::kJEQZ_IMM8 ||
                                op == abc::Opcode::kJEQZ_IMM16 ||
                                op == abc::Opcode::kJNEZ_IMM8 ||
                                op == abc::Opcode::kJNEZ_IMM16;
                       });
      Check(branch != body.instructions.end() && branch->branch_target);
      const auto end = body.instructions.back().offset.value +
                       body.instructions.back().decoded.size;
      auto jump =
          test::Body(
              {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM32), 0, 0, 0, 0})
              .instructions.front();
      jump.offset = {end};
      jump.branch_target = branch->branch_target;
      jump.decoded.branch_delta = std::int64_t(jump.branch_target->value) - end;
      jump.decoded.operands[0].raw_bits =
          static_cast<std::uint32_t>(*jump.decoded.branch_delta);
      branch->branch_target = abc::CodeOffset{end};
      branch->decoded.branch_delta = std::int64_t(end) - branch->offset.value;
      branch->decoded.operands[0].raw_bits =
          static_cast<std::uint32_t>(*branch->decoded.branch_delta);
      body.instructions.push_back(jump);
    }
    if (mode == "copies") {
      auto copy = body.instructions.front();
      Check(copy.decoded.opcode == abc::Opcode::kLDA_V8 ||
            copy.decoded.opcode == abc::Opcode::kMOV_V8_V8);
      const std::uint32_t size = copy.decoded.size,
                          at = copy.offset.value + size;
      const auto shifted = [&](std::uint32_t pc) {
        return pc >= at ? pc + size : pc;
      };
      for (auto& instruction : body.instructions) {
        instruction.offset.value = shifted(instruction.offset.value);
        if (instruction.branch_target) {
          instruction.branch_target->value =
              shifted(instruction.branch_target->value);
          instruction.decoded.branch_delta =
              std::int64_t(instruction.branch_target->value) -
              instruction.offset.value;
        }
      }
      for (auto& region : body.tries) {
        const auto end = shifted(region.offset.value + region.length);
        region.offset.value = shifted(region.offset.value);
        region.length = end - region.offset.value;
      }
      for (auto& handler : body.handlers) {
        const auto end = shifted(handler.offset.value + handler.length);
        handler.offset.value = shifted(handler.offset.value);
        handler.length = end - handler.offset.value;
      }
      copy.offset = {at};
      body.instructions.insert(body.instructions.begin() + 1, copy);
    }
    const auto flow_owner =
        std::make_shared<ExceptionFlow>(Take<ExceptionFlow>(BuildExceptionFlow(
            body, header.profile, frame.registers, {}, {}, &frame)));
    const auto& flow = *flow_owner;
    IdfQuota work{kDefaultSemanticWork};
    const auto ssa_owner = std::make_shared<ExceptionSsa>(
        Take<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, work)));
    const auto& ssa = *ssa_owner;
    std::vector<EntryValueFact> entries;
    for (std::uint32_t v = 0; v < ssa.Values().size(); ++v)
      if (std::holds_alternative<MethodEntry>(ssa.Values()[v].origin))
        if (const auto* reg =
                std::get_if<abc::RegisterId>(&ssa.Values()[v].slot);
            reg && reg->value >= method.code->registers)
          entries.push_back({{v}, ValueKinds::Language()});
    const auto env = Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
    auto locals = Take<ExceptionLocals>(
        ProveExceptionLocals(flow, env, {}, {}, ClosureWrites::kLanguage));
    const auto captures =
        Take<ExceptionCaptures>(PlanExceptionCaptures(flow, env));
    const auto modules =
        Take<ExceptionModules>(ProveExceptionModules(flow, ssa));
    auto loads = locals.loads;
    loads.insert(loads.end(), captures.loads.begin(), captures.loads.end());
    loads.insert(loads.end(), modules.loads.begin(), modules.loads.end());
    const auto values = Take<ExceptionValueFlow>(ExceptionValueFlow::Build(
        flow, ssa, entries, work, {}, PendingExceptionValues::kLanguage, loads,
        {}, CallResultValues::kLanguage, FrameReceiver::kUnknown,
        kSourceArithmetic, ExceptionThrowRefinement::kGuardedCatchPayload,
        GlobalReadValues::kUnknown, flow_owner, ssa_owner));
    const auto proof = Take<ExceptionInputProof>(
        VerifyExceptionInputs(flow, ssa, env, values, kDefaultSemanticWork,
                              nullptr, ExceptionInputDomain::kHoleTransport));
    const PhasedValueInput input{flow, ssa, env, values, proof, frame};
    const auto graph = Take<ValueProgram>(ImportPhasedValues(input));
    const auto plan =
        Take<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto keys = Take<IteratorKeys>(
        ResolveIteratorKeys({*file, header, index}, method.offset, graph));
    const auto scan = Take<IteratorProtocolScan>(
        ScanIteratorProtocols(graph, input, plan, keys));
    auto summary = Take<CloseCompletionSummary>(AnalyzeCloseCompletion(
        body, header.profile, graph, input, plan, keys, scan));
    auto owner = Take<VerifiedProtocolRegionOwnership>(
        VerifyProtocolRegionOwnership(body, header.profile, graph, input, plan,
                                      keys, scan, std::move(summary)));
    auto legacy = Take<ExceptionControl>(
        BuildExceptionControl(body, graph, input, plan, {}, &owner));
    const ForOfEvaluationInput old_input{body,   input, plan,
                                         legacy, owner, kSourceContract};
    auto old_evaluation = Take<EvaluationPlan>(BuildPhasedEvaluationPlan(
        graph, {}, IncomingCells::kConditional, {}, {}, {}, {},
        ClosureWrites::kLanguage, {}, {}, &old_input));
    auto constants = Take<TreeConstants>(
        ResolveTreeConstants(*file, header, index, method.offset, graph));
    FunctionSyntax syntax{};
    syntax.owner = {0};
    syntax.method = method.offset;
    syntax.identity.contract = kSourceContract;
    syntax.callable = CallableKind::kFunction;
    syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(frame.parameters);
    syntax.names = constants.names;
    syntax.texts = constants.texts;
    const ExceptionTreeInput before{body, input, plan, legacy, old_input};
    auto failed = BuildExceptionFunctionDraft(graph, before, old_evaluation,
                                              syntax, constants.View());
    const auto failure = Take<FunctionTreeFailure>(std::move(failed));
    if (!(failure.tree && failure.tree->census &&
          failure.tree->census->operation)) {
      std::cerr << "initial failure " << unsigned(failure.kind);
      if (failure.tree)
        std::cerr << " obligation=" << unsigned(failure.tree->obligation);
      std::visit(
          [](const auto& x) {
            if constexpr (std::is_enum_v<std::decay_t<decltype(x)>>)
              std::cerr << " cause=" << unsigned(x);
          },
          failure.cause);
      std::cerr << '\n';
    }
    Check(failure.tree && failure.tree->census &&
          failure.tree->census->operation);
    Check(failure.tree->census->kind == TreeCensusError::kOrphanExpression);
    const auto census = *failure.tree->census;
    auto json = TreeFailureJson(*failure.tree);
    Check(ParseTreeFailure(json) == failure.tree);
    json["census"]["kind"] = 999;
    Check(!ParseTreeFailure(json));
    auto derived = Take<IteratorContinuationProof>(ProveIteratorContinuations(
        body, graph, input, plan, owner, legacy, census));
    const auto verify = [&](const IteratorContinuationProof& p,
                            SemanticBudget budget = SemanticBudget{}) {
      return VerifyIteratorContinuations(body, graph, input, plan, owner, p,
                                         budget);
    };
    Take<std::uint64_t>(verify(derived));
    auto changed = derived;
    changed.continuations.front().exhausted.successor ^= 1;
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kContinuation);
    changed = derived;
    changed.continuations.front().retained_continuation =
        changed.continuations.front().legacy_continuation;
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kContinuation);
    changed = derived;
    changed.continuations.front().loop.value ^= 1;
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kContinuation);
    changed = derived;
    changed.continuations.front().original_target.value ^= 1;
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kContinuation);
    changed = derived;
    Check(!changed.coverage.empty());
    changed.coverage.pop_back();
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kCoverage);
    changed = derived;
    Check(!changed.cuts.empty());
    changed.cuts.front().pending.payload = SsaValueId{UINT32_MAX};
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kPayload);
    changed = derived;
    changed.contexts.front().region = TryId{UINT32_MAX};
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kProtection);
    changed = derived;
    Check(!changed.copies.empty());
    changed.copies.front().edge.value ^= 1;
    Check(std::get<ForOfPlacementFailure>(verify(changed)).kind ==
          ForOfPlacementError::kCopyPlacement);
    auto unrelated = census;
    unrelated.kind = TreeCensusError::kNone;
    Check(std::get<ForOfPlacementFailure>(
              ProveIteratorContinuations(body, graph, input, plan, owner,
                                         legacy, unrelated))
              .kind == ForOfPlacementError::kActivation);
    unrelated = census;
    unrelated.operation = OpId{0};
    Check(std::get<ForOfPlacementFailure>(
              ProveIteratorContinuations(body, graph, input, plan, owner,
                                         legacy, unrelated))
              .kind == ForOfPlacementError::kActivation);
    Check(std::get<ForOfPlacementFailure>(
              verify(derived, {0, kDefaultSemanticWork}))
              .kind == ForOfPlacementError::kResourceLimit);
    Check(std::get<ForOfPlacementFailure>(
              verify(derived, {SemanticBudget{}.bytes, 0}))
              .kind == ForOfPlacementError::kWorkLimit);
    auto external = graph;
    const auto original_source =
        external.edges[derived.continuations.front().edge.value].source;
    external.edges.front().target = original_source;
    Check(std::holds_alternative<ForOfPlacementFailure>(
        ProveIteratorContinuations(body, external, input, plan, owner, legacy,
                                   census)));
    const auto shared =
        std::make_shared<const IteratorContinuationProof>(derived);
    auto control = Take<ExceptionControl>(
        BuildExceptionControl(body, graph, input, plan, {}, &owner, shared));
    Take<std::uint64_t>(
        VerifyExceptionControl(body, graph, input, plan, control, {}, &owner));
    const ForOfEvaluationInput selected{body,    input, plan,
                                        control, owner, kSourceContract};
    auto evaluation = Take<EvaluationPlan>(BuildPhasedEvaluationPlan(
        graph, {}, IncomingCells::kConditional, {}, {}, {}, {},
        ClosureWrites::kLanguage, {}, {}, &selected));
    const ExceptionTreeInput after{body, input, plan, control, selected};
    auto draft = Take<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(
        graph, after, evaluation, syntax, constants.View()));
    Check(draft.flow && draft.flow->original &&
          draft.flow->original->iterations);
    auto wrong = control;
    auto broken = derived;
    broken.coverage.pop_back();
    wrong.continuations =
        std::make_shared<const IteratorContinuationProof>(broken);
    const ForOfEvaluationInput wrong_input{body,  input, plan,
                                           wrong, owner, kSourceContract};
    const ExceptionTreeInput wrong_tree{body, input, plan, wrong, wrong_input};
    Check(std::holds_alternative<TreeFlowError>(
        VerifyExceptionTreeFlow(graph, wrong_tree, evaluation, draft.tree)));
    auto raw = *draft.flow->original;
    raw.iterations->view.continuations = wrong.continuations;
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, raw)));
    broken = derived;
    broken.cuts.front().pending.payload = SsaValueId{UINT32_MAX};
    raw.iterations->view.continuations =
        std::make_shared<const IteratorContinuationProof>(broken);
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, raw)));
    broken = derived;
    broken.copies.front().source_phase = ExceptionPhase::kCatch;
    raw.iterations->view.continuations =
        std::make_shared<const IteratorContinuationProof>(broken);
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, raw)));
    broken = derived;
    broken.copies.front().transfer = RaiseTransfer{TryId{UINT32_MAX}};
    raw.iterations->view.continuations =
        std::make_shared<const IteratorContinuationProof>(broken);
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, raw)));
    raw.iterations->view.continuations.reset();
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, raw)));
    TreeSemanticFailure negative;
    auto orphan = draft.tree;
    const auto extra =
        ExprId{static_cast<std::uint32_t>(orphan.expressions.size())};
    orphan.expressions.push_back({TreeLiteral{UndefinedLiteral{}},
                                  orphan.statements[orphan.body.value].origin});
    Check(
        std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(
            graph, after, evaluation, orphan, constants.View(), {},
            TreeControlCheck::kFresh, &negative)));
    Check(negative.census &&
          negative.census->kind == TreeCensusError::kOrphanExpression &&
          negative.census->expression == extra);
    std::cout << "method=" << method.offset.value
              << " nodes=" << graph.blocks.size() << " work=" << derived.work
              << " bytes=" << derived.allocated_bytes
              << " continuations=" << derived.continuations.size()
              << " cuts=" << derived.cuts.size() << '\n';
    ++count;
  }
  Check(count != 0);
}
