#include "../src/source/non_executable_json.h"
#include "../src/source/cell_parameter_default_json.h"
#include "../src/source/sendable_failure_json.h"
#include "../src/source/concurrent_failure_json.h"
#include "../src/source/async_failure_json.h"
#include "../src/source/internal_value_json.h"
#include "../src/source/family_creation_json.h"
#include "../src/source/exception_region_json.h"
#include "../src/source/tree_print_failure_json.h"
#include "record_failure_json.h"
#include "../src/source/guarded_call_json.h"
#include "../src/source/control_failure_json.h"
#include "../src/source/tree_failure_json.h"
#include "../src/source/evaluation_failure_json.h"

#include <type_traits>

namespace abcd::tools {
namespace {
nlohmann::json DescribeCauses(const source::FailureCauseChain& chain) {
  nlohmann::json causes = nlohmann::json::array();
  for (const auto& cause : chain.View())
    causes.push_back({{"domain", cause.domain}, {"value", cause.value}});
  return causes;
}
nlohmann::json DescribeReference(const source::CallReferenceEvidence& evidence) {
  nlohmann::json reference = {{"kind", static_cast<unsigned>(evidence.kind)}, {"call", evidence.call.value}};
  if (evidence.property) reference["property"] = evidence.property->value;
  if (evidence.placement) reference["placement"] = static_cast<unsigned>(*evidence.placement);
  if (evidence.temporary) reference["temporary"] = static_cast<unsigned>(*evidence.temporary);
  if (evidence.consumer) reference["consumer"] = evidence.consumer->value;
  if (evidence.checked) reference["checked"] = source::GuardedCallJson(*evidence.checked);
  return reference;
}
}  // namespace

void DescribeRecordFailure(const source::RecordSourceResult& result,
                           nlohmann::json& record) {
  std::visit([&](const auto& failure) {
    using T = std::decay_t<decltype(failure)>;
    if constexpr (std::is_same_v<T, source::PrintedSource>) {
      if(failure.arkts)record["source_language"]="arkts";
      if (failure.omitted_members.empty()) return;
      auto& omissions = record["source_omitted_members"];
      omissions = nlohmann::json::array();
      for (const auto& member : failure.omitted_members) {
        source::FailureCauseChain chain;
        std::visit([&](auto cause) { source::AppendFailureCause(chain, cause); }, member.cause);
        if (member.internal_value) source::AppendFailureCause(chain, member.internal_value->reason);
        nlohmann::json item{{"constructor", member.constructor.value}, {"member", member.member.value},
            {"method", member.method.value}, {"pc", member.pc.value}, {"causes", DescribeCauses(chain)}};
        if (member.internal_value) item["internal_value"] = source::InternalValueJson(*member.internal_value);
        if (member.working_pc) item["working_pc"] = member.working_pc->value;
        if (member.creation_method) item["creation_method"] = member.creation_method->value;
        if (member.creation_pc) item["creation_pc"] = member.creation_pc->value;
        if (member.node) item["exception_node"] = member.node->value;
        if (member.value) item["value"] = member.value->value;
        omissions.push_back(std::move(item));
      }
    } else if constexpr (std::is_same_v<T, source::RouteRefusal>) {
      record["source_failure_domain"] = "route_refusal";
      record["source_failure"] = {
          {"class", source::RouteFailureClassName(failure.failure.failure_class)},
          {"reason", source::ValueRouteErrorName(failure.failure.reason)}};
      if (failure.failure.pc) record["source_failure"]["pc"] = failure.failure.pc->value;
      if (failure.failure.method) record["source_failure"]["method"] = failure.failure.method->value;
      if (failure.failure.working_pc) record["source_failure"]["working_pc"] = failure.failure.working_pc->value;
      if (failure.failure.creation_method) record["source_failure"]["creation_method"] = failure.failure.creation_method->value;
      if (failure.failure.creation_pc) record["source_failure"]["creation_pc"] = failure.failure.creation_pc->value;
      if (failure.failure.sendable) record["source_failure"]["sendable"] = source::SendableJson(*failure.failure.sendable);
      if (failure.failure.shared_module) record["source_failure"]["shared_module"] = source::SharedModuleJson(*failure.failure.shared_module);
      if (failure.failure.concurrent) record["source_failure"]["concurrent"] = source::ConcurrentJson(*failure.failure.concurrent);
      if (failure.failure.async) record["source_failure"]["async"] = source::AsyncCompletionJson(*failure.failure.async);
      if (failure.failure.exception_region) record["source_failure"]["exception_region"] = source::ExceptionRegionJson(*failure.failure.exception_region);
      if (failure.failure.family_creation) record["source_failure"]["family_creation"] = source::FamilyCreationJson(*failure.failure.family_creation);
      if (failure.failure.non_executable) record["source_failure"]["non_executable"] = source::NonExecutableJson(*failure.failure.non_executable);
      if (failure.failure.internal_value) record["source_failure"]["internal_value"] = source::InternalValueJson(*failure.failure.internal_value);
      if (failure.failure.printing) record["source_failure"]["printing"] = source::TreePrintFailureJson(*failure.failure.printing);
      if (failure.failure.evaluation) record["source_failure"]["evaluation"] = source::EvaluationFailureJson(*failure.failure.evaluation);
      if (failure.failure.tree) record["source_failure"]["tree"] = source::TreeFailureJson(*failure.failure.tree);
      if (failure.failure.cell_parameter_default) record["source_failure"]["cell_parameter_default"]=source::CellParameterDefaultJson(*failure.failure.cell_parameter_default);
      if (failure.failure.deferred) record["source_failure"]["deferred"] = source::DeferredCellJson(*failure.failure.deferred);
      if (failure.failure.control) record["source_failure"]["control"] = source::ControlJson(*failure.failure.control);
      if (failure.failure.capture) {
        const auto& use = *failure.failure.capture;
        auto& capture = record["source_failure"]["capture"];
        capture = {{"ordinal", use.ordinal}, {"depth", use.cell.depth.value},
            {"slot", use.cell.slot.value}, {"requested", static_cast<unsigned>(use.requested)}};
        if (use.source) {
          capture["method"] = use.source->method.value;
          if (use.source->pc) capture["pc"] = use.source->pc->value;
          if (use.source->working_pc) capture["working_pc"] = use.source->working_pc->value;
        }
      }
      if (failure.failure.call_reference) {
        record["source_failure"]["call_reference"] = DescribeReference(*failure.failure.call_reference);
      }
      record["source_failure"]["causes"] = DescribeCauses(failure.failure.causes);
      record["source_failure"]["causes_truncated"] = failure.failure.causes.truncated;
    } else if constexpr (std::is_same_v<T, source::RecordProgramFailure>) {
      record["source_failure_domain"] = "record_program";
      record["source_error"] = static_cast<unsigned>(failure.kind);
      if (failure.sendable) record["source_sendable"] = source::SendableJson(*failure.sendable);
      if (failure.shared_module) record["source_shared_module"] = source::SharedModuleJson(*failure.shared_module);
      if (failure.concurrent) record["source_concurrent"] = source::ConcurrentJson(*failure.concurrent);
      if (failure.async) record["source_async"] = source::AsyncCompletionJson(*failure.async);
      if (failure.exception_region) record["source_exception_region"] = source::ExceptionRegionJson(*failure.exception_region);
      if (failure.family_creation) record["source_family_creation"] = source::FamilyCreationJson(*failure.family_creation);
      if (failure.non_executable) record["source_non_executable"] = source::NonExecutableJson(*failure.non_executable);
      if (failure.internal_value) record["source_internal_value"] = source::InternalValueJson(*failure.internal_value);
      if (failure.printing) record["source_printing"] = source::TreePrintFailureJson(*failure.printing);
      if (failure.evaluation) record["source_evaluation"] = source::EvaluationFailureJson(*failure.evaluation);
      if (failure.tree) record["source_tree"] = source::TreeFailureJson(*failure.tree);
      if (failure.cell_parameter_default) record["source_cell_parameter_default"]=source::CellParameterDefaultJson(*failure.cell_parameter_default);
      if (failure.deferred) record["source_deferred"] = source::DeferredCellJson(*failure.deferred);
      if (failure.call_reference) record["source_call_reference"] = DescribeReference(*failure.call_reference);
      if (failure.method) record["source_method"] = failure.method->value;
      if (failure.working_pc) record["source_working_pc"] = failure.working_pc->value;
      if (failure.pc) record["source_pc"] = failure.pc->value;
      record["source_causes"] = DescribeCauses(failure.causes);
      record["source_causes_truncated"] = failure.causes.truncated;
    }
  }, result);
}

void DescribeModuleContextFailure(const source::RecordModuleFailure& failure, nlohmann::json& record) {
  source::FailureCauseChain chain;
  std::visit([&](const auto& reason) {
    using T=std::decay_t<decltype(reason)>;
    if constexpr(std::is_enum_v<T>)source::AppendFailureCause(chain,reason);
    else {
      source::AppendFailureCause(chain,reason.kind);
      if constexpr(std::is_same_v<T,abc::AttributeFailure>)
        if(reason.metadata_error)source::AppendFailureCause(chain,*reason.metadata_error);
    }
  },failure.reason);
  // Preserve the historical status/index fields, add the actual typed leaf.
  record["source_error"]=failure.reason.index();
  record["source_failure_domain"]="module_context";
  record["source_causes"]=DescribeCauses(chain);
  record["source_causes_truncated"]=chain.truncated;
}

bool RecordFailureIsFatal(const source::RecordSourceResult& result,
                          bool process_fault) {
  if (process_fault) return true;
  const auto* failure = std::get_if<source::RecordProgramFailure>(&result);
  return failure && failure->kind == source::RecordProgramError::kAllocationFailure;
}
}  // namespace abcd::tools
