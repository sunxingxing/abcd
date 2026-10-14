#include "../../../src/source/for_in_plan.h"
#include "../../../src/source/for_in_emission.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/recovery_flow.h"
#include "abcd/analysis/exception_locals.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/for_in_evaluation.h"
#include "../../../src/source/evaluation_failure_json.h"
#include "../../../src/source/phase_corridor.h"
#include "../../../src/source/for_in_events.h"
#include "../../../src/source/exception_forwarding.h"
#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/analysis/value_edits.h"
#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/value_import.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <bit>
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
bool emission_checks = false;
const corpus::AbcFile* emission_file = nullptr;
const abc::Header* emission_header = nullptr;
const abc::MetadataIndex* emission_index = nullptr;
abc::EntityOffset emission_method{};
ValueProgram Import(const abc::MethodBody& body, std::uint32_t locals, std::uint32_t parameters,
                    abc::ProfileId profile = abc::ProfileId::kDynamic1206, bool prove_locals = false) {
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, parameters, {}));
  const auto flowed = BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame);
  if (!std::holds_alternative<ExceptionFlow>(flowed)) std::cerr << "flow variant " << flowed.index() << " instructions " << body.instructions.size() << std::endl;
  Check(std::holds_alternative<ExceptionFlow>(flowed));
  const auto& flow = std::get<ExceptionFlow>(flowed);
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::uint32_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    if (std::holds_alternative<MethodEntry>(value.origin))
      if (const auto* reg = std::get_if<abc::RegisterId>(&value.slot); reg && reg->value >= locals)
        entries.push_back({{v}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  std::vector<ExceptionLoadFact> loads;
  if (prove_locals) loads = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env)).loads;
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries, quota, {},
      PendingExceptionValues::kLanguage, loads, {}, CallResultValues::kLanguage, FrameReceiver::kInitialized));
  const auto checked = VerifyExceptionInputs(flow, ssa, env, values, kDefaultSemanticWork, nullptr, ExceptionInputDomain::kHoleTransport);
  if (const auto* failure = std::get_if<ExceptionInputFailure>(&checked))
    std::cerr << "input " << static_cast<unsigned>(failure->kind) << '\n';
  Check(std::holds_alternative<ExceptionInputProof>(checked));
  const PhasedValueInput input{flow, ssa, env, values, std::get<ExceptionInputProof>(checked), frame};
  const auto imported = ImportPhasedValues(input);
  Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(imported);
  Check(std::holds_alternative<PhasedImportProof>(VerifyImportedPhases(graph, input)));
  if (emission_checks && std::holds_alternative<ForInPlan>(BuildForInPlan(graph))) {
    const auto plan = std::get<ForInPlan>(BuildForInPlan(graph));
    const auto events = std::get<ForInEvents>(BuildForInEvents(graph, plan));
    const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
    const auto controls = BuildExceptionControl(body, graph, input, regions);
    Check(std::holds_alternative<ExceptionControl>(controls));
    const auto& control = std::get<ExceptionControl>(controls);
    const auto view = BuildForInEmissionView(graph, regions, control, plan, events);
    if (const auto* e = std::get_if<ForInFailure>(&view)) std::cerr << "view failure " << unsigned(e->kind) << " op " << (e->operation ? e->operation->value : 99999) << '\n';
    Check(std::holds_alternative<ForInEmissionView>(view));
    Check(std::holds_alternative<std::uint64_t>(VerifyForInEmissionView(graph, regions, control, plan, events, std::get<ForInEmissionView>(view))));
    auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
    FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(parameters);
    TreeConstants constants{}; constants.operations.resize(graph.ops.size());
    if (emission_file) constants = std::get<TreeConstants>(ResolveTreeConstants(*emission_file, *emission_header,
        *emission_index, emission_method, graph));
    syntax.names = constants.names; syntax.texts = constants.texts; syntax.method = emission_method;
    const ExceptionTreeInput context{body, input, regions, control};
    const auto tree = BuildForInTreeDraft(graph, context, evaluation, syntax, constants.View());
    if (const auto* e = std::get_if<FunctionTreeFailure>(&tree)) {
      std::cerr << "draft failure " << unsigned(e->kind) << " cause " << e->cause.index();
      if (e->tree) { std::cerr << " obligation " << unsigned(e->tree->obligation); if(e->tree->location.operation) std::cerr << " op " << e->tree->location.operation->value; }
      if (const auto* f = std::get_if<TreeFlowError>(&e->cause)) std::cerr << " flow " << unsigned(*f);
      if (const auto* f = std::get_if<ExpressionFailure>(&e->cause)) std::cerr << " expr " << unsigned(f->kind);
      std::cerr << std::endl;
    }
    Check(std::holds_alternative<ForInTreeDraft>(tree));
    const auto& draft = std::get<ForInTreeDraft>(tree);
    Check(draft.flow.original != nullptr); // Original operand correspondence now retained.
    std::vector<std::optional<SymbolId>> aliases(draft.tree.symbols.size());
    for(std::uint32_t b=0;b<aliases.size();++b)aliases[b]=SymbolId{b};
    std::vector<std::optional<PrimitiveId>> literals(aliases.size());
    std::vector<std::optional<ExprId>> replacements(draft.tree.expressions.size());
    std::vector<std::uint8_t> omitted(draft.tree.statements.size()),normal(replacements.size());
    std::vector<ExpressionEvaluation> effects(replacements.size(),ExpressionEvaluation::kRequired);
    LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};
    projection.original=draft.flow.original.get();
    auto recovered=BuildRecoveryFlow(draft.tree,projection);
    if(const auto* error=std::get_if<TreeStructureError>(&recovered))std::cerr << "recovery failure " << unsigned(*error) << std::endl;
    Check(std::holds_alternative<RecoveryFlow>(recovered));
    const auto& recovery=std::get<RecoveryFlow>(recovered);
    for(const auto& native:draft.flow.original->enumerations) {
      const auto& ports=*recovery.enumerations[native.statement.value];
      Check(ports.acquire_before!=ports.acquire_commit && ports.next_before!=ports.next_commit && ports.key_write!=ports.key_commit);
      Check(!FindOriginalResult(*draft.flow.original,native.original.advance.operation));
      Check(recovery.events[ports.key_commit.value].definition.has_value());
    }
    if(plan.loops.size()<=2) {
      const auto reject_original=[&](OriginalFlow changed) {
        auto candidate=recovery;candidate.original=&changed;
        bool rejected=false;
        try { TreeCheckBudget q({}); VerifyForInRecovery(draft.tree,candidate,q); }
        catch(TreeStructureError){rejected=true;}
        Check(rejected);
      };
      auto wrong=*draft.flow.original;
      wrong.enumerations[0].original.advance.commit=wrong.enumerations[0].original.advance.before; reject_original(std::move(wrong));
      wrong=*draft.flow.original;
      wrong.exceptional_exits.erase(wrong.exceptional_exits.begin()+wrong.enumerations[0].original.acquisition.exceptional_exit.value);reject_original(std::move(wrong));
      wrong=*draft.flow.original;
      std::swap(wrong.enumerations[0].original.key.success,wrong.enumerations[0].original.key.exhausted);reject_original(std::move(wrong));
      wrong=*draft.flow.original;
      wrong.enumerations[0].original.acquisition.result=wrong.enumerations[0].original.advance.result;reject_original(std::move(wrong));
      if(const auto* route=std::get_if<ForwardedExceptionRoute>(&draft.flow.original->enumerations[0].acquisition)) {
        wrong=*draft.flow.original;
        ++std::get<ForwardedExceptionRoute>(wrong.enumerations[0].acquisition).payload.value;reject_original(std::move(wrong));
      }
      const auto reject_flow=[&](RecoveryFlow changed) {
        bool rejected=false;try{TreeCheckBudget q({});VerifyForInRecovery(draft.tree,changed,q);}catch(TreeStructureError){rejected=true;}Check(rejected);
      };
      auto wrong_flow=recovery;const auto stmt=draft.flow.original->enumerations[0].statement;
      wrong_flow.events[wrong_flow.enumerations[stmt.value]->next_before.value].effects.reset();reject_flow(std::move(wrong_flow));
      wrong_flow=recovery;
      auto& ports=*wrong_flow.enumerations[stmt.value];
      for(auto e=wrong_flow.events[ports.done.value].outgoing;e;e=wrong_flow.edges[*e].outgoing)
        if(wrong_flow.edges[*e].completion==FlowCompletion::kTaken)wrong_flow.edges[*e].to=ports.key_write;
      reject_flow(std::move(wrong_flow));
      wrong_flow=recovery;wrong_flow.statement_semantics[stmt.value]->repeat_point=wrong_flow.enumerations[stmt.value]->acquire_before;reject_flow(std::move(wrong_flow));
      for(std::uint32_t i=0;i<draft.tree.statements.size();++i) {
        const auto* declaration=std::get_if<DeclarationStmt>(&draft.tree.statements[i].statement);
        if(!declaration || declaration->symbol!=draft.flow.original->enumerations[0].key)continue;
        omitted[i]=1;
        Check(std::holds_alternative<TreeStructureError>(BuildRecoveryFlow(draft.tree,projection)));
        omitted[i]=0;
      }
      if(!draft.flow.original->enumeration_inputs.empty()) {
        wrong_flow=recovery;
        const auto& input=draft.flow.original->enumeration_inputs.front();
        auto& action=*wrong_flow.actions[input.expression.value];
        const auto& before=*wrong_flow.enumerations[draft.flow.original->enumerations[input.loop].statement.value];
        // Retain a real read event but move its occurrence to the pre-loop
        // declaration read: the successful key definition must dominate it.
        wrong_flow.events[before.rhs.value].kind=FlowEventKind::kRead;
        wrong_flow.events[before.rhs.value].read=draft.flow.original->enumerations[input.loop].key;
        action.before=before.rhs;
        reject_flow(std::move(wrong_flow));
      }
      for(const auto& exit:draft.flow.original->enumeration_exits) {
        wrong_flow=recovery;
        for(auto e=wrong_flow.events[wrong_flow.statements[exit.statement.value].value].outgoing;e;e=wrong_flow.edges[*e].outgoing)
          if(wrong_flow.edges[*e].completion==FlowCompletion::kJump)
            wrong_flow.edges[*e].to=wrong_flow.enumerations[draft.flow.original->enumerations[exit.loop].statement.value]->acquire_before;
        reject_flow(std::move(wrong_flow));
      }

    }

    Check(draft.flow.enumerations.size() == events.loops.size());
    for (std::size_t i=0;i<events.loops.size();++i) {
      Check(draft.flow.enumerations[i].original == events.loops[i]);
      Check(!draft.flow.expressions[plan.loops[i].acquisition.value] && !draft.flow.expressions[plan.loops[i].advance.value]);
    }
    Check(std::count_if(draft.tree.statements.begin(), draft.tree.statements.end(), [](const auto& n) {
      return std::holds_alternative<ForInStmt>(n.statement);
    }) == plan.loops.size());
    const auto published = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, constants.View());
    Check(std::holds_alternative<ExceptionFunctionDraft>(published));
    if (plan.loops.size() <= 2) {
      auto forged = std::get<ForInEmissionView>(view);
      forged.loops[0].body.reset();
      Check(std::holds_alternative<ForInFailure>(VerifyForInEmissionView(graph, regions, control, plan, events, forged)));
      forged = std::get<ForInEmissionView>(view);
      if (const auto* route = std::get_if<ForwardedExceptionRoute>(&forged.loops[0].acquisition)) {
        const auto exit = route->exit;
        forged.loops[0].acquisition = PhysicalCatch{exit};
        Check(std::holds_alternative<ForInFailure>(VerifyForInEmissionView(graph, regions, control, plan, events, forged)));
        forged = std::get<ForInEmissionView>(view);
        ++std::get<ForwardedExceptionRoute>(forged.loops[0].acquisition).payload.value;
        Check(std::holds_alternative<ForInFailure>(VerifyForInEmissionView(graph, regions, control, plan, events, forged)));
        forged = std::get<ForInEmissionView>(view);
        ++std::get<ForwardedExceptionRoute>(forged.loops[0].acquisition).exit.value;
        Check(std::holds_alternative<ForInFailure>(VerifyForInEmissionView(graph, regions, control, plan, events, forged)));
        forged = std::get<ForInEmissionView>(view);
        const auto handler = std::get<ForwardedExceptionRoute>(forged.loops[0].acquisition).handler;
        forged.forwarded_blocks[handler.value].reset();
        Check(std::holds_alternative<ForInFailure>(VerifyForInEmissionView(graph, regions, control, plan, events, forged)));
      }
      const auto reject = [&](FunctionTree changed) {
        Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, changed)));
      };
      auto changed = draft.tree;
      const auto at = std::find_if(changed.statements.begin(), changed.statements.end(), [](const auto& n) {
        return std::holds_alternative<ForInStmt>(n.statement);
      });
      const auto si = std::size_t(at - changed.statements.begin());
      std::get<ForInStmt>(at->statement).key = SymbolId{0}; reject(std::move(changed));
      changed = draft.tree;
      std::get<ForInStmt>(changed.statements[si].statement).binding = ForInBinding::kPerIterationLet; reject(std::move(changed));
      changed = draft.tree;
      changed.statements[si].origin = TreeOriginId{graph.ops[plan.loops[0].advance.value].origin.value}; reject(std::move(changed));
      changed = draft.tree;
      changed.operations.erase(std::find_if(changed.operations.begin(), changed.operations.end(), [&](const auto& p) { return p.operation == plan.loops[0].advance; })); reject(std::move(changed));
      changed = draft.tree;
      const auto key = std::get<ForInStmt>(changed.statements[si].statement).key;
      const auto native_body = std::get<ForInStmt>(changed.statements[si].statement).body;
      changed.symbols[key.value].scope = std::get<BlockStmt>(changed.statements[native_body.value].statement).scope;
      reject(std::move(changed)); // RHS/assignment target cannot see a body-local key.
      changed = draft.tree;
      for (auto& n : changed.statements) if (auto* jump = std::get_if<ContinueStmt>(&n.statement)) {
        jump->target = changed.body; reject(std::move(changed)); break;
      }
    }
    if (plan.loops.size() >= 200) {
      const auto boundary = [&](bool bytes) {
        std::uint64_t low = 0, high = bytes ? SemanticBudget{}.bytes : SemanticBudget{}.work;
        while (low < high) {
          const auto middle = low + (high-low)/2;
          SemanticBudget budget{}; if(bytes) budget.bytes = middle; else budget.work = middle;
          const auto attempt = BuildForInTreeDraft(graph, context, evaluation, syntax, constants.View(), budget);
          if(std::holds_alternative<ForInTreeDraft>(attempt)) high = middle;
          else {
            const auto& error = std::get<FunctionTreeFailure>(attempt);
            Check(error.kind == (bytes ? FunctionTreeError::kResourceLimit : FunctionTreeError::kWorkLimit));
            low = middle + 1;
          }
        }
        Check(low != 0);
        SemanticBudget exact{}; if(bytes) exact.bytes = low; else exact.work = low;
        Check(std::holds_alternative<ForInTreeDraft>(BuildForInTreeDraft(graph, context, evaluation, syntax, constants.View(), exact)));
        if(bytes) --exact.bytes; else --exact.work;
        const auto denied = BuildForInTreeDraft(graph, context, evaluation, syntax, constants.View(), exact);
        Check(std::holds_alternative<FunctionTreeFailure>(denied) &&
            std::get<FunctionTreeFailure>(denied).kind == (bytes ? FunctionTreeError::kResourceLimit : FunctionTreeError::kWorkLimit));
        return low;
      };
      const auto bytes = boundary(true), work = boundary(false);
      std::cout << "emission scale " << plan.loops.size() << " minimum bytes " << bytes << " work " << work << '\n';
    }
  }
  return graph;
}
abc::MethodBody Body(unsigned count, bool caught, bool split = false,
                     std::vector<std::uint8_t> handler_bytes = {0x61, 4, 0x60, 4, 0xfe, 0},
                     bool repeat_advance = false, bool identity_cycle = false, bool key_cell = false) {
  const auto byte = [](abc::Opcode op) { return static_cast<std::uint8_t>(op); };
  std::vector<std::uint8_t> bytes;
  if(key_cell) bytes = {byte(abc::Opcode::kNEWLEXENV_IMM8), 1,
      byte(abc::Opcode::kLDA_V8), 3, byte(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0};
  struct Catch { std::uint32_t acquire, handler; };
  std::vector<Catch> catches;
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {byte(abc::Opcode::kLDA_V8), 0});
    const auto acquire = static_cast<std::uint32_t>(bytes.size());
    bytes.push_back(byte(abc::Opcode::kGETPROPITERATOR));
    bytes.insert(bytes.end(), {byte(abc::Opcode::kSTA_V8), 1});
    if (caught) {
      bytes.insert(bytes.end(), {byte(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(handler_bytes.size() + 2)});
      const auto handler = static_cast<std::uint32_t>(bytes.size());
      bytes.insert(bytes.end(), handler_bytes.begin(), handler_bytes.end());
      catches.push_back({acquire, handler});
    }
    const auto header = bytes.size();
    bytes.insert(bytes.end(), {byte(abc::Opcode::kGETNEXTPROPNAME_V8), 1, byte(abc::Opcode::kSTA_V8), 2});
    if (repeat_advance) bytes.insert(bytes.end(), {byte(abc::Opcode::kGETNEXTPROPNAME_V8), 1, byte(abc::Opcode::kSTA_V8), 2});
    if (split) bytes.insert(bytes.end(), {byte(abc::Opcode::kJMP_IMM8), 2, byte(abc::Opcode::kLDA_V8), 2, byte(abc::Opcode::kSTA_V8), 3});
    bytes.insert(bytes.end(), {byte(abc::Opcode::kLDUNDEFINED), byte(abc::Opcode::kEQ_IMM8_V8), 0, 2});
    const auto branch = bytes.size();
    bytes.insert(bytes.end(), {byte(abc::Opcode::kJNEZ_IMM8), 0,
        byte(abc::Opcode::kLDA_V8), 2, byte(abc::Opcode::kISTRUE), byte(abc::Opcode::kSTA_V8), 3});
    if (key_cell) bytes.insert(bytes.end(), {byte(abc::Opcode::kLDA_V8), 2,
        byte(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
    if (identity_cycle) bytes.insert(bytes.end(), {byte(abc::Opcode::kMOV_V8_V8), 6, 1,
        byte(abc::Opcode::kMOV_V8_V8), 1, 6});
    const auto back = bytes.size();
    bytes.insert(bytes.end(), {byte(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(header - back)});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
  }
  if(key_cell) bytes.insert(bytes.end(), {byte(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, byte(abc::Opcode::kRETURN)});
  else bytes.push_back(byte(abc::Opcode::kRETURNUNDEFINED));
  auto body = test::Body(bytes);
  for (const auto& item : catches) {
    abc::TryRegion region; region.offset = {item.acquire}; region.length = 1; region.handler_count = 1; region.first_handler = static_cast<std::uint32_t>(body.handlers.size());
    body.tries.push_back(region);
    abc::CatchHandler handler; handler.offset = {item.handler}; handler.length = static_cast<std::uint32_t>(handler_bytes.size());
    body.handlers.push_back(handler);
  }
  return body;
}
ForInPlan Prove(const ValueProgram& graph) {
  auto result = BuildForInPlan(graph);
  if (const auto* error = std::get_if<ForInFailure>(&result)) {
    std::cerr << "for-in " << static_cast<unsigned>(error->kind);
    if (error->operation) std::cerr << " op " << error->operation->value;
    if (error->value) std::cerr << " value " << error->value->value;
    if (error->use) std::cerr << " use " << error->use->value;
    std::cerr << '\n';
  }
  Check(std::holds_alternative<ForInPlan>(result));
  auto plan = std::get<ForInPlan>(std::move(result));
  Check(std::holds_alternative<std::uint64_t>(VerifyForInPlan(graph, plan)));
  const auto events = BuildForInEvents(graph, plan);
  if (const auto* error = std::get_if<ForInFailure>(&events))
    std::cerr << "events " << static_cast<unsigned>(error->kind) << " op " << error->operation.value_or(OpId{UINT32_MAX}).value << std::endl;
  Check(std::holds_alternative<ForInEvents>(events));
  Check(std::holds_alternative<std::uint64_t>(VerifyForInEvents(graph, plan, std::get<ForInEvents>(events))));
  return plan;
}
void Mutations() {
  const auto graph = Import(Body(1, true), 0, 5);
  const auto plan = Prove(graph); Check(plan.loops.size() == 1 && plan.forwarding.size() == 1);
  for (unsigned mode = 0; mode < 12; ++mode) {
    auto changed = plan;
    switch (mode) {
      case 0: changed.loops[0].advance = changed.loops[0].acquisition; break;
      case 1: std::swap(changed.loops[0].done_edge, changed.loops[0].body_edge); break;
      case 2: changed.loops[0].enumerator = changed.loops[0].key; break;
      case 3: changed.forwarding.clear(); break;
      case 4: changed.forwarding[0].exception = changed.loops[0].key; break;
      case 5: ++changed.revision.value; break;
      case 6: changed.loops.push_back(changed.loops[0]); break;
      case 7: changed.loops.clear(); break;
      case 8: changed.loops[0].body = changed.loops[0].exit; break;
      case 9: changed.loops[0].object = graph.ops[changed.loops[0].advance.value].first_use; break;
      case 10: Check(!changed.keys.empty()); changed.keys.clear(); break;
      case 11: changed.keys[0].use = graph.ops[changed.loops[0].advance.value].first_use; break;
    }
    Check(std::holds_alternative<ForInFailure>(VerifyForInPlan(graph, changed)));
  }
  auto bad = graph;
  const auto next = plan.loops[0].advance;
  Check(!ReplaceUse(bad, bad.ops[next.value].first_use, plan.loops[0].key));
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kIdentity);
  bad = graph;
  auto& condition = std::get<ValueBranch>(bad.ops[plan.loops[0].decision.value].operation);
  condition.predicate = UnaryOperator::kToNumber;
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kDone);
  bad = graph;
  const auto thrown = std::find_if(bad.ops.begin(), bad.ops.end(), [](const auto& op) { return std::holds_alternative<ValueThrow>(op.operation); });
  Check(thrown != bad.ops.end());
  Check(!ReplaceLiteralUse(bad, thrown->first_use, SpecialValue::kUndefined));
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kExceptionCorrespondence);
}
void Adverse() {
  const auto repeated = Import(Body(1, true, false, {0x61,4,0x60,4,0xfe,0}, true), 0, 5);
  Check(std::get<ForInFailure>(BuildForInPlan(repeated)).kind == ForInError::kAdvance);
  const auto graph = Import(Body(1, true), 0, 5);
  const auto plan = Prove(graph);
  auto bad = graph;
  auto copy = std::find_if(bad.ops.begin() + plan.loops[0].decision.value + 1, bad.ops.end(), [&](const auto& op) {
    const auto* input = op.use_count == 1 ? std::get_if<ValueId>(&bad.uses[op.first_use.value].operand) : nullptr;
    return std::holds_alternative<ValueCopy>(op.operation) && input;
  });
  Check(copy != bad.ops.end());
  Check(!ReplaceUse(bad, copy->first_use, plan.loops[0].enumerator));
  copy->operation = ValueUnary{UnaryOperator::kToBoolean};
  const auto escaped = std::get<ForInFailure>(BuildForInPlan(bad));
  Check(escaped.kind == ForInError::kEscape);
  bad = graph;
  Check(!ReplaceUse(bad, bad.ops[0].first_use, plan.loops[0].key));
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kKeyBinding);
  bad = graph;
  auto& decision = bad.ops[plan.loops[0].decision.value];
  Check(!ReplaceUse(bad, decision.first_use, plan.loops[0].key));
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kDone);
  bad = graph;
  const auto after = plan.loops[0].acquisition.value + 2;
  bad.ops[after].normal = LanguageEffect::kHeapRead;
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kAcquisitionOrder);
  bad = graph;
  for (auto& exit : bad.exceptional_exits) if (exit.op == plan.loops[0].advance)
    exit.handler = std::find_if(graph.exceptional_exits.begin(), graph.exceptional_exits.end(),
        [&](const auto& e) { return e.op == plan.loops[0].acquisition; })->handler;
  Check(std::get<ForInFailure>(BuildForInPlan(bad)).kind == ForInError::kCatchState);
}
void ForwardingPaths() {
  // The handler branches on an unrelated language value. Every normal path
  // rethrows its caught value; neither adjacency nor a linear handler is used.
  const auto branch = static_cast<std::uint8_t>(abc::Opcode::kJNEZ_IMM8);
  const std::vector<std::uint8_t> bytes{0x61,4,0x60,0,branch,6,0x60,4,0xfe,0,0x60,4,0xfe,0};
  const auto graph = Import(Body(1, true, true, bytes), 0, 5);
  Check(Prove(graph).forwarding.size() == 1);
  const auto summary = std::get<ExceptionForwarding>(AnalyzeExceptionForwarding(graph));
  Check(std::holds_alternative<ExceptionForwarding>(AnalyzeExceptionForwarding(graph, {SemanticBudget{}.bytes, summary.work})));
  Check(std::get<ForInFailure>(AnalyzeExceptionForwarding(graph, {SemanticBudget{}.bytes, summary.work - 1})).kind == ForInError::kWorkLimit);
  Check(std::get<ForInFailure>(AnalyzeExceptionForwarding(graph, {0})).kind == ForInError::kResourceLimit);
  // One branch returns: a forwarding summary must be universal.
  auto returns = bytes;
  returns[8] = static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED);
  returns[9] = static_cast<std::uint8_t>(abc::Opcode::kNOP);
  const auto ordinary = Import(Body(1, true, false, returns), 0, 5);
  Check(std::get<ForInFailure>(BuildForInPlan(ordinary)).kind == ForInError::kExceptionCorrespondence);
  // A normal cycle with a throwing alternative is not guaranteed to forward.
  auto cycle = bytes; cycle[8] = static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8); cycle[9] = 252;
  const auto loops = Import(Body(1, true, false, cycle), 0, 5);
  Check(std::get<ForInFailure>(BuildForInPlan(loops)).kind == ForInError::kExceptionCorrespondence);
  auto effect = graph;
  const auto handler = Prove(graph).forwarding[0].handler;
  const auto thrown = std::find_if(effect.ops.begin(), effect.ops.end(), [](const auto& op) {
    return std::holds_alternative<ValueThrow>(op.operation);
  });
  Check(thrown != effect.ops.end());
  thrown->before_throw = LanguageEffect::kHeapWrite;
  const auto result = AnalyzeExceptionForwarding(effect);
  Check(std::holds_alternative<ExceptionForwarding>(result));
  Check(!std::get<ExceptionForwarding>(result).blocks[handler.value]);
}
void Normal() {
  const auto body = Body(1, false, true);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 5, {}));
  const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 5));
  const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 5, 0));
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, method, proof, frame));
  Check(!VerifyImportedValues(graph, method, proof, frame));
  Check(Prove(graph).loops.size() == 1);
  // This increment integrates phased callers only; normal protocol proof is
  // not yet a normal expression-order certificate.
  Check(std::holds_alternative<EvaluationFailure>(BuildEvaluationPlan(graph)));
}
void Scales() {
  for (unsigned count : {200U, 201U, 400U, 800U}) {
    const auto graph = Import(Body(count, true, true), 0, 5);
    const auto plan = Prove(graph); Check(plan.loops.size() == count);
    Check(plan.work < 1024 * (graph.ops.size() + graph.values.size() + graph.uses.size()) * std::bit_width(graph.blocks.size()));
    Check(std::holds_alternative<ForInPlan>(BuildForInPlan(graph, {SemanticBudget{}.bytes, plan.work})));
    Check(std::get<ForInFailure>(BuildForInPlan(graph, {SemanticBudget{}.bytes, plan.work - 1})).kind == ForInError::kWorkLimit);
    std::size_t low = 0, high = SemanticBudget{}.bytes;
    while (low < high) {
      const auto middle = low + (high - low) / 2;
      const auto result = BuildForInPlan(graph, {middle});
      if (const auto* failure = std::get_if<ForInFailure>(&result)) {
        Check(failure->kind == ForInError::kResourceLimit); low = middle + 1;
      } else high = middle;
    }
    std::cout << "scale " << count << " work " << plan.work << " byte_boundary " << low << std::endl;
    Check(std::holds_alternative<ForInPlan>(BuildForInPlan(graph, {low})));
    Check(std::get<ForInFailure>(BuildForInPlan(graph, {low - 1})).kind == ForInError::kResourceLimit);
  }
}
void EventMutations() {
  const auto graph = Import(Body(1, true, true), 0, 5);
  const auto plan = Prove(graph);
  const auto proof = std::get<ForInEvents>(BuildForInEvents(graph, plan));
  const auto& pair = proof.loops[0];
  Check(pair.advance.result == pair.key.next_result);
  Check(pair.advance.before != pair.advance.commit);
  Check(pair.key.success == plan.loops[0].body_edge);
  Check(pair.key.exhausted == plan.loops[0].done_edge);
  const auto reject = [&](const ForInEvents& changed) {
    const auto checked = VerifyForInEvents(graph, plan, changed);
    Check(std::holds_alternative<ForInFailure>(checked));
    const auto& failure = std::get<ForInFailure>(checked);
    Check(failure.kind != ForInError::kResourceLimit && failure.kind != ForInError::kWorkLimit);
  };
  for (unsigned mode = 0; mode < 10; ++mode) {
    auto changed = proof;
    switch (mode) {
      case 0: changed.loops[0].advance.commit = pair.advance.before; break;
      case 1: changed.loops[0].advance.result = pair.acquisition.result; break;
      case 2: changed.loops[0].advance.exceptional_exit = pair.acquisition.exceptional_exit; break;
      case 3: std::swap(changed.loops[0].key.success, changed.loops[0].key.exhausted); break;
      // Key write on exhaustion is not an advance Commit, even though next
      // completed normally and produced the undefined exhaustion sentinel.
      case 4: changed.loops[0].key.success = pair.key.exhausted; break;
      case 5: changed.loops.clear(); break;
      case 6: changed.loops.push_back(pair); break;
      case 7: changed.loops[0].acquisition = pair.advance; break;
      case 8: ++changed.cfg_revision.value; break;
      case 9: changed.loops[0].key.next_result = pair.acquisition.result; break;
    }
    reject(changed);
  }
  auto bad = graph;
  // The raw Commit, rather than a reconstructed neighbouring PC, owns success.
  std::get<OpResult>(bad.values[pair.advance.result.value].origin).commit = pair.advance.before;
  Check(std::holds_alternative<ForInFailure>(VerifyForInEvents(bad, plan, proof)));
  bad = graph;
  bad.exceptional_exits.erase(bad.exceptional_exits.begin() + pair.advance.exceptional_exit.value);
  Check(std::holds_alternative<ForInFailure>(VerifyForInEvents(bad, plan, proof)));
  bad = graph;
  bad.edges[pair.key.exhausted.value].target = bad.edges[pair.key.success.value].target;
  Check(std::holds_alternative<ForInFailure>(VerifyForInEvents(bad, plan, proof)));
  auto wrong_plan = plan;
  wrong_plan.forwarding.clear();
  Check(std::holds_alternative<ForInFailure>(VerifyForInEvents(graph, wrong_plan, proof)));
}
void EventScales() {
  for (unsigned count : {200U, 201U, 400U, 800U}) {
    const auto graph = Import(Body(count, true, true), 0, 5);
    const auto plan = std::get<ForInPlan>(BuildForInPlan(graph));
    const auto proof = std::get<ForInEvents>(BuildForInEvents(graph, plan));
    Check(proof.loops.size() == count);
    const auto work = std::get<std::uint64_t>(VerifyForInEvents(graph, plan, proof));
    Check(std::holds_alternative<ForInEvents>(BuildForInEvents(graph, plan, {proof.allocated_bytes, proof.work})));
    Check(std::get<ForInFailure>(BuildForInEvents(graph, plan, {proof.allocated_bytes - 1})).kind == ForInError::kResourceLimit);
    Check(std::get<ForInFailure>(BuildForInEvents(graph, plan, {proof.allocated_bytes, proof.work - 1})).kind == ForInError::kWorkLimit);
    Check(std::holds_alternative<std::uint64_t>(VerifyForInEvents(graph, plan, proof, {proof.allocated_bytes, work})));
    Check(std::get<ForInFailure>(VerifyForInEvents(graph, plan, proof, {proof.allocated_bytes, work - 1})).kind == ForInError::kWorkLimit);
    Check(proof.work < 1024 * (graph.ops.size() + graph.values.size() + graph.uses.size()) * std::bit_width(graph.blocks.size()));
    std::cout << "event scale " << count << " build_work " << proof.work << " verify_work " << work
              << " bytes " << proof.allocated_bytes << std::endl;
  }
}
EvaluationPlan Evaluated(const ValueProgram& graph) {
  auto result = BuildPhasedEvaluationPlan(graph);
  if (const auto* e = std::get_if<EvaluationFailure>(&result)) {
    std::cerr << "evaluation " << unsigned(e->kind) << " obligation " << unsigned(e->evaluation.obligation)
      << " op " << e->evaluation.location.operation.value_or(OpId{UINT32_MAX}).value << std::endl;
    if (e->evaluation.for_in) std::cerr << "protocol " << unsigned(e->evaluation.for_in->kind) << std::endl;
  }
  Check(std::holds_alternative<EvaluationPlan>(result));
  auto plan = std::get<EvaluationPlan>(std::move(result));
  Check(plan.for_in && plan.for_in_events);
  Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, plan)));
  for (const auto& loop : plan.for_in->loops) {
    for (const auto op : {loop.acquisition, loop.advance}) {
      Check(plan.operations[op.value].kind == EvaluationKind::kForInEvent);
      Check(!plan.operations[op.value].consumer && !plan.operations[op.value].temporary);
      Check(PhaseCorridors::Event(graph.ops[op.value], plan.operations[op.value]));
    }
    Check(!plan.for_in->uses[loop.object.value]); // RHS is a real source use.
  }
  for (const auto& key : plan.for_in->keys) Check(!plan.for_in->uses[key.use.value]);
  return plan;
}
void Evaluation() {
  for (bool caught : {false, true}) for (bool split : {false, true}) {
    const auto graph = Import(Body(1, caught, split), 0, 5);
    const auto plan = Evaluated(graph); Check(plan.for_in->loops.size() == 1);
    const auto& loop = plan.for_in->loops.front();
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
      auto bad = plan;
      switch (mutation) {
        case 0: bad.for_in->operations[loop.advance.value].reset(); break;
        case 1: bad.for_in->uses[graph.ops[loop.advance.value].first_use.value].reset(); break;
        case 2: bad.for_in->values[loop.enumerator.value]->role = ForInRole::kNextResult; break;
        case 3: bad.operations[loop.acquisition.value].kind = EvaluationKind::kElidedPure; break;
        case 4: std::swap(bad.for_in_events->loops[0].key.success,bad.for_in_events->loops[0].key.exhausted); break;
        case 5: bad.for_in_events.reset(); break;
      }
      Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, bad)));
    }
    auto changed = graph;
    changed.edges[loop.done_edge.value].target = changed.edges[loop.body_edge.value].target;
    Check(std::holds_alternative<EvaluationFailure>(BuildPhasedEvaluationPlan(changed)));
    changed = graph;
    changed.uses[graph.ops[loop.advance.value].first_use.value].life = ValueLife::kRemoved;
    Check(std::holds_alternative<EvaluationFailure>(BuildPhasedEvaluationPlan(changed)));
    changed = graph;
    // A body observation of the VM iterator is not a source key read.
    Check(!plan.for_in->keys.empty());
    changed.uses[plan.for_in->keys.front().use.value].operand = loop.enumerator;
    Check(std::holds_alternative<EvaluationFailure>(BuildPhasedEvaluationPlan(changed)));
  }
  const auto cycle = Import(Body(1,true,true,{0x61,4,0x60,4,0xfe,0},false,true),0,7);
  const auto cyclic = Evaluated(cycle);
  Check(cyclic.for_in->loops.size() == 1 && cyclic.phis && !cyclic.phis->for_in_inputs.empty());
  auto wrong_phi = cyclic;
  wrong_phi.phis->for_in_inputs.pop_back();
  Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(cycle,wrong_phi)));
  wrong_phi = cyclic;
  for (auto& projection : wrong_phi.phis->projection) if (projection == PhiCopyUse::kForInProtocol) {
    projection = PhiCopyUse::kRetained; break;
  }
  Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(cycle,wrong_phi)));
  for (std::size_t i=0;i<cyclic.phis->copies.size();++i)
    if(cycle.values[cyclic.phis->copies[i].destination.value].domain == ValueKinds::PropertyEnumerator())
      Check(cyclic.phis->projection[i] == PhiCopyUse::kForInProtocol);
  const auto duplicate = Import(Body(1, true, false, {0x61,4,0x60,4,0xfe,0}, true), 0, 5);
  Check(std::holds_alternative<EvaluationFailure>(BuildPhasedEvaluationPlan(duplicate)));
  EvaluationEvidence e;
  e.location.pc = abc::CodeOffset{0}; e.location.working_pc = abc::CodeOffset{7};
  e.for_in = ForInFailure{ForInError::kEscape, OpId{0}, OpId{4}, ValueId{6}, UseId{2}, WorkBlockId{3}, EdgeId{5}};
  Check(ParseEvaluationFailure(EvaluationFailureJson(e)) == e);
  TreeFailureEvidence t; t.obligation = TreeObligation::kForInCorrespondence; t.location = e.location;
  Check(ParseTreeFailure(TreeFailureJson(t)) == t);
  for (unsigned count : {200U,201U,400U,800U}) {
    const auto graph = Import(Body(count,true,true),0,5);
    const auto plan = Evaluated(graph); Check(plan.for_in->loops.size() == count);
    EvaluationPlan protocol{graph.revision,graph.cfg_revision,{}};
    SemanticBudget left; const auto initial = left;
    Check(!BuildForInEvaluation(graph,protocol,left));
    const auto bytes = initial.bytes-left.bytes; const auto work = initial.work-left.work;
    EvaluationPlan exact{graph.revision,graph.cfg_revision,{}};
    SemanticBudget budget{bytes,work}; Check(!BuildForInEvaluation(graph,exact,budget));
    EvaluationPlan low{graph.revision,graph.cfg_revision,{}};
    budget = {bytes-1,work};
    Check(BuildForInEvaluation(graph,low,budget)->kind == EvaluationError::kResourceLimit);
    low = {graph.revision,graph.cfg_revision,{}}; budget = {bytes,work-1};
    Check(BuildForInEvaluation(graph,low,budget)->kind == EvaluationError::kWorkLimit);
    std::cout << "evaluation scale " << count << " bytes " << bytes << " work " << work << std::endl;
  }
}
void File(const char* path, unsigned target) {
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto hash = std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned found = 0;
  for (const auto& method : index.methods) {
    if (!method.code || (target && method.offset.value != target)) continue;
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(*file, header.profile, *method.code));
    if (std::none_of(body.instructions.begin(), body.instructions.end(), [](const auto& ins) { return ins.decoded.opcode == abc::Opcode::kGETPROPITERATOR; })) continue;
    std::cout << method.offset.value << ' ' << method.name << std::endl;
    if (emission_checks) { emission_file=file.get(); emission_header=&header; emission_index=&index; emission_method=method.offset; }
    const auto graph = Import(body, method.code->registers, method.code->arguments, header.profile);
    if (!target) {
      for (const auto& op : graph.ops) if (const auto* enumeration = std::get_if<ValueEnumeration>(&op.operation)) {
        const auto id = OpId{static_cast<std::uint32_t>(&op - graph.ops.data())};
        const auto exit = std::find_if(graph.exceptional_exits.begin(), graph.exceptional_exits.end(),
            [&](const auto& e) { return e.op == id; });
        Check(exit != graph.exceptional_exits.end());
        Check(exit->handler.has_value() == (enumeration->action == EnumerationAction::kAcquire));
      }
      if (method.name == "#*#captured") Check(std::any_of(graph.ops.begin(), graph.ops.end(), [](const auto& op) {
        return std::holds_alternative<ValueEnvironmentPush>(op.operation);
      }));
    }
    const auto result = BuildForInPlan(graph);
    if (const auto* failure = std::get_if<ForInFailure>(&result)) {
      std::cout << "refusal " << static_cast<unsigned>(failure->kind);
      if (failure->operation) std::cout << " pc " << graph.origins[graph.ops[failure->operation->value].origin.value].pc.value;
      std::cout << std::endl;
      if (!target) Check(method.name == "#*#captured" && failure->kind == ForInError::kAcquisitionOrder);
    } else {
      const auto& plan = std::get<ForInPlan>(result);
      Check(std::holds_alternative<std::uint64_t>(VerifyForInPlan(graph, plan)));
      const auto events = BuildForInEvents(graph, plan);
      Check(std::holds_alternative<ForInEvents>(events));
      Check(std::holds_alternative<std::uint64_t>(VerifyForInEvents(graph, plan, std::get<ForInEvents>(events))));
      const auto evaluation = Evaluated(graph);
      Check(evaluation.for_in->loops == plan.loops);
      std::cout << "proved " << plan.loops.size() << " forwarding " << plan.forwarding.size() << std::endl;
      for (const auto& loop : plan.loops) std::cout << "acquire " << graph.origins[graph.ops[loop.acquisition.value].origin.value].pc.value
          << " next " << graph.origins[graph.ops[loop.advance.value].origin.value].pc.value << std::endl;
    }
    ++found;
  }
  Check(found != 0);
}
}
int main(int argc, char** argv) {
  if (argc > 2 && std::string_view(argv[1]) == "--emission-file") { emission_checks = true; File(argv[2], argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 0); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--emission") {
    emission_checks = true;
    for(bool caught : {false,true}) for(bool split : {false,true}) Import(Body(2,caught,split),0,5);
    Import(Body(1,false,false,{0x61,4,0x60,4,0xfe,0},false,true),0,7);
    Import(Body(1,true,false,{0x61,4,0x60,4,0xfe,0},false,false,true),0,5,abc::ProfileId::kDynamic1206,true);
    auto shared = Body(1,true);
    const auto advance = std::find_if(shared.instructions.begin(),shared.instructions.end(),[](const auto& i) {
      return i.decoded.opcode == abc::Opcode::kGETNEXTPROPNAME_V8;
    });
    Check(advance != shared.instructions.end());
    auto region = shared.tries.front(); region.offset = advance->offset; region.length = 2;
    region.first_handler = static_cast<std::uint32_t>(shared.handlers.size());
    shared.handlers.push_back(shared.handlers.front()); shared.tries.push_back(region);
    const auto shared_graph = Import(shared,0,5);
    const auto rejected = BuildForInPlan(shared_graph);
    Check(std::holds_alternative<ForInFailure>(rejected) && std::get<ForInFailure>(rejected).kind == ForInError::kCatchState);
    for (unsigned size : {200U,201U,400U,800U}) Import(Body(size,true),0,5);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--evaluation") { Evaluation(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--events") { EventMutations(); EventScales(); return 0; }
  if (argc > 1) { File(argv[1], argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 0); return 0; }
  for (bool caught : {false, true}) for (bool split : {false, true}) {
    const auto graph = Import(Body(1, caught, split), 0, 5);
    const auto plan = Prove(graph); Check(plan.loops.size() == 1);
    Check(plan.forwarding.size() == static_cast<unsigned>(caught));
  }
  Normal(); Mutations(); Adverse(); ForwardingPaths(); Scales();
}
