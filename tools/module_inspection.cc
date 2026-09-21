#include "module_inspection.h"

#include <ostream>

#include "abcd/abc/module_info.h"
#include "abcd/abc/record_attributes.h"

namespace abcd::tools {
namespace {
enum class ModuleFault {
  kPackaging,
  kDescriptorRequired,
  kAttributes,
  kPayload,
  kString
};
}
ModuleCounts& ModuleCounts::operator+=(const ModuleCounts& other) {
  merged_files += other.merged_files;
  bundle_files += other.bundle_files;
  checked_records += other.checked_records;
  modules += other.modules;
  module_requests += other.module_requests;
  module_imports += other.module_imports;
  module_exports += other.module_exports;
  module_strings += other.module_strings;
  failed_module_records += other.failed_module_records;
  return *this;
}
nlohmann::json SerializeCounts(const ModuleCounts& c) {
  return {{"merged_files", c.merged_files},
          {"bundle_files", c.bundle_files},
          {"checked_records", c.checked_records},
          {"modules", c.modules},
          {"module_requests", c.module_requests},
          {"module_imports", c.module_imports},
          {"module_exports", c.module_exports},
          {"module_strings", c.module_strings},
          {"failed_module_records", c.failed_module_records}};
}
ModuleCounts InspectModules(const corpus::AbcFile& file,
                            const abc::Header& header,
                            const abc::MetadataIndex& index,
                            std::ostream& diagnostics,
                            std::optional<std::string_view> bundle_descriptor) {
  ModuleCounts counts;
  const auto fail = [&](ModuleFault kind, abc::EntityOffset offset,
                        int detail) {
    if (counts.failed_module_records++) diagnostics << ',';
    diagnostics << nlohmann::json{{"stage", "modules"},
                                  {"kind", static_cast<int>(kind)},
                                  {"entity_offset", offset.value},
                                  {"detail", detail}};
  };
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  if (const auto* failure = std::get_if<abc::AttributeFailure>(&packaging)) {
    fail(ModuleFault::kPackaging, failure->field,
         static_cast<int>(failure->kind));
    return counts;
  }
  const auto inspect = [&](const abc::AttributeResult& parsed) {
    if (const auto* failure = std::get_if<abc::AttributeFailure>(&parsed)) {
      fail(ModuleFault::kAttributes, failure->field,
           static_cast<int>(failure->kind));
      return;
    }
    const auto& attributes = std::get<abc::RecordAttributes>(parsed);
    // Bundle attributes always carry flag presence; merged runtime publishes
    // only records carrying CommonJS/JSON fields.
    if (!attributes.common_js.has_value() && !attributes.json_content) return;
    bool valid = true;
    const auto string = [&](abc::EntityOffset offset) {
      if (!valid) return;
      const auto value = abc::ReadStringEntity(file, header, offset);
      if (const auto* failure = std::get_if<abc::MetadataError>(&value)) {
        fail(ModuleFault::kString, offset, static_cast<int>(*failure));
        valid = false;
      } else {
        ++counts.module_strings;
      }
    };
    if (attributes.json_content) string(*attributes.json_content);
    if (attributes.module) {
      const auto decoded =
          abc::ReadModuleInfo(file, header, *attributes.module);
      if (const auto* failure = std::get_if<abc::ModuleFailure>(&decoded)) {
        if (valid)
          fail(ModuleFault::kPayload, failure->offset,
               static_cast<int>(failure->kind));
        return;
      }
      const auto& module = std::get<abc::ModuleInfo>(decoded);
      for (const auto offset : module.requests) string(offset);
      for (const auto& entry : module.imports) {
        string(entry.local);
        string(entry.imported);
      }
      for (const auto& entry : module.namespace_imports) string(entry.local);
      for (const auto& entry : module.local_exports) {
        string(entry.local);
        string(entry.exported);
      }
      for (const auto& entry : module.indirect_exports) {
        string(entry.exported);
        string(entry.imported);
      }
      if (!valid) return;
      ++counts.modules;
      counts.module_requests += module.requests.size();
      counts.module_imports +=
          module.imports.size() + module.namespace_imports.size();
      counts.module_exports += module.local_exports.size() +
                               module.indirect_exports.size() +
                               module.star_exports.size();
    }
    if (valid) ++counts.checked_records;
  };
  if (std::get<abc::FilePackaging>(packaging) == abc::FilePackaging::kBundle) {
    ++counts.bundle_files;
    // No module fields means descriptor-independent selection. Otherwise do
    // not silently substitute a host path for the runtime constructor argument.
    if (!bundle_descriptor) {
      for (const auto& record : index.records) {
        if (record.descriptor == "L_ESModuleRecord;" && record.field_count != 0) {
          fail(ModuleFault::kDescriptorRequired, record.offset, 0);
          return counts;
        }
      }
    }
    inspect(abc::ReadBundleRecordAttributes(
        file, header, index, bundle_descriptor.value_or(std::string_view{})));
  } else {
    ++counts.merged_files;
    for (const auto& record : index.records)
      inspect(abc::ReadMergedRecordAttributes(file, header, index, record));
  }
  return counts;
}
}  // namespace abcd::tools
