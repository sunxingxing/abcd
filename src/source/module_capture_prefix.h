#ifndef ABCD_SOURCE_MODULE_CAPTURE_PREFIX_H_
#define ABCD_SOURCE_MODULE_CAPTURE_PREFIX_H_
#include "function_input.h"
#include "abcd/source/record_module_context.h"
#include "abcd/abc/method_target.h"
namespace abcd::source {
enum class ModulePrefixError { kResourceLimit, kWorkLimit, kRead, kChanged };
class ModuleCapturePrefix {
 public:
  static std::variant<std::optional<ModuleCapturePrefix>, ModulePrefixError> Build(
      const corpus::AbcFile&, const abc::Header&, const abc::MetadataIndex&,
      const abc::MethodLookup&, const abc::MethodInfo&, const RecordModuleContext&,
      const FunctionInput&, analysis::SemanticBudget);
  std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> For(const FunctionInput& input) const {
    return identity_ == input.Identity() && method_ == input.Method() &&
        instructions_ == input.Input().instructions.data() && cells_ == input.Proof().locals.cells.data()
        ? std::span(prefixes_) : std::span<const std::optional<analysis::LocalAccessPlan::UnexposedInitialization>>{};
  }
  std::uint64_t Work() const { return work_; }
  std::size_t AllocatedBytes() const { return sizeof(*this) + prefixes_.capacity() * sizeof(decltype(prefixes_)::value_type); }
 private:
  ModuleCapturePrefix() = default;
  std::uint64_t work_ = 0;
  RouteInputIdentity identity_;
  abc::EntityOffset method_;
  // Move-stable immutable input arenas; never dereferenced as external owners.
  const analysis::SemanticInstruction* instructions_ = nullptr;
  const analysis::LocalCell* cells_ = nullptr;
  std::vector<std::optional<analysis::LocalAccessPlan::UnexposedInitialization>> prefixes_;
};
}
#endif
