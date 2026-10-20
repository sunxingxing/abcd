#include "../../../src/source/constructor_event_view.h"

#include <array>

#include "../../../src/source/call_plan.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/phase_composition.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "analysis/ssa_fixture.h"
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using abcd::test::Check;
namespace {
ValueProgram Graph(unsigned count, bool copies = false, bool rename = false,
                   bool escape = false) {
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode o) {
    const auto n = static_cast<std::uint16_t>(o);
    code.push_back(n);
    if (n > UINT8_MAX) code.push_back(n >> 8);
  };
  const auto reg = [&](unsigned r) {
    constexpr std::array<unsigned, 4> p{3, 1, 2, 0};
    return static_cast<std::uint8_t>(rename && r < p.size() ? p[r] : r);
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
  for (unsigned n = 0; n < count; ++n) {
    load(6);
    op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);
    code.insert(code.end(), {0, 0, 0});
    save(0);
    load(7);
    op(abc::Opcode::kISTRUE);
    const auto branch = jump(abc::Opcode::kJEQZ_IMM32);
    const auto arm = [&](unsigned constructor) {
      load(constructor);
      save(1);
      op(abc::Opcode::kCREATEOBJECTWITHBUFFER_IMM8_ID16);
      code.insert(code.end(), {0, 0, 0});
      save(2);
      op(abc::Opcode::kCREATEEMPTYARRAY_IMM8);
      code.push_back(0);
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8);
      code.insert(code.end(), {0, 0, 0, reg(2)});
      if (escape) {
        load(6);
        op(abc::Opcode::kCALLARG1_IMM8_V8);
        code.insert(code.end(), {0, reg(2)});
      }
      if (copies) {
        op(abc::Opcode::kMOV_V8_V8);
        code.insert(code.end(), {reg(2), reg(2)});
      }
      op(abc::Opcode::kNEWOBJRANGE_IMM8_IMM8_V8);
      code.insert(code.end(), {0, 2, reg(1)});
    };
    // Keep the constructor/argument window contiguous under the permutation.
    arm(8);
    const auto join = jump(abc::Opcode::kJMP_IMM32);
    bind(branch);
    arm(9);
    bind(join);
    save(3);
    load(0);
    op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);
    code.insert(code.end(), {0, 6, reg(3)});
  }
  op(abc::Opcode::kRETURN);
  const auto body = test::Body(code);
  const auto frame = std::get<abc::FrameLayout>(
      abc::BuildFrameLayout(6, 4, {false, false, false, false}));
  const auto method = std::get<SemanticMethod>(
      BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 10));
  const auto proof = VerifySemanticSource(
      body, method, 6, 4, {}, CapturedValues::kReject, {},
      ClosureWrites::kUnknown, {}, CallResultValues::kLanguage);
  Check(std::holds_alternative<SemanticProof>(proof));
  auto graph = std::get<ValueProgram>(
      ImportNormalValues(body, method, std::get<SemanticProof>(proof), frame));
  Check(!std::holds_alternative<ValueSimplifyError>(
      SimplifyValues(graph, graph.revision)));
  return graph;
}
void Run(unsigned count, bool copies = false, bool rename = false) {
  const auto graph = Graph(count, copies, rename);
  const auto built = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationPlan>(built));
  const auto& plan = std::get<EvaluationPlan>(built);
  Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph, plan)));
  TreeCheckBudget budget({});
  const auto view = BuildConstructorEventView(graph, plan, budget);
  VerifyConstructorEventView(graph, plan, view, budget);
  std::size_t invocations = 0, literals = 0;
  for (const auto& event : view.events)
    if (event) {
      invocations += event->role == ConstructorEventRole::kInvocation;
      literals += event->role == ConstructorEventRole::kLiteral;
    }
  Check(invocations == 2 * count && literals >= 4 * count);
  const auto limits = SemanticBudget{budget.Bytes(), budget.Work()};
  for (const auto b : {limits, SemanticBudget{limits.bytes - 1, limits.work},
                       SemanticBudget{limits.bytes, limits.work - 1}}) {
    bool pass = false;
    try {
      TreeCheckBudget exact(b);
      const auto v = BuildConstructorEventView(graph, plan, exact);
      VerifyConstructorEventView(graph, plan, v, exact);
      pass = true;
    } catch (TreeStructureError e) {
      Check(e == TreeStructureError::kResourceLimit ||
            e == TreeStructureError::kWorkLimit);
    }
    Check(pass == (b.bytes == limits.bytes && b.work == limits.work));
  }
  std::cout << count << " constructors: " << limits.bytes << " bytes / "
            << limits.work << " work\n";
  if (count != 1) return;
  const auto first =
      std::find_if(view.events.begin(), view.events.end(), [](const auto& x) {
        return x && x->role == ConstructorEventRole::kInvocation;
      });
  Check(first != view.events.end());
  const auto id = first->value().operation;
  for (unsigned mutation = 0; mutation < 13; ++mutation) {
    auto wrong = view;
    switch (mutation) {
      case 0:
        wrong.events[id.value]->constructor.value++;
        break;
      case 1:
        wrong.events[id.value]->exception.reset();
        break;
      case 2:
        wrong.events[id.value]->role = ConstructorEventRole::kLiteral;
        break;
      case 3:
        wrong.events[id.value].reset();
        break;
      case 4:
        wrong.outputs[id.value].reset();
        break;
      case 5:
        wrong.events[id.value]->consumer.reset();
        break;
      case 6:
        wrong.events[id.value]->commit = WorkBlockId{0};
        break;
      case 7:
        wrong.outputs[id.value]->value++;
        break;
      case 8:
        wrong.events[id.value]->handler = ValueHandler{};
        break;
      case 9:
        wrong.inputs[graph.ops[id.value].first_use.value]->value = ValueId{0};
        break;
      case 10:
        wrong.inputs[graph.ops[id.value].first_use.value]->role =
            OperandRole::kArgument;
        break;
      case 11:
        wrong.events[id.value + 1] = wrong.events[id.value];
        break;
      case 12:
        std::swap(wrong.events[id.value], wrong.events[id.value - 1]);
        break;
    }
    bool refused = false;
    try {
      TreeCheckBudget check({});
      VerifyConstructorEventView(graph, plan, wrong, check);
    } catch (ConstructorEventError) {
      refused = true;
    }
    Check(refused);
  }
  const auto escaped = Graph(1, false, false, true);
  const auto e = BuildEvaluationPlan(escaped);
  Check(std::holds_alternative<EvaluationPlan>(e));
  // A private literal exposed before invocation cannot join the constructor's
  // owned forest; the independent LiteralPlan/call checks retain the refusal.
  Check(std::holds_alternative<CallPlanFailure>(
      BuildCallPlan(escaped, std::get<EvaluationPlan>(e))));
}
}  // namespace
int main() {
  Run(1);
  Run(1, true);
  Run(1, true, true);
  for (auto n : {200u, 201u, 400u, 800u}) Run(n);
}
