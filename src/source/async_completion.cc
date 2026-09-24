#include "abcd/source/async_completion.h"

#include <algorithm>
#include <climits>
#include <limits>

#include "abcd/abc/control_transfer.h"
#include "abcd/abc/register_access.h"
#include "abcd/analysis/coroutine.h"

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
bool RebaseBranches(const abc::MethodBody& original,
                    std::span<const abc::CodeOffset> origins,
                    abc::MethodBody& source) {
  for (auto& site : source.instructions) {
    if (!site.branch_target) continue;
    const auto target = *site.branch_target;
    const auto retained = std::lower_bound(origins.begin(), origins.end(), target);
    if (retained == origins.end()) return false;
    const auto target_index = static_cast<std::size_t>(retained - origins.begin());
    // A direct jump to RETURN would bypass the original promise settlement.
    // Only a jump to the removed RESOLVE may be redirected to that return.
    if (*retained == target && IsReturn(source.instructions[target_index]))
      return false;
    if (*retained != target) {
      const auto old = std::lower_bound(
          original.instructions.begin(), original.instructions.end(), target,
          [](const auto& instruction, auto pc) { return instruction.offset < pc; });
      // Only a proven settlement immediately followed by its retained return
      // can be a removed branch target. Never round into a deleted VM region.
      if (old == original.instructions.end() || old->offset != target ||
          old->decoded.opcode != abc::Opcode::kASYNCFUNCTIONRESOLVE_V8 ||
          std::uint64_t(target.value) + old->decoded.size != retained->value ||
          !IsReturn(source.instructions[target_index]))
        return false;
    }
    const auto destination = source.instructions[target_index].offset;
    const auto delta = std::int64_t(destination.value) - site.offset.value;
    if (site.decoded.operand_count != 1) return false;
    auto& operand = site.decoded.operands[0];
    const auto width = operand.width_bits;
    if (operand.kind != abc::OperandKind::kImmediate || !width ||
        width > sizeof(std::int32_t) * CHAR_BIT)
      return false;
    const auto magnitude = std::int64_t{1} << (width - 1);
    if (delta < -magnitude || delta >= magnitude) return false;
    operand.raw_bits = static_cast<std::uint64_t>(delta) &
                       ((std::uint64_t{1} << width) - 1);
    site.decoded.branch_delta = delta;
    site.branch_target = destination;
  }
  return true;
}
}  // namespace
AsyncCompletion ProveAsyncCompletion(const abc::MethodBody& body,
                                     const abc::FrameLayout& frame,
                                     std::size_t bytes,
                                     std::vector<abc::CodeOffset>* source_pcs,
                                     std::vector<analysis::AwaitSite>* awaits,
                                     abc::ProfileId profile) {
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
  auto retained = handler - 3;
  const auto site_bytes = sizeof(abc::BodyInstruction) + sizeof(abc::CodeOffset);
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
  try {
  std::vector<analysis::AwaitRegion> regions;
  const bool has_await = awaits && std::any_of(sites.begin(), sites.end(),
      [&](const auto& site) {
        const auto lifted = analysis::LiftCoroutineInstruction(
            site.decoded, frame.registers);
        const auto* transfer = std::get_if<analysis::CoroutineTransfer>(&lifted);
        return transfer && transfer->kind == analysis::CoroutineTransferKind::kAwait;
      });
  if (has_await) {
    auto found = analysis::FindAwaitRegions(body, profile, frame.registers, bytes);
    if (const auto* failure = std::get_if<analysis::AwaitRegionFailure>(&found))
      return failure->kind == analysis::AwaitRegionError::kResourceLimit
          ? FunctionEmitError::kResourceLimit : FunctionEmitError::kKind;
    regions = std::get<std::vector<analysis::AwaitRegion>>(std::move(found));
  }
  const auto region_bytes = regions.capacity() * sizeof(analysis::AwaitRegion);
  if (region_bytes > bytes) return FunctionEmitError::kResourceLimit;
  bytes -= region_bytes;
  std::size_t next_region = 0;
  for (std::size_t i = enter + 2; i < handler - 2; ++i) {
    const auto& site = sites[i];
    if (next_region < regions.size() && site.offset == regions[next_region].await) {
      const auto& region = regions[next_region++];
      if (region.state != promise || region.continuation >= sites[handler - 2].offset)
        return FunctionEmitError::kKind;
      while (i + 1 < handler - 2 && sites[i + 1].offset < region.continuation) {
        ++i;
        if (sites[i].offset != region.result_store &&
            sites[i].offset != region.mode && sites[i].offset != region.mode_store)
          --retained;
      }
      continue;
    }
    abc::RegisterId settled;
    if (IsRegister(site, Opcode::kASYNCFUNCTIONRESOLVE_V8, &settled)) {
      if (settled != promise || i + 1 >= handler - 2 || !IsReturn(sites[i + 1]))
        return FunctionEmitError::kKind;
      --retained;
      continue;
    }
    const auto control = abc::GetControlTransfer(site.decoded.opcode);
    if (!control || *control == abc::ControlTransfer::kSuspend)
      return FunctionEmitError::kKind;
    if (*control == abc::ControlTransfer::kReturn) {
      if (!IsReturn(site) || i == enter + 2 ||
          !IsRegister(sites[i - 1], Opcode::kASYNCFUNCTIONRESOLVE_V8, &settled) ||
          settled != promise)
        return FunctionEmitError::kKind;
    }
    if (site.branch_target &&
        (*site.branch_target < sites[enter + 2].offset ||
         *site.branch_target > sites[handler - 1].offset))
      return FunctionEmitError::kKind;
    if (TouchesRegister(site, promise, frame.registers))
      return FunctionEmitError::kKind;
  }
    if (next_region != regions.size()) return FunctionEmitError::kKind;
    if (regions.size() > bytes / sizeof(analysis::AwaitSite))
      return FunctionEmitError::kResourceLimit;
    std::vector<analysis::AwaitSite> await_sites;
    await_sites.reserve(regions.size());
    if (await_sites.capacity() > bytes / sizeof(analysis::AwaitSite))
      return FunctionEmitError::kResourceLimit;
    bytes -= await_sites.capacity() * sizeof(analysis::AwaitSite);
    if (retained > bytes / site_bytes) return FunctionEmitError::kResourceLimit;
    abc::MethodBody source;
    source.instructions.reserve(retained);
    if (source.instructions.capacity() >
        bytes / sizeof(abc::BodyInstruction))
      return FunctionEmitError::kResourceLimit;
    source.instructions.insert(source.instructions.end(), sites.begin(),
                               sites.begin() + enter);
    next_region = 0;
    for (std::size_t i = enter + 2; i < handler - 2; ++i) {
      if (sites[i].decoded.opcode == Opcode::kASYNCFUNCTIONRESOLVE_V8) continue;
      source.instructions.push_back(sites[i]);
      if (next_region < regions.size() && sites[i].offset == regions[next_region].await) {
        const auto& region = regions[next_region++];
        await_sites.push_back({sites[i].offset, region, region.mode});
        while (i + 1 < handler - 2 && sites[i + 1].offset < region.continuation) {
          ++i;
          if (sites[i].offset == region.result_store ||
              sites[i].offset == region.mode || sites[i].offset == region.mode_store)
            source.instructions.push_back(sites[i]);
        }
      }
    }
    source.instructions.push_back(sites[handler - 1]);
    std::vector<abc::CodeOffset> origins;
    {
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
    next_region = 0;
    std::size_t next_mode = 0;
    for (auto& site : source.instructions) {
      if (next_mode < await_sites.size() &&
          site.offset == await_sites[next_mode].origin.mode)
        await_sites[next_mode++].mode_pc = abc::CodeOffset{next_pc};
      if (next_region < await_sites.size() &&
          site.offset == await_sites[next_region].origin.await)
        await_sites[next_region++].pc = abc::CodeOffset{next_pc};
      site.offset = abc::CodeOffset{next_pc};
      next_pc += site.decoded.size;
    }
    if (!RebaseBranches(body, origins, source)) return FunctionEmitError::kKind;
    if (source_pcs) *source_pcs = std::move(origins);
    if (awaits) *awaits = std::move(await_sites);
    return source;
  } catch (const std::bad_alloc&) {
    return FunctionEmitError::kResourceLimit;
  } catch (const std::length_error&) {
    return FunctionEmitError::kResourceLimit;
  }
}
}  // namespace abcd::source
