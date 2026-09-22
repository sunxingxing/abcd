#include "leaf_sources.h"

#include <algorithm>
#include <cstring>
#include <ostream>
#include <streambuf>
#include <tuple>

#include "abcd/abc/method_target.h"
#include "abcd/analysis/function_creation.h"

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
class DiscardBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize count) override {
    return count;
  }
};
class FragmentBuffer : public std::streambuf {
 public:
  explicit FragmentBuffer(std::span<char> bytes) : remaining_(bytes) {}

 private:
  std::streamsize xsputn(const char* bytes, std::streamsize count) override {
    if (count < 0 || static_cast<std::size_t>(count) > remaining_.size())
      return 0;
    std::memcpy(remaining_.data(), bytes, static_cast<std::size_t>(count));
    remaining_ = remaining_.subspan(static_cast<std::size_t>(count));
    return count;
  }
  std::span<char> remaining_;
};
}  // namespace

std::variant<LeafSources, BodyEmitFailure> LeafSources::Build(
    const abc::MethodBody& body, const MethodConstants& constants,
    const abc::MethodLookup& methods, LocalLexicalBudget budget) {
  LeafSources result;
  abc::CodeOffset offset{};
  const auto failure = [&](BodyEmitError kind) {
    return BodyEmitFailure{kind, offset, {}, {}};
  };
  try {
    for (const auto& instruction : body.instructions)
      result.count_ += std::holds_alternative<analysis::FunctionCreation>(
          analysis::LiftFunctionCreation(instruction.decoded));
    if (result.count_ == 0) return result;
    constexpr auto kSiteBytes = sizeof(analysis::IndependentCreation) +
                                sizeof(std::size_t) + sizeof(Fragment);
    constexpr auto kBuildBytes = kSiteBytes + sizeof(Candidate);
    if (result.count_ > budget.bytes / kBuildBytes)
      return failure(BodyEmitError::kResourceLimit);
    budget.bytes -= result.count_ * kBuildBytes;
    result.bytes_ = result.count_ * kSiteBytes;
    result.facts_ =
        std::make_unique<analysis::IndependentCreation[]>(result.count_);
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
    DiscardBuffer discarded;
    std::ostream discard(&discarded);
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
      fragment.text = std::make_unique<char[]>(bytes);
      fragment.size = bytes;
      FragmentBuffer buffer({fragment.text.get(), bytes});
      std::ostream output(&buffer);
      child_budget.bytes = budget.bytes;
      child_budget.output.bytes = bytes;
      const auto emitted =
          WriteLeafCreation(site, constants, methods, output, child_budget);
      if (const auto* failed = std::get_if<BodyEmitFailure>(&emitted))
        return *failed;
      if (std::get<std::size_t>(emitted) != bytes)
        return failure(BodyEmitError::kCreation);
      for (auto i = begin; i < end; ++i)
        result.sites_[candidates[i].site] = fragments;
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
}  // namespace abcd::source
