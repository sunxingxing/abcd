#ifndef ABCD_SOURCE_FUNCTION_INPUT_H_
#define ABCD_SOURCE_FUNCTION_INPUT_H_

#include "abcd/source/source_route.h"
#include "capture_plan.h"

namespace abcd::source {
enum class InputCaptureMode : std::uint8_t { kClosed, kConditional };
enum class FunctionInputError : std::uint8_t {
  kMode, kCapture, kResourceLimit, kWorkLimit
};
struct FunctionInputFailure {
  std::variant<analysis::SemanticFailure, analysis::SemanticProofFailure, FunctionInputError> cause;
};
struct InputCaptureAccess {
  analysis::InstructionId instruction;
  analysis::LocalAccessKind kind;
};
// Same-body input IR and proof cannot be mixed or mutated after construction.
// Conditional mode records premises, NOT an unconditional input/source receipt.
// Captures includes reads AND writes, in instruction order, without deduping
// occurrences. A write-only capture also requires a valid initialized source
// binding at its parent creation; it is not an invented declaration in child.
class FunctionInput {
 public:
  FunctionInput(FunctionInput&&) noexcept = default;
  FunctionInput& operator=(FunctionInput&&) noexcept = default;
  [[nodiscard]] static std::variant<FunctionInput, FunctionInputFailure> Build(
      RouteInputIdentity identity, abc::EntityOffset method, abc::EntityOffset record,
      const abc::MethodBody& body, const abc::FrameLayout& frame,
      InputCaptureMode mode, analysis::SemanticBudget budget = {});
  const RouteInputIdentity& Identity() const { return identity_; }
  abc::EntityOffset Method() const { return method_; }
  abc::EntityOffset Record() const { return record_; }
  InputCaptureMode Mode() const { return mode_; }
  const analysis::SemanticMethod& Input() const { return input_; }
  const analysis::SemanticProof& Proof() const { return proof_; }
  std::span<const CaptureUse> Captures() const { return captures_; }
  std::span<const InputCaptureAccess> CaptureAccesses() const { return accesses_; }
  // All unpreserved callable/class/buffer sites, not just DEFINEFUNC.
  // Complete transitive child validation must discharge these separately.
  std::span<const analysis::InstructionId> Children() const { return proof_.locals.capture_barriers; }
  std::size_t AllocatedBytes() const { return allocated_bytes_; }
 private:
  FunctionInput(RouteInputIdentity identity, abc::EntityOffset method,
      abc::EntityOffset record, InputCaptureMode mode,
      analysis::SemanticMethod&& input, analysis::SemanticProof&& proof,
      std::vector<CaptureUse>&& captures, std::vector<InputCaptureAccess>&& accesses,
      std::size_t allocated_bytes);
  RouteInputIdentity identity_;
  abc::EntityOffset method_;
  abc::EntityOffset record_;
  InputCaptureMode mode_ = InputCaptureMode::kClosed;
  analysis::SemanticMethod input_;
  analysis::SemanticProof proof_;
  std::vector<CaptureUse> captures_;
  std::vector<InputCaptureAccess> accesses_;
  std::size_t allocated_bytes_ = 0;
};
enum class InputBindingError : std::uint8_t { kIdentity, kTarget, kMode };
using InputBindingResult = std::variant<CapturePlan, CaptureError, InputBindingError>;
// The unchanged index and genuine BindCreation result belong to these inputs'
// file identity. Resolves every DIRECT child capture; descendants remain the
// child's Children obligations. Success does not authorize scalarization,
// erase closure-write premises, close an SCC, or certify executable child AST.
[[nodiscard]] InputBindingResult BindFunctionInputs(
    const abc::MetadataIndex& index, const FunctionInput& parent,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const FunctionInput& child, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
