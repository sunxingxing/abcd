#include "../../../src/source/function_tree.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/instruction_sources.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include "../../support/analysis/pure_control_oracle.h"
#include "../../support/instruction_positions.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
using test::Check;
namespace {
enum class Form { kDirect, kReversedOperands, kNegated, kMissingUndefined, kDifferentResult, kBarrier,
                  kCopiedSplit, kBooleanGuard, kCallProducer, kEffectPrefix, kSplitStatements, kReversedStatements };
abc::MethodBody Body(unsigned count, Form form, abc::ProfileId profile) {
  std::vector<std::uint8_t> bytes;
  const auto emit = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> 8));
  };
  const auto load = [&](std::uint8_t slot) { emit(abc::Opcode::kLDA_V8); bytes.push_back(slot); };
  const auto store = [&](std::uint8_t slot) { emit(abc::Opcode::kSTA_V8); bytes.push_back(slot); };
  const auto read = [&](std::uint8_t name) {
    load(2); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(), {0, name, 0});
  };
  const auto jump = [&](abc::Opcode opcode) {
    const auto pc = bytes.size(); emit(opcode); const auto operand = bytes.size();
    bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return std::pair{pc, operand};
  };
  const auto patch = [&](auto branch, std::size_t target) {
    const auto delta = static_cast<std::uint32_t>(target - branch.first);
    for (unsigned i = 0; i < sizeof(delta); ++i) bytes[branch.second + i] = static_cast<std::uint8_t>(delta >> (i * 8));
  };
  read(0);
  if (form == Form::kCallProducer) {
    emit(abc::Opcode::kCALLTHIS0_IMM8_V8); bytes.insert(bytes.end(), {0, 2});
  }
  store(0);
  std::vector<std::pair<std::size_t, std::size_t>> exits;
  for (unsigned i = 0; i < count; ++i) for (const bool null : {true, false}) {
    if (!null && form == Form::kMissingUndefined) continue;
    if ((form == Form::kBarrier || form >= Form::kEffectPrefix) && !null) {
      read(2);
      if (form != Form::kEffectPrefix) emit(abc::Opcode::kDEBUGGER);
      if (form == Form::kSplitStatements) {
        const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size());
      }
    }
    if (form == Form::kCopiedSplit || form == Form::kSplitStatements) {
      load(0); store(1);
      const auto split = jump(abc::Opcode::kJMP_IMM32); patch(split, bytes.size());
    }
    if (form == Form::kBooleanGuard) {
      emit(abc::Opcode::kLDNULL); emit(abc::Opcode::kSTRICTEQ_IMM8_V8);
      bytes.insert(bytes.end(), {0, 0}); store(1);
    }
    emit(null ? abc::Opcode::kLDNULL : abc::Opcode::kLDUNDEFINED);
    std::uint8_t slot = form == Form::kCopiedSplit || form == Form::kSplitStatements || form == Form::kBooleanGuard ? 1 : 0;
    if (form == Form::kReversedOperands || form == Form::kReversedStatements) { store(1); load(0); slot = 1; }
    emit(form == Form::kNegated ? abc::Opcode::kSTRICTEQ_IMM8_V8 : abc::Opcode::kSTRICTNOTEQ_IMM8_V8);
    bytes.insert(bytes.end(), {0, slot});
    if (form == Form::kNegated) emit(abc::Opcode::kISFALSE);
    exits.push_back(jump(abc::Opcode::kJEQZ_IMM32));
  }
  if (form == Form::kDifferentResult) read(2); else load(0);
  const auto skip = jump(abc::Opcode::kJMP_IMM32);
  const auto fallback = bytes.size(); read(1);
  for (const auto exit : exits) patch(exit, fallback);
  patch(skip, bytes.size()); emit(abc::Opcode::kRETURN);
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta) instruction.branch_target = abc::CodeOffset{static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction); pc += decoded.size;
  }
  return body;
}
std::optional<std::string> Source(unsigned count, Form form, abc::ProfileId profile, std::uint64_t& work) {
  const auto body = Body(count, form, profile);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(2, 1, {}));
  RouteInputIdentity identity{}; identity.profile = profile;
  const auto input_result = FunctionInput::Build(identity, {}, {}, body, frame, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FunctionInput>(input_result)); const auto& input = std::get<FunctionInput>(input_result);
  auto graph = std::get<analysis::ValueProgram>(analysis::ImportNormalValues(body, input.Input(), input.Proof(), frame));
  Check(!std::holds_alternative<analysis::ValueSimplifyError>(analysis::SimplifyValues(graph, graph.revision)));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  const auto evaluation_result = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(evaluation_result)); const auto& evaluation = std::get<EvaluationPlan>(evaluation_result);
  Check(!std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(graph, evaluation)));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict; syntax.names = {u"value", u"probe", u"fallback", u"other"};
  syntax.parameters.push_back({NameId{0}, NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* read = std::get_if<analysis::ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
  const ExpressionConstants view{constants, 4, 0, {}, {}};
  TreeCheckBudget projection_budget({});
  const auto regions = FindSelectionExpressions(graph, control, evaluation, projection_budget);
  if (form == Form::kBarrier || form >= Form::kEffectPrefix) {
    // Original statements and discarded effectful reads must execute on their
    // original conditional paths; a native logical cannot erase that prefix.
    for (const auto& region : regions) Check(!region.logical);
  }
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), view);
  Check(std::holds_alternative<ConstructedFunction>(built)); const auto& tree = std::get<ConstructedFunction>(built).tree;
  const auto meaning = VerifyTreeSemantics(graph, control, evaluation, tree, view);
  if (const auto* error = std::get_if<TreeSemanticError>(&meaning)) {
    std::cerr << "profile=" << unsigned(profile) << " count=" << count << " form=" << unsigned(form)
              << " semantics=" << unsigned(*error) << '\n';
    const auto flow = VerifyTreeFlow(graph, control, evaluation, tree);
    if (const auto* failure = std::get_if<TreeFlowError>(&flow)) std::cerr << "flow=" << unsigned(*failure) << '\n';
    const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
    (void)PrintFamilyDraft(family, std::cerr);
  }
  Check(std::holds_alternative<TreeSemantics>(meaning));
  if (count == 1 && (form <= Form::kNegated || form == Form::kCopiedSplit || form == Form::kCallProducer)) {
    const auto& original=*std::get<TreeSemantics>(meaning).flow.original;
    BindingInitializers bindings;
    const auto b=tree.symbols.size(),x=tree.expressions.size(),s=tree.statements.size();
    bindings.aliases.resize(b);bindings.storage.resize(b);bindings.read_bindings.resize(b);bindings.literals.resize(b);
    bindings.read_sources.resize(x);bindings.read_literals.resize(x);bindings.replacements.resize(x);bindings.transports.resize(x);
    bindings.omitted.resize(s);bindings.statements.resize(s);
    std::vector<FlowValueEquation> equations(x);std::vector<ExprId> order;std::vector<std::uint8_t> private_bindings(b);
    for(unsigned i=0;i<x;++i) {
      order.push_back({i});if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};
    }
    LegacyFlowProjection projection{bindings.aliases,bindings.literals,bindings.replacements,bindings.omitted,{},{},bindings.statements,private_bindings};
    const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{bindings,equations,order,&original}));
    test::CheckPureControls(graph,model.flow);
    bool nullish=false;
    for(unsigned i=0;i<original.operations.size();++i)if(const auto& input=original.operations[i].control;input&&input->kind==OriginalControlKind::kNullishSelection) {
      const auto current=FindFlowOriginalControl(model.flow,analysis::OpId{i});Check(current.has_value());
      Check(current->kind==OriginalControlKind::kNullishSelection&&current->expression==input->expression);nullish=true;
      // The folded tests control selection; their shared anchor is neither
      // an independent physical action nor the logical expression's value.
      Check(!original.operations[i].result_expression&&!original.operations[i].intrinsic_expression);
    }
    Check(nullish);
    for(const auto literal:{TreeLiteral{NullLiteral{}},TreeLiteral{UndefinedLiteral{}},
                           TreeLiteral{BooleanLiteral{false}},TreeLiteral{BooleanLiteral{true}}}) {
      auto candidate=bindings;const auto id=PrimitiveId{static_cast<unsigned>(candidate.primitives.size())};
      candidate.primitives.push_back(literal);
      std::optional<ExprId> captured;
      for(const auto& operation:original.operations)if(operation.control&&operation.control->test){captured=operation.control->expression;break;}
      Check(captured.has_value());candidate.transports[captured->value]=ExpressionTransport{*captured,id};
      const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{candidate,equations,order,&original}));
      const auto expected=PrimitiveTruthDomains(literal,tree.texts);
      for(unsigned i=0;i<original.operations.size();++i)if(original.operations[i].control&&original.operations[i].control->test) {
        const auto actual=OriginalControlDomains(trial.flow,analysis::OpId{i});Check(actual.has_value());
        const auto& test=*original.operations[i].control->test;
        Check(actual->evaluated==(expected&test.evaluated));Check(actual->true_values==(expected&test.true_values));
        Check(!FindFlowOriginalResult(trial.flow,analysis::OpId{i}));
      }
      // Trial facts must not replace the immutable current proposal's facts.
      test::CheckPureControls(graph,model.flow);
    }
    if(form==Form::kDirect) {
    bool checked = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto* logical = std::get_if<LogicalExpr>(&tree.expressions[i].expression);
      if (!logical || logical->operation != TreeLogical::kNullish) continue;
      auto wrong = tree;
      std::get<LogicalExpr>(wrong.expressions[i].expression).operation = TreeLogical::kOr;
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      wrong = tree;
      auto& reversed = std::get<LogicalExpr>(wrong.expressions[i].expression);
      std::swap(reversed.left, reversed.right);
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      Check(std::holds_alternative<AssignmentExpr>(tree.expressions[logical->left.value].expression));
      wrong = tree;
      auto& save = std::get<AssignmentExpr>(wrong.expressions[logical->left.value].expression);
      save.value = logical->right;
      Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view)));
      checked = true;
    }
    Check(checked);
    }
  }
  work = std::get<ConstructedFunction>(built).work + std::get<TreeSemantics>(meaning).work;
  if (form == Form::kBarrier && count == 1) {
    bool guard = false, assignment = false, final = false, order = false;
    for (std::size_t i = 0; i < tree.expressions.size(); ++i) {
      const auto& expression = tree.expressions[i].expression;
      if (const auto* unary = std::get_if<UnaryExpr>(&expression)) {
        const auto* read = std::get_if<SymbolExpr>(&tree.expressions[unary->operand.value].expression);
        if (!read || tree.symbols[read->symbol.value].temporary != TemporaryReason::kDecisionPredicate) continue;
        auto wrong = tree; std::get<UnaryExpr>(wrong.expressions[i].expression).operation = TreeUnary::kPlus;
        Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view))); guard = true;
      }
      if (const auto* saved = std::get_if<AssignmentExpr>(&expression)) {
        const auto* target = std::get_if<SymbolTarget>(&saved->target);
        if (!target || tree.symbols[target->symbol.value].temporary != TemporaryReason::kDecisionPredicate) continue;
        auto wrong = tree; std::get<AssignmentExpr>(wrong.expressions[i].expression).target = SymbolTarget{tree.parameters.front().symbol};
        Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view))); assignment = true;
      }
    }
    for (std::size_t i = 0; i < tree.statements.size(); ++i) {
      const auto& statement = tree.statements[i].statement;
      if (const auto* branch = std::get_if<IfStmt>(&statement)) {
        const auto* read = std::get_if<SymbolExpr>(&tree.expressions[branch->condition.value].expression);
        if (!read || tree.symbols[read->symbol.value].temporary != TemporaryReason::kDecisionPredicate) continue;
        auto wrong = tree; wrong.expressions[branch->condition.value].expression = TreeLiteral{BooleanLiteral{false}};
        Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view))); final = true;
      }
      if (const auto* block = std::get_if<BlockStmt>(&statement)) {
        for (std::uint32_t child = 1; child < block->child_count; ++child) {
          const auto after = tree.children[block->first_child + child];
          const auto before = tree.children[block->first_child + child - 1];
          if (!std::holds_alternative<DebuggerStmt>(tree.statements[after.value].statement) ||
              !std::holds_alternative<ExpressionStmt>(tree.statements[before.value].statement)) continue;
          auto wrong = tree; std::swap(wrong.children[block->first_child + child], wrong.children[block->first_child + child - 1]);
          Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, wrong, view))); order = true;
        }
      }
    }
    Check(guard && assignment && final && order);
  }
  const auto sources = std::get<InstructionSources>(BuildInstructionSources(input, graph, tree));
  const auto family = std::get<ConstructedFamily>(BuildFunctionFamily({{tree, {}, {}}}, tree.owner));
  std::ostringstream source; const auto printed = PrintFamilyDraft(family, source);
  if(const auto* error=std::get_if<TreePrintError>(&printed))std::cerr<<"print count="<<count<<" form="<<unsigned(form)<<" error="<<unsigned(*error)<<'\n';
  Check(std::holds_alternative<PrintedFamilyDraft>(printed)); const auto& positions = std::get<PrintedFamilyDraft>(printed);
  test::CheckInstructionPositions(tree, sources, positions.members.front(), positions.functions.front());
  if (form == Form::kBarrier || form >= Form::kEffectPrefix) {
    std::size_t debuggers = 0;
    for (const auto& statement : tree.statements) debuggers += std::holds_alternative<DebuggerStmt>(statement.statement);
    Check(debuggers == (form == Form::kEffectPrefix ? 0 : count));
  }
  const bool expected = form <= Form::kNegated || form == Form::kCopiedSplit || form == Form::kCallProducer;
  Check((source.str().find("??") != std::string::npos) == expected);
  return source.str();
}
std::string PhasedControlSource(unsigned count,Form form,abc::ProfileId profile,std::uint64_t& work,std::size_t& bytes) {
  using namespace analysis;
  const auto body=Body(count,form,profile);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{}));
  const auto raw=std::get<ExceptionFlow>(BuildExceptionFlow(body,profile,frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(raw,frame.registers,ssa_work));
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(raw));
  std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))entries.push_back({{i},ValueKinds::Language()});
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(raw,ssa,entries,value_work,{},
      PendingExceptionValues::kLanguage,{}, {},CallResultValues::kLanguage));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(raw,ssa,env,values));
  const PhasedValueInput input{raw,ssa,env,values,proof,frame};const auto graph=std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=std::get<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=std::get<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  const ExceptionTreeInput context{body,input,regions,control};
  FunctionSyntax syntax{};syntax.identity.profile=profile;syntax.owner={3};syntax.callable=CallableKind::kFunction;
  syntax.strictness=Strictness::kStrict;syntax.names={u"value",u"probe",u"fallback",u"other"};
  syntax.parameters.push_back({NameId{0},NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(unsigned i=0;i<graph.ops.size();++i)
    if(const auto* read=std::get_if<ValueNamedRead>(&graph.ops[i].operation))constants[i]=NameId{read->name.value+1};
  const auto draft=std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{constants,4}));
  const auto& tree=draft.tree;const auto& original=*draft.flow->original;
  BindingInitializers bindings;const auto b=tree.symbols.size(),x=tree.expressions.size(),s=tree.statements.size();
  bindings.aliases.resize(b);bindings.storage.resize(b);bindings.read_bindings.resize(b);bindings.literals.resize(b);
  bindings.read_sources.resize(x);bindings.read_literals.resize(x);bindings.replacements.resize(x);bindings.transports.resize(x);
  bindings.omitted.resize(s);bindings.statements.resize(s);
  std::vector<FlowValueEquation> equations(x);std::vector<ExprId> order;std::vector<std::uint8_t> private_bindings(b);
  for(unsigned i=0;i<x;++i) {
    order.push_back({i});if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};
  }
  LegacyFlowProjection projection{bindings.aliases,bindings.literals,bindings.replacements,bindings.omitted,{},{},bindings.statements,private_bindings};
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{bindings,equations,order,&original}));
  unsigned covered=0,partial=0;
  for(unsigned i=0;i<original.phases.size();++i) {
    const auto& phase=original.phases[i];if(!phase.operation)continue;
    const auto& operation=original.operations[phase.operation->value];if(!operation.control||!operation.control->test)continue;
    const auto current=FindOriginalControlPhase(model.flow,{i},phase.phase,*phase.operation);
    Check(current&&current->test==operation.control->test);++covered;
    partial+=current->test->evaluated!=TruthDomains::All();
    Check(!FindOriginalPhase(model.flow,{i},phase.phase,*phase.operation));
    Check(!model.flow.original_actions[phase.operation->value]);
    if(count==1) {
      auto missing=model.flow;missing.original_phases[i].reset();
      Check(!FindOriginalControlPhase(missing,{i},phase.phase,*phase.operation));
      missing=model.flow;missing.original_phases[i]=current->normal;
      Check(!FindOriginalControlPhase(missing,{i},phase.phase,*phase.operation));
      missing=model.flow;missing.normal_exits[current->expression.value].reset();
      Check(!FindOriginalControlPhase(missing,{i},phase.phase,*phase.operation));
      Check(!FindOriginalControlPhase(model.flow,{i},ExceptionPhase::kCatch,*phase.operation));
    }
  }
  if(!covered)std::cerr<<"phased controls profile="<<unsigned(profile)<<" count="<<count<<" form="<<unsigned(form)
      <<" covered="<<covered<<" partial="<<partial<<'\n';
  Check(covered>0);
  if(count==1) {
    // Probe the raw incoming-certificate API independently of whether the
    // printer needs a scalar phi store for this native selection. A missing
    // Commit must withdraw the contribution, even with a retained input.
    for(unsigned i=0;i<original.edges.size();++i)if(const auto owner=original.edge_controls[i]) {
      const auto block=original.edges[i].source;
      const auto current=FindOriginalControlPhase(model.flow,block,ExceptionPhase::kCommit,*owner);if(!current)continue;
      auto truths=model.flow.normal_truths;truths[current->expression.value]=TruthDomains::All();
      const OriginalSourceDefinition contribution{OriginalSavedDefinition{SpecialValue::kTrue,IncomingKey{EdgeId{i}}},
          tree.parameters.front().symbol,current->expression};
      Check(OriginalDefinitionProof(model.flow,contribution,truths).state==FlowOriginalProofState::kAvailable);
      auto missing=model.flow;missing.original_phases[block.value].reset();
      Check(OriginalDefinitionProof(missing,contribution,truths).state==FlowOriginalProofState::kUnavailable);
    }
    for(const auto literal:{TreeLiteral{NullLiteral{}},TreeLiteral{UndefinedLiteral{}},
                           TreeLiteral{BooleanLiteral{false}},TreeLiteral{BooleanLiteral{true}}}) {
      auto candidate=bindings;candidate.primitives.push_back(literal);
      for(const auto& operation:original.operations)if(operation.control&&operation.control->test) {
        const auto captured=operation.control->expression;
        candidate.transports[captured.value]=ExpressionTransport{captured,PrimitiveId{0}};
      }
      const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{candidate,equations,order,&original}));
      const auto expected=PrimitiveTruthDomains(literal,tree.texts);
      for(unsigned i=0;i<original.phases.size();++i) {
        const auto& phase=original.phases[i];if(!phase.operation)continue;
        const auto& operation=original.operations[phase.operation->value];if(!operation.control||!operation.control->test)continue;
        const auto current=FindOriginalControlPhase(trial.flow,{i},phase.phase,*phase.operation);Check(current.has_value());
        const auto domains=OriginalControlDomains(trial.flow,*phase.operation);Check(domains.has_value());
        Check(domains->evaluated==(expected&current->test->evaluated));
        Check(domains->true_values==(expected&current->test->true_values));
      }
    }
    if(form==Form::kDirect) {
      // Withdraw one premise at a time. These negative proof inputs are not
      // new decoded programs; they ensure raw effects cannot become inert by
      // virtue of sharing a pure selection's source anchor.
      for(unsigned premise=0;premise<4;++premise) {
        auto missing=original;std::optional<OpId> changed;
        for(unsigned i=0;i<missing.operations.size();++i)if(missing.operations[i].control&&missing.operations[i].control->test) {
          auto& operation=missing.operations[i];changed=OpId{i};
          switch(premise) {
            case 0:operation.control->test.reset();break;
            case 1:operation.normal=LanguageEffect::kUserCode;break;
            case 2:operation.before_throw=LanguageEffect::kHeapWrite;break;
            case 3:operation.exceptional=ExceptionalCompletion::kMayThrow;break;
          }
          break;
        }
        Check(changed.has_value());
        const auto withdrawn=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,{bindings,equations,order,&missing}));
        Check(!FindOriginalControlPhase(withdrawn.flow,missing.operations[changed->value].block,ExceptionPhase::kBefore,*changed));
      }
    }
  }
  work=model.flow.work;bytes=model.flow.allocated_bytes;
  std::ostringstream source;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,source,{}, {},nullptr,&original)));
  return source.str();
}
}
int main(int argc,char** argv) {
  const bool phased_only=argc==2&&std::string_view(argv[1])=="--phased";
  if(!phased_only)
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1u, 200u, 201u, 402u}) for (unsigned form = 0; form <= unsigned(Form::kReversedStatements); ++form) {
      std::uint64_t work = 0;
      const auto source = Source(count, Form(form), profile, work);
      std::cout << nlohmann::json{{"profile", unsigned(profile)}, {"count", count}, {"form", form},
          {"work", work}, {"source", source ? nlohmann::json(*source) : nlohmann::json(nullptr)}} << '\n';
    }
  for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(const auto count:{1u,200u,201u,402u})
      for(const auto form:{Form::kDirect,Form::kReversedOperands,Form::kNegated,Form::kCopiedSplit,Form::kCallProducer}) {
        if(count!=1&&form!=Form::kDirect)continue;
        std::uint64_t work=0;std::size_t bytes=0;const auto source=PhasedControlSource(count,form,profile,work,bytes);
        std::cout<<nlohmann::json{{"phased",true},{"profile",unsigned(profile)},{"count",count},{"form",unsigned(form)},
            {"work",work},{"bytes",bytes},{"source",source}}<<'\n';
      }
}
