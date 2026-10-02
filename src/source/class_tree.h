#ifndef ABCD_SOURCE_CLASS_TREE_H_
#define ABCD_SOURCE_CLASS_TREE_H_
#include "prepared_family.h"
#include "class_initializer_file.h"
#include "syntax_resolution.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "abcd/abc/class_buffer.h"
namespace abcd::source {
// One class-creation expression, with optional native static fields whose
// actual invocation is projected by the enclosing tree. Decorators, export
// stores and surrounding captures remain outside. Members keep wire order.
struct ClassStaticFields {
  ClassInitializerCall call;
  FunctionContext context;
  FunctionTree tree;
  abc::EntityOffset record;
};
struct ClassDefinitionTree {
  ClassDefinitionSite site;
  std::array<std::optional<PreparedFamily>, 2> bodies;
};
struct ClassTree {
  RouteInputIdentity identity;
  ClassConstructorSite creation;
  std::u16string name;
  // Constructor context and literal occurrences are resolved before the parent
  // AST. Body preparation fills this same construction-owned object only while
  // no consumers run; publication/printing requires every occurrence below.
  CallableKind callable = CallableKind::kMethod;
  std::vector<analysis::ClassSiteId> member_sites;
  std::optional<ClassInitializerCall> initializer_site;
  std::optional<PreparedFamily> constructor;
  std::vector<PreparedFamily> members;
  std::vector<ClassDefinitionTree> definitions;
  std::optional<ClassStaticFields> initializer;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool BodiesPrepared() const;
};
enum class ClassTreeError : std::uint8_t { kCreation, kMember, kName, kResourceLimit, kWorkLimit };
using ClassTreeFailure = std::variant<ClassTreeError, ClassContextFailure, FamilyPrepareFailure,
    abc::MetadataError, abc::LiteralFailure, abc::ClassBufferError, ClassInitializerReadFailure,
    SyntaxResolutionFailure, EvaluationError, ControlError, FunctionTreeError, ModuleAliasFailure, ModuleAliasTreeError>;
// Resolve the actual creation and member inventory without preparing bodies.
// This construction-only object may enter the parent's AST, but cannot print.
[[nodiscard]] std::variant<ClassTree, ClassTreeFailure> BuildClassLayout(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::ClassMethodSites&, const FileFunction&, analysis::OpId,
    RouteInputIdentity, analysis::SemanticBudget = {},
    const ClassInitializerCall* = nullptr, std::span<const ClassDefinitionSite> = {});
// Fresh unchanged caller and same-file indexes are admitted by the caller.
// Constructor and every member use the shared family pipeline. Refusal of any
// member refuses this creation by default. Explicit AllowPartial may retain a
// supported prefix/issue tree; no old writer or fabricated body substitutes.
// Definition sites are ordered, actual occurrences for this class. The enclosing
// evaluation must independently prove their projection before publication.
[[nodiscard]] std::variant<ClassTree, ClassTreeFailure> BuildClassTree(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const analysis::ClassMethodSites&,
    const FileFunction& creator, analysis::OpId creation, RouteInputIdentity,
    const GlobalScope* globals = nullptr, analysis::SemanticBudget budget = {},
    const ClassInitializerCall* initializer = nullptr,
    std::span<const ClassDefinitionSite> definitions = {},
    RecoveryMode recovery = RecoveryMode::kCompleteOnly, FunctionOwnerId first_owner = {});
struct PrintedClassTree {
  // Each member's positions remain relative to its own byte offset, preserving
  // the existing family source maps without expanding their origin graphs.
  struct Member { std::size_t offset; PrintedFamilyDraft positions; };
  std::vector<Member> members; // Constructor first.
  std::optional<PrintedFunction> initializer;
  std::size_t bytes = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  bool incomplete = false;
};
// The caller supplies the unchanged built tree and discards partial output on failure.
[[nodiscard]] std::variant<PrintedClassTree, TreePrintError, FamilyPrepareFailure> PrintClassTree(
    const ClassTree&, std::ostream&, EmitBudget = {}, analysis::SemanticBudget = {},
    const GlobalScope* globals = nullptr, std::size_t indent = 0);
}
#endif
