#ifndef ABCD_SOURCE_GUARDED_STORES_H_
#define ABCD_SOURCE_GUARDED_STORES_H_
#include "../analysis/guarded_reads.h"
#include "abcd/analysis/value_environment.h"
#include "abcd/analysis/exception_dispatch.h"
#include "tree_checks.h"
namespace abcd::source {
enum class GuardedStoreError : std::uint8_t {
  kInput, kStorage, kOrder, kCatchState, kCorrespondence
};
struct GuardedStore {
  analysis::OpId read, guard, store;
  analysis::CapturedDepth depth;
  analysis::LexicalSlot slot;
  analysis::UseId input;
  analysis::ValueId value;
  analysis::WorkBlockId success;
  std::optional<analysis::ValueHandler> handler;
  bool operator==(const GuardedStore& other) const {
    return read == other.read && guard == other.guard && store == other.store &&
        depth == other.depth && slot.value == other.slot.value && input == other.input &&
        value == other.value && success == other.success && handler == other.handler;
  }
};
struct GuardedStores {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<GuardedStore> stores; // original store order
};
// Freshly verified phased graph/environment/guard inputs and its original
// dispatch index. Guard Commit -> store is an effect-free must-reach interval;
// native PutValue's extra TDZ test cannot fail after that original guard.
[[nodiscard]] GuardedStores BuildGuardedStores(const analysis::ValueProgram&,
    const analysis::ValueEnvironments&, const analysis::GuardedReads&,
    const analysis::ExceptionDispatch&, TreeCheckBudget&);
void VerifyGuardedStores(const analysis::ValueProgram&,
    const analysis::ValueEnvironments&, const analysis::GuardedReads&,
    const analysis::ExceptionDispatch&, const GuardedStores&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
