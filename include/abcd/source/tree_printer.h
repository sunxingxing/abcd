#ifndef ABCD_SOURCE_TREE_PRINTER_H_
#define ABCD_SOURCE_TREE_PRINTER_H_

#include <iosfwd>

#include "abcd/source/emission.h"
#include "abcd/source/global_reference.h"
#include "abcd/source/verified_function.h"

namespace abcd::source {
struct GeneratedRange {
  std::size_t begin = 0;
  std::size_t end = 0;
  auto operator<=>(const GeneratedRange&) const = default;
};
enum class FunctionFragment : std::uint8_t { kClassMember, kConditionalExpression, kFunctionExpression, kModuleBody, kStaticFields };
enum class TreePrintError : std::uint8_t {
  kUnsupportedContext, kOutputLimit, kWrite, kResourceLimit, kWorkLimit,
  kInvalidLiteral
};
struct PrintedFunction {
  struct RealmBinding {
    RealmBindingPolicy policy;
    std::u16string name;
  };
  struct SymbolBinding { std::u16string name; };
  struct ArgumentSnapshotBinding { std::u16string name; ArgumentSnapshotAbi abi; };
  struct ModuleBinding { abc::EntityOffset record; ModuleOwnerId owner; };
  RouteInputIdentity identity;
  abc::EntityOffset method;
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  FunctionFragment fragment = FunctionFragment::kClassMember;
  std::size_t bytes = 0;
  // Nested class members retain their own identities and local ranges. Add
  // enclosing_offset to locate them in this fragment; no origin DAG expansion.
  std::size_t enclosing_offset = 0;
  std::vector<PrintedFunction> class_members;
  // Recovery completeness only, not a claim of behavioral verification.
  bool incomplete = false;
  // Formal ordinal indexes complete parameter syntax (name and initializer),
  // excluding separators. Signature migration maps to these events, not to a
  // body-wide range or a search for matching generated text.
  std::vector<GeneratedRange> parameters;
  // Indexed by the same immutable tree's ExprId/PropertyId/StmtId/AnchorId. Combine these
  // event ranges with its origin DAG, never reparse JS to recover provenance.
  // Ranges are half-open UTF-8 byte coordinates relative to this fragment;
  // conditional family drafts instead use the enclosing draft's coordinates.
  std::vector<GeneratedRange> expressions;
  std::vector<GeneratedRange> properties;
  std::vector<GeneratedRange> statements;
  std::vector<std::size_t> anchors;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Owned publication requirement; neither an ABC-derived fact nor transferable
  // permission to print the same tree inside a different lexical enclosure.
  std::optional<RealmBinding> realm_binding;
  std::optional<SymbolBinding> symbol_binding;
  std::optional<ArgumentSnapshotBinding> argument_snapshot_binding;
  std::optional<ModuleBinding> module_binding;
};
// A class-body fragment from a straight-line initializer. Native module imports
// require an exact record destination and are checked before writing. Every root
// must be a receiver field definition, followed by an undefined return. The
// caller separately establishes the actual class receiver and placement.
[[nodiscard]] std::variant<PrintedFunction, TreePrintError> PrintStaticFields(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {}, std::size_t indent = 0,
    const GlobalScope* scope = nullptr, std::optional<abc::EntityOffset> record = {});

// Direct syntax output, including marked partial trees. No CFG/SSA or publication
// owner is required. Structural safety is checked before writing; callers retain
// ownership and must not mutate the tree during printing. Standalone function/
// arrow expressions have no child lookup; use the shared family entry for those.
[[nodiscard]] std::variant<PrintedFunction, TreePrintError> PrintFunctionTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr);

// Output context for an otherwise unchanged initializer AST. Only the record
// adapter may select this after proving a sole anonymous default export. The
// printer rechecks the binding's declaration and transitive lack of uses before
// suppressing its source name; bytecode origins and initializer order stay put.
struct ModuleDefaultExport { StmtId declaration; ModuleSymbol binding; };

// Same AST printer for a module initializer. No callable envelope is emitted;
// only a final top-level return is allowed and its value is evaluated/discarded.
// The record adapter owns entry identity, declarations, imports and exports.
[[nodiscard]] std::variant<PrintedFunction, TreePrintError> PrintModuleTree(
    const FunctionTree& tree, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr,
    std::optional<ModuleDefaultExport> default_export = {});

// Emits a member fragment for a CLASS body, not an object literal or ordinary
// function. The containing class/record still needs its own creation proof.
// No bytecode, analysis or legacy writer is consulted. Input tree/stream storage
// is caller-owned; temporary actions and returned positions share scratch.
// Failure may leave partial output: callers must discard it, never publish it.
// Globals require the actual complete destination GlobalScope, checked before
// any output. The scope used during preparation is not transferable permission.
// Module reads additionally require the exact native ModuleEnclosure prelude;
// the returned module_binding records that obligation, not record completion.
[[nodiscard]] std::variant<PrintedFunction, TreePrintError> PrintFunctionTree(
    const VerifiedFunction& function, std::ostream& output,
    EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {},
    const GlobalScope* scope = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_TREE_PRINTER_H_
