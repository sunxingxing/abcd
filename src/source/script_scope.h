#ifndef ABCD_SOURCE_SCRIPT_SCOPE_H_
#define ABCD_SOURCE_SCRIPT_SCOPE_H_
#include "abcd/source/record_module_context.h"
#include "abcd/source/source_tree.h"
#include "tree_constants.h"
namespace abcd::source {
struct ControlPlan;
struct EvaluationPlan;
enum class ScriptScopeError : std::uint8_t {
  kContract,
  kOwner,
  kEntry,
  kDeclaration,
  kOrder,
  kEscape,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit,
  kPrivateScope,
  kHostGlobalAccessor
};
struct ScriptDeclaration {
  analysis::OpId store;
  NameId name;
  GlobalDeclaration kind;
  analysis::ValueOperand value;
  analysis::LanguageEffect normal, before_throw;
  analysis::ExceptionalCompletion exceptional;
  std::optional<analysis::ValueHandler> handler;
  bool operator==(const ScriptDeclaration&) const;
};
struct ScriptScope {
  abc::EntityOffset record, entry;
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<ScriptDeclaration> declarations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// No module table or module binding is fabricated. The record adapter proves
// same-file script ownership; this pass owns only original global commits.
std::variant<ScriptScope, ScriptScopeError> BuildScriptScope(
    const analysis::ValueProgram&, const TreeConstants&,
    abc::EntityOffset record, abc::EntityOffset entry, SourceContract,
    analysis::SemanticBudget = {});
std::variant<std::uint64_t, ScriptScopeError> VerifyScriptScope(
    const analysis::ValueProgram&, const TreeConstants&, const ScriptScope&,
    abc::EntityOffset expected_record, SourceContract,
    analysis::SemanticBudget = {});
// The checked scope is sealed only after original event correspondence and
// syntax position checks. It is not a generic permission to print declarations.
class ScriptScopeProof {
 public:
  static std::variant<std::shared_ptr<const ScriptScopeProof>, ScriptScopeError>
  Verify(const analysis::ValueProgram&, const TreeConstants&,
         const ScriptScope&, FunctionTree&, const ControlPlan&,
         const EvaluationPlan&, std::span<const abc::MethodInfo>,
         analysis::SemanticBudget = {});
  bool Owns(const FunctionTree&) const;
  bool HasVar(std::u16string_view) const;
  std::size_t AllocatedBytes() const;
  bool Matches(const FunctionTree& tree) const {
    return tree.identity == identity_ && tree.method == entry_ &&
           tree.script_record == record_ && tree.value_revision == revision_ &&
           tree.cfg_revision == cfg_revision_;
  }
  abc::EntityOffset Record() const { return record_; }
  abc::EntityOffset Entry() const { return entry_; }
  std::span<const ScriptDeclaration> Declarations() const {
    return declarations_;
  }

 private:
  ScriptScopeProof() = default;
  RouteInputIdentity identity_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_revision_;
  abc::EntityOffset record_, entry_;
  std::vector<ScriptDeclaration> declarations_;
  std::vector<abc::EntityOffset> owners_;
  std::vector<std::u16string> variables_;
};
}  // namespace abcd::source
#endif
