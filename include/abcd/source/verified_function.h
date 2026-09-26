#ifndef ABCD_SOURCE_VERIFIED_FUNCTION_H_
#define ABCD_SOURCE_VERIFIED_FUNCTION_H_

#include <memory>
#include <optional>
#include <string_view>
#include <variant>

#include "abcd/analysis/class_method_sites.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/source/source_tree.h"
#include "abcd/source/function_abi.h"

namespace abcd::source {
enum class FunctionPrepareError : std::uint8_t {
  kContext, kBody, kValues, kControl, kEvaluation, kConstants, kParameters,
  kTree, kFileChanged, kResourceLimit, kWorkLimit, kAbi
};
struct FunctionPrepareFailure {
  FunctionPrepareError kind;
  std::optional<MethodRouteReceipt> values;
  std::optional<abc::CodeOffset> pc;
};
// Owned immutable syntax plus same-input creation/ABI/source evidence. No public
// constructor accepts a raw tree or a mutable verification report. The factory
// reads the selected ABC body itself and runs the mandatory fresh value route.
// Its printer needs no file/index/SSA readers and cannot invoke legacy writers.
// File identity, metadata, lookup, roles and sites must belong to the same
// immutable input. Their caller-owned storage is outside the method allowance.
// Successful preparation is not source emission or record completion.
class VerifiedFunction {
 public:
  VerifiedFunction(VerifiedFunction&&) noexcept;
  VerifiedFunction& operator=(VerifiedFunction&&) noexcept;
  ~VerifiedFunction();
  [[nodiscard]] static std::variant<VerifiedFunction, FunctionPrepareFailure> PrepareClass(
      const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& sites,
      analysis::ClassSiteId site, RouteInputIdentity identity,
      analysis::SemanticBudget budget = {});
  [[nodiscard]] const FunctionTree& Tree() const;
  [[nodiscard]] std::u16string_view Name() const;
  [[nodiscard]] const analysis::ClassMethodSite& Creation() const;
  [[nodiscard]] const MethodRouteReceipt& Receipt() const;
  [[nodiscard]] const abc::FrameLayout& Frame() const;
  [[nodiscard]] const FunctionAbi& Abi() const;
  [[nodiscard]] std::size_t AllocatedBytes() const;
 private:
  struct Storage;
  explicit VerifiedFunction(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
};
}  // namespace abcd::source
#endif
