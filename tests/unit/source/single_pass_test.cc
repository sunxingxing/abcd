#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>

#include "../../../src/source/completion_ports.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/for_of_emission.h"
#include "../../../src/source/for_of_evaluation.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/tree_constants.h"
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
  auto file = Take<std::unique_ptr<corpus::AbcFile>>(
      corpus::AbcFile::Open(argc > 1 ? argv[1] : ABCD_SINGLE_PASS_FIXTURE));
  const auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index =
      Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned count = 0;
  for (const auto& method : index.methods) {
    bool selected_method = method.name.ends_with("#onceSync");
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
    const auto refused = Take<ExceptionControlRegionFailure>(
        BuildExceptionControl(body, graph, input, plan));
    Check(refused.kind == ExceptionControlError::kMultipleExits);
    auto derived = DeriveCompletionPorts(body, header.profile, graph, input,
                                         plan, keys, scan, refused);
    if (const auto* f = std::get_if<CompletionPortFailure>(&derived);
        f && f->edge) {
      std::cerr << "port witness pc="
                << flow.instructions[flow.nodes[f->edge->source.value]
                                         .instruction.value]
                       .pc.value
                << '\n';
      const auto local = Take<CloseCompletionSummary>(AnalyzeCloseCompletion(
          body, header.profile, graph, input, plan, keys, scan));
      for (const auto& r : local.regions)
        for (const auto& m : r.members)
          if (m.node == f->edge->source)
            std::cerr << "member read=" << r.read.value
                      << " role=" << unsigned(m.role) << '\n';
    }
    auto ports = Take<CompletionPorts>(std::move(derived));
    std::cout << "method=" << method.offset.value
              << " entries=" << ports.entries.size()
              << " exits=" << ports.exits.size()
              << " resumes=" << ports.resumes.size() << " work=" << ports.work
              << " bytes=" << ports.allocated_bytes
              << " nodes=" << flow.nodes.size()
              << " qualified=" << ports.qualified.size() << '\n';
    const auto verify = [&](const CompletionPorts& changed,
                            SemanticBudget budget = SemanticBudget{}) {
      return VerifyCompletionPorts(body, header.profile, graph, input, plan,
                                   keys, scan, changed, budget);
    };
    Take<std::uint64_t>(verify(ports));
    auto composed = Take<CloseCompletionSummary>(AnalyzeCloseCompletion(
        body, header.profile, graph, input, plan, keys, scan, {},
        kSourceContract, std::make_shared<CompletionPorts>(ports)));
    for (const auto& r : composed.regions)
      if (r.boundary && r.boundary->missing) {
        const auto& f = *r.boundary->missing;
        std::cerr << "missing read=" << r.read.value
                  << " gap=" << unsigned(*f.ownership_gap);
        if (f.node)
          std::cerr
              << " pc="
              << flow.instructions[flow.nodes[f.node->value].instruction.value]
                     .pc.value;
        std::cerr << '\n';
      }
    auto owner = Take<VerifiedProtocolRegionOwnership>(
        VerifyProtocolRegionOwnership(body, header.profile, graph, input, plan,
                                      keys, scan, std::move(composed)));
    auto controlled = Take<ExceptionControl>(
        BuildExceptionControl(body, graph, input, plan, {}, &owner));
    Take<std::uint64_t>(VerifyExceptionControl(body, graph, input, plan,
                                               controlled, {}, &owner));
    for (std::uint32_t t = 0; t < plan.regions.size(); ++t) {
      bool compiler = false;
      for (const auto& region : scan.regions)
        compiler |= region.region.value == t;
      if (compiler || plan.layout.regions[t].inactive) continue;
      bool calls = false;
      const auto& handler = plan.layout.regions[t].handler;
      for (auto i = handler.begin.value; i < handler.end.value; ++i)
        calls |= std::holds_alternative<OrdinaryCall>(
                     flow.instructions[i].operation) ||
                 std::holds_alternative<ReceiverCall>(
                     flow.instructions[i].operation);
      if (calls)
        Check(controlled.region_roles[t] == ExceptionRegionRole::kOrdinaryTry);
    }

    auto identity = Take<ForOfIdentityIndex>(IndexForOfIdentities(
        body, graph, input, plan, controlled, owner, kSourceContract));

    const ForOfEvaluationInput original{body,       input, plan,
                                        controlled, owner, kSourceContract};
    auto rejected = BuildForOfEvaluationPlan(graph, original);
    const auto legacy = Take<ForOfIterationFailure>(std::move(rejected));
    Check(legacy.scope.has_value());
    auto proof_result = ProveSinglePassScopes(body, graph, input, plan,
                                              controlled, owner, *legacy.scope);
    if (const auto* f = std::get_if<SinglePassFailure>(&proof_result)) {
      std::cerr << "single failure=" << unsigned(f->kind);
      if (f->edge)
        std::cerr << " pc="
                  << flow.instructions[flow.nodes[f->edge->source.value]
                                           .instruction.value]
                         .pc.value;
      std::cerr << '\n';
    }
    auto scopes = Take<SinglePassScopes>(std::move(proof_result));
    Take<std::uint64_t>(VerifySinglePassScopes(body, graph, input, plan,
                                               controlled, owner, scopes));
    const auto check_scope = [&](const SinglePassScopes& candidate,
                                 SemanticBudget budget = SemanticBudget{}) {
      return VerifySinglePassScopes(body, graph, input, plan, controlled, owner,
                                    candidate, budget);
    };
    const auto protocol = legacy.scope->protocol.value;
    auto wrong = scopes;
    wrong.scopes[protocol]->entry = wrong.scopes[protocol]->continuation;
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kEntry);
    wrong = scopes;
    wrong.coverage.pop_back();
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kCoverage);
    wrong = scopes;
    Check(!wrong.dead_entries.empty());
    wrong.dead_entries.pop_back();
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kRepeat);
    wrong = scopes;
    wrong.parents[protocol] = IteratorProtocolId{protocol};
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kParent);
    wrong = scopes;
    wrong.protection.front().region = TryId{UINT32_MAX};
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kProtection);
    wrong = scopes;
    wrong.cuts.pop_back();
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kCoverage);
    wrong = scopes;
    wrong.cuts.front().pending.payload = SsaValueId{UINT32_MAX};
    Check(std::get<SinglePassFailure>(check_scope(wrong)).kind ==
          SinglePassError::kPayload);
    auto activation = *legacy.scope;
    activation.advance.value++;
    Check(std::get<SinglePassFailure>(ProveSinglePassScopes(body, graph, input,
                                                            plan, controlled,
                                                            owner, activation))
              .kind == SinglePassError::kActivation);
    Check(std::get<SinglePassFailure>(
              check_scope(scopes, {0, kDefaultSemanticWork}))
              .kind == SinglePassError::kResourceLimit);
    Check(std::get<SinglePassFailure>(
              check_scope(scopes, {SemanticBudget{}.bytes, 0}))
              .kind == SinglePassError::kWorkLimit);
    auto priority = ports;
    const auto nonthrow = std::find_if(
        priority.exits.begin(), priority.exits.end(), [](const auto& e) {
          return e.original.pending.kind != PendingCompletionKind::kThrow;
        });
    Check(nonthrow != priority.exits.end());
    nonthrow->original.pending.kind = PendingCompletionKind::kThrow;
    Check(std::holds_alternative<CompletionPortFailure>(VerifyCompletionPorts(
        body, header.profile, graph, input, plan, keys, scan, priority)));
    const auto shared = std::make_shared<const SinglePassScopes>(scopes);
    auto placed = Take<ExceptionControl>(BuildExceptionControl(
        body, graph, input, plan, {}, &owner, {}, shared));
    Take<std::uint64_t>(
        VerifyExceptionControl(body, graph, input, plan, placed, {}, &owner));
    Check(placed.skeleton.loops.size() == controlled.skeleton.loops.size());
    const ForOfEvaluationInput selected{body,   input, plan,
                                        placed, owner, kSourceContract};
    auto evaluated =
        Take<ForOfEvaluationPlan>(BuildForOfEvaluationPlan(graph, selected));
    auto view = Take<ForOfEmissionView>(
        BuildForOfEmissionView(graph, selected, evaluated));
    auto evaluation = Take<EvaluationPlan>(BuildPhasedEvaluationPlan(
        graph, {}, IncomingCells::kConditional, {}, {}, {}, {},
        ClosureWrites::kLanguage, {}, {}, &selected));
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
    const ExceptionTreeInput tree_input{body, input, plan, placed, selected};
    auto tree = BuildExceptionFunctionDraft(graph, tree_input, evaluation,
                                            syntax, constants.View());
    if (const auto* f = std::get_if<FunctionTreeFailure>(&tree)) {
      std::cerr << "tree kind=" << unsigned(f->kind);
      if (f->tree) {
        std::cerr << " obligation=" << unsigned(f->tree->obligation);
        if (f->tree->location.pc)
          std::cerr << " pc=" << f->tree->location.pc->value;
      }
      std::visit(
          [](const auto& e) {
            if constexpr (std::is_enum_v<std::decay_t<decltype(e)>>)
              std::cerr << " cause=" << unsigned(e);
          },
          f->cause);
      std::cerr << '\n';
    }
    auto draft = Take<ExceptionFunctionDraft>(std::move(tree));
    Check(draft.flow && draft.flow->original &&
          draft.flow->original->iterations);
    const auto& original_flow = *draft.flow->original;
    const auto& native = *original_flow.iterations;
    Take<TreeStorage>(VerifyInitialForOfRecovery(draft.tree, original_flow));
    std::vector<std::optional<SymbolId>> aliases(draft.tree.symbols.size());
    std::vector<std::optional<PrimitiveId>> literals(draft.tree.symbols.size());
    std::vector<std::optional<ExprId>> replacements(
        draft.tree.expressions.size());
    std::vector<std::uint8_t> omitted(draft.tree.statements.size()),
        normal(draft.tree.expressions.size());
    std::vector<ExpressionEvaluation> effects(draft.tree.expressions.size(),
                                              ExpressionEvaluation::kRequired);
    for (std::uint32_t i = 0; i < aliases.size(); ++i) aliases[i] = SymbolId{i};
    LegacyFlowProjection projection{aliases, literals, replacements,
                                    omitted, effects,  normal};
    projection.original = &original_flow;
    auto recovery =
        Take<RecoveryFlow>(BuildRecoveryFlow(draft.tree, projection));
    const auto statement = native.statements[protocol];
    const auto& runtime = *recovery.iterations[statement.value];
    const auto entry = std::find_if(
        recovery.events.begin(), recovery.events.end(), [&](const auto& e) {
          for (auto edge = e.outgoing; edge;
               edge = recovery.edges[*edge].outgoing)
            if (recovery.edges[*edge].to ==
                recovery.iteration_closes[runtime.throw_close].entry)
              return true;
          return false;
        });
    Check(entry != recovery.events.end());
    // Introduce a reachable implicit repeat into an owned trial flow. This
    // cannot be justified by the original dead nominal-repeat evidence.
    recovery.events[runtime.body.value].outgoing = recovery.edges.size();
    recovery.edges.push_back(
        {runtime.body,
         runtime.events[static_cast<std::size_t>(ForOfEventKind::kNextCall)]
             .before,
         FlowCompletion::kNormal});
    TreeCheckBudget trial({});
    bool refused_repeat = false;
    try {
      VerifySinglePassRecovery(original_flow, native, recovery, trial);
    } catch (TreeStructureError) {
      refused_repeat = true;
    }
    Check(refused_repeat);
    auto changed = original_flow;
    auto broken = scopes;
    broken.coverage.pop_back();
    changed.iterations->view.single_pass =
        std::make_shared<const SinglePassScopes>(std::move(broken));
    Check(std::holds_alternative<TreeStructureError>(
        VerifyInitialForOfRecovery(draft.tree, changed)));
    std::cout << "nodes=" << flow.nodes.size()
              << " scopes=" << placed.iterator_scopes.size()
              << " scope_nodes=" << scopes.coverage.size()
              << " work=" << scopes.work << " bytes=" << scopes.allocated_bytes
              << '\n';
    ++count;
  }
  Check(count != 0);
}
