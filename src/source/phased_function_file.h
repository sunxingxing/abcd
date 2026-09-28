#ifndef ABCD_SOURCE_PHASED_FUNCTION_FILE_H_
#define ABCD_SOURCE_PHASED_FUNCTION_FILE_H_

#include "phased_body.h"
#include "abcd/analysis/exception_locals.h"

namespace abcd::source {
enum class PhasedFileError : std::uint8_t {
  kIdentity, kMethod, kFrame, kChanged, kResourceLimit, kWorkLimit
};
using PhasedFileFailure = std::variant<PhasedFileError, abc::MetadataError,
    abc::FrameError, abc::BodyFailure, PhasedMethodError, analysis::ExceptionLocalFailure>;
// File-backed conditional method input, NOT a prepared source family. The
// immutable heap owner keeps decoded body, original phases and capture scopes
// together across moves/vector growth. Views never point into a moved envelope.
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
      const PhasedModuleBinding* modules = nullptr);
  const abc::MethodBody& Body() const;
  const PhasedBody& Phases() const;
  const analysis::ExceptionLocals& Locals() const;
  abc::EntityOffset Record() const;
  std::size_t AllocatedBytes() const;
  // Decoded instruction-byte charge plus phase/local recorded work. Metadata
  // and container parsing retain their existing byte caps, not full work counts.
  std::uint64_t Work() const;
 private:
  struct Storage;
  explicit FilePhasedFunction(std::unique_ptr<const Storage> storage);
  std::unique_ptr<const Storage> storage_;
};
}  // namespace abcd::source
#endif
