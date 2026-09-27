#ifndef ABCD_SOURCE_MODULE_READS_H_
#define ABCD_SOURCE_MODULE_READS_H_

#include "abcd/analysis/value_ir.h"
#include "abcd/abc/module_info.h"

namespace abcd::source {
struct ImportedModuleBinding {
  abc::ModuleRequestId request;
  abc::EntityOffset specifier;
  abc::EntityOffset local;
  abc::EntityOffset imported;
};
struct NamespaceModuleBinding {
  abc::ModuleRequestId request;
  abc::EntityOffset specifier;
  abc::EntityOffset local;
};
struct LocalModuleBinding { abc::EntityOffset local; };
using ModuleReadTarget = std::variant<ImportedModuleBinding,
    NamespaceModuleBinding, LocalModuleBinding>;
struct ModuleReadBinding {
  analysis::OpId operation;
  // Space+slot is the runtime identity. Equal local-name entities in different
  // nonadjacent groups must never coalesce these bindings.
  analysis::ValueModuleRead read;
  ModuleReadTarget target;
};
struct ModuleReads {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ModuleReadBinding> bindings; // Sorted by operation, not by name.
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class ModuleReadError : std::uint8_t {
  kMissingModule, kSlot, kRequest, kResourceLimit, kWorkLimit
};
struct ModuleReadFailure {
  std::variant<ModuleReadError, abc::MetadataError> reason;
  std::optional<abc::CodeOffset> pc;
};
// Same-file decoded module and verified unchanged working IR are prerequisites.
// Resolves metadata identity only: no JS identifier, initialization, TDZ or
// dependency-evaluation proof. In particular, never remove a Hole guard here.
// Names remain file references. Adjacent equal local names share one VM slot;
// equal nonadjacent names do not. Supplied module/graph storage is caller-owned.
[[nodiscard]] std::variant<ModuleReads, ModuleReadFailure> ResolveModuleReads(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::ModuleInfo* module, const analysis::ValueProgram& graph,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
