#ifndef ABCD_SOURCE_RECORD_MODULE_CONTEXT_H_
#define ABCD_SOURCE_RECORD_MODULE_CONTEXT_H_

#include <memory>
#include <utility>
#include <variant>

#include "abcd/abc/module_info.h"
#include "abcd/abc/record_attributes.h"

namespace abcd::source {

enum class RecordModuleError {
  kBundleOwnership,
  kCommonJs,
  kResourceLimit
};
struct RecordModuleFailure {
  std::variant<RecordModuleError, abc::AttributeFailure, abc::ModuleFailure>
      reason;
};
struct RecordModuleBudget {
  abc::StringBudget names;
  abc::ModuleBudget module;
};

// Shares the module table for one record. The returned handle keeps that table
// alive across deferred work, but the borrowed AbcFile still needs an owner in
// the task. File identity and indexed method owner must both match.
class RecordModuleContext {
 public:
  abc::EntityOffset record() const { return record_; }
  std::shared_ptr<const abc::ModuleInfo> ForOwner(
      const corpus::AbcFile& file, abc::EntityOffset owner) const {
    return &file == file_ && owner == record_ ? module_ : nullptr;
  }

 private:
  friend std::variant<RecordModuleContext, RecordModuleFailure>
  ReadRecordModuleContext(const corpus::AbcFile&, const abc::Header&,
                          const abc::MetadataIndex&, const abc::RecordInfo&,
                          abc::FilePackaging, RecordModuleBudget);
  RecordModuleContext(const corpus::AbcFile& file, abc::EntityOffset record,
                      std::shared_ptr<const abc::ModuleInfo> module)
      : file_(&file), record_(record), module_(std::move(module)) {}
  const corpus::AbcFile* file_;
  abc::EntityOffset record_;
  std::shared_ptr<const abc::ModuleInfo> module_;
};
using RecordModuleResult =
    std::variant<RecordModuleContext, RecordModuleFailure>;

// Merged records own their module fields. Bundle module ownership requires a
// runtime descriptor and is deliberately rejected until that proof is carried
// by this API. The caller reserves both budgets within the process cap.
[[nodiscard]] RecordModuleResult ReadRecordModuleContext(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    abc::FilePackaging packaging, RecordModuleBudget budget = {});

}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_MODULE_CONTEXT_H_
