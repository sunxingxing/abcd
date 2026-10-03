#ifndef ABCD_SOURCE_FAMILY_FILE_H_
#define ABCD_SOURCE_FAMILY_FILE_H_

#include "capture_evaluation.h"
#include "creation_tree_context.h"
#include "function_family.h"
#include "function_file.h"
#include "family_function.h"
#include "phased_creations.h"
#include "object_buffer.h"
#include "record_array.h"
#include "class_initializer_file.h"

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
struct FamilyArray {
  analysis::InstructionId site;
  analysis::BufferedArrayCreation creation;
  // Retain primitive-only evidence without removing the allocation/barrier.
  BufferedArray buffer;
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
  std::vector<FamilyArray> arrays;
};
struct FileFamily {
  // Preorder, root at kRoot. FamilyId indexes occurrences; syntax owners live
  // in a caller-reserved contiguous namespace, never a method-entity namespace.
  static constexpr FamilyId kRoot{0};
  std::vector<FileFamilyMember> members;
  std::size_t allocated_bytes = 0;
  // Discovery/binding work only. ReadFunctionValues still has per-pass caps;
  // this is NOT a cumulative cost for all upstream method analysis passes.
  std::uint64_t work = 0;
  // External class/function creator is borrowed through immediate tree/write
  // verification only. PreparedFamily never retains this reference.
  std::optional<ParentModuleInput> root_modules;
  FunctionOwnerId first_owner{};
  // External root binding owns its cell evidence. It is not an intra-family
  // FunctionExpr edge and does not discharge source visibility or write checks.
  std::optional<CaptureCells> root_capture;
  // Reader/tree/write entry points verify the complete range before mapping.
  [[nodiscard]] FunctionOwnerId Owner(FamilyId member) const {
    return {first_owner.value + member.value};
  }
};
enum class FamilyFileError : std::uint8_t {
  kMethod, kCreation, kOccurrence, kRecursiveCreation, kResourceLimit, kWorkLimit, kUnsupportedFlow
};
struct FamilyFileFailure {
  std::variant<FamilyFileError, FunctionFileFailure, CreationContextFailure,
      CaptureCellError, CaptureEvaluationFailure, BodyEmitFailure, PhasedFileFailure, PhasedCreationFailure,
      ClassInitializerError, BufferedArrayError, abc::MetadataError, abc::LiteralFailure,
      ClassInitializerReadFailure, ClassSelfError, ClassSelfOrderError> cause;
  std::optional<abc::EntityOffset> method;
  std::optional<abc::EntityOffset> parent;
  std::optional<abc::CodeOffset> creation_pc;
  // Requested root, retained while method/parent follow actual DFS occurrences.
  // This binds a descendant input refusal to its containing family; it grants
  // neither a child body nor permission to ignore other failure categories.
  std::optional<abc::EntityOffset> root;
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
// with no direct lexical access. A class root can bind those cells only when
// its genuine normal root_creator is supplied; absent/outward owners refuse.
// Root capture evidence is owned, not a fictional intra-family creation edge.
// Tree/write consumers still require external AST and visibility integration.
// Result remains conditional: no root enclosure, transitive-write certificate,
// complete AST, source text or executable receipt is produced here.
[[nodiscard]] std::variant<FileFamily, FamilyFileFailure> ReadFunctionFamily(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    abc::MethodPosition root, RouteInputIdentity identity,
    analysis::SemanticBudget budget = {}, const RecordModuleContext* modules = nullptr,
    const FunctionContext* owner = nullptr, ModuleOwnerId module_owner = {},
    FamilyClassMode classes = FamilyClassMode::kReject,
    const ParentModuleInput* root_modules = nullptr, FunctionOwnerId first_owner = {},
    const FileFunction* root_creator = nullptr);
}  // namespace abcd::source
#endif
