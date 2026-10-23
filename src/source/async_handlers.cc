#include "async_handlers.h"

#include <algorithm>
#include <limits>

namespace abcd::source {
namespace {
constexpr std::size_t kCompletionRegionCount = 1;
constexpr std::size_t kCompletionHandlerCount = 1;

bool Inside(abc::CodeOffset offset, std::uint32_t length,
            abc::CodeOffset begin, abc::CodeOffset end) {
  return offset >= begin && offset < end &&
         std::uint64_t(offset.value) + length <= end.value;
}
}  // namespace

bool HasAsyncCompletionCatch(const abc::MethodBody& body,
                             abc::CodeOffset begin,
                             abc::CodeOffset handler,
                             std::uint64_t code_end) {
  if (body.tries.empty() || body.handlers.empty() ||
      code_end > std::numeric_limits<std::uint32_t>::max())
    return false;
  const auto& outer = body.tries.back();
  const auto& caught = body.handlers.back();
  const auto user_handlers = body.handlers.size() - kCompletionHandlerCount;
  if (outer.offset != begin ||
      std::uint64_t(outer.offset.value) + outer.length != handler.value ||
      outer.first_handler != user_handlers ||
      outer.handler_count != kCompletionHandlerCount || caught.type ||
      caught.offset != handler ||
      // es2abc may record the completion catch with code_size 0; the pinned
      // interpreter uses only its start, and it is the last code anyway.
      (caught.length != 0 && std::uint64_t(caught.offset.value) + caught.length != code_end))
    return false;
  for (std::size_t i = 0; i + kCompletionRegionCount < body.tries.size(); ++i) {
    const auto& region = body.tries[i];
    if (!Inside(region.offset, region.length, begin, handler) ||
        region.first_handler > user_handlers ||
        region.handler_count > user_handlers - region.first_handler)
      return false;
  }
  for (std::size_t i = 0; i < user_handlers; ++i) {
    const auto& user = body.handlers[i];
    if (!Inside(user.offset, user.length, begin, handler)) return false;
  }
  return true;
}

std::optional<FunctionEmitError> RebaseAsyncHandlers(
    const abc::MethodBody& original, std::span<const abc::CodeOffset> origins,
    abc::MethodBody& source, std::size_t bytes, AsyncCompletionEvidence* evidence) {
  const auto failure = [&](AsyncCompletionCheck check, std::optional<abc::CodeOffset> pc = {},
      std::optional<abc::CodeOffset> working_pc = {}) {
    if(evidence) { *evidence = {}; evidence->check = check;
      evidence->location.pc = pc; evidence->location.working_pc = working_pc; }
    return FunctionEmitError::kKind;
  };
  if (original.tries.empty() || original.handlers.empty() ||
      source.instructions.empty() || origins.size() != source.instructions.size())
    return failure(AsyncCompletionCheck::kHandlerInput);
  const auto source_end = std::uint64_t(source.instructions.back().offset.value) +
                          source.instructions.back().decoded.size;
  if (source_end > std::numeric_limits<std::uint32_t>::max())
    return failure(AsyncCompletionCheck::kHandlerRange, origins.back(), source.instructions.back().offset);
  const auto boundary = [&](std::uint64_t pc) -> std::optional<abc::CodeOffset> {
    if (pc == original.handlers.back().offset.value)
      return abc::CodeOffset{static_cast<std::uint32_t>(source_end)};
    const auto retained = std::lower_bound(origins.begin(), origins.end(), pc,
        [](abc::CodeOffset offset, std::uint64_t value) { return offset.value < value; });
    if (retained == origins.end()) return {};
    const auto index = static_cast<std::size_t>(retained - origins.begin());
    if (retained->value != pc) {
      const auto old = std::lower_bound(original.instructions.begin(),
          original.instructions.end(), pc, [](const auto& site, std::uint64_t value) {
            return site.offset.value < value;
          });
      if (old == original.instructions.end() || old->offset.value != pc ||
          old->decoded.opcode != abc::Opcode::kASYNCFUNCTIONRESOLVE_V8 ||
          pc + old->decoded.size != retained->value ||
          source.instructions[index].decoded.opcode != abc::Opcode::kRETURN)
        return {};
    }
    return source.instructions[index].offset;
  };
  const auto try_count = original.tries.size() - kCompletionRegionCount;
  const auto handler_count = original.handlers.size() - kCompletionHandlerCount;
  if (try_count > bytes / sizeof(abc::TryRegion))
    return FunctionEmitError::kResourceLimit;
  source.tries.reserve(try_count);
  if (source.tries.capacity() > bytes / sizeof(abc::TryRegion))
    return FunctionEmitError::kResourceLimit;
  bytes -= source.tries.capacity() * sizeof(abc::TryRegion);
  if (handler_count > bytes / sizeof(abc::CatchHandler))
    return FunctionEmitError::kResourceLimit;
  source.handlers.reserve(handler_count);
  if (source.handlers.capacity() > bytes / sizeof(abc::CatchHandler))
    return FunctionEmitError::kResourceLimit;
  for (std::size_t i = 0; i < try_count; ++i) {
    auto region = original.tries[i];
    const auto begin = boundary(region.offset.value);
    const auto end = boundary(std::uint64_t(region.offset.value) + region.length);
    if (!begin || !end || *end <= *begin) return failure(AsyncCompletionCheck::kTryBoundary, region.offset, begin);
    region.offset = *begin;
    region.length = end->value - begin->value;
    source.tries.push_back(region);
  }
  for (std::size_t i = 0; i < handler_count; ++i) {
    auto handler = original.handlers[i];
    const auto begin = boundary(handler.offset.value);
    const auto end = boundary(std::uint64_t(handler.offset.value) + handler.length);
    if (!begin || !end || *end < *begin) return failure(AsyncCompletionCheck::kHandlerBoundary, handler.offset, begin);
    handler.offset = *begin;
    handler.length = end->value - begin->value;
    source.handlers.push_back(handler);
  }
  return {};
}
}  // namespace abcd::source
