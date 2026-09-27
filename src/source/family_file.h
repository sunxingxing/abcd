#ifndef ABCD_SOURCE_FAMILY_FILE_H_
#define ABCD_SOURCE_FAMILY_FILE_H_

#include "capture_evaluation.h"
#include "creation_tree_context.h"
#include "function_family.h"
#include "function_file.h"

namespace abcd::source {
struct FamilyCreation {
  FamilyId parent;
  analysis::InstructionId site;
  analysis::CreationBinding creation;
  CaptureCells cells;
};
struct FileFamilyMember {
  FileFunction function;
  // Root enclosure/callable context is deliberately absent. Every child has
  // an actual occurrence-bound context, not a target-method guess.
  std::optional<CreationTreeContext> context;
  std::optional<FamilyCreation> incoming;
  std::vector<FunctionBinding> children;
};
struct FileFamily {
  // Preorder, root at kRoot. Occurrence owners equal these stable member IDs;
  // identical method entities at distinct sites still own distinct syntax.
  static constexpr FamilyId kRoot{0};
  std::vector<FileFamilyMember> members;
  std::size_t allocated_bytes = 0;
  // Discovery/binding work only. ReadFunctionValues still has per-pass caps;
  // this is NOT a cumulative cost for all upstream method analysis passes.
  std::uint64_t work = 0;
};
enum class FamilyFileError : std::uint8_t {
  kMethod, kCreation, kOccurrence, kRecursiveCreation, kResourceLimit, kWorkLimit
};
struct FamilyFileFailure {
  std::variant<FamilyFileError, FunctionFileFailure, CreationContextFailure,
      CaptureCellError, CaptureEvaluationFailure> cause;
  std::optional<abc::EntityOffset> method;
  std::optional<abc::EntityOffset> parent;
  std::optional<abc::CodeOffset> creation_pc;
};
// Reads the complete supported creation family, using an explicit DFS stack
// and active-method index. No C++ recursion, per-edge metadata scan, guessed
// child, ignored failed body, or sharing of mutable state between occurrences.
// A template cycle is a typed refusal pending shared-template/SCC lowering;
// runtime recursion through captured values is not such a cycle.
// Same unchanged file/header/index/lookup/roles/sites/context are caller-owned
// and admitted. Every retained member and the next pass's scratch share budget.
// Result remains conditional: no root enclosure, transitive-write certificate,
// complete AST, source text or executable receipt is produced here.
[[nodiscard]] std::variant<FileFamily, FamilyFileFailure> ReadFunctionFamily(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    abc::MethodPosition root, RouteInputIdentity identity,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr);
}  // namespace abcd::source
#endif
