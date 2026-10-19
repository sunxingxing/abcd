#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/literal_arm_view.h"
#include "../../../src/source/literal_reference_view.h"
#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/phase_composition.h"
#include "abcd/analysis/arithmetic_semantics.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
// One outer private literal with N ordered definitions, a selected pair of
// independently proved nested literal forests, then the member invocation.
ValueProgram Graph(unsigned count, bool split = false, bool escape = false,
                   bool rename = false, bool retained = false,
                   bool producer_outside_try = false) {
  std::vector<std::uint8_t> code;
  std::uint32_t try_begin = 0;
  const auto local = [&](std::uint8_t value) {
    constexpr std::uint8_t permutation[] = {2, 0, 3, 1};
    return rename && value < std::size(permutation) ? permutation[value]
                                                    : value;
  };
  const auto emit = [&](abc::Opcode op) {
    const auto n = static_cast<std::uint16_t>(op);
    code.push_back(static_cast<std::uint8_t>(n));
    if (n > UINT8_MAX) code.push_back(static_cast<std::uint8_t>(n >> 8));
  };
  const auto load = [&](std::uint8_t r) {
    emit(abc::Opcode::kLDA_V8);
    code.push_back(local(r));
  };
  const auto save = [&](std::uint8_t r) {
    emit(abc::Opcode::kSTA_V8);
    code.push_back(local(r));
  };
  const auto object = [&](std::uint8_t r) {
    emit(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16);
    code.insert(code.end(), {0, 0, 0});
    save(r);
  };
  const auto field = [&](std::uint8_t r) {
    emit(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
    code.insert(code.end(), {0, 0, 0, local(r)});
  };
  const auto constant = [&] {
    emit(abc::Opcode::kLDAI_IMM32);
    code.insert(code.end(), {1, 0, 0, 0});
  };
  const auto jump = [&](abc::Opcode op) {
    const auto at = code.size();
    emit(op);
    code.insert(code.end(), 4, 0);
    return at;
  };
  const auto bind = [&](std::size_t at) {
    const auto delta = static_cast<std::uint32_t>(code.size() - at);
    for (unsigned j = 0; j < 4; ++j)
      code[at + 1 + j] = static_cast<std::uint8_t>(delta >> (8 * j));
  };
  {
    if (retained) {
      load(9);
      emit(abc::Opcode::kCALLARG0_IMM8);
      code.push_back(0);
      save(8);
      if (producer_outside_try)
        try_begin = static_cast<std::uint32_t>(code.size());
    }
    load(7);
    emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    code.insert(code.end(), {0, 0, 0});
    save(0);
    object(1);
    for (unsigned n = 0; n < count; ++n) {
      constant();
      field(1);
    }
    load(8);
    emit(abc::Opcode::kISTRUE);
    const auto branch = jump(abc::Opcode::kJEQZ_IMM32);
    const auto arm = [&] {
      object(2);
      emit(abc::Opcode::kCREATEEMPTYARRAY_IMM8);
      code.push_back(0);
      field(2);
      if (split) {
        emit(abc::Opcode::kMOV_V8_V8);
        code.insert(code.end(), {local(2), local(2)});
        emit(abc::Opcode::kJMP_IMM8);
        code.push_back(2);
      }
      if (escape) {
        load(9);
        emit(abc::Opcode::kCALLARG1_IMM8_V8);
        code.insert(code.end(), {0, local(2)});
      }
      load(2);
    };
    arm();
    const auto join = jump(abc::Opcode::kJMP_IMM32);
    bind(branch);
    arm();
    bind(join);
    field(1);
    constant();
    field(1);
    load(1);
    save(3);
    load(0);
    emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);
    code.insert(code.end(), {0, 7, local(3)});
  }
  emit(abc::Opcode::kRETURNUNDEFINED);
  const auto handler_pc = static_cast<std::uint32_t>(code.size());
  emit(abc::Opcode::kRETURN);
  auto body = test::Body(code);
  abc::TryRegion region;
  region.offset = {try_begin};
  region.length = handler_pc - try_begin;
  region.handler_count = 1;
  abc::CatchHandler handler;
  handler.offset = {handler_pc};
  handler.length = 1;
  body.tries.push_back(region);
  body.handlers.push_back(handler);
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(4, 7, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(
      BuildExceptionFlow(body, abc::ProfileId::kDynamic1206, frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(
      ExceptionSsa::Build(flow, frame.registers, ssa_work));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& v = ssa.Values()[i];
    const auto* r = std::get_if<abc::RegisterId>(&v.slot);
    if (r && r->value >= frame.locals &&
        std::holds_alternative<MethodEntry>(v.origin))
      entries.push_back(
          {{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  const auto env =
      std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow, ssa, entries, value_work, {}, PendingExceptionValues::kLanguage, {},
      {}, CallResultValues::kLanguage, FrameReceiver::kUnknown,
      ArithmeticSemantics::kSourceNumeric));
  const auto proof_result = VerifyExceptionInputs(flow, ssa, env, values);
  test::Check(std::holds_alternative<ExceptionInputProof>(proof_result));
  const auto proof = std::get<ExceptionInputProof>(proof_result);
  return std::get<ValueProgram>(
      ImportPhasedValues({flow, ssa, env, values, proof, frame}));
}
std::uint64_t Check(unsigned count, bool split, bool rename = false,
                    bool retained = false, bool producer_outside_try = false) {
  const auto graph =
      Graph(count, split, false, rename, retained, producer_outside_try);
  const auto built = BuildPhasedEvaluationPlan(graph);
  if (const auto* e = std::get_if<EvaluationFailure>(&built))
    std::cerr << "build failure count=" << count << " kind=" << int(e->kind)
              << " cause=" << e->evaluation.plan_cause.index() << " check="
              << (e->evaluation.input_check ? int(*e->evaluation.input_check)
                                            : -1)
              << " obligation=" << int(e->evaluation.obligation) << " op="
              << (e->evaluation.location.operation
                      ? int(e->evaluation.location.operation->value)
                      : -1)
              << '\n';
  test::Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& plan = std::get<EvaluationPlan>(built);
  const auto calls = BuildPhasedCallPlan(graph, plan);
  test::Check(std::holds_alternative<CallPlan>(calls));
  test::Check(plan.phase_compositions.size() == 1);
  TreeCheckBudget budget({});
  const auto arms = IndexNestedLiteralInputs(graph, plan, budget);
  test::Check(arms == plan.literal_arm_inputs);
  for (bool bytes : {false, true}) {
    auto limits = SemanticBudget{budget.Bytes(), budget.Work()};
    TreeCheckBudget exact(limits);
    test::Check(IndexNestedLiteralInputs(graph, plan, exact) == arms);
    if (bytes)
      --limits.bytes;
    else
      --limits.work;
    bool refused = false;
    try {
      TreeCheckBudget limited(limits);
      static_cast<void>(IndexNestedLiteralInputs(graph, plan, limited));
    } catch (TreeStructureError e) {
      refused = e == TreeStructureError::kResourceLimit ||
                e == TreeStructureError::kWorkLimit;
    } catch (EvaluationError e) {
      refused = e == EvaluationError::kResourceLimit ||
                e == EvaluationError::kWorkLimit;
    }
    test::Check(refused);
  }
  TreeCheckBudget references({});
  std::vector<std::optional<OpId>> members;
  const auto views = IndexLiteralReferences(graph, plan, references, &members);
  std::size_t found = 0;
  for (const auto& v : views)
    if (v) {
      ++found;
      test::Check(v->events >= 4 && v->may_throw);
    }
  test::Check(found == 1);
  for (bool bytes : {false, true}) {
    auto limits = SemanticBudget{references.Bytes(), references.Work()};
    std::vector<std::optional<OpId>> repeated;
    TreeCheckBudget exact(limits);
    test::Check(IndexLiteralReferences(graph, plan, exact, &repeated) == views);
    test::Check(repeated == members);
    if (bytes)
      --limits.bytes;
    else
      --limits.work;
    bool refused = false;
    try {
      TreeCheckBudget limited(limits);
      static_cast<void>(
          IndexLiteralReferences(graph, plan, limited, &repeated));
    } catch (TreeStructureError e) {
      refused = e == TreeStructureError::kResourceLimit ||
                e == TreeStructureError::kWorkLimit;
    } catch (EvaluationError e) {
      refused = e == EvaluationError::kResourceLimit ||
                e == EvaluationError::kWorkLimit;
    }
    test::Check(refused);
  }
  std::uint64_t selection_work = 0;
  if (retained) {
    // The preceding call remains a real root, outside the selected forest.
    test::Check(std::holds_alternative<ValueInvoke>(graph.ops[1].operation));
    test::Check(plan.operations[1].kind == EvaluationKind::kRoot &&
                plan.operations[1].temporary);
    test::Check(!members[1] && !plan.phase_prefixes[1]);
    TreeCheckBudget selection({});
    VerifyPhaseSelectionOperands(graph, plan, selection);
    selection_work = selection.Work();
    for (bool bytes : {false, true}) {
      auto limits = SemanticBudget{selection.Bytes(), selection.Work()};
      TreeCheckBudget exact(limits);
      VerifyPhaseSelectionOperands(graph, plan, exact);
      if (bytes)
        --limits.bytes;
      else
        --limits.work;
      bool refused = false;
      try {
        TreeCheckBudget limited(limits);
        VerifyPhaseSelectionOperands(graph, plan, limited);
      } catch (TreeStructureError e) {
        refused = e == TreeStructureError::kResourceLimit ||
                  e == TreeStructureError::kWorkLimit;
      } catch (EvaluationError e) {
        refused = e == EvaluationError::kResourceLimit ||
                  e == EvaluationError::kWorkLimit;
      }
      test::Check(refused);
    }
  }
  std::cout << count << " definitions: arm bytes=" << budget.Bytes()
            << " work=" << budget.Work()
            << " reference bytes=" << references.Bytes()
            << " work=" << references.Work() << '\n';
  if (count != 1) return budget.Work() + references.Work() + selection_work;
  const auto rejects = [&](const EvaluationPlan& wrong) {
    test::Check(std::holds_alternative<EvaluationFailure>(
        VerifyPhasedEvaluationPlan(graph, wrong)));
  };
  auto wrong = plan;
  if (retained) {
    wrong.operations[1].temporary.reset();
    rejects(wrong);
    wrong = plan;
    wrong.phase_prefixes[1] = plan.phase_selections.front().branch;
    wrong.phase_selections.front().prefix.push_back(OpId{1});
    rejects(wrong);
    wrong = plan;
    wrong.operations[1] = {
        EvaluationKind::kInline,
        graph.ops[plan.phase_selections.front().branch.value].first_use,
        {}};
    rejects(wrong);
  }
  wrong = plan;
  wrong.literal_arm_inputs.clear();
  rejects(wrong);
  for (std::size_t i = 0; i < arms.size(); ++i)
    if (arms[i]) {
      wrong = plan;
      ++wrong.literal_arm_inputs[i]->value;
      rejects(wrong);
      break;
    }
  wrong = plan;
  const auto& outer = wrong.literals->objects.front();
  std::swap(wrong.literals->definitions[outer.first],
            wrong.literals->definitions[outer.first + outer.count - 1]);
  rejects(wrong);
  wrong = plan;
  wrong.phase_compositions.clear();
  rejects(wrong);
  wrong = plan;
  wrong.phase_compositions.push_back(wrong.phase_compositions.front());
  rejects(wrong);
  wrong = plan;
  wrong.phase_selections.front().execution =
      wrong.phase_selections.front().branch;
  rejects(wrong);
  for (std::size_t i = 0; i < views.size(); ++i)
    if (views[i]) {
      wrong = plan;
      wrong.operations[i].consumer = graph.ops[views[i]->call.value].first_use;
      rejects(wrong);
      wrong = plan;
      wrong.logical_objects[i] = OpId{static_cast<std::uint32_t>(i)};
      rejects(wrong);
      auto changed = graph;
      for (auto& exit : changed.exceptional_exits)
        if (exit.op == views[i]->last) exit.handler.reset();
      test::Check(std::holds_alternative<EvaluationFailure>(
          VerifyPhasedEvaluationPlan(changed, plan)));
      break;
    }
  return budget.Work() + references.Work() + selection_work;
}
}  // namespace
int main(int argc, char**) {
  if (argc > 1) {
    Check(1, false, false, true);
    Check(1, true, true, true);
    Check(1, false, false, true, true);
    std::uint64_t previous = 0;
    for (const auto count : {200U, 201U, 400U, 800U}) {
      const auto work = Check(count, false, false, true);
      if (previous) test::Check(work < 3 * previous);
      previous = work;
    }
    return 0;
  }
  Check(1, false);
  Check(1, true);
  Check(1, true, true);
  std::uint64_t previous = 0;
  for (const auto count : {200U, 201U, 400U, 800U}) {
    const auto work = Check(count, false);
    if (previous) test::Check(work < 3 * previous);
    previous = work;
  }
  const auto graph = Graph(1, false, true);
  const auto rejected = BuildPhasedEvaluationPlan(graph);
  if (const auto* failure = std::get_if<EvaluationFailure>(&rejected))
    test::Check(failure->kind != EvaluationError::kResourceLimit &&
                failure->kind != EvaluationError::kWorkLimit);
  if (const auto* plan = std::get_if<EvaluationPlan>(&rejected))
    test::Check(std::holds_alternative<CallPlanFailure>(
        BuildPhasedCallPlan(graph, *plan)));
}
