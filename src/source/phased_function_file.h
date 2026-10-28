#ifndef ABCD_SOURCE_PHASED_FUNCTION_FILE_H_
#define ABCD_SOURCE_PHASED_FUNCTION_FILE_H_

#include "phased_body.h"
#include "protocol_rebase.h"
#include "phased_parameter_defaults.h"
#include "../analysis/guarded_reads.h"
#include "function_input.h"
#include "guarded_stores.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/source/async_completion.h"

namespace abcd::source {
enum class PhasedFileError : std::uint8_t {
  kIdentity, kMethod, kFrame, kChanged, kResourceLimit, kWorkLimit
};
using PhasedFileFailure = std::variant<PhasedFileError, abc::MetadataError,
    abc::FrameError, abc::BodyFailure, PhasedMethodError, analysis::ExceptionLocalFailure, FunctionEmitError, PhasedInputFailure, PhasedFlowFailure, PhasedModuleFailure, PhasedImportFailure, PhasedControlFailure, analysis::GuardedReadError, GuardedStoreError, AsyncCompletionFailure, ConcurrentFailure, ProtocolRebaseError, PhasedParameterError>;
// File-backed conditional method input, NOT a prepared source family. The
// immutable heap owner keeps working body, phase analyses and capture scopes
// together across moves/vector growth. Views never point into a moved envelope.
// Async completion normalization retains the decoded body for debug names and
// original PCs in the common value/tree origins. Body() is the working view.
// Same unchanged file/header/index/hash/occurrence are caller prerequisites.
// Context and optional binding are borrowed during Read only, never retained.
class FilePhasedFunction {
 public:
  FilePhasedFunction(FilePhasedFunction&&) noexcept;
  FilePhasedFunction& operator=(FilePhasedFunction&&) noexcept;
  ~FilePhasedFunction();
  [[nodiscard]] static std::variant<FilePhasedFunction, PhasedFileFailure> Read(
      const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
      const FunctionContext& context, analysis::SemanticBudget budget = {},
      const PhasedModuleBinding* modules = nullptr, InputCaptureMode mode = InputCaptureMode::kClosed, const ParentModuleInput* parent_modules = nullptr,
      const InheritedModuleInput* inherited = nullptr);
  std::variant<std::uint64_t, ForOfPlacementFailure> RetryIteratorContinuation(
      const TreeCensusFailure&, analysis::SemanticBudget);
  const abc::MethodBody& Body() const;
  const abc::MethodBody& OriginalBody() const;
  const PhasedBody& Phases() const;
  std::shared_ptr<const PhasedParameterDefaults> ParameterDefaults() const;
  const analysis::ExceptionLocals& Locals() const;
  const analysis::GuardedReads& GuardedReads() const;
  std::span<const analysis::InstructionId> Children() const;
  InputCaptureMode Mode() const;
  std::span<const CaptureUse> Captures() const;
  std::span<const InputCaptureAccess> CaptureAccesses() const;
  bool MatchesOwner(const FunctionContext& context) const;
  bool MatchesModuleCreation(const FunctionInput&, analysis::InstructionId) const;
  std::span<const InputModuleRead> InitializedModuleReads() const;
  const ModuleSlotSet& EntryModules() const;
  abc::EntityOffset Record() const;
  std::size_t AllocatedBytes() const;
  // Decoded instruction-byte charge plus phase/local recorded work. Metadata
  // and container parsing retain their existing byte caps, not full work counts.
  std::uint64_t Work() const;
 private:
  struct Storage;
  explicit FilePhasedFunction(std::unique_ptr<Storage> storage);
  std::unique_ptr<Storage> storage_;
};
[[nodiscard]] std::variant<std::uint64_t, FunctionInputError> VerifyInitializedModuleReads(
    const FunctionInput&, analysis::InstructionId, const FilePhasedFunction&, analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
