#ifndef ABCD_SOURCE_CAPTURE_INPUT_H_
#define ABCD_SOURCE_CAPTURE_INPUT_H_

#include "function_input.h"

namespace abcd::source {
class FilePhasedFunction;
// Borrowed original-input view, not a conversion from phased SSA to normal SSA.
// Keep the real owner alive and unchanged throughout occurrence binding.
class CaptureInput {
 public:
  CaptureInput(const FunctionInput& input) : input_(&input) {}
  CaptureInput(const FilePhasedFunction& input) : input_(&input) {}
  const RouteInputIdentity& Identity() const;
  abc::EntityOffset Method() const;
  abc::EntityOffset Record() const;
  InputCaptureMode Mode() const;
  bool MatchesOwner(const FunctionContext& owner) const;
  std::span<const CaptureUse> Captures() const;
  std::span<const InputCaptureAccess> CaptureAccesses() const;
  std::span<const analysis::SemanticInstruction> Instructions() const;
  CaptureInput WithCaptures(std::span<const CaptureUse> captures) const {
    auto result = *this; result.captures_ = captures; return result;
  }
 private:
  std::variant<const FunctionInput*, const FilePhasedFunction*> input_;
  std::optional<std::span<const CaptureUse>> captures_;
};
}  // namespace abcd::source
#endif
