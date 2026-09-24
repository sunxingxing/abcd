#include "async_completion.h"

#include <limits>

#include "abcd/abc/register_access.h"

namespace abcd::source {
namespace {
bool IsRegister(const abc::BodyInstruction& instruction,
                abc::Opcode opcode, abc::RegisterId* register_id) {
  const auto& decoded = instruction.decoded;
  if (decoded.opcode != opcode || decoded.operand_count != 1 ||
      decoded.operands[0].kind != abc::OperandKind::kRegister ||
      decoded.operands[0].raw_bits >
          std::numeric_limits<std::uint32_t>::max())
    return false;
  *register_id = abc::RegisterId{
      static_cast<std::uint32_t>(decoded.operands[0].raw_bits)};
  return true;
}
bool IsReturn(const abc::BodyInstruction& instruction) {
  return instruction.decoded.opcode == abc::Opcode::kRETURN &&
         instruction.decoded.operand_count == 0;
}
bool TouchesRegister(const abc::BodyInstruction& instruction,
                     abc::RegisterId register_id, std::uint32_t count) {
  const auto accesses = abc::GetRegisterAccesses(instruction.decoded, count);
  const auto* known = std::get_if<abc::RegisterAccesses>(&accesses);
  if (!known) return true;
  for (std::size_t i = 0; i < known->count; ++i) {
    const auto& window = known->windows[i];
    if (register_id.value >= window.first.value &&
        register_id.value - window.first.value < window.count)
      return true;
  }
  return false;
}
}  // namespace
AsyncCompletion ProveAsyncCompletion(const abc::MethodBody& body,
                                     const abc::FrameLayout& frame,
                                     std::size_t bytes,
                                     std::vector<abc::CodeOffset>* source_pcs) {
  using abc::Opcode;
  const auto& sites = body.instructions;
  if (sites.size() < 7 || body.tries.size() != 1 ||
      body.handlers.size() != 1)
    return FunctionEmitError::kKind;
  std::size_t enter = 0;
  while (enter < sites.size() &&
         sites[enter].decoded.opcode != Opcode::kASYNCFUNCTIONENTER) {
    if (sites[enter].decoded.opcode != Opcode::kMOV_V4_V4 &&
        sites[enter].decoded.opcode != Opcode::kMOV_V8_V8)
      return FunctionEmitError::kKind;
    ++enter;
  }
  if (enter + 5 >= sites.size() ||
      sites[enter].decoded.operand_count != 0)
    return FunctionEmitError::kKind;
  abc::RegisterId promise;
  if (!IsRegister(sites[enter + 1], Opcode::kSTA_V8, &promise) ||
      promise.value >= frame.locals)
    return FunctionEmitError::kKind;
  for (std::size_t i = 0; i < enter; ++i) {
    const auto& copy = sites[i].decoded;
    if (copy.operand_count != 2 ||
        copy.operands[0].kind != abc::OperandKind::kRegister ||
        copy.operands[1].kind != abc::OperandKind::kRegister ||
        copy.operands[0].raw_bits >= frame.locals ||
        copy.operands[0].raw_bits == promise.value ||
        copy.operands[1].raw_bits < frame.locals ||
        copy.operands[1].raw_bits >= frame.registers)
      return FunctionEmitError::kKind;
  }
  const auto handler = sites.size() - 3;
  abc::RegisterId caught, resolved, rejected;
  if (handler <= enter + 4 ||
      !IsRegister(sites[handler], Opcode::kSTA_V8, &caught) ||
      caught == promise || caught.value >= frame.locals ||
      !IsRegister(sites[handler + 1], Opcode::kASYNCFUNCTIONREJECT_V8,
                  &rejected) ||
      !IsReturn(sites[handler + 2]) ||
      !IsRegister(sites[handler - 2], Opcode::kASYNCFUNCTIONRESOLVE_V8,
                  &resolved) ||
      !IsReturn(sites[handler - 1]) || resolved != promise ||
      rejected != promise)
    return FunctionEmitError::kKind;
  const auto retained = handler - 3;
  const auto site_bytes = sizeof(abc::BodyInstruction) +
                         (source_pcs ? sizeof(abc::CodeOffset) : 0);
  if (retained > bytes / site_bytes)
    return FunctionEmitError::kResourceLimit;
  const auto& region = body.tries[0];
  const auto& catch_handler = body.handlers[0];
  const std::uint64_t code_end =
      std::uint64_t(sites.back().offset.value) + sites.back().decoded.size;
  if (region.offset != sites[enter + 2].offset ||
      std::uint64_t(region.offset.value) + region.length !=
          sites[handler].offset.value ||
      region.first_handler != 0 || region.handler_count != 1 ||
      catch_handler.type || catch_handler.offset != sites[handler].offset ||
      std::uint64_t(catch_handler.offset.value) + catch_handler.length !=
      code_end || code_end > std::numeric_limits<std::uint32_t>::max())
    return FunctionEmitError::kKind;
  for (std::size_t i = enter + 2; i < handler - 2; ++i) {
    const auto& site = sites[i];
    if (site.branch_target || site.decoded.jump || site.decoded.conditional ||
        IsReturn(site) ||
        TouchesRegister(site, promise, frame.registers))
      return FunctionEmitError::kKind;
  }
  try {
    abc::MethodBody source;
    source.instructions.reserve(retained);
    if (source.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return FunctionEmitError::kResourceLimit;
    source.instructions.insert(source.instructions.end(), sites.begin(),
                               sites.begin() + enter);
    source.instructions.insert(source.instructions.end(),
                               sites.begin() + enter + 2,
                               sites.begin() + handler - 2);
    source.instructions.push_back(sites[handler - 1]);
    std::vector<abc::CodeOffset> origins;
    if (source_pcs) {
      const auto remaining = bytes - source.instructions.capacity() *
                                     sizeof(abc::BodyInstruction);
      if (retained > remaining / sizeof(abc::CodeOffset))
        return FunctionEmitError::kResourceLimit;
      origins.reserve(retained);
      if (origins.capacity() > remaining / sizeof(abc::CodeOffset))
        return FunctionEmitError::kResourceLimit;
      for (const auto& site : source.instructions) origins.push_back(site.offset);
    }
    std::uint32_t next_pc = 0;
    for (auto& site : source.instructions) {
      site.offset = abc::CodeOffset{next_pc};
      next_pc += site.decoded.size;
    }
    if (source_pcs) *source_pcs = std::move(origins);
    return source;
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
}
}  // namespace abcd::source
