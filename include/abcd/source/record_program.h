#ifndef ABCD_SOURCE_RECORD_PROGRAM_H_
#define ABCD_SOURCE_RECORD_PROGRAM_H_
#include "abcd/source/tree_print_failure.h"

#include "abcd/abc/header.h"
#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/emission.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
enum class RecordProgramError : unsigned {
  kProfile = 0,
  kEntry = 1,
  kModule = 2,
  kBody = 3,
  kFrame = 4,
  kInstruction = 5,
  kValue = 6,
  kClass = 7,
  kExport = 8,
  kResourceLimit = 9,
  kOutputLimit = 10,
  kRead = 11,
  kWrite = 12,
  kFileChanged = 13,
  kFunction = 14,
  kInitializer = 15,
  kAccessor = 16,
  kObject = 17,
  // Process allocation failure (bad_alloc/length_error) caught at the record
  // entry. Never isolated to one record: the export stops.
  kAllocationFailure = 18,
  // The record exceeded its own RecordProgramBudget, proven at a record-owned
  // budget check. Isolated to this record; other records are still exported.
  // Lower stages may also fold real allocation failures into kResourceLimit;
  // exporters decide those from the out-of-band allocation signal.
  kRecordBudget = 19,
  // A whole-file source index (method roles, function/class creation sites,
  // source path budget) could not be built; every record of the file reports
  // it with the typed causes. The structural export still completes.
  kFileIndex = 20,
  // This record's own source path could not be planned (identity evidence,
  // encoding limit or case-folded collision); other records are unaffected.
  kSourcePath = 21
};
enum class RecordModuleKindError : std::uint8_t { kCommonJs, kShared, kTopLevelAwait, kJson };
enum class RecordModuleProofError : std::uint8_t {
  kOwner, kAliasBinding, kReadInventory, kSymbolRange, kSymbolBinding
};
struct RecordProgramFailure {
  RecordProgramError kind;
  std::optional<abc::CodeOffset> pc;
  // Typed causes from the stage that produced this record failure (S2).
  FailureCauseChain causes = {};
  std::optional<CallReferenceEvidence> call_reference;
  std::optional<abc::EntityOffset> method;
  std::optional<abc::CodeOffset> working_pc;
  std::optional<TreeFailureEvidence> tree;
  std::optional<EvaluationEvidence> evaluation;
  std::optional<DeferredCellEvidence> deferred;
  std::optional<TreePrintFailure> printing;
};
struct RecordProgramBudget {
  std::size_t bytes = 128 * 1024 * 1024;
  std::size_t export_bytes = 16 * 1024 * 1024;
  EmitBudget output;
  abc::StringBudget strings;
};
// Record-level admission for the value-tree route, not blanket source
// admission for every instruction or method in an admitted profile.
[[nodiscard]] bool SupportsRecordProgramProfile(abc::ProfileId profile);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_RECORD_PROGRAM_H_
