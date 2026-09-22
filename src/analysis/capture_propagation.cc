#include "abcd/analysis/capture_propagation.h"

#include <algorithm>
#include <map>
#include <memory_resource>
#include <new>
#include <tuple>

namespace abcd::analysis {
namespace {
auto Key(CaptureRequirement requirement) {
  return std::tuple{requirement.context.value, requirement.captured.depth.value,
                    requirement.captured.slot.value};
}
struct RequirementLess {
  bool operator()(CaptureRequirement left, CaptureRequirement right) const {
    return Key(left) < Key(right);
  }
};
// Account actual PMR requests, including old and new vector buffers during
// growth. This resource never escapes Build, nor does storage allocated by it.
class CaptureScratch final : public std::pmr::memory_resource {
 public:
  explicit CaptureScratch(std::size_t limit) : limit_(limit) {}
  std::size_t Live() const { return live_; }
  std::size_t Peak() const { return peak_; }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit_ - live_) throw std::bad_alloc{};
    auto* allocated =
        std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live_ += bytes;
    peak_ = std::max(peak_, live_);
    return allocated;
  }
  void do_deallocate(void* memory, std::size_t bytes,
                     std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    live_ -= bytes;
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  std::size_t limit_;
  std::size_t live_ = 0;
  std::size_t peak_ = 0;
};
}  // namespace

std::variant<CapturePropagation, CapturePropagationFailure>
CapturePropagation::Build(std::span<const CaptureContext> contexts,
                          std::span<const CaptureRequirement> seeds,
                          CapturePropagationBudget budget) {
  CaptureContextId current;
  std::optional<CapturedSlot> captured;
  const auto fail = [&](auto cause) -> CapturePropagationFailure {
    return {cause, current, captured};
  };
  if (contexts.size() > UINT32_MAX ||
      contexts.size() > budget.bytes / sizeof(CaptureContextInfo))
    return fail(CapturePropagationError::kResourceLimit);
  for (std::size_t i = 0; i < contexts.size(); ++i) {
    current = {static_cast<std::uint32_t>(i)};
    if (!contexts[i].method)
      return fail(CapturePropagationError::kInvalidContext);
    if (contexts[i].parent && contexts[i].parent->context.value >= i)
      return fail(CapturePropagationError::kInvalidParent);
  }
  try {
    CapturePropagation result;
    result.context_count_ = contexts.size();
    const auto context_bytes = contexts.size() * sizeof(CaptureContextInfo);
    if (!contexts.empty()) {
      result.contexts_ =
          std::make_unique<CaptureContextInfo[]>(contexts.size());
      for (std::size_t i = 0; i < contexts.size(); ++i)
        result.contexts_[i] = {contexts[i].method->Origin(),
                               contexts[i].parent};
    }
    CaptureScratch scratch{budget.bytes - context_bytes};
    std::pmr::map<CaptureRequirement, CaptureFactId, RequirementLess> index{
        &scratch};
    std::pmr::vector<CaptureFact> facts{&scratch};
    const auto admit = [&](CaptureRequirement requirement) {
      const auto found = index.find(requirement);
      if (found != index.end()) return found->second;
      if (facts.size() == UINT32_MAX || facts.size() == facts.max_size() ||
          index.size() == index.max_size())
        throw std::bad_alloc{};
      const CaptureFactId id{static_cast<std::uint32_t>(facts.size())};
      index.emplace(requirement, id);
      facts.push_back({requirement, CaptureBoundary{}});
      return id;
    };
    for (const auto seed : seeds) {
      current = seed.context;
      captured = seed.captured;
      if (current.value >= contexts.size())
        return fail(CapturePropagationError::kInvalidContext);
      if (!contexts[current.value].method->Captures().Find(seed.captured))
        return fail(CapturePropagationError::kMissingSeed);
      admit(seed);
    }
    for (std::size_t cursor = 0; cursor < facts.size(); ++cursor) {
      // Copy before admission: admitting a parent can reallocate facts.
      const auto requirement = facts[cursor].requirement;
      current = requirement.context;
      captured = requirement.captured;
      const auto& context = contexts[current.value];
      if (!context.parent) continue;
      const auto parent = *context.parent;
      ++result.transfers_;
      const auto rebased = contexts[parent.context.value].method->Rebase(
          parent.creation, context.method->Origin(), requirement.captured);
      const auto* binding = std::get_if<LexicalBinding>(&rebased);
      if (!binding) {
        if (const auto* error = std::get_if<MethodCaptureError>(&rebased))
          return fail(*error);
        if (const auto* error = std::get_if<EnvironmentError>(&rebased))
          return fail(*error);
        return fail(std::get<CaptureRebaseError>(rebased));
      }
      if (const auto* local =
              std::get_if<EnvironmentId>(&binding->environment)) {
        facts[cursor].destination =
            CaptureLocal{parent.context, *local, binding->slot};
      } else {
        const auto next = admit(
            {parent.context,
             {std::get<CapturedDepth>(binding->environment), binding->slot}});
        facts[cursor].destination = next;
      }
    }
    constexpr auto kFactBytes = sizeof(CaptureFact) + sizeof(CaptureFactId);
    if (facts.size() >
        (budget.bytes - context_bytes - scratch.Live()) / kFactBytes)
      return fail(CapturePropagationError::kResourceLimit);
    result.fact_count_ = facts.size();
    result.peak_ =
        context_bytes +
        std::max(scratch.Peak(), scratch.Live() + facts.size() * kFactBytes);
    if (!facts.empty()) {
      result.facts_ = std::make_unique<CaptureFact[]>(facts.size());
      result.index_ = std::make_unique<CaptureFactId[]>(facts.size());
      std::copy(facts.begin(), facts.end(), result.facts_.get());
      std::size_t at = 0;
      for (const auto& entry : index) result.index_[at++] = entry.second;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return fail(CapturePropagationError::kResourceLimit);
  }
}

const CaptureFact* CapturePropagation::Find(
    CaptureRequirement requirement) const {
  if (!index_) return nullptr;
  const auto* begin = index_.get();
  const auto* end = begin + fact_count_;
  const auto* found = std::lower_bound(
      begin, end, requirement, [&](CaptureFactId id, CaptureRequirement key) {
        return Key(facts_[id.value].requirement) < Key(key);
      });
  return found != end &&
                 Key(facts_[found->value].requirement) == Key(requirement)
             ? &facts_[found->value]
             : nullptr;
}

const CaptureFact* CapturePropagation::Find(CaptureFactId id) const {
  return id.value < Facts().size() ? &facts_[id.value] : nullptr;
}

std::size_t CapturePropagation::AllocatedBytes() const {
  return Contexts().size() * sizeof(CaptureContextInfo) +
         Facts().size() * (sizeof(CaptureFact) + sizeof(CaptureFactId));
}
}  // namespace abcd::analysis
