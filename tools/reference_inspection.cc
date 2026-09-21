#include "reference_inspection.h"

#include "abcd/abc/literal_array.h"
#include "abcd/abc/method_references.h"
#include "abcd/abc/string_entity.h"

namespace abcd::tools {
std::optional<nlohmann::json> InspectReferences(const corpus::AbcFile& file,
                                                const abc::Header& header,
                                                const abc::MetadataIndex& index,
                                                const abc::MethodInfo& method,
                                                const abc::MethodBody& body,
                                                MethodCounts* counts) {
  nlohmann::json diagnostic{
      {"method_offset", method.offset.value},
      {"code_offset", method.code->offset.value},
      {"stage", StageLabel(InspectionStage::kReferences)}};
  const auto result =
      abc::ResolveMethodReferences(file, header, index, method.offset, body);
  if (const auto* failure = std::get_if<abc::ReferenceFailure>(&result)) {
    diagnostic["kind"] = static_cast<int>(failure->kind);
    diagnostic["pc"] = failure->pc.value;
    if (failure->metadata_error)
      diagnostic["metadata_error"] = static_cast<int>(*failure->metadata_error);
    return diagnostic;
  }
  const auto& references = std::get<abc::MethodReferences>(result);
  counts->reference_operands += references.operand_count;
  counts->unique_references += references.references.size();
  for (const auto& reference : references.references) {
    diagnostic["pc"] = reference.first_use.value;
    diagnostic["entity_offset"] = reference.target.offset.value;
    diagnostic["pool_index"] = reference.slot.value;
    diagnostic["entity_kind"] = static_cast<int>(reference.target.kind);
    switch (reference.target.kind) {
      case abc::PoolEntityKind::kMethod:
        // Range resolution only: method membership is a separate validation.
        break;
      case abc::PoolEntityKind::kString: {
        const auto text =
            abc::ReadStringEntity(file, header, reference.target.offset);
        if (const auto* failure = std::get_if<abc::MetadataError>(&text)) {
          diagnostic["metadata_error"] = static_cast<int>(*failure);
          return diagnostic;
        }
        ++counts->string_literals;
        break;
      }
      case abc::PoolEntityKind::kLiteralArray: {
        const auto literal =
            abc::ReadLiteralArray(file, header, reference.target.offset);
        if (const auto* failure = std::get_if<abc::LiteralFailure>(&literal)) {
          diagnostic["literal_error"] = static_cast<int>(failure->kind);
          diagnostic["literal_offset"] = failure->offset.value;
          return diagnostic;
        }
        ++counts->literal_arrays;
        counts->literal_entries +=
            std::get<abc::LiteralArray>(literal).entries.size();
        break;
      }
    }
  }
  return std::nullopt;
}
}  // namespace abcd::tools
