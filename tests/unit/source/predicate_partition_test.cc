#include "../../../src/analysis/phased_value_import.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/phase_evaluation.h"
#include "../../../src/source/evaluation_liveness.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/source/tree_printer.h"
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
enum class Shape { kPure, kOutsideCatch, kEffect, kCoercing, kMultiple };
std::string Source(abc::ProfileId profile, unsigned locals, unsigned count, Shape shape, std::uint64_t& work) {
  const auto op=[](abc::Opcode code){return static_cast<std::uint8_t>(code);};
  std::vector<std::uint8_t> bytes;
  const auto emit=[&](abc::Opcode code,unsigned operand){bytes.push_back(op(code));bytes.push_back(static_cast<std::uint8_t>(operand));};
  emit(abc::Opcode::kLDA_V8,locals);
  bytes.insert(bytes.end(),{op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,0,0});
  emit(abc::Opcode::kSTA_V8,0);
  const auto protected_end=bytes.size();
  for(unsigned i=0;i<count;++i)bytes.insert(bytes.end(),{op(abc::Opcode::kMOV_V8_V8),static_cast<std::uint8_t>(locals-1),0,
      op(abc::Opcode::kMOV_V8_V8),0,static_cast<std::uint8_t>(locals-1)});
  if(shape==Shape::kEffect){emit(abc::Opcode::kLDA_V8,locals);bytes.insert(bytes.end(),{op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,1,0});}
  bytes.push_back(op(abc::Opcode::kLDTRUE));
  bytes.insert(bytes.end(),{op(shape==Shape::kCoercing?abc::Opcode::kEQ_IMM8_V8:abc::Opcode::kSTRICTEQ_IMM8_V8),0,0});
  for(unsigned i=0;i<count;++i)bytes.push_back(op(abc::Opcode::kISFALSE));
  if(shape==Shape::kMultiple)emit(abc::Opcode::kSTA_V8,0);
  const auto branch=bytes.size();emit(abc::Opcode::kJEQZ_IMM8,0);
  if(shape==Shape::kMultiple)emit(abc::Opcode::kLDA_V8,0);else bytes.push_back(op(abc::Opcode::kLDTRUE));
  bytes.push_back(op(abc::Opcode::kRETURN));
  bytes[branch+1]=static_cast<std::uint8_t>(bytes.size()-branch);
  bytes.insert(bytes.end(),{op(abc::Opcode::kLDFALSE),op(abc::Opcode::kRETURN)});
  const auto handler_pc=static_cast<std::uint32_t>(bytes.size());bytes.push_back(op(abc::Opcode::kRETURN));
  auto body=test::Body(bytes);
  abc::TryRegion region;region.offset={0};region.length=shape==Shape::kOutsideCatch?protected_end:handler_pc;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={handler_pc};handler.length=1;
  body.tries={region};body.handlers={handler};
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(locals, 1, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, profile, frame.registers, {}, {}, &frame));
  IdfQuota quota{kDefaultSemanticWork}; const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::size_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v]; const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= frame.locals && std::holds_alternative<MethodEntry>(value.origin))
      entries.push_back({{static_cast<std::uint32_t>(v)}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto lexical = std::get<ExceptionLocals>(ProveExceptionLocals(flow, env));
  IdfQuota value_quota{kDefaultSemanticWork};
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries,
      value_quota, {}, PendingExceptionValues::kLanguage, lexical.loads, {}, CallResultValues::kLanguage));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto evaluation = std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  std::optional<OpId> comparison,branch_op;
  for(std::size_t i=0;i<graph.ops.size();++i){
    if(std::holds_alternative<ValueBinary>(graph.ops[i].operation))comparison=OpId{static_cast<std::uint32_t>(i)};
    if(std::holds_alternative<ValueBranch>(graph.ops[i].operation))branch_op=OpId{static_cast<std::uint32_t>(i)};
  }
  test::Check(comparison&&branch_op);
  const auto branch_input=ResolveEvaluationOperand(evaluation,graph.uses[graph.ops[branch_op->value].first_use.value].operand);
  const auto producer=std::get<OpResult>(graph.values[std::get<ValueId>(branch_input).value].origin).op;
  const bool expected=shape!=Shape::kMultiple && (shape!=Shape::kCoercing || count!=0);
  test::Check((evaluation.operations[producer.value].kind==EvaluationKind::kInline)==expected);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph,evaluation)));
  if(shape==Shape::kPure||shape==Shape::kOutsideCatch){
    for(std::size_t i=0;i<graph.ops.size();++i)if(std::holds_alternative<ValueNamedRead>(graph.ops[i].operation))
      test::Check(evaluation.operations[i].kind==EvaluationKind::kRoot);
  }
  work=evaluation.work;
  // Deliberately reattach a retained throwing getter to the pure condition.
  // The independent verifier must reject loss of its original exception site.
  if(shape==Shape::kPure||shape==Shape::kOutsideCatch) {
    unsigned mutations=0;
    for(std::size_t i=0;i<graph.ops.size();++i)if(std::holds_alternative<ValueNamedRead>(graph.ops[i].operation)) {
      const auto& compare=graph.ops[comparison->value];
      for(std::uint32_t j=0;j<compare.use_count;++j) {
        const UseId use{compare.first_use.value+j};
        const auto operand=ResolveEvaluationOperand(evaluation,graph.uses[use.value].operand);
        const auto* value=std::get_if<ValueId>(&operand);
        if(!value||*value!=*graph.ops[i].result)continue;
        ++mutations;
        auto forged=evaluation;forged.operations[i]={EvaluationKind::kInline,use,{}};
        test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,forged)));
      }
    }
    test::Check(mutations==1);
  }
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto control = std::get<ExceptionControl>(BuildExceptionControl(body, graph, input, regions));
  const ExceptionTreeInput exceptions{body, input, regions, control};
  FunctionSyntax syntax{}; syntax.identity.profile = profile; syntax.method = {300};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.texts = {u"number"}; syntax.parameters.resize(1); syntax.names={u"value",u"effect"};
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i=0;i<graph.ops.size();++i) if(const auto* read=std::get_if<ValueNamedRead>(&graph.ops[i].operation))
    constants[i]=NameId{read->name.value};
  const ExpressionConstants resolved{constants, 2, 1, TextId{0}};
  const auto built = BuildExceptionFunctionDraft(graph, exceptions, evaluation, syntax, resolved);
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& tree = std::get<ExceptionFunctionDraft>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph, exceptions, evaluation, tree, resolved)));
  std::ostringstream source; test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, source))); return source.str();
}
}
int main(int argc,char**) {
  for(auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(unsigned locals:{2U,7U})
      for(auto shape:{Shape::kPure,Shape::kOutsideCatch,Shape::kEffect,Shape::kCoercing,Shape::kMultiple}) {
        std::uint64_t previous=0;
        for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
          std::uint64_t work=0;const auto source=Source(profile,locals,count,shape,work);
          if(count>=400)test::Check(work<3*previous);
          previous=work;
          if(argc>1)std::cout<<nlohmann::json{{"source",source},{"count",count},{"outside",shape==Shape::kOutsideCatch},
            {"effect",shape==Shape::kEffect},{"coercing",shape==Shape::kCoercing}}<<'\n';
        }
      }
}
