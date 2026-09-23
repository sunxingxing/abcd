#ifndef ABCD_SOURCE_RECORD_PRELUDE_H_
#define ABCD_SOURCE_RECORD_PRELUDE_H_

#include <iosfwd>
#include <variant>

#include "abcd/analysis/guarded_global_call.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_function.h"

namespace abcd::source {
enum class RecordPreludeError {
  kProfile,
  kMetadata,
  kRead,
  kFileChanged,
  kGlobalName,
  kPropertyName,
  kFunction,
  kResourceLimit,
  kOutputLimit,
  kWrite
};
using RecordPreludeResult = std::variant<std::size_t, RecordPreludeError>;

// The matcher proves the bytecode shape and dead transient state. This writer
// separately proves textual global references and the fresh function target.
// A caller must stage/atomically publish the enclosing record: a second-pass
// IO or file mutation failure can leave a prefix in this stream.
[[nodiscard]] RecordPreludeResult WriteGuardedGlobalCall(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::MethodInfo& entry, const abc::MethodBody& body,
    const analysis::GuardedGlobalCall& guard,
    const abc::ModuleInfo& module, const analysis::MethodRoleIndex& roles,
    GlobalScope scope, std::ostream& output, LocalLexicalBudget budget = {},
    abc::StringBudget strings = {});
}  // namespace abcd::source
#endif
