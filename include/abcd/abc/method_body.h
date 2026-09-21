#ifndef ABCD_ABC_METHOD_BODY_H_
#define ABCD_ABC_METHOD_BODY_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "abcd/abc/instruction.h"
#include "abcd/abc/metadata_index.h"
#include "abcd/corpus/abc_file.h"

namespace abcd::abc {

struct CodeOffset {
  std::uint32_t value = 0;
  auto operator<=>(const CodeOffset&) const = default;
};

struct CatchTypeIndex {
  std::uint32_t value = 0;
  auto operator<=>(const CatchTypeIndex&) const = default;
};

struct BodyInstruction {
  CodeOffset offset;
  DecodedInstruction decoded;
  std::optional<CodeOffset> branch_target;
};

struct CatchHandler {
  std::optional<CatchTypeIndex> type;
  CodeOffset offset;
  std::uint32_t length = 0;
};

struct TryRegion {
  CodeOffset offset;
  std::uint32_t length = 0;
  std::uint32_t first_handler = 0;
  std::uint32_t handler_count = 0;
};

struct MethodBody {
  std::vector<BodyInstruction> instructions;
  // Preserve file order: sorting overlapping regions or handlers can change
  // catch selection. Handler slices are contiguous in the flat handler array.
  std::vector<TryRegion> tries;
  std::vector<CatchHandler> handlers;
};

enum class BodyError {
  kUnsupportedProfile,
  kFile,
  kMetadataMismatch,
  kCodeRange,
  kEmptyBody,
  kInstruction,
  kRegisterRange,
  kBranchRange,
  kBranchBoundary,
  kMalformedException,
  kExceptionRange,
  kExceptionBoundary,
  kResourceLimit,
};

struct BodyFailure {
  BodyError kind;
  CodeOffset offset;
  std::optional<InstructionError> instruction_error;
  std::optional<corpus::FileError> file_error;
};

struct BodyBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

using BodyResult = std::variant<MethodBody, BodyFailure>;

// Bounded synchronous primitive for a future lazy task executor. The caller
// retains the file throughout this call; successful results own all their data.
// This validates encodings/control-flow ranges, not instruction semantics.
[[nodiscard]] BodyResult DecodeMethodBody(const corpus::AbcFile& file,
                                          ProfileId profile,
                                          const CodeInfo& code,
                                          BodyBudget budget = {});

}  // namespace abcd::abc
#endif  // ABCD_ABC_METHOD_BODY_H_
