#include "abcd/analysis/exception_dispatch.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <utility>

namespace abcd::analysis {
namespace {

struct Event {
  abc::CodeOffset pc;
  std::uint32_t region;
  bool start;
};

// The pinned dynamic ECMAScript runtime supplies this rule. Recognition of
// historical containers does not independently certify their VM semantics.
bool Supported(abc::ProfileId profile) {
  switch (profile) {
    case abc::ProfileId::kDynamic9:
    case abc::ProfileId::kDynamic1102:
    case abc::ProfileId::kDynamic1202:
    case abc::ProfileId::kDynamic1206:
    case abc::ProfileId::kDynamic1301:
    case abc::ProfileId::kDynamic2400:
      return true;
    case abc::ProfileId::kUnknown:
    case abc::ProfileId::kStatic0107:
      return false;
  }
  return false;
}

bool Boundary(const abc::MethodBody& body, std::uint32_t pc,
              std::uint32_t code_end) {
  if (pc == code_end) {
    return true;
  }
  auto it = std::lower_bound(
      body.instructions.begin(), body.instructions.end(), abc::CodeOffset{pc},
      [](const abc::BodyInstruction& instruction, abc::CodeOffset offset) {
        return instruction.offset < offset;
      });
  return it != body.instructions.end() && it->offset.value == pc;
}

bool FitsBudget(std::size_t regions, std::size_t bytes) {
  constexpr std::size_t kPerRegion =
      2 * sizeof(Event) + sizeof(std::uint32_t) + 2 * sizeof(DispatchSpan);
  return regions <= bytes / kPerRegion;
}

template <typename T>
bool ReserveCharged(std::vector<T>* values, std::size_t count,
                    std::size_t* charged, std::size_t limit) {
  if (count > (limit - *charged) / sizeof(T)) {
    return false;
  }
  values->reserve(count);
  if (values->capacity() > (limit - *charged) / sizeof(T)) {
    return false;
  }
  *charged += values->capacity() * sizeof(T);
  return true;
}

}  // namespace

std::optional<DispatchTarget> ExceptionDispatch::Find(
    abc::CodeOffset pc) const {
  auto it =
      std::upper_bound(spans.begin(), spans.end(), pc,
                       [](abc::CodeOffset offset, const DispatchSpan& span) {
                         return offset < span.begin;
                       });
  if (it == spans.begin()) {
    return std::nullopt;
  }
  --it;
  return pc < it->end ? std::optional<DispatchTarget>(it->target)
                      : std::nullopt;
}

DispatchResult BuildExceptionDispatch(const abc::MethodBody& body,
                                      abc::ProfileId profile,
                                      DispatchBudget budget) {
  if (!Supported(profile)) {
    return DispatchFailure{DispatchError::kUnsupportedProfile, {0}};
  }
  if (body.instructions.empty()) {
    return DispatchFailure{DispatchError::kEmptyBody, {0}};
  }
  if (body.tries.size() > std::numeric_limits<std::uint32_t>::max() ||
      body.handlers.size() > std::numeric_limits<std::uint32_t>::max()) {
    return DispatchFailure{DispatchError::kResourceLimit, {0}};
  }

  std::uint32_t code_end = 0;
  for (const auto& instruction : body.instructions) {
    if (instruction.offset.value != code_end || instruction.decoded.size == 0 ||
        instruction.decoded.size >
            std::numeric_limits<std::uint32_t>::max() - code_end) {
      return DispatchFailure{DispatchError::kMalformedBody, instruction.offset};
    }
    code_end += instruction.decoded.size;
  }

  std::uint32_t next_handler = 0;
  std::size_t eligible = 0;
  for (const auto& region : body.tries) {
    if (region.first_handler != next_handler ||
        region.handler_count > body.handlers.size() - next_handler) {
      return DispatchFailure{DispatchError::kMalformedBody, region.offset};
    }
    next_handler += region.handler_count;
    if (region.length > code_end - std::min(region.offset.value, code_end) ||
        region.offset.value > code_end ||
        !Boundary(body, region.offset.value, code_end) ||
        !Boundary(body, region.offset.value + region.length, code_end)) {
      return DispatchFailure{DispatchError::kInvalidBoundary, region.offset};
    }
    eligible += region.length > 0 && region.handler_count > 0;
  }
  if (next_handler != body.handlers.size()) {
    return DispatchFailure{DispatchError::kMalformedBody, {0}};
  }
  for (const auto& handler : body.handlers) {
    if (handler.offset.value >= code_end ||
        handler.length > code_end - handler.offset.value ||
        !Boundary(body, handler.offset.value, code_end) ||
        !Boundary(body, handler.offset.value + handler.length, code_end)) {
      return DispatchFailure{DispatchError::kInvalidBoundary, handler.offset};
    }
  }
  if (!FitsBudget(eligible, budget.bytes)) {
    return DispatchFailure{DispatchError::kResourceLimit, {0}};
  }

  try {
    std::vector<Event> events;
    std::vector<std::uint32_t> active;
    ExceptionDispatch dispatch;
    std::size_t charged = 0;
    if (!ReserveCharged(&events, 2 * eligible, &charged, budget.bytes) ||
        !ReserveCharged(&active, eligible, &charged, budget.bytes) ||
        !ReserveCharged(&dispatch.spans, 2 * eligible, &charged,
                        budget.bytes)) {
      return DispatchFailure{DispatchError::kResourceLimit, {0}};
    }
    for (std::uint32_t region = 0; region < body.tries.size(); ++region) {
      const auto& trial = body.tries[region];
      if (trial.length == 0 || trial.handler_count == 0) {
        continue;
      }
      events.push_back({trial.offset, region, true});
      events.push_back({{trial.offset.value + trial.length}, region, false});
    }
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.pc < b.pc; });

    // Dynamic Method::FindCatchBlock walks encoded tries and chooses the first
    // catch of the first covering nonempty try, regardless of catch type.
    // The interpreter places the thrown value in the handler accumulator;
    // dispatch construction does not classify which instructions may throw.
    auto greater = std::greater<std::uint32_t>{};
    for (std::size_t i = 0; i < events.size();) {
      const auto pc = events[i].pc;
      do {
        if (events[i].start) {
          active.push_back(events[i].region);
          std::push_heap(active.begin(), active.end(), greater);
        }
        ++i;
      } while (i < events.size() && events[i].pc == pc);
      while (!active.empty()) {
        const auto& trial = body.tries[active.front()];
        if (trial.offset.value + trial.length > pc.value) {
          break;
        }
        std::pop_heap(active.begin(), active.end(), greater);
        active.pop_back();
      }
      if (active.empty() || i == events.size() || pc == events[i].pc) {
        continue;
      }
      const auto region = active.front();
      const auto handler = body.tries[region].first_handler;
      const DispatchTarget target{
          {region}, {handler}, body.handlers[handler].offset};
      if (!dispatch.spans.empty() && dispatch.spans.back().end == pc &&
          dispatch.spans.back().target == target) {
        dispatch.spans.back().end = events[i].pc;
      } else {
        dispatch.spans.push_back({pc, events[i].pc, target});
      }
    }
    return dispatch;
  } catch (const std::bad_alloc&) {
    return DispatchFailure{DispatchError::kResourceLimit, {0}};
  }
}

}  // namespace abcd::analysis
