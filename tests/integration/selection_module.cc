#include "../../src/source/function_values.h"
#include "../../src/analysis/phased_value_import.h"
#include "../../src/source/exception_tree.h"
#include "../../src/source/module_guards.h"
#include "abcd/analysis/exception_modules.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/source/call_plan.h"
#include "../../src/source/phase_evaluation.h"
#include "../../src/source/instruction_sources.h"
#include "analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr unsigned kByteBits = 8;
enum class CallPrefix { kNone, kGetter, kCall, kNullish, kWrappedNullish, kEscapingNullish, kMemberWrappedNullish, kMemberBarrierNullish, kRetainedInput, kRetainedGetter, kRetainedBarrier, kRetainedPrefix, kRetainedLiteralArgs };
enum class Selection { kConditional, kAnd, kOr, kDecisionAnd, kDecisionOr };
enum class TruthInput { kBoolean, kNegated, kVmPredicate, kObserved, kRepeated };
bool Decision(Selection selection) {
  return selection == Selection::kDecisionAnd || selection == Selection::kDecisionOr;
}
abc::MethodBody DecisionBody(unsigned count, bool direct, Selection selection, bool caught = false, bool one_arm = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  const bool conjunction = selection == Selection::kDecisionAnd;
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    // Two predicates share one statement arm. The module read/guard belongs
    // only to the conditional RHS, even when either arm returns no value.
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0, op(abc::Opcode::kISTRUE)});
    const auto first = bytes.size();
    bytes.insert(bytes.end(), {op(conjunction ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8), 0,
        op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0, op(kGuard),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits), 3, 0});
    if (!direct) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    if (!one_arm) bytes.push_back(op(abc::Opcode::kISTRUE));
    const auto second = bytes.size();
    if (!one_arm) bytes.insert(bytes.end(), {op(conjunction ? abc::Opcode::kJEQZ_IMM8 : abc::Opcode::kJNEZ_IMM8), 0});
    const auto arm = [&](std::uint8_t name) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, name, 0,
          op(abc::Opcode::kCALLTHIS0_IMM8_V8), 0, 3});
    };
    arm(conjunction ? 0 : 3);
    if (one_arm) {
      bytes[first + 1] = static_cast<std::uint8_t>(bytes.size() - first);
      continue;
    }
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[first + 1] = static_cast<std::uint8_t>(bytes.size() - first);
    bytes[second + 1] = static_cast<std::uint8_t>(bytes.size() - second);
    arm(conjunction ? 3 : 0);
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
  }
  bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {0}; region.length = handler_pc; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  return body;
}
abc::MethodBody Body(unsigned count, bool direct, bool barrier, Selection selection, bool caught = false,
    unsigned truth_depth = 1, TruthInput truth = TruthInput::kBoolean) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    // Retrieve the member before selecting its argument. One arm owns the
    // original read/guard pair; the other keeps the left value or a constant.
    if (truth != TruthInput::kObserved) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0,
        op(abc::Opcode::kSTA_V8), 2});
    if (selection == Selection::kConditional)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_STR_ID16), 0, 0,
          op(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, 2});
    else if (truth != TruthInput::kRepeated) {
      if (truth != TruthInput::kVmPredicate)
        for (unsigned test = 0; test < truth_depth; ++test) bytes.push_back(op(abc::Opcode::kISTRUE));
      if (truth == TruthInput::kNegated) bytes.push_back(op(abc::Opcode::kISFALSE));
      if (truth == TruthInput::kObserved) bytes.insert(bytes.end(), {
          op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 3, 0, 3,
          op(abc::Opcode::kLDA_V8), 3, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0,
          op(abc::Opcode::kLDA_V8), 1});
    }
    const auto branch = bytes.size();
    std::vector<std::size_t> repeated;
    if (truth == TruthInput::kRepeated) {
      for (unsigned test = 0; test < truth_depth; ++test) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kISTRUE)});
        repeated.push_back(bytes.size());
        bytes.insert(bytes.end(), {op(selection == Selection::kOr ? abc::Opcode::kJNEZ_IMM32 : abc::Opcode::kJEQZ_IMM32), 0, 0, 0, 0});
      }
    } else bytes.insert(bytes.end(), {op((selection == Selection::kOr) != (truth == TruthInput::kNegated) ? abc::Opcode::kJNEZ_IMM8 :
        abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0,
        op(kGuard), static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits), 3, 0});
    if (!direct) bytes.insert(bytes.end(), {op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    if (barrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    if (truth == TruthInput::kRepeated) {
      for (const auto test : repeated) {
        const auto displacement = static_cast<std::uint32_t>(bytes.size() - test);
        for (unsigned byte = 0; byte < sizeof(displacement); ++byte)
          bytes[test + 1 + byte] = static_cast<std::uint8_t>(displacement >> (byte * kByteBits));
      }
    } else bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    if (selection == Selection::kConditional)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 0xc0, 1, 0, 0}); // 448, the independent fallback.
    else bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 0,
        op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, 3, 1});
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto handler_pc = static_cast<std::uint32_t>(bytes.size());
  if (caught) bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  if (caught) {
    abc::TryRegion region; region.offset = {0}; region.length = handler_pc; region.handler_count = 1;
    abc::CatchHandler handler; handler.offset = {handler_pc}; handler.length = 1;
    body.tries.push_back(region); body.handlers.push_back(handler);
  }
  return body;
}
void Run(unsigned count, bool direct, bool print, bool barrier = false,
    Selection selection = Selection::kConditional, abc::ProfileId profile = abc::ProfileId::kDynamic1206) {
  const auto body = Decision(selection) ? DecisionBody(count, direct, selection) : Body(count, direct, barrier, selection);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(3, 1, {}));
  RouteInputIdentity identity; identity.profile = profile;
  const auto input = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body, frame,
      InputCaptureMode::kClosed));
  const auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  const auto& graph = values.graph;
  std::vector<ModuleEvaluation> modules;
  for (const auto& pair : values.modules.pairs)
    modules.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  test::Check(modules.size() == count);
  const auto planned = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, modules);
  test::Check(std::holds_alternative<EvaluationPlan>(planned));
  const auto& evaluation = std::get<EvaluationPlan>(planned);
  const auto calls = BuildCallPlan(graph, evaluation);
  if (barrier) { test::Check(std::get<CallPlanFailure>(calls) == CallPlanError::kReference); return; }
  test::Check(std::holds_alternative<CallPlan>(calls));
  unsigned logical = 0;
  for (const auto& placement : evaluation.operations) logical += placement.kind == EvaluationKind::kLogicalValue;
  test::Check(logical == (Decision(selection) ? 0 : count));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  if (Decision(selection)) {
    unsigned decisions = 0;
    for (const auto& node : control.nodes) decisions += node.decision.has_value();
    test::Check(decisions == count);
  }
  FunctionSyntax syntax{};
  syntax.identity = identity; syntax.method = input.Method(); syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"other", u"imported"}; syntax.texts = {u"on"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  syntax.modules = {{{{7}, {0}}, NameId{5}, ModuleAccess::kReadOnly, NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  }
  const ExpressionConstants resolved{constants, 6, 1};
  const auto built = BuildFunctionTree(graph, control, evaluation, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, resolved)));
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, tree));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  for (const auto& module : modules) {
    const auto read = module.pair.read.value, guard = module.pair.guard->value;
    test::Check(flow.expressions[read] && flow.expressions[read] == flow.expressions[guard]);
  }
  if (count <= 2) {
    auto forged = evaluation; forged.modules.front().pair.guard.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[modules.front().pair.guard->value].kind = EvaluationKind::kRemoved;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
    if (Decision(selection)) {
      const auto guard = *modules.front().pair.guard;
      std::optional<ExprId> other;
      for (const auto& placement : tree.operations)
        if (const auto* expression = std::get_if<ExprId>(&placement.node);
            expression && *expression != *flow.expressions[guard.value]) { other = *expression; break; }
      test::Check(other.has_value());
      auto wrong = tree;
      for (auto& placement : wrong.operations) if (placement.operation == guard) placement.node = *other;
      test::Check(std::holds_alternative<TreeFlowError>(VerifyTreeFlow(graph, control, evaluation, wrong)));
    }
  }
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
}

abc::MethodBody CallSelectionBody(unsigned count, bool computed, CallPrefix prefix, unsigned guards, unsigned depth = 1) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  const bool member_wrapped = prefix == CallPrefix::kMemberBarrierNullish || prefix == CallPrefix::kMemberWrappedNullish;
  test::Check(!member_wrapped || depth <= UINT8_MAX - 5);
  const std::uint8_t value = member_wrapped ? 3 + depth : 3, flag = value + 1, wrap = value + 2;
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    if (prefix == CallPrefix::kRetainedGetter) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_V8),value,op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,2,0,op(abc::Opcode::kSTA_V8),2});
    if (member_wrapped) for (unsigned level = 0; level < depth; ++level) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_V8), wrap, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0,
         op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(3 + level)});
    if (prefix == CallPrefix::kMemberBarrierNullish) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_V8), value, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), value,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 0, 0, op(abc::Opcode::kSTA_V8), 0});
    if (prefix == CallPrefix::kRetainedLiteralArgs) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_STR_ID16),0,0,op(abc::Opcode::kSTA_V8),2});
    if (prefix == CallPrefix::kRetainedPrefix) bytes.insert(bytes.end(),
        {op(abc::Opcode::kLDA_V8),value,op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,2,0,op(abc::Opcode::kSTA_V8),2});
    if (prefix == CallPrefix::kRetainedGetter || prefix == CallPrefix::kRetainedPrefix) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),2});
    else if (prefix == CallPrefix::kNone || prefix == CallPrefix::kRetainedInput || prefix == CallPrefix::kRetainedBarrier || prefix == CallPrefix::kRetainedLiteralArgs) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), flag});
    else {
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), value,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 1, 0});
      if (prefix == CallPrefix::kCall || prefix == CallPrefix::kNullish || prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kMemberBarrierNullish || prefix == CallPrefix::kMemberWrappedNullish || prefix == CallPrefix::kEscapingNullish) {
        bytes.insert(bytes.end(), {op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, value, flag, op(abc::Opcode::kSTA_V8), 2});
        if (prefix == CallPrefix::kCall) bytes.insert(bytes.end(), {
            op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 3, 0});
      }
    }
    if (prefix == CallPrefix::kNullish || prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kMemberBarrierNullish || prefix == CallPrefix::kMemberWrappedNullish || prefix == CallPrefix::kEscapingNullish) {
      std::vector<std::size_t> branches;
      for (unsigned guard = 0; guard < guards; ++guard) {
        bytes.insert(bytes.end(), {op(guard % 2 ? abc::Opcode::kLDUNDEFINED : abc::Opcode::kLDNULL),
            op(abc::Opcode::kSTRICTNOTEQ_IMM8_V8), 0, 2});
        branches.push_back(bytes.size()); bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM16), 0, 0});
      }
      const auto read = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
      for (const auto branch : branches) {
        const auto distance = bytes.size() - branch;
        test::Check(distance <= INT16_MAX);
        bytes[branch + 1] = static_cast<std::uint8_t>(distance);
        bytes[branch + 2] = static_cast<std::uint8_t>(distance >> kByteBits);
      }
      bytes.push_back(op(abc::Opcode::kLDUNDEFINED));
      const auto skip = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
      bytes[read + 1] = static_cast<std::uint8_t>(bytes.size() - read);
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 2, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
      bytes[skip + 1] = static_cast<std::uint8_t>(bytes.size() - skip);
    } else {
    bytes.push_back(op(abc::Opcode::kISTRUE));
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJEQZ_IMM8), 0});
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {op(abc::Opcode::kJMP_IMM8), 0});
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    if (prefix == CallPrefix::kRetainedBarrier) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    if (prefix == CallPrefix::kRetainedInput || prefix == CallPrefix::kRetainedBarrier || prefix == CallPrefix::kRetainedGetter || prefix == CallPrefix::kRetainedPrefix || prefix == CallPrefix::kRetainedLiteralArgs)
      bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(prefix == CallPrefix::kRetainedGetter || prefix == CallPrefix::kRetainedPrefix ? 2 : flag)});
    else if (computed) bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8),
        static_cast<std::uint8_t>(prefix == CallPrefix::kCall ? 2 : value),
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kLDAI_IMM32), 0xc0, 1, 0, 0});
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    }
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 0});
    if (prefix == CallPrefix::kRetainedLiteralArgs)
      bytes.insert(bytes.end(), {op(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8),0,value,2,1});
    else bytes.insert(bytes.end(), {op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, value, 1});
    if (member_wrapped) {
      for (unsigned level = depth; level-- > 0;) bytes.insert(bytes.end(),
          {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(3 + level),
           op(abc::Opcode::kCALLTHIS2_IMM8_V8_V8_V8), 0, wrap, flag, 1});
    } else if (prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kEscapingNullish) {
      bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 1, op(abc::Opcode::kLDA_V8), 5});
      if (prefix == CallPrefix::kEscapingNullish) bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, 2, 1});
      else bytes.insert(bytes.end(), {op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1});
    }
  }
  bytes.push_back(op(abc::Opcode::kRETURN));
  const auto end = static_cast<std::uint32_t>(bytes.size());
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body = test::Body(bytes);
  abc::TryRegion region; region.offset = {0}; region.length = end; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {end}; handler.length = 1;
  body.tries.push_back(region); body.handlers.push_back(handler);
  return body;
}

std::uint64_t RunPhased(unsigned count, bool direct, bool print, Selection selection,
    abc::ProfileId profile = abc::ProfileId::kDynamic1206, bool one_arm = false, bool call_selection = false, bool computed = false, CallPrefix prefix = CallPrefix::kNone, unsigned guard_count = 2, unsigned depth = 1, bool effect_barrier = false,
    unsigned truth_depth = 1, TruthInput truth = TruthInput::kBoolean) {
  const auto body = call_selection ? CallSelectionBody(count, computed, prefix, guard_count, depth) : Decision(selection) ? DecisionBody(count, direct, selection, true, one_arm) :
      Body(count, direct, effect_barrier, selection, true, truth_depth, truth);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout((prefix == CallPrefix::kMemberWrappedNullish || prefix == CallPrefix::kMemberBarrierNullish) ? 3 + depth : 3, call_selection ? ((prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kEscapingNullish || prefix == CallPrefix::kMemberWrappedNullish || prefix == CallPrefix::kMemberBarrierNullish) ? 3 : 2) : 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& value = ssa.Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto modules = std::get<ExceptionModules>(ProveExceptionModules(flow, ssa));
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  // Use the same language-valued ordinary-call contract as the production
  // source route; receiver calls already carry their independent fixed domain.
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_work, {}, PendingExceptionValues::kLanguage, modules.loads, {}, NormalCallResults(kSourceContract)));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto guards = std::get<ModuleGuards>(BuildPhasedModuleGuards(input, graph));
  std::vector<ModuleEvaluation> bindings;
  for (const auto& pair : guards.pairs)
    bindings.push_back({pair, std::get<ValueModuleRead>(graph.ops[pair.read.value].operation), {{7}, {0}}});
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(
      graph, {}, IncomingCells::kReject, {}, bindings));
  if (effect_barrier) {
    test::Check(evaluation.phase_selections.empty());
    test::Check(std::get<CallPlanFailure>(BuildPhasedCallPlan(graph, evaluation)) == CallPlanError::kReference);
    return 0;
  }
  if (prefix == CallPrefix::kMemberBarrierNullish || prefix == CallPrefix::kRetainedBarrier) {
    test::Check(std::get<CallPlanFailure>(BuildPhasedCallPlan(graph, evaluation)) == CallPlanError::kReference);
    return 0;
  }
  if (prefix == CallPrefix::kEscapingNullish) {
    test::Check(evaluation.phase_selections.empty());
    test::Check(std::get<CallPlanFailure>(BuildPhasedCallPlan(graph, evaluation)) == CallPlanError::kReference);
    return 0;
  }
  if (!Decision(selection) && !call_selection) {
    test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, evaluation)));
    test::Check(evaluation.phase_selections.size() == count);
    if (count == 1) {
      auto missing = evaluation; missing.phase_selections.clear();
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, missing)));
    }
  }
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  unsigned decisions = 0;
  for (const auto& node : control.skeleton.nodes) decisions += node.decision.has_value();
  test::Check(decisions == (truth == TruthInput::kRepeated ? count * (truth_depth - 1) :
      (prefix == CallPrefix::kNullish || prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kMemberBarrierNullish || prefix == CallPrefix::kMemberWrappedNullish || prefix == CallPrefix::kEscapingNullish) ? count * (guard_count - 1) : one_arm || !Decision(selection) ? 0 : count));
  const ExceptionTreeInput context{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"value", u"method", u"probe", u"option", u"other", u"imported"};
  syntax.texts = {u"on", u"number"};
  syntax.parameters = {{NameId{0}, NameEvidence::kDebugRange}};
  if (call_selection) {
    syntax.names.push_back(u"flag");
    syntax.parameters.push_back({NameId{6}, NameEvidence::kDebugRange});
    if (prefix == CallPrefix::kWrappedNullish || prefix == CallPrefix::kMemberWrappedNullish) {
      syntax.names.push_back(u"wrap"); syntax.parameters.push_back({NameId{7}, NameEvidence::kDebugRange});
    }
    test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(graph, evaluation)));
    test::Check(evaluation.phase_selections.size() == count);
    auto forged = evaluation; forged.phase_selections.clear();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    if (count == 1) test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhaseExpressions(graph, forged, {})));
    forged = evaluation; forged.phase_transports.clear();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.phase_prefixes.clear();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.phase_references.clear();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    if (prefix == CallPrefix::kMemberWrappedNullish) {
      test::Check(evaluation.phase_selections.front().ancestors.size() == depth);
      forged = evaluation; forged.phase_selections.front().ancestors.clear();
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
      forged = evaluation; ++forged.phase_selections.front().ancestors.front().call.value;
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
    if (prefix != CallPrefix::kNone && prefix != CallPrefix::kRetainedInput && prefix != CallPrefix::kRetainedGetter && prefix != CallPrefix::kRetainedLiteralArgs) {
      test::Check(!evaluation.phase_selections.front().prefix.empty());
      forged = evaluation; forged.phase_selections.front().prefix.clear();
      test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    }
    forged = evaluation; forged.phase_selections.front().input = graph.ops[std::get<PhaseCallReference>(forged.phase_selections.front().owner).callee.value].first_use;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    forged = evaluation; forged.operations[std::get<PhaseCallReference>(forged.phase_selections.front().owner).callee.value].consumer = UseId{static_cast<std::uint32_t>(graph.uses.size())};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
  }
  syntax.modules = {{{{7}, {0}}, NameId{5}, ModuleAccess::kReadOnly, NameEvidence::kMetadata}};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (const auto* read = std::get_if<ValueNamedRead>(&graph.ops[i].operation)) constants[i] = NameId{read->name.value + 1};
    if (const auto* write = std::get_if<ValueNamedWrite>(&graph.ops[i].operation)) constants[i] = NameId{write->name.value + 1};
    if (std::holds_alternative<ValueString>(graph.ops[i].operation)) constants[i] = TextId{0};
  }
  ExpressionConstants resolved{constants, syntax.names.size(), syntax.texts.size()};
  resolved.number_type_text = TextId{1};
  const auto built = BuildExceptionFunctionDraft(graph, context, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  if (one_arm) {
    unsigned branches = 0;
    for (const auto& node : tree.statements) if (const auto* branch = std::get_if<IfStmt>(&node.statement)) {
      ++branches;
      test::Check(!branch->alternate);
      const auto& body = std::get<BlockStmt>(tree.statements[branch->consequent.value].statement);
      test::Check(body.child_count != 0);
    }
    test::Check(branches == count);
  }
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, context, evaluation, tree, resolved)));
  const auto tree_flow = std::get<TreeFlow>(VerifyExceptionTreeFlow(graph, context, evaluation, tree));
  if (!Decision(selection) && !call_selection && selection != Selection::kConditional) {
    for (const auto& call : evaluation.phase_selections) {
      const auto* logical = std::get_if<LogicalExpr>(&tree.expressions[tree_flow.expressions[call.branch.value]->value].expression);
      if (truth == TruthInput::kVmPredicate || truth == TruthInput::kObserved) {
        test::Check(!logical && std::holds_alternative<ConditionalExpr>(tree.expressions[tree_flow.expressions[call.branch.value]->value].expression));
        continue;
      }
      test::Check(logical && logical->operation == (selection == Selection::kAnd ? TreeLogical::kAnd : TreeLogical::kOr));
      if (count == 1) {
        auto wrong = tree;
        auto& node = std::get<LogicalExpr>(wrong.expressions[tree_flow.expressions[call.branch.value]->value].expression);
        node.operation = node.operation == TreeLogical::kAnd ? TreeLogical::kOr : TreeLogical::kAnd;
        test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, context, evaluation, wrong, resolved)));
        wrong = tree;
        auto& operands = std::get<LogicalExpr>(wrong.expressions[tree_flow.expressions[call.branch.value]->value].expression);
        std::swap(operands.left, operands.right);
        test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, context, evaluation, wrong, resolved)));
      }
    }
  }
  if (call_selection) {
    auto wrong = tree;
    const auto& call = evaluation.phase_selections.front();
    const auto expression = *tree_flow.expressions[call.branch.value];
    wrong.expressions[expression.value].origin = TreeOriginId{graph.ops[call.branch.value].origin.value};
    test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, context, evaluation, wrong, resolved)));
    if (count <= 2 && (prefix == CallPrefix::kRetainedInput || prefix == CallPrefix::kRetainedGetter ||
        prefix == CallPrefix::kRetainedPrefix || prefix == CallPrefix::kRetainedLiteralArgs)) {
      wrong = tree;
      auto& selected = wrong.expressions[expression.value].expression;
      if (auto* conditional = std::get_if<ConditionalExpr>(&selected))
        std::swap(conditional->alternate, conditional->consequent);
      else {
        auto* logical = std::get_if<LogicalExpr>(&selected); test::Check(logical != nullptr);
        std::swap(logical->left, logical->right);
      }
      // The independent original-edge operand checker must reject replacing
      // the retained branch value with the other arm's constant expression.
      test::Check(std::holds_alternative<TreeSemanticError>(VerifyExceptionTreeSemantics(graph, context, evaluation, wrong, resolved)));
    }
  }
  for (const auto& binding : bindings) {
    const auto read = binding.pair.read.value, guard = binding.pair.guard->value;
    test::Check(tree_flow.expressions[read] && tree_flow.expressions[read] == tree_flow.expressions[guard]);
  }
  if (count == 1 && !bindings.empty()) {
    auto forged = evaluation; forged.modules.front().pair.guard.reset();
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph, forged)));
    auto wrong = tree;
    const auto guard = *bindings.front().pair.guard;
    for (auto& placement : wrong.operations) if (placement.operation == guard) placement.node = AnchorId{0};
    test::Check(std::holds_alternative<TreeFlowError>(VerifyExceptionTreeFlow(graph, context, evaluation, wrong)));
  }
  if (print) {
    std::ostringstream output;
    test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
    std::cout << output.str();
  }
  return std::get<ExceptionFunctionDraft>(built).work;
}

}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 || argc == 3) {
    const std::string_view option{argv[1]};
    test::Check(argc == 2 || std::string_view{argv[2]} == "--twice");
    const auto count = argc == 3 ? 2U : 1U;
    if (option == "--phased-truth-chain-source" || option == "--phased-truth-repeated-source" || option == "--phased-truth-negated-source" ||
        option == "--phased-truth-vm-source" || option == "--phased-truth-observed-source")
      RunPhased(count, false, true, Selection::kAnd, abc::ProfileId::kDynamic1206, false, false, false,
          CallPrefix::kNone, 2, 1, false, 201,
          option == "--phased-truth-negated-source" ? TruthInput::kNegated :
          option == "--phased-truth-vm-source" ? TruthInput::kVmPredicate :
          option == "--phased-truth-observed-source" ? TruthInput::kObserved :
          option == "--phased-truth-repeated-source" ? TruthInput::kRepeated : TruthInput::kBoolean);
    else if (option == "--phased-retained-prefix-source" || option == "--phased-retained-literal-args-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, false,
          option == "--phased-retained-prefix-source" ? CallPrefix::kRetainedPrefix : CallPrefix::kRetainedLiteralArgs);
    else if (option == "--phased-retained-input-source" || option == "--phased-retained-getter-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, false,
          option == "--phased-retained-getter-source" ? CallPrefix::kRetainedGetter : CallPrefix::kRetainedInput);
    else if (option == "--phased-escaping-nullish-refusal")
      RunPhased(count, false, false, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, true, CallPrefix::kEscapingNullish);
    else if (option == "--phased-member-wrapped-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, true, CallPrefix::kMemberWrappedNullish);
    else if (option == "--phased-nullish-source" || option == "--phased-wrapped-nullish-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, true,
          option == "--phased-wrapped-nullish-source" ? CallPrefix::kWrappedNullish : CallPrefix::kNullish);
    else if (option == "--phased-prefix-getter-source" || option == "--phased-prefix-call-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true, true,
          option == "--phased-prefix-call-source" ? CallPrefix::kCall : CallPrefix::kGetter);
    else if (option == "--phased-call-selection-source" || option == "--phased-computed-call-selection-source")
      RunPhased(count, false, true, Selection::kConditional, abc::ProfileId::kDynamic1206, false, true,
          option == "--phased-computed-call-selection-source");
    else if (option == "--phased-module-selection" || option == "--phased-module-selection-source" ||
        option == "--phased-module-selection-direct-source" || option == "--phased-module-and-source" ||
        option == "--phased-module-and-direct-source" || option == "--phased-module-or-source" ||
        option == "--phased-module-or-direct-source")
      RunPhased(count, option.ends_with("-direct-source"), option != "--phased-module-selection",
          option == "--phased-module-and-source" || option == "--phased-module-and-direct-source" ? Selection::kAnd :
          option == "--phased-module-or-source" || option == "--phased-module-or-direct-source" ? Selection::kOr : Selection::kConditional);
    else if (option == "--phased-one-arm-source" || option == "--phased-one-arm-direct-source")
      RunPhased(count, option == "--phased-one-arm-direct-source", true, Selection::kDecisionAnd,
          abc::ProfileId::kDynamic1206, true);
    else if (option == "--phased-decision-and-source" || option == "--phased-decision-and-direct-source")
      RunPhased(count, option == "--phased-decision-and-direct-source", true, Selection::kDecisionAnd);
    else if (option == "--phased-decision-or-source" || option == "--phased-decision-or-direct-source")
      RunPhased(count, option == "--phased-decision-or-direct-source", true, Selection::kDecisionOr);
    else if (option == "--source" || option == "--direct-source")
      Run(count, option == "--direct-source", true);
    else if (option == "--and-source" || option == "--and-direct-source")
      Run(count, option == "--and-direct-source", true, false, Selection::kAnd);
    else if (option == "--or-source" || option == "--or-direct-source")
      Run(count, option == "--or-direct-source", true, false, Selection::kOr);
    else if (option == "--decision-and-source" || option == "--decision-and-direct-source")
      Run(count, option == "--decision-and-direct-source", true, false, Selection::kDecisionAnd);
    else if (option == "--decision-or-source" || option == "--decision-or-direct-source")
      Run(count, option == "--decision-or-direct-source", true, false, Selection::kDecisionOr);
    else test::Check(false);
    return 0;
  }
  std::vector<std::uint64_t> truth_work;
  for (const auto depth : {50U, 100U, 200U, 201U, 400U}) {
    const auto work = RunPhased(1, false, false, Selection::kAnd, abc::ProfileId::kDynamic1206,
        false, false, false, CallPrefix::kNone, 2, 1, false, depth, TruthInput::kRepeated);
    if (!truth_work.empty()) test::Check(work <= 3 * truth_work.back());
    truth_work.push_back(work);
  }
  std::cout << "Selection truth work:";
  for (const auto work : truth_work) std::cout << ' ' << work;
  std::cout << '\n';
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto depth : {1U, 2U, 200U, 201U})
      for (const auto truth : {TruthInput::kBoolean, TruthInput::kNegated, TruthInput::kVmPredicate, TruthInput::kObserved, TruthInput::kRepeated})
        for (const auto selection : {Selection::kAnd, Selection::kOr})
          RunPhased(1, false, false, selection, profile, false, false, false, CallPrefix::kNone, 2, 1, false, depth, truth);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U})
      for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
        for (const bool direct : {false, true})
          RunPhased(count, direct, false, selection, profile);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
      for (const bool direct : {false, true})
        RunPhased(1, direct, false, selection, profile, false, false, false, CallPrefix::kNone, 2, 1, true);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U})
      for (const auto prefix : {CallPrefix::kRetainedInput, CallPrefix::kRetainedGetter, CallPrefix::kRetainedBarrier, CallPrefix::kRetainedPrefix, CallPrefix::kRetainedLiteralArgs})
        RunPhased(count, false, false, Selection::kConditional, profile, false, true, false, prefix);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    RunPhased(1, false, false, Selection::kConditional, profile, false, true, true, CallPrefix::kMemberBarrierNullish);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto depth : {1U, 2U, 200U, 201U})
      RunPhased(1, false, false, Selection::kConditional, profile, false, true, true, CallPrefix::kMemberWrappedNullish, 2, depth);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto guards : {2U, 200U, 201U, 402U})
      RunPhased(1, false, false, Selection::kConditional, profile, false, true, true, CallPrefix::kNullish, guards);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U})
      for (const bool computed : {false, true}) {
        RunPhased(count, false, false, Selection::kConditional, profile, false, true, computed);
        if (computed) for (const auto prefix : {CallPrefix::kGetter, CallPrefix::kCall, CallPrefix::kNullish, CallPrefix::kWrappedNullish, CallPrefix::kMemberWrappedNullish})
          RunPhased(count, false, false, Selection::kConditional, profile, false, true, computed, prefix);
      }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U})
      for (const auto selection : {Selection::kDecisionAnd, Selection::kDecisionOr})
        for (const bool direct : {false, true}) {
          RunPhased(count, direct, false, selection, profile);
          if (selection == Selection::kDecisionAnd) RunPhased(count, direct, false, selection, profile, true);
        }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const auto count : {1U, 2U, 200U, 201U, 402U})
      for (const auto selection : {Selection::kDecisionAnd, Selection::kDecisionOr})
        for (const bool direct : {false, true}) Run(count, direct, false, false, selection, profile);
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U})
    for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
      for (const bool direct : {false, true}) Run(count, direct, false, false, selection);
  for (const auto selection : {Selection::kConditional, Selection::kAnd, Selection::kOr})
    for (const bool direct : {false, true}) Run(1, direct, false, true, selection);
}
