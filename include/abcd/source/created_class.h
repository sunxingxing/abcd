#ifndef ABCD_SOURCE_CREATED_CLASS_H_
#define ABCD_SOURCE_CREATED_CLASS_H_
#include <optional>
#include <span>
#include <variant>

#include "abcd/abc/class_buffer.h"
#include "abcd/analysis/class_heritage.h"
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
struct ImportedClassParent {
  std::size_t snapshot = 0;
};
struct GlobalClassParent {
  std::size_t snapshot = 0;
};
using ClassParent = std::variant<ImportedClassParent, GlobalClassParent>;
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
  kAccessorTarget
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
                 abc::LiteralFailure, abc::ClassBufferError>;
// Same unchanged indexed file/lookup and pinned 7.0 semantics as
// WriteCreatedFunction. Caller supplies heritage proven from this caller body
// and matching site; matching PC alone is not provenance authentication.
// Resolves real constructor, buffer, name, ABI and body. Emits a fresh complete
// class expression for a proven no-extends class, or a pinned v7 derived class
// whose caller owns an import/global snapshot and whose length-zero constructor
// passes ProveForwardingDerivedConstructor. This API does not prove parent
// identity on its own; the record interpreter supplies that provenance.
// Nonempty buffers require a whole-file role index from the same immutable
// context, ordinary methods, matching runtime names, and nonreserved unique
// keys per placement. Unsupported members reject the entire class.
// Parent/control flow and later field/member assignments remain caller work.
// Other derived constructors and outer captures are not supported. Internal class
// name supplies an immutable self binding without consulting mutable exports.
// Retained target/buffer and scratch share budget.bytes; input indexes, strings
// and output are separately admitted. Preflight rejects semantic/budget errors
// without output. IO/file mutation/allocation failure require atomic
// publication.
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
    std::optional<ClassParent> parent = {});
}  // namespace abcd::source
#endif
