#include "leaf_sources.h"

#include <algorithm>
#include <cstring>
#include <ostream>
#include <streambuf>
#include <tuple>

#include "abcd/abc/method_target.h"
#include "abcd/analysis/function_creation.h"
#include "lexical_name.h"

namespace abcd::source {
namespace {
struct Candidate {
  abc::MethodPosition target;
  analysis::FunctionLength length;
  std::size_t instruction;
  std::size_t site;
};
auto Key(const Candidate& candidate) {
  return std::tuple(candidate.target.value, candidate.length.value);
}
class MeasureBuffer : public CaptureBuffer {
 public:
  bool Capture(analysis::CapturedSlot) override {
    if (holes == SIZE_MAX) return false;
    ++holes;
    return true;
  }
  std::size_t holes = 0;

 private:
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};
class FragmentBuffer : public CaptureBuffer {
 public:
  FragmentBuffer(std::span<char> bytes, std::span<CaptureHole> holes)
      : remaining_(bytes), holes_(holes) {}
  bool Capture(analysis::CapturedSlot cell) override {
    if (holes_.empty()) return false;
    holes_.front() = {written_, cell};
    holes_ = holes_.subspan(1);
    return true;
  }
  bool Complete() const { return remaining_.empty() && holes_.empty(); }

 private:
  std::streamsize xsputn(const char* bytes, std::streamsize count) override {
    if (count < 0 || static_cast<std::size_t>(count) > remaining_.size())
      return 0;
    std::memcpy(remaining_.data(), bytes, static_cast<std::size_t>(count));
    remaining_ = remaining_.subspan(static_cast<std::size_t>(count));
    written_ += static_cast<std::size_t>(count);
    return count;
  }
  std::span<char> remaining_;
  std::span<CaptureHole> holes_;
  std::size_t written_ = 0;
};
}  // namespace

std::variant<LeafSources, BodyEmitFailure> LeafSources::Build(
    const abc::MethodBody& body, const analysis::EnvironmentFlow& environments,
    const MethodConstants& constants, const abc::MethodLookup& methods,
    LocalLexicalBudget budget) {
  LeafSources result;
  abc::CodeOffset offset{};
  const auto failure = [&](BodyEmitError kind) {
    return BodyEmitFailure{kind, offset, {}, {}};
  };
  try {
    if (environments.before.size() != body.instructions.size())
      return failure(BodyEmitError::kCreation);
    for (const auto& instruction : body.instructions)
      result.count_ += std::holds_alternative<analysis::FunctionCreation>(
          analysis::LiftFunctionCreation(instruction.decoded));
    if (result.count_ == 0) return result;
    constexpr auto kSiteBytes = sizeof(analysis::ClosureCreation) +
                                sizeof(std::size_t) + sizeof(Fragment);
    constexpr auto kBuildBytes = kSiteBytes + sizeof(Candidate);
    if (result.count_ > budget.bytes / kBuildBytes)
      return failure(BodyEmitError::kResourceLimit);
    budget.bytes -= result.count_ * kBuildBytes;
    result.bytes_ = result.count_ * kSiteBytes;
    result.facts_ =
        std::make_unique<analysis::ClosureCreation[]>(result.count_);
    result.sites_ = std::make_unique<std::size_t[]>(result.count_);
    result.fragments_ = std::make_unique<Fragment[]>(result.count_);
    auto candidates = std::make_unique<Candidate[]>(result.count_);
    std::size_t next = 0;
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      const auto& instruction = body.instructions[i];
      const auto lifted = analysis::LiftFunctionCreation(instruction.decoded);
      const auto* creation = std::get_if<analysis::FunctionCreation>(&lifted);
      if (!creation) continue;
      offset = instruction.offset;
      if (creation->kind != analysis::CreationKind::kFunction)
        return failure(BodyEmitError::kCreation);
      const auto resolved = abc::ResolveMethodTarget(
          constants.file, constants.header, constants.index, methods,
          constants.method, creation->method);
      const auto* target = std::get_if<abc::MethodTarget>(&resolved);
      if (!target) return failure(BodyEmitError::kCreation);
      result.facts_[next] = {offset};
      candidates[next] = {target->position, creation->length, i, next};
      ++next;
    }
    std::sort(candidates.get(), candidates.get() + result.count_,
              [](const auto& a, const auto& b) { return Key(a) < Key(b); });
    std::size_t fragments = 0;
    auto remaining_output = budget.output.bytes;
    for (std::size_t begin = 0; begin < result.count_;) {
      std::size_t end = begin + 1;
      while (end < result.count_ &&
             Key(candidates[begin]) == Key(candidates[end]))
        ++end;
      const auto& site = body.instructions[candidates[begin].instruction];
      offset = site.offset;
      auto child_budget = budget;
      child_budget.output.bytes = remaining_output;
      MeasureBuffer measured;
      std::ostream discard(&measured);
      const auto checked =
          WriteLeafCreation(site, constants, methods, discard, child_budget);
      if (const auto* failed = std::get_if<BodyEmitFailure>(&checked))
        return *failed;
      const auto bytes = std::get<std::size_t>(checked);
      // Every site repeats this expression's bytes, but the child is prepared
      // only once even when different pool slots alias the same target method.
      if (bytes != 0 && end - begin > remaining_output / bytes)
        return BodyEmitFailure{
            BodyEmitError::kCreation, offset, {}, EmitError::kOutputLimit};
      remaining_output -= (end - begin) * bytes;
      if (bytes > budget.bytes) return failure(BodyEmitError::kResourceLimit);
      budget.bytes -= bytes;
      result.bytes_ += bytes;
      auto& fragment = result.fragments_[fragments];
      const auto holes = measured.holes;
      if (holes > budget.bytes / sizeof(CaptureHole))
        return failure(BodyEmitError::kResourceLimit);
      budget.bytes -= holes * sizeof(CaptureHole);
      result.bytes_ += holes * sizeof(CaptureHole);
      fragment.text = std::make_unique<char[]>(bytes);
      fragment.size = bytes;
      if (holes) fragment.holes = std::make_unique<CaptureHole[]>(holes);
      fragment.hole_count = holes;
      FragmentBuffer buffer({fragment.text.get(), bytes},
                            {fragment.holes.get(), holes});
      std::ostream output(&buffer);
      child_budget.bytes = budget.bytes;
      child_budget.output.bytes = bytes;
      const auto emitted =
          WriteLeafCreation(site, constants, methods, output, child_budget);
      if (const auto* failed = std::get_if<BodyEmitFailure>(&emitted))
        return *failed;
      if (std::get<std::size_t>(emitted) != bytes || !buffer.Complete())
        return failure(BodyEmitError::kCreation);
      // Bind per creation, never cache parent owners with the target template.
      // Every semantic captured load/store currently has exactly one hole;
      // future text elision must retain a separate complete proof requirement.
      const auto rows = end - begin;
      if (holes > budget.bytes / sizeof(analysis::LocalCell) / rows)
        return failure(BodyEmitError::kResourceLimit);
      const auto binding_count = holes * rows;
      const auto binding_bytes = binding_count * sizeof(analysis::LocalCell);
      budget.bytes -= binding_bytes;
      result.bytes_ += binding_bytes;
      if (binding_count)
        fragment.bindings =
            std::make_unique<analysis::LocalCell[]>(binding_count);
      for (auto i = begin; i < end; ++i) {
        const auto& candidate = candidates[i];
        offset = body.instructions[candidate.instruction].offset;
        if (!environments.before[candidate.instruction])
          return failure(BodyEmitError::kCreation);
        auto row = std::span(fragment.bindings.get(), binding_count)
                       .subspan((i - begin) * holes, holes);
        for (std::size_t h = 0; h < holes; ++h) {
          const auto cell = fragment.holes[h].cell;
          const auto resolved = environments.environments.ResolveCapture(
              *environments.before[candidate.instruction], cell.depth,
              cell.slot);
          const auto* binding =
              std::get_if<analysis::LexicalBinding>(&resolved);
          if (!binding) return failure(BodyEmitError::kCreation);
          const auto* owner =
              std::get_if<analysis::EnvironmentId>(&binding->environment);
          if (!owner) return failure(BodyEmitError::kCreation);
          row[h] = {*owner, binding->slot};
        }
        result.facts_[candidate.site].captures = row;
        result.sites_[candidates[i].site] = fragments;
      }
      ++fragments;
      begin = end;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return failure(BodyEmitError::kResourceLimit);
  } catch (const std::length_error&) {
    return failure(BodyEmitError::kResourceLimit);
  }
}

BodyEmitResult LeafSources::Write(std::size_t site, std::ostream& output,
                                  EmitBudget budget) const {
  const auto& fragment = fragments_[sites_[site]];
  const auto& fact = facts_[site];
  std::size_t written = 0;
  std::optional<EmitError> error;
  const auto text = [&](std::string_view value) {
    if (error) return;
    if (value.size() > budget.bytes - written) {
      error = EmitError::kOutputLimit;
      return;
    }
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) {
      error = EmitError::kWrite;
      return;
    }
    written += value.size();
  };
  try {
    std::size_t begin = 0;
    for (std::size_t h = 0; h < fragment.hole_count && !error; ++h) {
      const auto end = fragment.holes[h].offset;
      text({fragment.text.get() + begin, end - begin});
      const LexicalName name{fact.captures[h]};
      text(name.Text());
      begin = end;
    }
    text({fragment.text.get() + begin, fragment.size - begin});
  } catch (const std::ios_base::failure&) {
    error = EmitError::kWrite;
  }
  if (error)
    return BodyEmitFailure{BodyEmitError::kCreation, fact.offset, {}, *error};
  return written;
}
}  // namespace abcd::source
