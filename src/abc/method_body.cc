#include "abcd/abc/method_body.h"
#include "abcd/abc/register_access.h"

#include <algorithm>
#include <limits>
#include <new>
#include <span>
#include <utility>

#include "metadata_cursor.h"

namespace abcd::abc {
namespace {
constexpr std::size_t kBoundaryBits =
    std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t kInitialCapacity = 16;
constexpr std::uint32_t kCatchPrefixBytes = 3;

BodyFailure Failure(BodyError kind, std::uint32_t offset = 0) {
  return {kind, CodeOffset{offset}, {}, {}};
}

BodyFailure FileFailure(corpus::FileError error) {
  return {BodyError::kFile, {}, {}, error};
}

bool DynamicProfile(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return true;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return false;
  }
  return false;
}

template <typename T>
bool Reserve(std::vector<T>* values, std::size_t required, std::size_t* live,
             std::size_t limit) {
  if (required <= values->capacity()) return true;
  const auto old_capacity = values->capacity();
  if (old_capacity > std::numeric_limits<std::size_t>::max() / 2) return false;
  const auto grown = old_capacity == 0 ? kInitialCapacity : old_capacity * 2;
  const auto requested = std::max(required, grown);
  // Admission includes the old buffer until reserve completes. Do not reserve
  // size()+1 repeatedly: that would make many small catch lists quadratic.
  if (requested > (limit - *live) / sizeof(T)) return false;
  values->reserve(requested);
  if (values->capacity() > (limit - *live) / sizeof(T)) return false;
  *live += (values->capacity() - old_capacity) * sizeof(T);
  return true;
}

void MarkBoundary(std::span<std::uint64_t> bits, std::uint32_t offset) {
  bits[offset / kBoundaryBits] |= std::uint64_t{1} << (offset % kBoundaryBits);
}

bool IsBoundary(std::span<const std::uint64_t> bits, std::uint32_t offset) {
  return (bits[offset / kBoundaryBits] &
          (std::uint64_t{1} << (offset % kBoundaryBits))) != 0;
}

bool ReadUleb(MetadataCursor* cursor, std::uint32_t* value) {
  const auto result = cursor->Uleb();
  if (const auto* number = std::get_if<std::uint32_t>(&result)) {
    *value = *number;
    return true;
  }
  return false;
}

std::optional<BodyFailure> CheckPrefix(const corpus::AbcFile& file,
                                       const CodeInfo& code) {
  if (file.Size() > std::numeric_limits<std::uint32_t>::max() ||
      code.offset.value < kDynamicHeaderSize ||
      code.offset.value >= file.Size() ||
      code.instruction_offset.value > file.Size() ||
      code.instruction_bytes > file.Size() - code.instruction_offset.value)
    return Failure(BodyError::kCodeRange);
  MetadataCursor cursor(file, code.offset.value, file.Size());
  std::uint32_t registers = 0, arguments = 0, bytes = 0, tries = 0;
  if (!ReadUleb(&cursor, &registers) || !ReadUleb(&cursor, &arguments) ||
      !ReadUleb(&cursor, &bytes) || !ReadUleb(&cursor, &tries) ||
      registers != code.registers || arguments != code.arguments ||
      bytes != code.instruction_bytes || tries != code.try_count ||
      cursor.Position() != code.instruction_offset.value)
    return Failure(BodyError::kMetadataMismatch);
  return std::nullopt;
}

std::optional<BodyFailure> CheckRange(std::uint32_t start, std::uint32_t length,
                                      std::uint32_t code_bytes,
                                      std::span<const std::uint64_t> boundaries,
                                      bool handler) {
  if (start > code_bytes || length > code_bytes - start ||
      (handler && start == code_bytes))
    return Failure(BodyError::kExceptionRange, start);
  if (!IsBoundary(boundaries, start) || !IsBoundary(boundaries, start + length))
    return Failure(BodyError::kExceptionBoundary, start);
  return std::nullopt;
}

std::optional<BodyFailure> DecodeExceptions(
    const corpus::AbcFile& file, const CodeInfo& code,
    std::span<const std::uint64_t> boundaries, MethodBody* body,
    std::size_t* live, std::size_t limit) {
  // Pinned CodeDataAccessor: try(start,length,count), then
  // catch(type+1,pc,size). Zero type encoding means catch-all. Never reorder
  // these encoded sequences.
  MetadataCursor cursor(file,
                        code.instruction_offset.value + code.instruction_bytes,
                        file.Size());
  if (code.try_count > (file.Size() - cursor.Position()) / kCatchPrefixBytes)
    return Failure(BodyError::kMalformedException, code.instruction_bytes);
  if (!Reserve(&body->tries, code.try_count, live, limit))
    return Failure(BodyError::kResourceLimit);
  for (std::uint32_t i = 0; i < code.try_count; ++i) {
    TryRegion region;
    if (!ReadUleb(&cursor, &region.offset.value) ||
        !ReadUleb(&cursor, &region.length) ||
        !ReadUleb(&cursor, &region.handler_count))
      return Failure(BodyError::kMalformedException, code.instruction_bytes);
    if (const auto error =
            CheckRange(region.offset.value, region.length,
                       code.instruction_bytes, boundaries, false))
      return error;
    if (region.handler_count >
        (file.Size() - cursor.Position()) / kCatchPrefixBytes)
      return Failure(BodyError::kMalformedException, region.offset.value);
    if (body->handlers.size() >
        std::numeric_limits<std::uint32_t>::max() - region.handler_count)
      return Failure(BodyError::kResourceLimit);
    region.first_handler = body->handlers.size();
    if (!Reserve(&body->handlers, body->handlers.size() + region.handler_count,
                 live, limit))
      return Failure(BodyError::kResourceLimit);
    for (std::uint32_t j = 0; j < region.handler_count; ++j) {
      CatchHandler handler;
      std::uint32_t encoded_type = 0;
      if (!ReadUleb(&cursor, &encoded_type) ||
          !ReadUleb(&cursor, &handler.offset.value) ||
          !ReadUleb(&cursor, &handler.length))
        return Failure(BodyError::kMalformedException, region.offset.value);
      if (encoded_type != 0) handler.type = CatchTypeIndex{encoded_type - 1};
      if (const auto error =
              CheckRange(handler.offset.value, handler.length,
                         code.instruction_bytes, boundaries, true))
        return error;
      body->handlers.push_back(handler);
    }
    body->tries.push_back(region);
  }
  return std::nullopt;
}

BodyResult DecodeBody(const corpus::AbcFile& file, ProfileId profile,
                      const CodeInfo& code, BodyBudget budget) {
  if (!DynamicProfile(profile)) return Failure(BodyError::kUnsupportedProfile);
  if (const auto error = file.CheckUnchanged()) return FileFailure(*error);
  if (const auto error = CheckPrefix(file, code)) return *error;
  if (code.instruction_bytes == 0) return Failure(BodyError::kEmptyBody);
  std::size_t live = 0;
  std::vector<std::uint8_t> bytes;
  if (!Reserve(&bytes, code.instruction_bytes, &live, budget.bytes))
    return Failure(BodyError::kResourceLimit);
  bytes.resize(code.instruction_bytes);
  if (const auto error = file.ReadAt(code.instruction_offset.value, bytes))
    return FileFailure(*error);
  std::vector<std::uint64_t> boundaries;
  const auto word_count = code.instruction_bytes / kBoundaryBits + 1;
  if (!Reserve(&boundaries, word_count, &live, budget.bytes))
    return Failure(BodyError::kResourceLimit);
  boundaries.resize(word_count);
  MethodBody body;
  const std::uint64_t frame_size =
      static_cast<std::uint64_t>(code.registers) + code.arguments;
  std::uint32_t pc = 0;
  while (pc < code.instruction_bytes) {
    auto decoded = DecodeInstruction(std::span(bytes).subspan(pc), profile);
    if (const auto* error = std::get_if<InstructionError>(&decoded))
      return BodyFailure{BodyError::kInstruction, CodeOffset{pc}, *error, {}};
    const auto& instruction = std::get<DecodedInstruction>(decoded);
    if (instruction.size == 0 || instruction.size > code.instruction_bytes - pc)
      return Failure(BodyError::kCodeRange, pc);
    if (std::holds_alternative<RegisterAccessError>(
            GetRegisterAccesses(instruction, frame_size)))
      return Failure(BodyError::kRegisterRange, pc);
    BodyInstruction located{CodeOffset{pc}, instruction, {}};
    if (instruction.branch_delta) {
      const auto delta = *instruction.branch_delta;
      if (delta < -static_cast<std::int64_t>(pc) ||
          delta >= static_cast<std::int64_t>(code.instruction_bytes) - pc)
        return Failure(BodyError::kBranchRange, pc);
      located.branch_target = CodeOffset{
          static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + delta)};
    }
    if (!Reserve(&body.instructions, body.instructions.size() + 1, &live,
                 budget.bytes))
      return Failure(BodyError::kResourceLimit, pc);
    body.instructions.push_back(std::move(located));
    MarkBoundary(boundaries, pc);
    pc += instruction.size;
  }
  MarkBoundary(boundaries, code.instruction_bytes);
  for (const auto& instruction : body.instructions) {
    if (instruction.branch_target &&
        !IsBoundary(boundaries, instruction.branch_target->value))
      return Failure(BodyError::kBranchBoundary, instruction.offset.value);
  }
  if (const auto error =
          DecodeExceptions(file, code, boundaries, &body, &live, budget.bytes))
    return *error;
  if (const auto error = file.CheckUnchanged()) return FileFailure(*error);
  return body;
}
}  // namespace

BodyResult DecodeMethodBody(const corpus::AbcFile& file, ProfileId profile,
                            const CodeInfo& code, BodyBudget budget) {
  try {
    return DecodeBody(file, profile, code, budget);
  } catch (const std::bad_alloc&) {
    return Failure(BodyError::kResourceLimit);
  }
}

}  // namespace abcd::abc
