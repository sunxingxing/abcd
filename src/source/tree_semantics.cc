#include "tree_semantics.h"

#include <new>
#include <stdexcept>

#include "../analysis/value_phi.h"
#include "class_initialization_ports.h"
#include "exception_tree.h"
#include "original_flow.h"
#include "tree_correspondence.h"

namespace abcd::source {
namespace {
std::variant<TreeSemantics, TreeSemanticError> VerifySemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget,
    const ExceptionTreeInput* exceptions,
    TreeControlCheck control_check, TreeSemanticFailure* failure) {
  if (failure) *failure = {};
  using Error = TreeSemanticError;
  try {
    auto checked_flow = exceptions
        ? VerifyExceptionTreeFlow(graph, *exceptions, evaluation, tree, budget, control_check, failure ? &failure->location : nullptr)
        : VerifyTreeFlow(graph, control, evaluation, tree, budget, failure ? &failure->location : nullptr);
    auto* flow = std::get_if<TreeFlow>(&checked_flow);
    if (!flow) {
      const auto error = std::get<TreeFlowError>(checked_flow);
      if (failure) failure->cause = error;
      if (error == TreeFlowError::kResourceLimit) return Error::kResourceLimit;
      if (error == TreeFlowError::kWorkLimit) return Error::kWorkLimit;
      return Error::kFlow;
    }
    TreeCheckBudget checked(budget);
    checked.Admit(flow->allocated_bytes, 1);
    checked.Step(flow->work);
    auto original=RetainOriginalFlow(graph,tree,*flow,checked);
    if (evaluation.for_of) {
      RequireTree(flow->for_of.has_value(),TreeStructureError::kOwnership);
      original->iterations.emplace(); auto& retained=*original->iterations;
      retained.view=std::move(flow->for_of->view);
      retained.statements=std::move(flow->for_of->statements);
      retained.closes=std::move(flow->for_of->closes);
      const auto copy=[&](auto& to,const auto& from){checked.Array(to,from.size());std::copy(from.begin(),from.end(),to.begin());};
      RequireTree(exceptions&&exceptions->for_of,TreeStructureError::kOwnership);
      std::vector<std::optional<OriginalCloseMember>> members;checked.Array(members,exceptions->input.flow.nodes.size());
      for(const auto& region:exceptions->for_of->ownership.Summary().regions)for(const auto& member:region.members){checked.Step();RequireTree(member.node.value<members.size(),TreeStructureError::kOwnership);const auto old=members[member.node.value];RequireTree(!old||old->protocol==region.protocol,TreeStructureError::kOwnership);members[member.node.value]=OriginalCloseMember{region.protocol,member.role};}
      checked.Array(retained.close_members,graph.phases.size());
      for(std::uint32_t b=0;b<graph.phases.size();++b){checked.Step();RequireTree(graph.phases[b].input.value<members.size(),TreeStructureError::kOwnership);retained.close_members[b]=members[graph.phases[b].input.value];}
      const auto& plan=*evaluation.for_of;
      copy(retained.input_values,graph.input.values);copy(retained.phis,plan.iteration.phis);
      copy(retained.loops,plan.identity.loops);copy(retained.operations,plan.operations);copy(retained.values,plan.values);
      copy(retained.events.events,plan.events.events);copy(retained.events.commits,plan.events.commits);copy(retained.events.ports,plan.events.ports);
      retained.events.revision=plan.events.revision;retained.events.cfg_revision=plan.events.cfg_revision;
      checked.Array(retained.bindings,retained.statements.size());
      for(std::uint32_t l=0;l<retained.statements.size();++l){checked.Step();const auto& node=tree.statements[retained.statements[l].value];const auto& loop=std::get<ForOfStmt>(node.statement);retained.bindings[l]={loop.target,loop.iterable,loop.body,node.origin};}
      std::vector<std::optional<std::uint32_t>> owners;checked.Array(owners,tree.statements.size());
      for(std::uint32_t l=0;l<retained.statements.size();++l){checked.Step();owners[retained.statements[l].value]=l;}
      checked.Array(retained.exits,tree.statements.size());std::size_t exit_count=0;
      for(std::uint32_t s=0;s<tree.statements.size();++s){checked.Step();const auto& node=tree.statements[s];
        if(const auto* jump=std::get_if<ContinueStmt>(&node.statement);jump&&owners[jump->target.value])retained.exits[exit_count++]={{s},*owners[jump->target.value],LoopTransferKind::kContinue,node.origin};
        if(const auto* jump=std::get_if<BreakStmt>(&node.statement);jump&&owners[jump->target.value])retained.exits[exit_count++]={{s},*owners[jump->target.value],LoopTransferKind::kBreak,node.origin};
      }
      retained.exits.resize(exit_count);
    }
    if (evaluation.for_in) {
      checked.Array(original->enumeration_operations, evaluation.for_in->operations.size());
      std::copy(evaluation.for_in->operations.begin(), evaluation.for_in->operations.end(), original->enumeration_operations.begin());
      checked.Array(original->enumeration_values, evaluation.for_in->values.size());
      std::copy(evaluation.for_in->values.begin(), evaluation.for_in->values.end(), original->enumeration_values.begin());
    }
    if(evaluation.for_in || evaluation.for_of) {
      auto identities = analysis::AnalyzeValueIdentities(graph, checked.Remaining());
      if (const auto* error = std::get_if<analysis::ValueSimplifyError>(&identities)) {
        switch (*error) {
          case analysis::ValueSimplifyError::kWorkLimit: return Error::kWorkLimit;
          case analysis::ValueSimplifyError::kResourceLimit: return Error::kResourceLimit;
          case analysis::ValueSimplifyError::kStaleRevision:
          case analysis::ValueSimplifyError::kRevisionLimit:
          case analysis::ValueSimplifyError::kInvalidGraph:
          case analysis::ValueSimplifyError::kCopyCycle:
          case analysis::ValueSimplifyError::kUnsupportedFlow: return Error::kOperand;
        }
      }
      auto& identity = std::get<analysis::ValueIdentities>(identities);
      checked.Admit(identity.allocated_bytes, 1); checked.Step(identity.work);
      original->canonical_values = std::move(identity.canonical);
    }
    if (evaluation.cells) {
      original->environments=evaluation.cells->environments;
      RequireTree(original->environments!=nullptr,TreeStructureError::kOwnership);
      // The source result can outlive EvaluationPlan. Admit retained topology
      // once here; current/trial flow borrows the same immutable owner.
      checked.Admit(original->environments->allocated_bytes,1);
    }
    CheckTreeOperands(graph, evaluation, tree, *flow, constants, checked,*original);
    CheckTreeCorrespondenceOrigins(graph, evaluation, tree, *flow, checked);
    RetainClassInitializationPorts(graph, tree, *original, checked);
    IndexOriginalInputs(*original,checked);
    flow->original=std::move(original);
    flow->allocated_bytes=checked.Bytes();
    return TreeSemantics{graph.revision, graph.cfg_revision, checked.Bytes(), checked.Work(), std::move(*flow)};
  } catch (Error error) { return error;
  } catch (TreeStructureError error) {
    return error == TreeStructureError::kWorkLimit       ? Error::kWorkLimit
           : error == TreeStructureError::kResourceLimit ? Error::kResourceLimit
                                                         : Error::kOperand;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<TreeSemantics, TreeSemanticError> VerifyTreeSemantics(
    const analysis::ValueProgram& graph, const ControlPlan& control,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget, TreeSemanticFailure* failure) {
  auto result = VerifySemantics(graph, control, evaluation, tree, constants, budget, nullptr, TreeControlCheck::kFresh, failure);
  if (failure) if (const auto* error = std::get_if<TreeSemanticError>(&result)) failure->kind = *error;
  return result;
}
std::variant<TreeSemantics, TreeSemanticError> VerifyExceptionTreeSemantics(
    const analysis::ValueProgram& graph, const ExceptionTreeInput& exceptions,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    ExpressionConstants constants, analysis::SemanticBudget budget, TreeControlCheck control_check, TreeSemanticFailure* failure) {
  auto result = VerifySemantics(graph, exceptions.control.skeleton, evaluation, tree, constants, budget, &exceptions,
      control_check, failure);
  if (failure) if (const auto* error = std::get_if<TreeSemanticError>(&result)) failure->kind = *error;
  return result;
}
}  // namespace abcd::source
