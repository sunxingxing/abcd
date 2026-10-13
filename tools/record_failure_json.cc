#include "record_failure_json.h"
#include "../src/source/guarded_call_json.h"
#include "../src/source/control_failure_json.h"
#include "../src/source/tree_failure_json.h"

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
    if constexpr (std::is_same_v<T, source::RouteRefusal>) {
      record["source_failure_domain"] = "route_refusal";
      record["source_failure"] = {
          {"class", source::RouteFailureClassName(failure.failure.failure_class)},
          {"reason", source::ValueRouteErrorName(failure.failure.reason)}};
      if (failure.failure.pc) record["source_failure"]["pc"] = failure.failure.pc->value;
      if (failure.failure.method) record["source_failure"]["method"] = failure.failure.method->value;
      if (failure.failure.working_pc) record["source_failure"]["working_pc"] = failure.failure.working_pc->value;
      if (failure.failure.creation_method) record["source_failure"]["creation_method"] = failure.failure.creation_method->value;
      if (failure.failure.creation_pc) record["source_failure"]["creation_pc"] = failure.failure.creation_pc->value;
      if (failure.failure.tree) record["source_failure"]["tree"] = source::TreeFailureJson(*failure.failure.tree);
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
      if (failure.tree) record["source_tree"] = source::TreeFailureJson(*failure.tree);
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
