#include "../../../src/source/evaluation_plan.h"
#include "../../../src/source/evaluation_failure_json.h"
#include "../../../src/source/evaluation_input_scope.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) { std::cerr << "evaluation_input_test:" << at.line() << '\n'; std::abort(); }
}
ValueProgram Graph(unsigned count, bool copy = false, bool split = false, unsigned shift = 0) {
  std::vector<std::uint8_t> bytes;
  const auto reg = static_cast<std::uint8_t>(shift);
  for (unsigned i = 0; i < count; ++i) {
    // Fresh array, then a conditional string, then an indexed definition.
    // The phi value does not exist at allocation, unlike an available RHS.
    bytes.insert(bytes.end(), {0x05,0,0x61,reg,0x60,static_cast<std::uint8_t>(shift+2)});
    const auto branch = bytes.size(); bytes.insert(bytes.end(), {0x4f,0});
    bytes.insert(bytes.end(), {0x3e,0,0});
    const auto jump = bytes.size(); bytes.insert(bytes.end(), {0x4d,0});
    bytes[branch+1] = static_cast<std::uint8_t>(bytes.size()-branch);
    bytes.insert(bytes.end(), {0x3e,1,0});
    bytes[jump+1] = static_cast<std::uint8_t>(bytes.size()-jump);
    if (copy) bytes.insert(bytes.end(), {0x61,static_cast<std::uint8_t>(shift+1),0x60,static_cast<std::uint8_t>(shift+1)});
    if (split) bytes.insert(bytes.end(), {0x4d,2});
    bytes.insert(bytes.end(), {0x79,0,reg,0,0,0x60,reg});
  }
  bytes.push_back(0x64);
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(shift+2,1,{false,false,false,false}));
  const auto semantic = std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,frame.registers));
  const auto proof = VerifySemanticSource(body,semantic,frame.locals,frame.parameters);
  Check(std::holds_alternative<SemanticProof>(proof));
  auto imported = ImportNormalValues(body,semantic,std::get<SemanticProof>(proof),frame);
  Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
void Located(const ValueProgram& graph) {
  EvaluationInputSite site;
  const auto literal = BuildLiteralPlan(graph,{}, {}, {}, {}, nullptr, &site);
  Check(std::get<LiteralError>(literal) == LiteralError::kDefinition);
  Check(site.check == EvaluationInputCheck::kLiteralStagedStorage && site.operation);
  Check(std::holds_alternative<ValueArrayAllocation>(graph.ops[site.operation->value].operation));
  // A caller-supplied staged array cannot bypass the independent literal
  // verifier. It must produce the same refusal and exact allocation witness.
  LiteralPlan forged{graph.revision,graph.cfg_revision};
  forged.staged.resize(graph.ops.size());
  for (std::size_t i=0;i<graph.ops.size();++i) {
    if (!std::holds_alternative<ValueIndexedWrite>(graph.ops[i].operation)) continue;
    const auto& use=graph.uses[graph.ops[i].first_use.value];
    const auto value=std::get<ValueId>(use.operand);
    const auto allocation=std::get<OpResult>(graph.values[value.value].origin).op;
    forged.objects.push_back({allocation,static_cast<std::uint32_t>(forged.definitions.size()),1,LiteralForm::kStatements});
    forged.definitions.push_back(OpId{static_cast<std::uint32_t>(i)});forged.staged[i]=1;
  }
  EvaluationInputSite independently_located;
  const auto verified=VerifyLiteralPlan(graph,forged,{}, {}, {}, {},nullptr,&independently_located);
  Check(std::get<LiteralError>(verified)==LiteralError::kDefinition);
  Check(independently_located==site);
  const auto built = BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationFailure>(built));
  const auto& f = std::get<EvaluationFailure>(built);
  Check(f.kind == EvaluationError::kUnsupportedOperation);
  Check(f.evaluation.plan_cause == EvaluationPlanCause{LiteralError::kDefinition});
  Check(f.evaluation.location == OperationLocation(graph,*site.operation));
  Check(ParseEvaluationFailure(EvaluationFailureJson(f.evaluation)) == f.evaluation);
}
std::uint64_t Boundary(const ValueProgram& graph, bool bytes) {
  std::uint64_t low=0, high=1;
  const auto run = [&](std::uint64_t bound) {
    SemanticBudget budget;
    if (bytes) budget.bytes=bound; else budget.work=bound;
    EvaluationInputSite site;
    const auto result=BuildLiteralPlan(graph,budget,{}, {}, {}, nullptr,&site);
    Check(std::holds_alternative<LiteralError>(result));
    const auto error=std::get<LiteralError>(result);
    Check(error == LiteralError::kDefinition || error == (bytes ? LiteralError::kResourceLimit : LiteralError::kWorkLimit));
    return error == LiteralError::kDefinition;
  };
  while (!run(high)) high*=2;
  while (high-low>1) { const auto mid=low+(high-low)/2; if(run(mid))high=mid;else low=mid; }
  Check(run(high) && !run(high-1)); return high;
}
void MissingShape() {
  ValueProgram graph;
  graph.origins = {{{0},{42},abc::CodeOffset{0}}, {{1},{43},abc::CodeOffset{1}}};
  graph.ops.push_back({ValueDefineAccessor{}, {0}, 0, {0}, 0, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {0}});
  graph.ops.push_back({ValueReturn{}, {0}, 1, {0}, 1, {},
      LanguageEffect::kNone, LanguageEffect::kNone, ExceptionalCompletion::kImpossible, {1}});
  graph.uses.push_back({OpId{1}, OperandRole::kReturnValue, ReadPurpose::kOperand, analysis::IntegerLiteral{0}, {}, {}, {}, {}});
  graph.blocks = {{{0},2,{0},0}};
  const auto result=BuildEvaluationPlan(graph);
  Check(std::holds_alternative<EvaluationFailure>(result));
  const auto& f=std::get<EvaluationFailure>(result);
  Check(f.kind==EvaluationError::kUnsupportedOperation);
  Check(f.evaluation.plan_cause==EvaluationPlanCause{EvaluationShapeError::kMissingShape});
  Check(f.evaluation.location.operation==OpId{0});
  Check(f.evaluation.location.pc==abc::CodeOffset{0} && f.evaluation.location.working_pc==abc::CodeOffset{42});
  graph.origins.clear();
  const auto absent=std::get<EvaluationFailure>(BuildEvaluationPlan(graph));
  Check(!absent.evaluation.location.pc && !absent.evaluation.location.working_pc);
}
void WitnessLifetime() {
  EvaluationInputSite site;
  { EvaluationInputScope successful(&site,{EvaluationInputCheck::kLiteralOrder,OpId{7}}); }
  Check(!site.check);
  try {
    EvaluationInputScope outer(&site,{EvaluationInputCheck::kLiteralOrder,OpId{7}});
    EvaluationInputScope inner(&site,{EvaluationInputCheck::kLiteralRhsAvailability,OpId{8},UseId{0},ValueId{0}});
    throw LiteralError::kLateValue;
  } catch (LiteralError) {}
  Check(site.operation == OpId{8} && site.use == UseId{0} && site.value == ValueId{0});
}
}
int main() {
  static_assert(std::is_trivially_copyable_v<EvaluationInputSite>);
  static_assert(std::is_trivially_copyable_v<EvaluationFailure>);
  WitnessLifetime(); MissingShape();
  for (bool copy : {false,true}) for (bool split : {false,true}) for (unsigned shift : {0U,7U}) Located(Graph(1,copy,split,shift));
  std::uint64_t previous_bytes=0, previous_work=0;
  for (unsigned count : {200U,201U,400U,800U}) {
    const auto graph=Graph(count); Located(graph);
    const auto bytes=Boundary(graph,true), work=Boundary(graph,false);
    if(previous_bytes) { Check(bytes<=previous_bytes*3); Check(work<=previous_work*3); }
    previous_bytes=bytes; previous_work=work;
  }
  EvaluationEvidence e; e.plan_cause=EvaluationShapeError::kMissingShape;
  auto j=EvaluationFailureJson(e); Check(ParseEvaluationFailure(j)==e);
  j["plan_cause"]["value"]=99U; Check(!ParseEvaluationFailure(j));
  j=EvaluationFailureJson(e);j["plan_cause"]["domain"]="Literal";Check(!ParseEvaluationFailure(j));
  j=EvaluationFailureJson(e);j["input_check"]=999U;Check(!ParseEvaluationFailure(j));
}
