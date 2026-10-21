#ifndef ABCD_SOURCE_CLASS_TREE_H_
#define ABCD_SOURCE_CLASS_TREE_H_
#include "prepared_family.h"
#include "class_initializer_file.h"
#include "syntax_resolution.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "abcd/abc/class_buffer.h"
#include "abcd/source/sendable_failure.h"
namespace abcd::source {
struct ClassTree;
// One class-creation expression, with optional native static fields whose
// actual invocation is projected by the enclosing tree. Decorators, export
// stores and surrounding captures remain outside. Members keep wire order.
struct ClassStaticFields {
  ClassInitializerCall call;
  FunctionContext context;
  FunctionTree tree;
  abc::EntityOffset record;
  TreeFlow flow;
};
// Actual unpublished syntax, not another raw-analysis diagnostic. In particular
// ordinary captured cells remain unresolved requirements, never fake aliases.
struct ClassSelfClassDraft {
  ClassInitializerTreeDraft initializer;
  ClassSelfOrder order;
  EvaluationPlan evaluation;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
struct ClassSelfOuterBinding {
  IncomingCell storage;
  analysis::CaptureDomain domain;
  CapturedSymbol target;
};
std::variant<ClassSelfClassDraft, ClassSelfInitializationFailure>
BuildClassSelfClassDraft(const ClassSelfInitializationInput&,
                         const ClassSelfInitialization&, const ClassSelfScope&,
                         ClassTree&, FileFunction& creator,
                         analysis::SemanticBudget = {},
                         const RecordModuleContext* = nullptr);
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfClassDraft(const ClassSelfInitializationInput&,
                          const ClassSelfInitialization&, const ClassSelfScope&,
                          const ClassTree&, const EvaluationPlan&,
                          const ClassInitializerTreeDraft&,
                          analysis::SemanticBudget = {},
                          std::span<const ClassSelfOuterBinding> = {},
                          std::optional<FunctionOwnerId> = {});
// Original input failure at a real root or descendant creation occurrence.
// The entire containing class member is omitted; a nested callback never gets
// a fabricated FunctionTree/body/signature or source receipt.
struct ClassMemberOmission {
  ClassTreeContext context;
  std::variant<analysis::SemanticProofFailure, analysis::ExceptionInputFailure, PhasedControlFailure> failure;
  abc::EntityOffset root;
  abc::EntityOffset method;
  std::optional<abc::EntityOffset> parent;
  std::optional<abc::CodeOffset> creation_pc;
  std::optional<abc::CodeOffset> working_pc;
  [[nodiscard]] abc::CodeOffset Pc() const {
    return std::visit([](const auto& proof) -> abc::CodeOffset {
      if constexpr (std::is_same_v<std::decay_t<decltype(proof)>, PhasedControlFailure>) return proof.pc.value_or(proof.input_gap->cause.pc);
      else return proof.pc;
    }, failure);
  }
  [[nodiscard]] bool InputGap() const {
    if (root != context.Target() || parent.has_value() != creation_pc.has_value() ||
        (!parent && method != root)) return false;
    return std::visit([](const auto& proof) {
      if constexpr (std::is_same_v<std::decay_t<decltype(proof)>, PhasedControlFailure>) {
        const auto* region = std::get_if<ExceptionControlRegionFailure>(&proof.cause);
        return proof.kind == PhasedMethodError::kControl && region &&
            region->kind == ExceptionControlError::kMultipleExits && region->exception_region &&
            proof.input_gap && proof.input_gap->cause.kind == analysis::ExceptionInputError::kInternalValue &&
            proof.input_gap->cause.node && proof.input_gap->cause.value;
      } else if constexpr (std::is_same_v<std::decay_t<decltype(proof)>, analysis::ExceptionInputFailure>)
        return proof.kind == analysis::ExceptionInputError::kInternalValue && proof.node && proof.value;
      else return proof.kind == analysis::SemanticProofError::kInternalValue;
    }, failure);
  }
};
struct ClassDefinitionTree {
  ClassDefinitionSite site;
  std::array<std::optional<PreparedFamily>, 2> bodies;
  std::array<std::optional<ClassMemberOmission>, 2> omissions;
};
struct SendableTypeSpelling {
  std::uint32_t field;
  ExprId assignment, constructor;
  SymbolId imported;
  ModuleSymbol binding;
  std::uint32_t depth;
  bool operator==(const SendableTypeSpelling&) const = default;
};
struct ClassTreeInitialization {
  std::shared_ptr<const ClassInnerScopeProof> certificate;
  ClassInnerBinding binding;
  std::vector<ClassSelfPort> ports;
  std::vector<ClassSelfCleanup> cleanup;
  std::optional<analysis::DispatchTarget> exception;
  FunctionOwnerId first_owner;
  std::uint64_t owner_end;
  // Lexical destinations of ordinary initializer captures. These are checked
  // at THIS ClassExpr's source scope, never copied or marked initialized.
  std::vector<CapturedSymbol> outer;
};
// Original installation identities, sealed by the combined private corridor.
// The creator verifier recomputes this inventory from the unchanged graph.
struct ClassInstallationOrder {
  ClassInitializerCall initializer;
  std::vector<analysis::OpId> definitions;
  bool operator==(const ClassInstallationOrder&) const = default;
};
struct ClassTree {
  std::optional<ClassInstallationOrder> installation_order;
  std::optional<ClassTreeInitialization> class_self;
  RouteInputIdentity identity;
  ClassConstructorSite creation;
  analysis::OpId operation{}; // Actual creation in the unchanged owner graph.
  std::u16string name;
  // Constructor context and literal occurrences are resolved before the parent
  // AST. Body preparation fills this same construction-owned object only while
  // no consumers run; publication/printing requires every occurrence below.
  CallableKind callable = CallableKind::kMethod;
  std::shared_ptr<const analysis::IndexedSendableLiteral> sendable;
  std::vector<analysis::ClassSiteId> member_sites;
  std::vector<SendableTypeSpelling> sendable_types;
  std::optional<ClassInitializerCall> initializer_site;
  std::optional<PreparedFamily> constructor;
  std::vector<std::optional<PreparedFamily>> members;
  std::vector<ClassMemberOmission> omissions; // Ordered by original property.
  std::vector<ClassDefinitionTree> definitions;
  std::optional<ClassStaticFields> initializer;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool BodiesPrepared() const;
  [[nodiscard]] bool DraftPrepared() const;
};
enum class ClassTreeError : std::uint8_t { kCreation, kMember, kName, kResourceLimit, kWorkLimit };
struct ClassParentScope { bool independent; std::uint64_t work; };
// A prepared class with no parent cells/modules can retain its exact creation
// in a phased parent without borrowing normal-SSA capture identities. Static
// initializer/defined-member projection needs its own phase bridge.
[[nodiscard]] std::variant<ClassParentScope, ClassTreeError> CheckClassParentScope(
    const ClassTree&, std::uint64_t work = analysis::kDefaultSemanticWork);
using ClassTreeFailure = std::variant<SendableFailure, ClassTreeError, ClassContextFailure, FamilyPrepareFailure,
    abc::MetadataError, abc::LiteralFailure, abc::ClassBufferError, ClassInitializerReadFailure,
    SyntaxResolutionFailure, EvaluationFailure, ControlError, ControlFailure, FunctionTreeFailure, ModuleAliasFailure, ModuleAliasTreeError, ArrayInitializationError>;
// Resolve the actual creation and member inventory without preparing bodies.
// This construction-only object may enter the parent's AST, but cannot print.
[[nodiscard]] std::variant<ClassTree, ClassTreeFailure> BuildClassLayout(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::ClassMethodSites&, const FileFunction&, analysis::OpId,
    RouteInputIdentity, analysis::SemanticBudget = {},
    const ClassInitializerCall* = nullptr, std::span<const ClassDefinitionSite> = {},
    const ClassProjection* shared_projection = nullptr);
// Bodies' residual inputs are prepared in the original emission order. No
// syntax owners/names are allocated here; member count reserves the same range.
struct ClassPreparation {
  ClassTree layout;
  std::vector<std::variant<FamilyPreparation, FamilyPrepareFailure>> bodies;
  RecoveryMode recovery = RecoveryMode::kCompleteOnly;
  std::uint64_t next_owner = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
[[nodiscard]] std::variant<ClassPreparation, ClassTreeFailure> PrepareClassTree(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const analysis::ClassMethodSites&,
    const FileFunction&, analysis::OpId, RouteInputIdentity, const GlobalScope*,
    analysis::SemanticBudget, const ClassInitializerCall*, std::span<const ClassDefinitionSite>,
    FunctionOwnerId, RecoveryMode, const CaptureStorage* creator_storage = nullptr,
    ClassRootBinding root_binding = ClassRootBinding::kBind,
    const ClassProjection* shared_projection = nullptr);
// Fresh unchanged caller and same-file indexes are admitted by the caller.
// Constructor and every member use the shared family pipeline. Refusal of any
// member refuses this creation by default. Explicit AllowPartial may retain a
// supported prefix/issue tree; no old writer or fabricated body substitutes.
// Definition sites are actual occurrences for this class. Their common CFG
// positions order native members, independently of physical bytecode layout. The enclosing
// evaluation must independently prove their projection before publication.
[[nodiscard]] std::variant<ClassTree, ClassTreeFailure> BuildClassTree(
    const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
    const abc::MethodLookup&, const analysis::MethodRoleIndex&,
    const analysis::FunctionCreationSites&, const analysis::ClassMethodSites&,
    const FileFunction& creator, analysis::OpId creation, RouteInputIdentity,
    const GlobalScope* globals = nullptr, analysis::SemanticBudget budget = {},
    const ClassInitializerCall* initializer = nullptr,
    std::span<const ClassDefinitionSite> definitions = {},
    RecoveryMode recovery = RecoveryMode::kCompleteOnly,
    FunctionOwnerId first_owner = {}, const CaptureTree* parent = nullptr,
    ClassPreparation* preparation = nullptr,
    const CaptureStorage* creator_storage = nullptr,
    ClassStaticFields* certified_initializer = nullptr);
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
  bool arkts = false;
};
// The caller supplies the unchanged built tree and discards partial output on failure.
[[nodiscard]] std::variant<PrintedClassTree, TreePrintFailure, FamilyPrepareFailure> PrintClassTree(
    const ClassTree&, std::ostream&, EmitBudget = {}, analysis::SemanticBudget = {},
    const GlobalScope* globals = nullptr, std::size_t indent = 0);
}
#endif
