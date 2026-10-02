#include "abcd/source/record_module_context.h"

#include <new>

namespace abcd::source {
RecordModuleResult ReadRecordModuleContext(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    abc::FilePackaging packaging, RecordModuleBudget budget) {
  switch (packaging) {
    case abc::FilePackaging::kBundle:
      return RecordModuleFailure{RecordModuleError::kBundleOwnership};
    case abc::FilePackaging::kMerged:
      break;
  }
  const auto parsed = abc::ReadMergedRecordAttributes(
      file, header, index, record, budget.names);
  if (const auto* failure = std::get_if<abc::AttributeFailure>(&parsed))
    return RecordModuleFailure{*failure};
  const auto& attributes = std::get<abc::RecordAttributes>(parsed);
  if (attributes.common_js && *attributes.common_js)
    return RecordModuleFailure{RecordModuleError::kCommonJs};
  if (!attributes.module)
    return RecordModuleContext(file, record.offset, {});
  auto decoded = abc::ReadModuleInfo(file, header, *attributes.module,
                                     budget.module);
  if (const auto* failure = std::get_if<abc::ModuleFailure>(&decoded))
    return RecordModuleFailure{*failure};
  try {
    return RecordModuleContext(
        file, record.offset,
        std::make_shared<const abc::ModuleInfo>(
            std::get<abc::ModuleInfo>(std::move(decoded))),
        !attributes.shared.value_or(false) && !attributes.top_level_await.value_or(false));
  } catch (const std::bad_alloc&) {
    return RecordModuleFailure{RecordModuleError::kResourceLimit};
  }
}
}  // namespace abcd::source
