#include "record_module.h"

#include <utility>

std::variant<std::optional<RecordModule>, RecordModuleError> ReadRecordModule(
    const abcd::corpus::AbcFile& file, const abcd::abc::Header& header,
    const abcd::abc::MetadataIndex& index, const abcd::abc::RecordInfo& record) {
  using namespace abcd;
  const auto parsed = abc::ReadMergedRecordAttributes(file, header, index, record);
  const auto* attributes = std::get_if<abc::RecordAttributes>(&parsed);
  if (!attributes) return RecordModuleError::kAttributes;
  if (!attributes->module) return std::optional<RecordModule>{};
  auto loaded = abc::ReadModuleInfo(file, header, *attributes->module);
  auto* module = std::get_if<abc::ModuleInfo>(&loaded);
  if (!module) return RecordModuleError::kModule;
  auto planned = source::BuildModuleExports(file, header, *module);
  auto* exports = std::get_if<source::ModuleExports>(&planned);
  if (!exports) return RecordModuleError::kExports;
  return std::optional<RecordModule>{RecordModule{std::move(*module), std::move(*exports)}};
}
