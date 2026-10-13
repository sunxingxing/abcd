#include "abcd/analysis/exception_regions.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace abcd::analysis {
namespace {
bool Contains(InstructionRange range, std::size_t instruction) {
  return range.begin.value <= instruction && instruction < range.end.value;
}
}

std::variant<ExceptionRegions, CatchRegionFailure> BuildExceptionRegions(
    const abc::MethodBody& body, const ExceptionFlow& flow, SemanticBudget budget) {
  using Error = CatchRegionError;
  const auto fail = [](Error kind, abc::CodeOffset pc = {}) {
    return CatchRegionFailure{kind, pc};
  };
  const auto count = body.instructions.size();
  if (!count || count > UINT32_MAX || body.tries.size() > UINT32_MAX ||
      count != flow.instructions.size()) return fail(Error::kInvalidFlow);
  try {
    ExceptionRegions result;
    std::size_t spent = 0;
    const auto charge = [&] {
      if (result.work_units == budget.work) throw fail(Error::kResourceLimit);
      ++result.work_units;
    };
    const auto reserve = [&]<class T>(std::vector<T>& items, std::size_t size) {
      if (size > (budget.bytes - spent) / sizeof(T)) throw fail(Error::kResourceLimit);
      items.reserve(size);
      if (items.capacity() > (budget.bytes - spent) / sizeof(T))
        throw fail(Error::kResourceLimit);
      spent += items.capacity() * sizeof(T);
    };
    std::vector<TryId> stack;
    std::vector<std::uint8_t> reachable;
    std::vector<ExceptionNodeId> pending;
    std::vector<std::optional<CatchScope>> entry_parents;
    auto& order = result.source_order;
    reserve(result.regions, body.tries.size());
    reserve(result.scopes, count);
    reserve(order, body.tries.size());
    reserve(stack, body.tries.size());
    reserve(entry_parents, body.tries.size());
    reserve(reachable, flow.nodes.size()); reserve(pending, flow.nodes.size());
    reachable.resize(flow.nodes.size());
    if (reachable.empty()) return fail(Error::kInvalidFlow);
    reachable[0] = 1; pending.push_back({0});
    for (std::size_t head = 0; head < pending.size(); ++head) {
      charge(); const auto& node = flow.nodes[pending[head].value];
      if (node.successor_count > node.successors.size()) return fail(Error::kInvalidFlow);
      for (const auto& arc : std::span(node.successors).first(node.successor_count)) {
        charge();
        if (arc.target.value >= reachable.size()) return fail(Error::kInvalidFlow);
        if (reachable[arc.target.value]) continue;
        reachable[arc.target.value] = 1; pending.push_back(arc.target);
      }
    }
    entry_parents.resize(body.tries.size());
    result.scopes.resize(count);
    const auto code_end = std::uint64_t(body.instructions.back().offset.value) +
                          body.instructions.back().decoded.size;
    const auto boundary = [&](std::uint64_t pc) -> InstructionId {
      charge();
      if (pc == code_end) return {static_cast<std::uint32_t>(count)};
      if (pc > UINT32_MAX) throw fail(Error::kBoundary);
      const abc::CodeOffset target{static_cast<std::uint32_t>(pc)};
      const auto found = std::lower_bound(body.instructions.begin(), body.instructions.end(), target,
          [&](const auto& instruction, auto value) { charge(); return instruction.offset < value; });
      if (found == body.instructions.end() || found->offset != target)
        throw fail(Error::kBoundary, target);
      return {static_cast<std::uint32_t>(found - body.instructions.begin())};
    };
    for (std::size_t i = 0; i < body.tries.size(); ++i) {
      charge();
      const auto& region = body.tries[i];
      if (region.handler_count != 1 || region.first_handler >= body.handlers.size())
        return fail(Error::kHandlerShape, region.offset);
      const auto& handler = body.handlers[region.first_handler];
      if (handler.type || !region.length || !handler.length)
        return fail(Error::kHandlerShape, handler.offset);
      CatchRegion candidate{{boundary(region.offset.value),
                             boundary(std::uint64_t(region.offset.value) + region.length)},
                            {boundary(handler.offset.value),
                             boundary(std::uint64_t(handler.offset.value) + handler.length)},
                            HandlerId{region.first_handler}, {}, {}};
      if (candidate.body.begin >= candidate.body.end ||
          candidate.body.end > candidate.handler.begin ||
          candidate.handler.begin >= candidate.handler.end)
        return fail(Error::kHandlerShape, handler.offset);
      result.regions.push_back(candidate);
      order.push_back({static_cast<std::uint32_t>(i)});
    }
    std::sort(order.begin(), order.end(), [&](TryId a, TryId b) {
      charge();
      const auto& left = result.regions[a.value];
      const auto& right = result.regions[b.value];
      return left.body.begin != right.body.begin ? left.body.begin < right.body.begin
                                                 : left.handler.end > right.handler.end;
    });
    const auto range = [&](CatchScope scope) {
      const auto& region = result.regions[scope.region.value];
      return scope.part == CatchPart::kProtected ? region.body : region.handler;
    };
    for (const auto id : order) {
      charge();
      auto& region = result.regions[id.value];
      while (!stack.empty() && result.regions[stack.back().value].handler.end <= region.body.begin) {
        charge();
        stack.pop_back();
      }
      if (!stack.empty()) {
        const auto parent_id = stack.back();
        const auto& parent = result.regions[parent_id.value];
        CatchScope scope{parent_id, CatchPart::kProtected};
        if (region.body.begin >= parent.handler.begin) scope.part = CatchPart::kHandler;
        const auto enclosing = range(scope);
        if (region.body.begin < enclosing.begin || region.handler.end > enclosing.end)
          return fail(Error::kCrossingScopes, body.instructions[region.body.begin.value].offset);
        region.parent = scope;
        region.outer_dispatch = scope.part == CatchPart::kProtected
            ? std::optional<TryId>{parent_id} : parent.outer_dispatch;
      }
      stack.push_back(id);
    }
    // Several nested protected scopes may begin at the same instruction. A
    // normal edge can enter all of them at once, but never a handler scope.
    // Outer-first memoization avoids walking a deep parent chain per CFG edge.
    for (const auto id : order) {
      charge();
      const auto& region = result.regions[id.value];
      entry_parents[id.value] = region.parent;
      if (region.parent && region.parent->part == CatchPart::kProtected &&
          result.regions[region.parent->region.value].body.begin == region.body.begin)
        entry_parents[id.value] = entry_parents[region.parent->region.value];
    }
    stack.clear();
    std::size_t next = 0;
    for (std::size_t i = 0; i < count; ++i) {
      charge();
      if (flow.instructions[i].pc != body.instructions[i].offset)
        return fail(Error::kInvalidFlow, body.instructions[i].offset);
      while (!stack.empty() && result.regions[stack.back().value].handler.end.value <= i) {
        charge();
        stack.pop_back();
      }
      while (next < order.size() && result.regions[order[next].value].body.begin.value == i) {
        charge();
        stack.push_back(order[next++]);
      }
      if (stack.empty()) continue;
      const auto id = stack.back();
      const auto& region = result.regions[id.value];
      if (Contains(region.body, i)) result.scopes[i] = CatchScope{id, CatchPart::kProtected};
      else if (Contains(region.handler, i)) result.scopes[i] = CatchScope{id, CatchPart::kHandler};
      else result.scopes[i] = region.parent;
    }
    for (const auto& node : flow.nodes) {
      charge();
      const auto source = node.instruction.value;
      if (source >= count || node.successor_count > node.successors.size())
        return fail(Error::kInvalidFlow);
      // Layout/provenance remains complete. Only executable arcs impose
      // lexical entry and dispatch obligations; dead predecessors do not.
      if (!reachable[&node - flow.nodes.data()]) continue;
      const auto pc = body.instructions[source].offset;
      if (node.phase == ExceptionPhase::kBefore && node.exception) {
        std::optional<TryId> selected;
        if (const auto scope = result.scopes[source])
          selected = scope->part == CatchPart::kProtected
              ? std::optional<TryId>{scope->region}
              : result.regions[scope->region.value].outer_dispatch;
        const auto& actual = node.exception->target;
        if (selected.has_value() != actual.has_value()) return fail(Error::kDispatchMismatch, pc);
        if (selected && (actual->region != *selected ||
            actual->handler != result.regions[selected->value].handler_id ||
            actual->handler_pc != body.instructions[
                result.regions[selected->value].handler.begin.value].offset))
          return fail(Error::kDispatchMismatch, pc);
      }
      for (std::size_t e = 0; e < node.successor_count; ++e) {
        charge();
        const auto& arc = node.successors[e];
        if (arc.target.value >= flow.nodes.size()) return fail(Error::kInvalidFlow, pc);
        const auto target = flow.nodes[arc.target.value].instruction.value;
        if (target >= count) return fail(Error::kInvalidFlow, pc);
        if (node.phase != ExceptionPhase::kCommit) continue;
        if (flow.nodes[arc.target.value].phase != ExceptionPhase::kBefore ||
            arc.kind == ExceptionArcKind::kException) return fail(Error::kInvalidFlow, pc);
        const auto scope = result.scopes[target];
        if (!scope || Contains(range(*scope), source)) continue;
        const auto& region = result.regions[scope->region.value];
        const auto entry_parent = entry_parents[scope->region.value];
        if (scope->part == CatchPart::kProtected && target == region.body.begin.value &&
            (!entry_parent || Contains(range(*entry_parent), source))) continue;
        return fail(Error::kNormalEntry, pc);
      }
    }
    result.allocated_bytes = result.regions.capacity() * sizeof(CatchRegion) +
        result.scopes.capacity() * sizeof(std::optional<CatchScope>) +
        result.source_order.capacity() * sizeof(TryId);
    return result;
  } catch (const CatchRegionFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return fail(Error::kResourceLimit);
  } catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}  // namespace abcd::analysis
