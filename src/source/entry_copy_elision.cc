#include "entry_copy_elision.h"

#include <algorithm>
#include <type_traits>

namespace abcd::source {
namespace {
enum class SlotReadMode { kBound, kRecord };
bool HasBoundReads(const SourceExpression& expression, SlotReadMode mode) {
  return std::visit([mode](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, analysis::ScalarExpression> ||
                  std::is_same_v<T, MemberExpression> ||
                  std::is_same_v<T, StringExpression> ||
                  std::is_same_v<T, StringComparison> ||
                  std::is_same_v<T, ModuleExpression> ||
                  std::is_same_v<T, LocalExpression> ||
                  std::is_same_v<T, FunctionExpression> ||
                  std::is_same_v<T, CapturedExpression> ||
                  std::is_same_v<T, analysis::EmptyObjectCreation> ||
                  std::is_same_v<T, GlobalExpression> ||
                  std::is_same_v<T, analysis::GlobalObjectLoad> ||
                  std::is_same_v<T, analysis::BufferedObjectCreation> ||
                  std::is_same_v<T, analysis::BufferedArrayCreation> ||
                  std::is_same_v<T, analysis::AwaitValue> ||
                  std::is_same_v<T, analysis::RestCopy> ||
                  std::is_same_v<T, CallExpression> ||
                  std::is_same_v<T, MemberCallExpression> ||
                  std::is_same_v<T, analysis::ConstructorCall>) {
      // Globals and literal buffers have no caller VM-slot inputs. RestCopy
      // uses an owner-proven native rest formal, not the accumulator name used
      // by its isolated scalar writer. AwaitValue reads through Value(). Keep
      // their effect/allocation/await admission checks in Expression unchanged.
      // Leaf functions receive MethodConstants, never caller ScalarNames.
      // Their outer bindings and CapturedExpression use typed lexical cells;
      // initialization/domain checks and zero-width capture tokens remain.
      // MemberExpression's optional binding receiver uses the same Value and
      // Expression dispatch as staged calls, including register observation.
      return true;
    } else if constexpr (std::is_same_v<T, ClassExpression>) {
      // The class parent identifier is observed explicitly in record mode.
      // Do not extend ABI substitution to this identifier-only consumer.
      return mode == SlotReadMode::kRecord;
    } else {
      // These owner-specific protocols remain outside the bound-read audit.
      // Never infer their reads from the already generated source.
      static_assert(std::is_same_v<T, MethodExpression> ||
                    std::is_same_v<T, AccessorExpression> ||
                    std::is_same_v<T, analysis::SuperRange>);
      return false;
    }
  }, expression);
}
bool HasObservedReads(const SourceAst& ast, SlotReadMode mode) {
  // ArgumentRegion delegates every branch statement to the same slot-aware
  // printer and reads its final arguments via Value. The flat audit below
  // includes the entire region. RegionTry installs the caught accumulator via
  // Target and delegates both bodies to Region; it has no hidden register read.
  return std::all_of(ast.statements.begin(), ast.statements.end(), [mode](const auto& statement) {
    return std::visit([mode](const auto& operation) {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, SourceAssignment> ||
                    std::is_same_v<T, SourceLocalAssignment> ||
                    std::is_same_v<T, SourceReturnExpression>) {
        return HasBoundReads(operation.value, mode);
      } else if constexpr (std::is_same_v<T, analysis::NoOperation> ||
                           std::is_same_v<T, SourceMemberStore> ||
                           std::is_same_v<T, SourceOwnStore> ||
                           std::is_same_v<T, SourceLocalStore> ||
                           std::is_same_v<T, SourceModuleStore> ||
                           std::is_same_v<T, SourceConditionalBreak> ||
                           std::is_same_v<T, SourceConditionalContinue> ||
                           std::is_same_v<T, SourceBreak> ||
                           std::is_same_v<T, SourceContinue> ||
                           std::is_same_v<T, SourceReturn> ||
                           std::is_same_v<T, SourceThrow> ||
                           std::is_same_v<T, SourceArrayCapture> ||
                           std::is_same_v<T, SourceArrayCommit> ||
                           std::is_same_v<T, SourceArrayLiteral> ||
                           std::is_same_v<T, SourceIndexedStore> ||
                           std::is_same_v<T, SourceStagedCall>) {
        // Staged calls bind/observe their accumulator and arguments through
        // Slot/Value. Their child statements are audited by this same flat
        // traversal, including nested intervals; do not skip their children.
        // BindingReceiver register reads also use Value; its other alternatives
        // are binding/cell reads without VM slots. Re-audit new receiver kinds.
        // Native array owners observe their target and each element via
        // Target/Slot/Statement. Their retained indexed-store anchors are
        // consumed by ArrayLiteral; standalone indexed stores still reject.
        // Audit every child expression here, not just the owning array node.
        return true;
      } else {
        static_assert(std::is_same_v<T, SourceMethodStore> ||
                      std::is_same_v<T, SourceInitializerCall>);
        return false;
      }
    }, statement.operation);
  });
}
}  // namespace

bool CanElideEntryCopies(const SourceAst& ast) {
  return !ast.regions.catches && HasObservedReads(ast, SlotReadMode::kBound);
}

bool CanObserveBodySlots(const SourceAst& ast) {
  return HasObservedReads(ast, SlotReadMode::kBound);
}

bool CanObserveRecordSlots(const SourceAst& ast) {
  return !ast.regions.catches && HasObservedReads(ast, SlotReadMode::kRecord);
}

bool IsBoundEntryCopy(const SourceStatement& statement,
                      std::span<const EntryBinding> bindings) {
  if (statement.fused_read || statement.fused_spill || statement.fused_reload ||
      statement.class_origin || statement.previous_origin) return false;
  const auto* assignment = std::get_if<SourceAssignment>(&statement.operation);
  const auto* target = assignment ? std::get_if<abc::RegisterId>(&assignment->target) : nullptr;
  const auto* expression = assignment
      ? std::get_if<analysis::ScalarExpression>(&assignment->value) : nullptr;
  const auto* value = expression ? std::get_if<analysis::ScalarValue>(expression) : nullptr;
  const auto* source = value ? std::get_if<abc::RegisterId>(value) : nullptr;
  return target && source && target->value < bindings.size() &&
         bindings[target->value].source == *source;
}
}  // namespace abcd::source
