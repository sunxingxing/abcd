#include "../../../src/source/literal_field_forest.h"

#include <iostream>

#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/call_plan.h"
#include "abcd/analysis/exception_captures.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
ValueProgram Graph(unsigned count, bool copies = false, bool split = false,
                   bool escape = false, bool rename = false) {
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode o) {
    const auto n = static_cast<std::uint16_t>(o);
    code.push_back(n);
    if (n > UINT8_MAX) code.push_back(n >> 8);
  };
  const auto reg = [&](unsigned r) {
    return static_cast<std::uint8_t>(rename && r < 2 ? 1 - r : r);
  };
  const auto load = [&](unsigned r) {
    op(abc::Opcode::kLDA_V8);
    code.push_back(reg(r));
  };
  const auto save = [&](unsigned r) {
    op(abc::Opcode::kSTA_V8);
    code.push_back(reg(r));
  };
  const auto jump = [&](abc::Opcode o) {
    const auto at = code.size();
    op(o);
    code.insert(code.end(), 4, 0);
    return at;
  };
  const auto bind = [&](std::size_t at) {
    const auto delta = static_cast<std::uint32_t>(code.size() - at);
    for (unsigned j = 0; j < 4; ++j) code[at + 1 + j] = delta >> (8 * j);
  };
  load(9);
  op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
  code.insert(code.end(), {0, 0, 0});
  save(0);
  op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16);
  code.insert(code.end(), {0, 0, 0});
  save(1);
  for (unsigned i = 0; i < count; ++i) {
    load(11);
    op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    code.insert(code.end(), {0, 0, 0});
    save(2);
    for (unsigned arg = 0; arg < 2; ++arg) {
      load(10);
      op(abc::Opcode::kISTRUE);
      const auto no = jump(abc::Opcode::kJEQZ_IMM32);
      op(abc::Opcode::kLDAI_IMM32);
      code.insert(code.end(), {1, 0, 0, 0});
      const auto done = jump(abc::Opcode::kJMP_IMM32);
      bind(no);
      op(abc::Opcode::kLDAI_IMM32);
      code.insert(code.end(), {2, 0, 0, 0});
      bind(done);
      save(3 + arg);
    }
    op(abc::Opcode::kNEWOBJRANGE_IMM8_IMM8_V8);
    code.insert(code.end(), {0, 3, 2});
    if (copies) {
      save(5);
      load(5);
    }
    if (split) {
      op(abc::Opcode::kJMP_IMM8);
      code.push_back(2);
    }
    op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
    code.insert(code.end(), {0, 0, 0, reg(1)});
    if (escape) {
      load(11);
      op(abc::Opcode::kCALLARG1_IMM8_V8);
      code.insert(code.end(), {0, reg(1)});
    }
  }
  load(1);
  save(5);
  load(0);
  op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);
  code.insert(code.end(), {0, 9, 5});
  op(abc::Opcode::kRETURN);
  const auto handler = static_cast<std::uint32_t>(code.size());
  op(abc::Opcode::kRETURN);
  auto body = test::Body(code);
  abc::TryRegion region;
  region.offset = {0};
  region.length = handler;
  region.handler_count = 1;
  abc::CatchHandler caught;
  caught.offset = {handler};
  caught.length = 1;
  body.tries.push_back(region);
  body.handlers.push_back(caught);
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(6, 6, {true, true, false, true}));
  const auto flow = std::get<ExceptionFlow>(
      BuildExceptionFlow(body, abc::ProfileId::kDynamic1301, frame.registers));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa =
      std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  const auto env =
      std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto captures =
      std::get<ExceptionCaptures>(PlanExceptionCaptures(flow, env));
  std::vector<EntryValueFact> entries;
  for (std::size_t i = 0; i < ssa.Values().size(); ++i) {
    const auto& v = ssa.Values()[i];
    const auto* r = std::get_if<abc::RegisterId>(&v.slot);
    if (std::holds_alternative<MethodEntry>(v.origin) && r &&
        r->value >= frame.locals)
      entries.push_back(
          {{static_cast<std::uint32_t>(i)}, ValueKinds::Language()});
  }
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(
      flow, ssa, entries, value_work, {}, PendingExceptionValues::kLanguage,
      captures.loads, {}, CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(
      VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  return std::get<ValueProgram>(ImportPhasedValues(input));
}
auto Run(unsigned count, bool copies = false, bool split = false,
         bool rename = false) {
  const auto graph = Graph(count, copies, split, false, rename);
  // This intentionally large synthetic method includes all independent
  // planning/verifier passes in one cumulative allowance. The exact field
  // proof boundaries below are measured separately; production defaults stay
  // unchanged and refuse when that allowance is exhausted.
  const auto built = BuildPhasedEvaluationPlan(
      graph, {768U * 1024U * 1024U, kDefaultSemanticWork});
  if (const auto* e = std::get_if<EvaluationFailure>(&built)) {
    std::cerr << "plan error " << unsigned(e->kind) << " count" << count
              << '\n';
    std::abort();
  }
  const auto& plan = std::get<EvaluationPlan>(built);
  test::Check(plan.literal_fields.objects.size() == 1);
  const auto verify = [&](SemanticBudget limit = SemanticBudget{
                              768U * 1024U * 1024U, kDefaultSemanticWork}) {
    TreeCheckBudget budget(limit);
    VerifyLiteralFieldForests(graph, plan, budget);
    return std::pair{budget.Bytes(), budget.Work()};
  };
  const auto [bytes, work] = verify();
  test::Check(verify({bytes, work}) == std::pair{bytes, work});
  for (const auto limit :
       {SemanticBudget{bytes - 1, work}, SemanticBudget{bytes, work - 1}}) {
    bool refused = false;
    try {
      verify(limit);
    } catch (TreeStructureError e) {
      refused = e == (limit.bytes < bytes ? TreeStructureError::kResourceLimit
                                          : TreeStructureError::kWorkLimit);
    } catch (EvaluationError e) {
      refused = e == (limit.bytes < bytes ? EvaluationError::kResourceLimit
                                          : EvaluationError::kWorkLimit);
    }
    test::Check(refused);
  }
  const auto reject = [&](const auto& changed) {
    bool no = false;
    try {
      TreeCheckBudget b({768U * 1024U * 1024U, kDefaultSemanticWork});
      VerifyLiteralFieldForests(graph, changed, b);
    } catch (EvaluationError) {
      no = true;
    }
    test::Check(no);
  };
  auto changed = plan;
  changed.literal_fields.objects.front().allocation.value = UINT32_MAX;
  reject(changed);
  test::Check(
      std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(
          graph, changed, {768U * 1024U * 1024U, kDefaultSemanticWork})));
  changed = plan;
  changed.literal_fields.objects.front().argument.value++;
  reject(changed);
  changed = plan;
  changed.literal_fields.objects.front().callee.value++;
  reject(changed);
  changed = plan;
  test::Check(!changed.literal_fields.prefixes.empty());
  changed.literal_fields.prefixes.push_back(
      changed.literal_fields.prefixes.front());
  reject(changed);
  changed = plan;
  changed.literal_fields.prefixes.front().value++;
  reject(changed);
  if (plan.literal_fields.prefixes.size() > 1) {
    changed = plan;
    std::reverse(changed.literal_fields.prefixes.begin(),
                 changed.literal_fields.prefixes.end());
    reject(changed);
  }
  test::Check(std::holds_alternative<CallPlan>(BuildPhasedCallPlan(
      graph, plan, {768U * 1024U * 1024U, kDefaultSemanticWork})));
  if (count == 1) {
    changed = plan;
    const auto& object = changed.literals->objects.front();
    if (object.count > 1) {
      std::swap(changed.literals->definitions[object.first],
                changed.literals->definitions[object.first + 1]);
      reject(changed);
    }
    changed = plan;
    changed.literal_fields
        .members[changed.literal_fields.prefixes.front().value]
        .reset();
    reject(changed);
    auto wrong_catch = graph;
    bool changed_catch = false;
    for (auto& exit : wrong_catch.exceptional_exits)
      if (plan.literal_fields.members[exit.op.value] && exit.handler) {
        exit.handler.reset();
        changed_catch = true;
        break;
      }
    test::Check(changed_catch);
    bool caught = false;
    try {
      TreeCheckBudget check({768U * 1024U * 1024U, kDefaultSemanticWork});
      VerifyLiteralFieldForests(wrong_catch, plan, check);
    } catch (EvaluationError) {
      caught = true;
    }
    test::Check(caught);
  }
  std::cout << count << " bytes=" << bytes << " work=" << work << '\n';
  return std::pair{bytes, work};
}
}  // namespace
int main() {
  Run(1);
  Run(1, true);
  Run(1, false, true);
  Run(1, true, true, true);
  std::pair<std::size_t, std::uint64_t> previous{};
  for (unsigned n : {200U, 201U, 400U, 800U}) {
    const auto cost = Run(n);
    if (n > 200)
      test::Check(cost.first < 3 * previous.first &&
                  cost.second < 3 * previous.second);
    previous = cost;
  }
  const auto escaped = Graph(1, false, false, true);
  const auto plan = BuildPhasedEvaluationPlan(escaped);
  if (const auto* value = std::get_if<EvaluationPlan>(&plan))
    test::Check(value->literal_fields.objects.empty());
}
