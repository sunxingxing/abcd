#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/selection_consumer.h"
#include "../../../src/source/evaluation_failure_json.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::cerr << "logical_coverage_test:" << at.line() << '\n'; std::abort(); }
}
// A selection computed BEFORE a fresh buffered literal. Its first field owns
// the use, so a standalone logical domain cannot silently claim that input.
ValueProgram Graph(unsigned count = 1, bool literal = true, bool copy = false,
    bool split = false, std::uint8_t shift = 0, bool nested = false, bool chain = false) {
  const auto reg = [&](unsigned r) { return static_cast<std::uint8_t>(r + shift); };
  std::vector<std::uint8_t> bytes;
  for (unsigned i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,0,0});
    if (copy) bytes.insert(bytes.end(), {0x61,reg(2),0x60,reg(2)});
    bytes.insert(bytes.end(), {0xfb,0x13,0});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {0x4f,0});
    std::optional<std::size_t> inner;
    if (nested) {
      bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,1,0,0xfb,0x13,0});
      inner = bytes.size(); bytes.insert(bytes.end(), {0x4f,0});
    }
    bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,1,0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {0x4d,0});
    if (nested) {
      bytes[*inner+1] = static_cast<std::uint8_t>(bytes.size()-*inner);
      bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,2,0});
      bytes[jump+1] = static_cast<std::uint8_t>(bytes.size()-jump);
    }
    const auto outer_jump = nested ? bytes.size() : jump;
    if (nested) bytes.insert(bytes.end(), {0x4d,0});
    bytes[branch+1] = static_cast<std::uint8_t>(bytes.size()-branch);
    bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,2,0});
    bytes[outer_jump+1] = static_cast<std::uint8_t>(bytes.size()-outer_jump);
    bytes.insert(bytes.end(), {0x61,reg(1)});
    if (chain) {
      bytes.insert(bytes.end(), {0xfb,0x13,0});
      const auto second = bytes.size(); bytes.insert(bytes.end(), {0x4f,0});
      bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,1,0});
      const auto skip = bytes.size(); bytes.insert(bytes.end(), {0x4d,0});
      bytes[second+1] = static_cast<std::uint8_t>(bytes.size()-second);
      bytes.insert(bytes.end(), {0x60,reg(3),0x42,0,2,0});
      bytes[skip+1] = static_cast<std::uint8_t>(bytes.size()-skip);
      bytes.insert(bytes.end(), {0x61,reg(1)});
    }
    if (split) bytes.insert(bytes.end(), {0x4d,2});
    if (literal) {
      bytes.insert(bytes.end(), {0x07,0,0,0,0x61,reg(0),0x60,reg(1),
          static_cast<std::uint8_t>(abc::Opcode::kDEFINEPROPERTYBYNAME_IMM8_ID16_V8),0,0,0,reg(0),0x60,reg(0)});
    } else bytes.insert(bytes.end(), {0x60,reg(1)});
    // Keep every repetition observable, without introducing another selection.
    if (count > 1) bytes.insert(bytes.end(), {0x42,0,0,0});
  }
  bytes.push_back(0x64);
  const auto body = test::Body(bytes); const auto locals = 3U + shift;
  const auto semantic = BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,locals+1);
  Check(std::holds_alternative<SemanticMethod>(semantic));
  const auto proof = VerifySemanticSource(body,std::get<SemanticMethod>(semantic),locals,1);
  Check(std::holds_alternative<SemanticProof>(proof));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals,1,{false,false,false,false}));
  auto imported = ImportNormalValues(body,std::get<SemanticMethod>(semantic),std::get<SemanticProof>(proof),frame);
  Check(std::holds_alternative<ValueProgram>(imported)); auto graph = std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
EvaluationPlan Plan(const ValueProgram& graph) {
  auto built = BuildEvaluationPlan(graph); Check(std::holds_alternative<EvaluationPlan>(built));
  auto plan = std::get<EvaluationPlan>(std::move(built));
  Check(!std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,plan))); return plan;
}
std::vector<SelectionRegion> Regions(const ValueProgram& graph, const EvaluationPlan& plan) {
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph)); TreeCheckBudget budget({});
  return FindSelectionExpressions(graph,control,plan,budget);
}
void Refused(const ValueProgram& graph, const EvaluationPlan& plan, LogicalCoverageCheck check) {
  TreeCheckBudget budget({}); bool rejected = false;
  try { VerifyLogicalValues(graph,plan,budget); }
  catch (const EvaluationFailure& f) {
    rejected = true; Check(f.kind == EvaluationError::kOwnership);
    Check(f.evaluation.logical_coverage && f.evaluation.logical_coverage->check == check);
    Check(ParseEvaluationFailure(EvaluationFailureJson(f.evaluation)) == f.evaluation);
  }
  Check(rejected);
}
void Mutations() {
  auto graph = Graph(); const auto plan = Plan(graph); const auto regions = Regions(graph,plan);
  Check(regions.size() == 1); const auto& r = regions.front();
  Check(plan.logical_values.empty() || !plan.logical_values[r.merged.value]);
  TreeCheckBudget budget({}); const auto use = FindSelectionConsumer(graph,r,budget).InlineUse(); Check(use.has_value());
  Check(plan.operations[std::get<OpId>(graph.uses[use->value].owner).value].kind == EvaluationKind::kInitializer);
  // Reproduce the previous orphan registration: the value map and consumer are
  // correct, but this domain never consumes the initializer's input.
  auto wrong = plan; wrong.logical_values.resize(graph.values.size());
  wrong.logical_values[r.merged.value] = r.branch;
  wrong.operations[r.branch.value] = {EvaluationKind::kLogicalValue,use,{}};
  Refused(graph,wrong,LogicalCoverageCheck::kConsumedEvent);
  wrong.operations[r.branch.value].consumer.reset(); Refused(graph,wrong,LogicalCoverageCheck::kRouteMember);
  wrong = plan; wrong.logical_values.resize(graph.values.size());
  wrong.logical_values[r.left.value] = r.branch; Refused(graph,wrong,LogicalCoverageCheck::kValueMap);

  graph = Graph(1,false,false,false,0,true); const auto nested = Plan(graph);
  const auto children = Regions(graph,nested); bool tested = false;
  for (const auto& child : children) if (child.forwarded) {
    wrong = nested; wrong.operations[child.branch.value].consumer.reset();
    Refused(graph,wrong,LogicalCoverageCheck::kForwardedRegistration); tested = true; break;
  }
  Check(tested);
}
void Variants() {
  for (bool copy : {false,true}) for (bool split : {false,true}) for (std::uint8_t shift : {0,7}) {
    const auto graph = Graph(1,true,copy,split,shift); const auto plan = Plan(graph);
    for (const auto& region : Regions(graph,plan))
      Check(plan.logical_values.empty() || !plan.logical_values[region.merged.value]);
  }
}
void RouteBoundary() {
  const auto graph = Graph(1,true,false,false,0,false,true); const auto plan = Plan(graph);
  unsigned retained = 0, fused = 0;
  for (const auto& region : Regions(graph,plan)) {
    if (region.parent || !region.complete_phi) continue;
    if (!plan.logical_values.empty() && plan.logical_values[region.merged.value]) ++fused;
    else ++retained;
  }
  Check(retained == 1 && fused == 1);
}
void Scale() {
  std::uint64_t previous = 0;
  for (unsigned count : {200,201,400,800}) {
    const auto graph = Graph(count,true,true,true); const auto plan = Plan(graph);
    if (previous) Check(plan.work < previous*3); previous = plan.work;
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{SemanticBudget{}.bytes,plan.work})));
    Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{SemanticBudget{}.bytes,plan.work-1})) == EvaluationError::kWorkLimit);
    std::size_t lo = 0, hi = SemanticBudget{}.bytes;
    while (lo+1 < hi) {
      const auto mid = lo+(hi-lo)/2; const auto result = BuildEvaluationPlan(graph,{mid,kDefaultSemanticWork});
      if (std::holds_alternative<EvaluationPlan>(result)) hi=mid;
      else { Check(std::get<EvaluationFailure>(result)==EvaluationError::kResourceLimit); lo=mid; }
    }
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{hi,kDefaultSemanticWork})));
    Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{hi-1,kDefaultSemanticWork}))==EvaluationError::kResourceLimit);
    std::cout << count << " work=" << plan.work << " budget=" << hi << '\n';
  }
}
}
int main() {
  Mutations(); Variants(); RouteBoundary(); Scale();
  EvaluationEvidence evidence;
  evidence.logical_coverage = LogicalCoverageEvidence{};
  auto json = EvaluationFailureJson(evidence);
  json["logical_coverage"]["check"] = 4; Check(!ParseEvaluationFailure(json));
  json = EvaluationFailureJson(evidence); json["logical_coverage"]["covered"] = 1;
  Check(!ParseEvaluationFailure(json));
  json = EvaluationFailureJson(evidence); json["logical_coverage"].erase("registered");
  Check(!ParseEvaluationFailure(json));
}
