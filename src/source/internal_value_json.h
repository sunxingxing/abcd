#ifndef ABCD_SOURCE_INTERNAL_VALUE_JSON_H_
#define ABCD_SOURCE_INTERNAL_VALUE_JSON_H_
#include "abcd/analysis/internal_value_failure.h"
#include <nlohmann/json.hpp>
namespace abcd::source {
inline nlohmann::json InternalValueJson(const analysis::InternalValueEvidence& e) {
  nlohmann::json j{{"reason", static_cast<unsigned>(e.reason)},
      {"observer_instruction", e.observer.value},};
  if (e.value) j["value"] = e.value->value;
  if (e.read_ordinal) j["read_ordinal"] = *e.read_ordinal;
  if (e.cell) j["cell"] = {{"environment", e.cell->environment.value}, {"slot", e.cell->slot.value}};
  if (e.allocation) j["allocation"] = e.allocation->value;
  if (e.store) j["store"] = e.store->value;
  if (e.producer) {
  j["producer_kind"] = e.producer->index();
  if (const auto* site = std::get_if<analysis::InstructionValue>(&*e.producer))
    j["producer_instruction"] = site->instruction.value;
  if (const auto* phi = std::get_if<analysis::PhiValue>(&*e.producer)) j["producer_block"] = phi->block.value;
  }
  if (e.kinds) {
    j["domains"] = nlohmann::json::array();
    for (auto kind : {analysis::VmValueKind::kLanguage, analysis::VmValueKind::kHole,
        analysis::VmValueKind::kEnvironment, analysis::VmValueKind::kOtherInternal,
        analysis::VmValueKind::kPropertyEnumerator, analysis::VmValueKind::kPatternRecord})
      if (e.kinds->Contains(kind)) j["domains"].push_back(static_cast<unsigned>(kind));
  }
  return j;
}
inline std::optional<analysis::InternalValueEvidence> ParseInternalValue(const nlohmann::json& j) {
  if (!j.is_object()) return {};
  const auto number = [&](const char* key) -> std::optional<std::uint32_t> {
    const auto it=j.find(key);
    if (it==j.end() || !it->is_number_unsigned() || it->get<std::uint64_t>()>UINT32_MAX) return {};
    return it->get<std::uint32_t>();
  };
  const auto reason=number("reason"), observer=number("observer_instruction");
  if (!reason || *reason>static_cast<unsigned>(analysis::InternalValueReason::kCellPreservation) || !observer) return {};
  analysis::InternalValueEvidence e{static_cast<analysis::InternalValueReason>(*reason), {}, {*observer}, {}, {}, {}};
  const auto optional_id=[&](const char* key, auto& out) {
    if (!j.contains(key)) return true;
    const auto value=number(key); if (!value) return false;
    using T=typename std::decay_t<decltype(out)>::value_type;
    out=T{*value}; return true;
  };
  if (!optional_id("value",e.value) || !optional_id("read_ordinal",e.read_ordinal) ||
      !optional_id("allocation",e.allocation) || !optional_id("store",e.store)) return {};
  if (j.contains("producer_kind")) {
    const auto origin=number("producer_kind"); if (!origin || *origin>2) return {};
    if (*origin==0) e.producer=analysis::MethodEntry{};
    if (*origin==1) {
      const auto site=number("producer_instruction"); if (!site) return {};
      e.producer=analysis::InstructionValue{{*site}};
    } else if (*origin==2) {
      const auto block=number("producer_block"); if (!block) return {};
      e.producer=analysis::PhiValue{{*block}};
    }
    if ((*origin!=1 && j.contains("producer_instruction")) || (*origin!=2 && j.contains("producer_block"))) return {};
  } else if (j.contains("producer_instruction") || j.contains("producer_block")) return {};
  if (j.contains("cell")) {
    const auto& cell=j["cell"];
    if (!cell.is_object()) return {};
    for (const auto* key:{"environment","slot"})
      if (!cell.contains(key) || !cell[key].is_number_unsigned() || cell[key].get<std::uint64_t>()>UINT32_MAX) return {};
    if (cell["slot"].get<std::uint64_t>()>UINT16_MAX) return {};
    e.cell=analysis::LocalCell{{cell["environment"].get<std::uint32_t>()},{cell["slot"].get<std::uint16_t>()}};
  }
  if (const auto it=j.find("domains"); it!=j.end()) {
    if (!it->is_array() || it->size()>analysis::kValueKindCount) return {};
    analysis::ValueKinds kinds;
    std::optional<unsigned> previous;
    for (const auto& item:*it) {
      if (!item.is_number_unsigned() || item.get<std::uint64_t>()>=analysis::kValueKindCount) return {};
      const auto domain=item.get<unsigned>();
      if (previous && *previous>=domain) return {};
      previous=domain;
      switch (static_cast<analysis::VmValueKind>(domain)) {
        case analysis::VmValueKind::kLanguage: kinds=kinds.Join(analysis::ValueKinds::Language()); break;
        case analysis::VmValueKind::kHole: kinds=kinds.Join(analysis::ValueKinds::Hole()); break;
        case analysis::VmValueKind::kEnvironment: kinds=kinds.Join(analysis::ValueKinds::Environment()); break;
        case analysis::VmValueKind::kOtherInternal: kinds=kinds.Join(analysis::ValueKinds::OtherInternal()); break;
        case analysis::VmValueKind::kPropertyEnumerator: kinds=kinds.Join(analysis::ValueKinds::PropertyEnumerator()); break;
        case analysis::VmValueKind::kPatternRecord: kinds=kinds.Join(analysis::ValueKinds::PatternRecord()); break;
      }
    }
    e.kinds=kinds;
  }
  const bool local=e.reason>=analysis::InternalValueReason::kCyclicCellInitialization;
  if (local) {
    if (!e.cell || e.value || e.producer || e.read_ordinal || e.kinds) return {};
  } else if (!e.value || !e.producer || !e.read_ordinal || e.cell || e.allocation || e.store ||
      analysis::InternalReason(e.kinds)!=e.reason) return {};
  return e;
}
}  // namespace abcd::source
#endif
