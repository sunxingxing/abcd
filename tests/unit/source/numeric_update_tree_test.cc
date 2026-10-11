#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/flow_kinds.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <sstream>
#include <bit>
#include <array>
#include <string_view>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr std::array<std::uint64_t, 5> kSeeds{
    UINT64_C(0x401c000000000000), UINT64_C(0x8000000000000000),
    UINT64_C(0x41dfffffffc00000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000)};
struct Fixture {
  Fixture(bool decrement, std::uint32_t count, bool phased = false, std::size_t seed = 0,
          abc::ProfileId profile = abc::ProfileId::kDynamic1206,
          unsigned copies = 0, bool split = false, bool arbitrary = false, bool convert = true, bool reverse = false) {
    // Pinned dynamic ISA: keep the converted old value distinct from each
    // update result. Mutating the old SSA snapshot corrupts the returned pair.
    std::vector<std::uint8_t> bytes;
    constexpr unsigned kByteBits = 8;
    const auto opcode = [&](abc::Opcode op) {
      bytes.push_back(static_cast<std::uint8_t>(op));
      if (static_cast<std::uint16_t>(op) > UINT8_MAX)
        bytes.push_back(static_cast<std::uint16_t>(op) >> kByteBits);
    };
    constexpr std::uint8_t kLocalRegisters = 3; // This generated function's frame layout.
    const auto reg = [&](std::uint8_t id) { return static_cast<std::uint8_t>(reverse && id < kLocalRegisters ? kLocalRegisters - 1 - id : id); };
    const auto load = [&](std::uint8_t id) { opcode(abc::Opcode::kLDA_V8); bytes.push_back(reg(id)); };
    const auto store = [&](std::uint8_t id) { opcode(abc::Opcode::kSTA_V8); bytes.push_back(reg(id)); };
    if (arbitrary) load(4);
    else {
      opcode(abc::Opcode::kFLDAI_IMM64);
      for (unsigned shift = 0; shift < sizeof(std::uint64_t) * kByteBits; shift += kByteBits)
        bytes.push_back(static_cast<std::uint8_t>(kSeeds[seed] >> shift));
    }
    if (convert) { opcode(abc::Opcode::kTONUMERIC_IMM8); bytes.push_back(0); }
    store(1);
    for (unsigned i = 0; i < copies; ++i) {
      opcode(abc::Opcode::kMOV_V8_V8); bytes.insert(bytes.end(), {reg(0), reg(1)});
      opcode(abc::Opcode::kMOV_V8_V8); bytes.insert(bytes.end(), {reg(1), reg(0)});
    }
    if (split) { opcode(abc::Opcode::kJMP_IMM8); bytes.push_back(2); }
    const auto protected_pc = static_cast<std::uint32_t>(bytes.size());
    for (std::uint32_t i = 0; i < count; ++i) {
      opcode(decrement ? abc::Opcode::kDEC_IMM8 : abc::Opcode::kINC_IMM8); bytes.push_back(0);
    }
    store(2);
    load(3); opcode(abc::Opcode::kCALLARGS2_IMM8_V8_V8); bytes.insert(bytes.end(), {0, reg(1), reg(2)});
    opcode(abc::Opcode::kCREATEEMPTYOBJECT); store(0);
    load(1); opcode(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); bytes.insert(bytes.end(), {0, 0, 0, reg(0)});
    load(2); opcode(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); bytes.insert(bytes.end(), {0, 1, 0, reg(0)});
    load(0); opcode(abc::Opcode::kRETURN);
    const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
    if (phased) { load(1); opcode(abc::Opcode::kRETURN); }
    auto body = test::Body(bytes);
    if (phased) {
      abc::TryRegion region; region.offset = {protected_pc};
      region.length = handler_pc - protected_pc; region.handler_count = 1;
      body.tries.push_back(region);
      abc::CatchHandler handler; handler.offset = {handler_pc};
      handler.length = static_cast<std::uint32_t>(bytes.size()) - handler_pc;
      body.handlers.push_back(handler);
    }
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, arbitrary ? 2 : 1, {}));
    RouteInputIdentity identity;
    identity.profile = profile;
    if (arbitrary) identity.contract = SourceContract::kLanguageNumericV1;
    switch (profile) {
      case abc::ProfileId::kDynamic1202: identity.abc_version = {12, 0, 2, 0}; break;
      case abc::ProfileId::kDynamic1206: identity.abc_version = {12, 0, 6, 0}; break;
      case abc::ProfileId::kDynamic1301: identity.abc_version = {13, 0, 1, 0}; break;
      default: test::Check(false);
    }
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = {200};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = {u"old", u"updated"}; syntax.parameters.resize(arbitrary ? 2 : 1);
    const auto prepare_constants = [&] {
      constants.resize(graph.ops.size());
      for (std::size_t i = 0; i < graph.ops.size(); ++i)
        if (const auto* write = std::get_if<ValueOwnWrite>(&graph.ops[i].operation))
          constants[i] = NameId{write->name.value};
    };
    if (phased) {
      const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
      IdfQuota quota{kDefaultSemanticWork};
      const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
      std::vector<EntryValueFact> entries;
      for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
        const auto& value = ssa.Values()[i];
        if (const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
            reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
          entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
      }
      const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
      IdfQuota value_work{kDefaultSemanticWork};
      const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
          value_work, {}, PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage,
          FrameReceiver::kUnknown, Arithmetic(identity.contract)));
      const auto checked = VerifyExceptionInputs(flow, ssa, env, values);
      test::Check(std::holds_alternative<ExceptionInputProof>(checked));
      const auto proof = std::get<ExceptionInputProof>(checked);
      const PhasedValueInput input{flow, ssa, env, values, proof, frame};
      graph = std::get<ValueProgram>(ImportPhasedValues(input));
      evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
      prepare_constants();
      const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
      const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
      const ExceptionTreeInput context{body, input, regions, control};
      const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, {constants, 2});
      test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
      tree = std::get<ExceptionFunctionDraft>(built).tree;
      original = std::get<ExceptionFunctionDraft>(built).flow->original;
      work = std::get<ExceptionFunctionDraft>(built).work;
      const auto valid = [&](const FunctionTree& candidate) {
        return std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(
            graph, context, evaluation, candidate, {constants, 2}));
      };
      test::Check(valid(tree)); Mutations(valid); ResultStorage(); OriginalActions(count == 1, !arbitrary, !arbitrary && seed < 3);
      return;
    }
    const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {200}, {100},
        body, frame, InputCaptureMode::kConditional));
    graph = std::get<FunctionValues>(BuildFunctionValues(input, body, frame)).graph;
    control = std::get<ControlPlan>(BuildControlPlan(graph));
    evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph));
    prepare_constants();
    auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), {constants, 2});
    test::Check(std::holds_alternative<ConstructedFunction>(built));
    work = std::get<ConstructedFunction>(built).work;
    original = std::get<ConstructedFunction>(built).flow->original;
    tree = std::get<ConstructedFunction>(std::move(built)).tree;
    test::Check(Valid(tree)); Mutations([&](const FunctionTree& candidate) { return Valid(candidate); }); ResultStorage(); OriginalActions(count == 1, !arbitrary, !arbitrary && seed < 3);
  }
  void OriginalActions(bool solve, bool numeric, bool finite_numeric) const {
    // The verified composite owns a saved operand and a native conversion
    // action. Source coverage/result identity cannot replace either relation.
    std::optional<ExprId> selected, saved;
    for (const auto& operation : original->operations) {
      if (!operation.expression) continue;
      const auto* sequence = std::get_if<SequenceExpr>(&tree.expressions[operation.expression->value].expression);
      if (!sequence || !std::holds_alternative<UpdateExpr>(tree.expressions[sequence->right.value].expression)) continue;
      test::Check(operation.intrinsic_expression == sequence->right);
      const auto& definition = original->definitions[sequence->left.value];
      test::Check(definition && std::holds_alternative<OriginalSavedDefinition>(definition->origin));
      const auto& assignment = std::get<AssignmentExpr>(tree.expressions[sequence->left.value].expression);
      test::Check(definition->binding == std::get<SymbolTarget>(assignment.target).symbol && definition->input == assignment.value);
      if (!selected) { selected = sequence->right; saved = sequence->left; }
    }
    if (!solve) return;
    test::Check(selected.has_value());
    BindingInitializers plan;
    const auto b = tree.symbols.size(), x = tree.expressions.size(), s = tree.statements.size();
    plan.aliases.resize(b); plan.literals.resize(b); plan.storage.resize(b); plan.read_bindings.resize(b);
    plan.read_sources.resize(x); plan.read_literals.resize(x); plan.replacements.resize(x); plan.transports.resize(x);
    plan.omitted.resize(s); plan.statements.resize(s);
    std::vector<FlowValueEquation> equations(x); std::vector<ExprId> order;
    std::vector<std::uint8_t> private_bindings(b), stable(b);
    for (unsigned i = 0; i < b; ++i) private_bindings[i] = tree.symbols[i].kind == BindingKind::kCompilerTemporary;
    for (const auto& parameter : tree.parameters) stable[parameter.symbol.value] = 1;
    for (unsigned i = 0; i < x; ++i) {
      order.push_back({i});
      if (const auto* read = std::get_if<SymbolExpr>(&tree.expressions[i].expression)) equations[i] = FlowBindingValue{read->symbol};
    }
    LegacyFlowProjection projection{plan.aliases, plan.literals, plan.replacements, plan.omitted, {}, {}, plan.statements, private_bindings};
    projection.stable_entries = stable;
    const auto prepare = [&](const OriginalFlow& raw, BindingPlanView candidate) {
      return std::get<RecoveryProjection>(PrepareRecoveryProjection(tree, projection, {candidate, equations, order, &raw}));
    };
    const auto action = [&](const RecoveryProjection& model) -> const FlowEvent& {
      for (const auto& event : model.flow.events)
        if (event.kind == FlowEventKind::kEvaluation && event.expression == selected) return event;
      test::Check(false); return model.flow.events.front();
    };
    const auto current = prepare(*original, plan);
    // Exercise imported exception-phase identities, including composite action
    // anchors. No fixture PC, register or adjacent source node supplies a phase.
    bool before_seen=false,commit_seen=false,withdrawal_seen=false;
    for(unsigned i=0;i<original->phases.size();++i) {
      const auto& phase=original->phases[i];if(!phase.operation)continue;
      const auto& operation=original->operations[phase.operation->value];
      const WorkBlockId block{i};
      if(phase.phase==ExceptionPhase::kBefore&&operation.intrinsic_expression) {
        const auto& native=current.flow.actions[operation.intrinsic_expression->value];
        test::Check(native&&FindOriginalPhase(current.flow,block,phase.phase,phase.operation)==native->before);
        test::Check(!FindOriginalPhase(current.flow,block,ExceptionPhase::kCommit,phase.operation));
        if(operation.intrinsic_expression==selected) {
          before_seen=true;
          // Conversion starts after the separately captured lexical read.
          const auto& event=current.flow.events[native->before.value];
          test::Check(event.kind==FlowEventKind::kEvaluation&&event.input);
          test::Check(current.flow.captured_inputs[event.input->value].value.point!=native->before);
          auto lost_before=*original;lost_before.phases[block.value].operation.reset();
          const auto source_action=prepare(lost_before,plan);
          test::Check(!source_action.flow.original_effects[selected->value]);
          test::Check(source_action.flow.actions[selected->value].has_value());
        }
      }
      const auto result=FindFlowOriginalResult(current.flow,*phase.operation);
      if(phase.phase!=ExceptionPhase::kCommit||!result||result->commit!=block)continue;
      const bool first_commit=!commit_seen;commit_seen=true;
      const auto completed=operation.completion==OriginalCompletionBoundary::kIntrinsicAction?
          current.flow.actions[operation.intrinsic_expression->value]->normal:
          current.flow.normal_exits[result->expression.value];
      test::Check(FindOriginalPhase(current.flow,block,phase.phase,phase.operation)==completed);
      if(!first_commit&&operation.intrinsic_expression!=selected)continue;
      auto missing=current.flow;missing.original_phases[block.value].reset();
      test::Check(!FindFlowOriginalResult(missing,*phase.operation));
      auto foreign=*original;foreign.phases[block.value].operation.reset();
      auto mismatched=current.flow;mismatched.original=&foreign;
      test::Check(!FindFlowOriginalResult(mismatched,*phase.operation));
      for(const auto& definition:original->definitions)if(definition)
        if(const auto* stored=std::get_if<OriginalResultDefinition>(&definition->origin);stored&&stored->operation==phase.operation) {
          test::Check(!OriginalDefinitionKinds(current.flow,*definition).IsEmpty());
          test::Check(OriginalDefinitionKinds(missing,*definition).IsEmpty());
          test::Check(OriginalDefinitionKinds(mismatched,*definition).IsEmpty());
        }
      if(operation.intrinsic_expression==selected&&result->kinds.IsFiniteNumber()) {
        test::Check(current.expressions.kinds[result->expression.value].IsFiniteNumber());
        const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(missing));
        const auto withdrawn=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(tree,missing,live,{plan,equations,order,original.get()}));
        test::Check(withdrawn.expressions[result->expression.value].IsNumber());
        test::Check(!withdrawn.expressions[result->expression.value].IsFiniteNumber());
        withdrawal_seen=true;
      }
    }
    if(!original->phases.empty())test::Check(before_seen&&commit_seen);
    if(!original->phases.empty()&&finite_numeric)test::Check(withdrawal_seen);
    test::Check(action(current).original_effect_valid && action(current).effects);
    test::Check(HasLanguageEffect(action(current).effects->normal, LanguageEffect::kUserCode) == !numeric);
    test::Check((action(current).effects->exceptional == ExceptionalCompletion::kImpossible) == numeric);
    test::Check(action(current).input.has_value());
    const auto& captured = current.flow.captured_inputs[action(current).input->value];
    test::Check(captured.kinds.IsNumber() == numeric);
    test::Check(current.expressions.evaluations[selected->value] == ExpressionEvaluation::kRequired);
    bool read = false, commit = false;
    for (const auto& event : current.flow.events) if (event.expression == selected) {
      if (event.kind == FlowEventKind::kRead) {
        read = true; test::Check(event.read_completion == FlowReadCompletion::kInitialized);
      }
    }
    for (const auto& definition : current.flow.definitions)
      if (definition.kind == FlowDefinitionKind::kUpdate && definition.write == selected) {
        test::Check(definition.input == captured.value); commit = true;
      }
    test::Check(read && commit);
    for (auto event : current.flow.events) if (event.kind == FlowEventKind::kRead && event.expression == selected) {
      // The verified native action can be inert while this independent read
      // lacks initialization proof. Never lend a conversion bound to GetValue.
      event.read_completion = FlowReadCompletion::kUnproved; event.original_effect_valid = true;
      const FlowEffectInput facts{current.expressions.evaluations, current.expressions.normal_stores,
          current.expressions.evaluations, current.expressions.kinds};
      // This no-throw bound is a synthetic adapter input, not a claim that
      // the decoded ISA's conservative effect table supplied this precision.
      auto bounded = current.flow; bounded.original_effects[selected->value] = SourceEffects{};
      const auto effects = EvaluateFlowEventEffects(bounded, tree, event, facts);
      test::Check(effects.intrinsic && effects.intrinsic->exceptional == ExceptionalCompletion::kMayThrow);
    }
    auto missing = *original; missing.definitions[saved->value].reset();
    const auto withdrawn = prepare(missing, plan);
    test::Check(!action(withdrawn).original_effect_valid);
    test::Check(HasLanguageEffect(action(withdrawn).effects->normal, LanguageEffect::kUserCode) == !numeric);
    // Editing the captured operand keeps IDs intact but withdraws the old
    // action proof. The candidate must solve initialization/effects anew.
    const auto input = std::get<AssignmentExpr>(tree.expressions[saved->value].expression).value;
    plan.primitives.push_back(BooleanLiteral{true});
    auto transports = plan.transports; transports[input.value] = ExpressionTransport{input, PrimitiveId{0}};
    auto candidate = BindingPlanView{plan}; candidate.transports = transports;
    const auto changed = prepare(*original, candidate);
    test::Check(!action(changed).original_effect_valid);
    // The new Boolean is independently primitive even after the raw identity
    // proof is withdrawn. Source kinds still discharge conversion effects.
    test::Check(!HasLanguageEffect(action(changed).effects->normal, LanguageEffect::kUserCode));
    // With declaration initialization withdrawn, the extra lexical GetValue
    // can fail. The raw conversion bound must not suppress that earlier phase.
    const auto binding = std::get<UpdateExpr>(tree.expressions[selected->value].expression).symbol;
    auto omitted = plan.omitted; bool declaration = false;
    for (unsigned i = 0; i < tree.statements.size(); ++i)
      if (const auto* node = std::get_if<DeclarationStmt>(&tree.statements[i].statement); node && node->symbol == binding) {
        omitted[i] = 1; declaration = true;
      }
    test::Check(declaration); candidate = BindingPlanView{plan}; candidate.omitted = omitted;
    const auto uninitialized = prepare(*original, candidate);
    bool throwing_read = false;
    for (const auto& event : uninitialized.flow.events)
      if (event.kind == FlowEventKind::kRead && event.expression == selected) {
        test::Check(event.effects && event.effects->exceptional == ExceptionalCompletion::kMayThrow);
        for (auto edge = event.outgoing; edge; edge = uninitialized.flow.edges[*edge].outgoing)
          throwing_read |= uninitialized.flow.edges[*edge].completion == FlowCompletion::kThrow;
      }
    test::Check(throwing_read);
  }
  void ResultStorage() const {
    // A materialized numeric result owns its update's private storage. Test
    // actual tree identity, independently of names or planner allocation.
    bool shared = false;
    for (const auto& node : tree.expressions) {
      const auto* publish = std::get_if<AssignmentExpr>(&node.expression);
      const auto* target = publish ? std::get_if<SymbolTarget>(&publish->target) : nullptr;
      const auto* sequence = publish ? std::get_if<SequenceExpr>(&tree.expressions[publish->value.value].expression) : nullptr;
      const auto* update = sequence ? std::get_if<UpdateExpr>(&tree.expressions[sequence->right.value].expression) : nullptr;
      if (!target || !update || node.origin != tree.expressions[sequence->right.value].origin) continue;
      test::Check(target->symbol == update->symbol); shared = true;
    }
    test::Check(shared);
  }
  template<class Valid> void Mutations(Valid valid) const {
    std::optional<ExprId> first, second;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i)
      if (std::holds_alternative<UpdateExpr>(tree.expressions[i].expression)) {
        if (!first) first = ExprId{static_cast<std::uint32_t>(i)};
        else { second = ExprId{static_cast<std::uint32_t>(i)}; break; }
      }
    test::Check(first.has_value());
    auto changed = tree;
    auto& update = std::get<UpdateExpr>(changed.expressions[first->value].expression);
    update.direction = update.direction == UpdateDirection::kIncrement ? UpdateDirection::kDecrement : UpdateDirection::kIncrement;
    test::Check(!valid(changed));
    changed = tree;
    auto& result = std::get<UpdateExpr>(changed.expressions[first->value].expression).result;
    result = result == UpdateResult::kPrefix ? UpdateResult::kPostfix : UpdateResult::kPrefix;
    test::Check(!valid(changed));
    changed = tree; std::get<UpdateExpr>(changed.expressions[first->value].expression).symbol = changed.parameters[0].symbol;
    test::Check(!valid(changed));
    const auto scratch = std::get<UpdateExpr>(tree.expressions[first->value].expression).symbol;
    changed = tree;
    for (auto& node : changed.expressions)
      if (auto* read = std::get_if<SymbolExpr>(&node.expression)) { read->symbol = scratch; break; }
    test::Check(!valid(changed));
    changed = tree;
    for (auto& node : changed.expressions)
      if (auto* sequence = std::get_if<SequenceExpr>(&node.expression)) {
        std::swap(sequence->left, sequence->right); break;
      }
    test::Check(!valid(changed));
    if (second) {
      changed = tree; std::get<UpdateExpr>(changed.expressions[second->value].expression).symbol = scratch;
      for (auto& node : changed.expressions)
        if (const auto* sequence = std::get_if<SequenceExpr>(&node.expression); sequence && sequence->right == second)
          std::get<AssignmentExpr>(changed.expressions[sequence->left.value].expression).target = SymbolTarget{scratch};
      test::Check(!valid(changed));
    }
  }
  bool Valid(const FunctionTree& candidate) const {
    return std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(
        graph, control, evaluation, candidate, {constants, 2}));
  }
  std::string Print() const {
    std::ostringstream output;
    test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, nullptr, original.get())));
    return output.str();
  }
  ValueProgram graph;
  ControlPlan control;
  EvaluationPlan evaluation;
  std::vector<ExpressionConstant> constants;
  FunctionTree tree;
  std::shared_ptr<const OriginalFlow> original;
  std::uint64_t work = 0;
};
std::string PrintUpdateSyntax(bool decrement, UpdateResult result) {
  // Syntax-only native Number/BigInt/coercion coverage, not imported ABC
  // acceptance for arbitrary value domains.
  FunctionTree tree;
  tree.callable = CallableKind::kFunction; tree.strictness = Strictness::kStrict;
  tree.names = {u"input", u"scratch"};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {2};
  tree.symbols = {{{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}},
      {{1}, {1}, BindingKind::kCompilerTemporary, DeclarationKind::kLet, NameEvidence::kSynthesized, TemporaryReason::kNumericUpdate}};
  tree.parameters = {{SymbolId{0}, {}, false}};
  tree.origins = {BindingOrigin{SymbolId{1}}};
  tree.expressions = {{SymbolExpr{SymbolId{0}}, {0}},
      {AssignmentExpr{SymbolTarget{SymbolId{1}}, ExprId{0}}, {0}},
      {UpdateExpr{SymbolId{1}, decrement ? UpdateDirection::kDecrement : UpdateDirection::kIncrement, result}, {0}},
      {SequenceExpr{ExprId{1}, ExprId{2}}, {0}}};
  tree.statements = {{DeclarationStmt{SymbolId{1}, {}}, {0}},
      {ReturnStmt{ExprId{3}}, {0}}, {BlockStmt{ScopeId{1}, 0, 2}, {0}}};
  tree.children = {StmtId{0}, StmtId{1}};
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  return output.str();
}
}
int main(int argc, char** argv) {
  const bool emit = argc == 2 && std::string_view{argv[1]} == "--emit";
  if (emit) std::cout << "export const cases = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool phased : {false, true})
      for (const bool decrement : {false, true}) {
        std::uint64_t previous = 0;
        for (const auto count : {1U, 2U, 200U, 201U, 400U}) {
          const Fixture fixture(decrement, count, phased, 0, profile);
          if (count == 400) test::Check(fixture.work < previous * 3);
          previous = fixture.work;
          if (emit && count <= 2) std::cout << "{count:" << count << ",decrement:" << decrement
              << ",caught:" << phased << ",seed:7,fn:" << fixture.Print() << "},\n";
        }
        for (const auto [copies, split] : {std::pair{1U, false}, std::pair{201U, false}, std::pair{201U, true}}) {
          const Fixture fixture(decrement, 1, phased, 0, profile, copies, split);
          if (emit) std::cout << "{count:1,decrement:" << decrement << ",caught:" << phased
              << ",seed:7,fn:" << fixture.Print() << "},\n";
        }
        {
          const Fixture fixture(decrement, 1, phased, 0, profile, 201, true, false, true, true);
          if (emit) std::cout << "{count:1,decrement:" << decrement << ",caught:" << phased
              << ",seed:7,fn:" << fixture.Print() << "},\n";
        }
        for (std::size_t seed = 1; seed < kSeeds.size(); ++seed) {
          const Fixture fixture(decrement, 1, phased, seed, profile);
          if (emit) {
            constexpr std::array<std::string_view, 5> kSeedText{"7", "-0", "2147483647", "Infinity", "-Infinity"};
            std::cout << "{count:1,decrement:" << decrement << ",caught:" << phased
                << ",seed:" << kSeedText[seed] << ",fn:" << fixture.Print() << "},\n";
          }
        }
      }
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const arbitrary = [\n";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool phased : {false, true})
      for (const bool decrement : {false, true}) for (const bool convert : {false, true}) for (const bool reverse : {false, true}) {
        const Fixture fixture(decrement, 1, phased, 0, profile, 201, true, true, convert, reverse);
        if (emit) std::cout << "{decrement:" << decrement << ",caught:" << phased << ",convert:" << convert
            << ",fn:" << fixture.Print() << "},\n";
      }
  if (emit) std::cout << "];\n";
  if (emit) std::cout << "export const syntax = [\n";
  for (const bool decrement : {false, true})
    for (const auto result : {UpdateResult::kPrefix, UpdateResult::kPostfix}) {
      const auto source = PrintUpdateSyntax(decrement, result);
      if (emit) std::cout << "{decrement:" << decrement << ",postfix:" << (result == UpdateResult::kPostfix)
          << ",fn:" << source << "},\n";
    }
  if (emit) std::cout << "];\n";
}
