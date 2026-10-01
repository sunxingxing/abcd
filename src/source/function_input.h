#ifndef ABCD_SOURCE_FUNCTION_INPUT_H_
#define ABCD_SOURCE_FUNCTION_INPUT_H_

#include "abcd/source/source_route.h"
#include "capture_plan.h"
#include "function_context.h"

namespace abcd::source {
struct ParentModuleInput;
enum class InputCaptureMode : std::uint8_t { kClosed, kConditional };
enum class FunctionInputError : std::uint8_t {
  kMode, kCapture, kResourceLimit, kWorkLimit, kOwner, kModuleOwner
};
struct FunctionInputFailure {
  std::variant<analysis::SemanticFailure, analysis::SemanticProofFailure, FunctionInputError> cause;
};
struct InputCaptureAccess {
  analysis::InstructionId instruction;
  analysis::LocalAccessKind kind;
};
struct EntryModuleStore {
  analysis::ModuleSlot slot;
  analysis::InstructionId instruction;
};
struct InputModuleRead {
  analysis::InstructionId instruction;
  analysis::ModuleSlot slot;
  abc::EntityOffset creator;
  abc::CodeOffset creation_pc;
  analysis::InstructionId store;
};
// Same-body input IR and proof cannot be mixed or mutated after construction.
// Conditional mode records premises, NOT an unconditional input/source receipt.
// Captures includes reads AND writes, in instruction order, without deduping
// occurrences. A write-only capture also requires a valid initialized source
// binding at its parent creation; it is not an invented declaration in child.
// Optional owner must already be resolved from the exact creation occurrence.
// It is checked before analysis and retained as an immutable stamp. Supported
// initialized callable owners grant an explicit frame-receiver premise, never
// an ABI-slot inference. Source publication rechecks the original owner.
class FunctionInput {
 public:
  FunctionInput(FunctionInput&&) noexcept = default;
  FunctionInput& operator=(FunctionInput&&) noexcept = default;
  [[nodiscard]] static std::variant<FunctionInput, FunctionInputFailure> Build(
      RouteInputIdentity identity, abc::EntityOffset method, abc::EntityOffset record,
      const abc::MethodBody& body, const abc::FrameLayout& frame,
      InputCaptureMode mode, analysis::SemanticBudget budget = {},
      const FunctionContext* owner = nullptr, const ParentModuleInput* modules = nullptr);
  bool HasOwner() const { return owner_.has_value(); }
  bool MatchesOwner(const FunctionContext& owner) const;
  const RouteInputIdentity& Identity() const { return identity_; }
  abc::EntityOffset Method() const { return method_; }
  abc::EntityOffset Record() const { return record_; }
  InputCaptureMode Mode() const { return mode_; }
  const analysis::SemanticMethod& Input() const { return input_; }
  const analysis::SemanticProof& Proof() const { return proof_; }
  std::span<const CaptureUse> Captures() const { return captures_; }
  std::span<const InputCaptureAccess> CaptureAccesses() const { return accesses_; }
  std::span<const InputModuleRead> InitializedModuleReads() const { return modules_; }
  // Entry-block writes are proven Language by this immutable input. Query an
  // actual later creation; branch-local writes and future stores are excluded.
  // A slot index is built once per method, not rescanned for every child.
  std::optional<analysis::InstructionId> ModuleStoreBefore(
      analysis::ModuleSlot slot, analysis::InstructionId creation) const;
  // All unpreserved callable/class/buffer sites, not just DEFINEFUNC.
  // Complete transitive child validation must discharge these separately.
  std::span<const analysis::InstructionId> Children() const { return proof_.locals.capture_barriers; }
  std::size_t AllocatedBytes() const { return allocated_bytes_; }
 private:
  // Snapshot only validated occurrence context. No public setter may attach an
  // owner after domain verification. Class properties distinguish same-PC uses.
  struct Owner {
    abc::EntityOffset creator;
    abc::CodeOffset pc;
    std::optional<std::uint32_t> property;
    CallableKind callable;
    Strictness strictness;
    abc::FrameLayout frame;
    bool operator==(const Owner&) const = default;
  };
  static Owner DescribeOwner(const FunctionContext& owner);
  FunctionInput(RouteInputIdentity identity, abc::EntityOffset method,
      abc::EntityOffset record, InputCaptureMode mode,
      analysis::SemanticMethod&& input, analysis::SemanticProof&& proof,
      std::vector<CaptureUse>&& captures, std::vector<InputCaptureAccess>&& accesses,
      std::size_t allocated_bytes, std::optional<Owner> owner,
      std::vector<EntryModuleStore>&& stores, std::vector<InputModuleRead>&& modules);
  RouteInputIdentity identity_;
  abc::EntityOffset method_;
  abc::EntityOffset record_;
  InputCaptureMode mode_ = InputCaptureMode::kClosed;
  analysis::SemanticMethod input_;
  analysis::SemanticProof proof_;
  std::vector<CaptureUse> captures_;
  std::vector<InputCaptureAccess> accesses_;
  std::size_t allocated_bytes_ = 0;
  std::optional<Owner> owner_;
  std::vector<EntryModuleStore> module_stores_;
  // Conditional owner premises. Original store/read/creation identities are
  // retained for independent family publication; input admission is no receipt.
  std::vector<InputModuleRead> modules_;
};
// Borrowed only while reading the immediate child. Never retain a pointer to a
// family member across vector growth. FunctionInput validates the genuine
// occurrence context, same record/file identity and original creation opcode.
struct ParentModuleInput {
  const FunctionInput& parent;
  analysis::InstructionId creation;
};
// Independent family edge check before publication. The root cannot carry
// these external premises. Rechecks original local read, entry store domain,
// exact creation and same-record identity; never grants a snapshot/const fact.
std::variant<std::uint64_t, FunctionInputError> VerifyInitializedModuleReads(
    const FunctionInput& parent, analysis::InstructionId creation,
    const FunctionInput& child, analysis::SemanticBudget budget = {});
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
