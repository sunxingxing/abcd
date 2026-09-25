#ifndef ABCD_SOURCE_CREATED_CLASS_H_
#define ABCD_SOURCE_CREATED_CLASS_H_
#include <optional>
#include <span>
#include <variant>

#include "abcd/abc/class_buffer.h"
#include "abcd/analysis/class_heritage.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/created_function.h"
#include "abcd/source/static_fields.h"

namespace abcd::source {
struct ClassAccessor {
  std::u16string key;
  abc::MethodPosition method;
  abc::ClassPlacement placement;
  abc::ClassMethodKind kind;
  std::uint16_t length = 0;
};
struct ClassAttachedMethod {
  std::u16string key;
  abc::MethodPosition method;
  abc::ClassPlacement placement;
  std::uint16_t length = 0;
};
// RuntimeSuperCallSpread re-reads Derived.[[Prototype]] on every invocation.
// Creation-time brand is insufficient: callers must certify this invariant
// for the entire lifetime of the emitted class, including external mutation.
enum class ParentRuntimeInvariant { kUnproven, kAlwaysJsFunction };
struct ImportedClassParent {
  std::size_t snapshot = 0;
  ParentRuntimeInvariant runtime = ParentRuntimeInvariant::kUnproven;
};
struct GlobalClassParent {
  std::size_t snapshot = 0;
  ParentRuntimeInvariant runtime = ParentRuntimeInvariant::kUnproven;
};
// Owner-proven initialized local snapshot at this exact creation site, not a
// source expression. The binding must outlive synchronous class emission.
// Inner class/IIFE binding collisions are rejected by the emitter.
struct BoundClassParent {
  std::string_view binding;
  ParentRuntimeInvariant runtime = ParentRuntimeInvariant::kUnproven;
};
// Caller-proven source identifier read at class creation. Unlike a local
// snapshot it may throw; the owner proves original order and non-shadowing.
struct NamedClassParent {
  std::string_view binding;
  ParentRuntimeInvariant runtime = ParentRuntimeInvariant::kUnproven;
};
using ClassParent =
    std::variant<ImportedClassParent, GlobalClassParent, BoundClassParent,
                 NamedClassParent>;
enum class CreatedClassError {
  kHeritage,
  kMembers,
  kMissingCode,
  kName,
  kResourceLimit,
  kModuleOwner,
  kStaticFieldOrder,
  kStaticFieldValue,
  kAccessorOrder,
  kAccessorTarget,
  kRecursiveExpansion
};
using CreatedClassResult =
    std::variant<std::size_t, CreatedClassError, FunctionEmitError, EmitError,
                 BodyEmitFailure, analysis::FlowFailure,
                 analysis::EnvironmentFlowFailure,
                 analysis::LinearLexicalFailure,
                 analysis::AccumulatorFailure, AcyclicError,
                 analysis::CreationError,
                 abc::MethodTargetError, abc::MetadataError,
                 abc::MethodAttributeError, abc::FrameError, abc::BodyFailure,
                 abc::LiteralFailure, abc::ClassBufferError, RouteRefusal>;
// Same unchanged indexed file/lookup and pinned 7.0 semantics as
// WriteCreatedFunction. Caller supplies heritage proven from this caller body
// and matching site; matching PC alone is not provenance authentication.
// Resolves real constructor, buffer, name, ABI and body. Emits a fresh complete
// class expression for a proven no-extends class, or a pinned v7 derived class
// whose caller owns an import/global snapshot and whose constructor passes
// the pure forwarding proof, the exact rest-spread/string-field proof, or a
// single range-super/guard SSA proof. The spread form additionally requires
// a trusted lifetime invariant that the current parent remains
// JSFunction-branded at every construction:
// import/global provenance and creation-time brand are both insufficient.
// v7 SuperCallSpread rejects constructible Proxy parents while native
// `super(...rest)` accepts them. This API does not prove parent
// identity on its own; the record interpreter supplies that provenance.
// Nonempty buffers require a whole-file role index from the same immutable
// context, ordinary methods, matching runtime names, and nonreserved unique
// keys per placement. Unsupported members reject the entire class.
// Parent/control flow and later field/member assignments remain caller work;
// attached methods require caller proof of the exact DEFINE/own-write chain.
// Other derived constructors and outer captures are not supported. Internal class
// name supplies an immutable self binding without consulting mutable exports.
// Retained target/buffer and scratch share budget.bytes; input indexes, strings
// and output are separately admitted. A private class template separates body
// preparation from publication, preserving typed captures and PC positions.
// Preparation failures publish nothing; IO/file mutation/allocation and final
// creation-site capture resolution failures require atomic publication.
// A supplied module belongs to the caller's record. Constructors and members
// in other records are rejected before receiving its import environment.
// Optional enclosing_scope certifies every outer source binding, sorted by
// name; absent proof rejects global free-identifier recovery. The class writer
// adds its own generated inner bindings to this check.
[[nodiscard]] CreatedClassResult WriteCreatedClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::BodyInstruction& instruction,
    analysis::ClassHeritage heritage, std::ostream& output,
    LocalLexicalBudget budget = {}, abc::StringBudget strings = {},
    const analysis::MethodRoleIndex* roles = nullptr,
    const abc::ModuleInfo* module = nullptr,
    const GlobalScope* enclosing_scope = nullptr,
    const StaticFields* static_fields = nullptr,
    std::span<const ClassAccessor> accessors = {},
    std::optional<ClassParent> parent = {},
    std::span<const ClassAttachedMethod> attached_methods = {});
// Typed counterpart for unchanged semantic IR. Decoding/frame validation has
// already happened; origin and heritage belong to this exact creation site.
// Pass the owning synchronous expansion context for nested class emission.
// A null context starts a bounded root; no asynchronous method tasks are used.
[[nodiscard]] CreatedClassResult WriteCreatedClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::ClassCreation& creation,
    abc::CodeOffset origin, analysis::ClassHeritage heritage,
    std::ostream& output, LocalLexicalBudget budget = {},
    abc::StringBudget strings = {},
    const analysis::MethodRoleIndex* roles = nullptr,
    const abc::ModuleInfo* module = nullptr,
    const GlobalScope* enclosing_scope = nullptr,
    const StaticFields* static_fields = nullptr,
    std::span<const ClassAccessor> accessors = {},
    std::optional<ClassParent> parent = {},
    std::span<const ClassAttachedMethod> attached_methods = {},
    CreationExpansion* expansion = nullptr);
}  // namespace abcd::source
#endif
