#ifndef ABCD_SOURCE_FAMILY_FILE_H_
#define ABCD_SOURCE_FAMILY_FILE_H_

#include "capture_evaluation.h"
#include "creation_tree_context.h"
#include "function_family.h"
#include "function_file.h"
#include "family_function.h"
#include "phased_creations.h"
#include "object_buffer.h"

namespace abcd::source {
struct FamilyCreation {
  FamilyId parent;
  analysis::InstructionId site;
  analysis::CreationBinding creation;
  CaptureCells cells;
  std::optional<analysis::OpId> phase_operation;
};
struct FamilyObject {
  analysis::InstructionId site;
  analysis::BufferedObjectCreation creation;
  // Primitive-only literal evidence, not permission to erase allocation or
  // its input barrier. AST construction must still account for this operation.
  ObjectBuffer buffer;
};
// An original class barrier delegated to the class tree, not a discharged
// closure edge. Normal family publication still rejects these obligations.
struct FamilyClassObligation {
  analysis::InstructionId site;
  analysis::OpId operation;
  analysis::OpId creation;
};
enum class FamilyClassMode : std::uint8_t { kReject, kRetainRoot };
struct FileFamilyMember {
  FamilyFunction function;
  // The root context is owned by PreparedFamily; input retains its occurrence
  // stamp when supplied. Children own their actual occurrence context here.
  std::optional<FunctionContext> context;
  std::optional<FamilyCreation> incoming;
  std::vector<FunctionBinding> children;
  std::vector<FamilyObject> objects;
  std::optional<PhasedCreations> phase_creations;
  std::optional<ClassProjection> class_projection;
  std::vector<FamilyClassObligation> classes;
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
  kMethod, kCreation, kOccurrence, kRecursiveCreation, kResourceLimit, kWorkLimit, kUnsupportedFlow
};
struct FamilyFileFailure {
  std::variant<FamilyFileError, FunctionFileFailure, CreationContextFailure,
      CaptureCellError, CaptureEvaluationFailure, BodyEmitFailure, PhasedFileFailure, PhasedCreationFailure,
      ClassInitializerError> cause;
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
// Optional root owner is checked and stamped before input verification. Each
// child's context is resolved before its input. Ownerless diagnostic callers
// do not gain initialized receiver or publication permissions.
// Phase roots require an actual owner; phase descendants retain their own
// original/working owners. Descendant storage demand forwards through members
// with no direct lexical access, but unresolved incoming root cells are refused.
// Result remains conditional: no root enclosure, transitive-write certificate,
// complete AST, source text or executable receipt is produced here.
[[nodiscard]] std::variant<FileFamily, FamilyFileFailure> ReadFunctionFamily(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    abc::MethodPosition root, RouteInputIdentity identity,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    const FunctionContext* owner = nullptr, ModuleOwnerId module_owner = {},
    FamilyClassMode classes = FamilyClassMode::kReject);
}  // namespace abcd::source
#endif
