#ifndef ABCD_SOURCE_LEAF_SOURCES_H_
#define ABCD_SOURCE_LEAF_SOURCES_H_

#include "leaf_creation.h"

namespace abcd::source {
// Prepared output artifacts, not semantic strings: only verified child source
// is cached. Analysis uses typed facts; emission copies immutable bytes. Target
// sharing saves compiler work, never shares a runtime function object.
class LeafSources {
 public:
  LeafSources(LeafSources&&) noexcept = default;
  LeafSources& operator=(LeafSources&&) noexcept = default;
  static std::variant<LeafSources, BodyEmitFailure> Build(
      const abc::MethodBody& body, const MethodConstants& constants,
      const abc::MethodLookup& methods, LocalLexicalBudget budget);
  std::span<const analysis::IndependentCreation> Facts() const {
    return {facts_.get(), facts_ ? count_ : 0};
  }
  std::string_view Source(std::size_t site) const {
    const auto& fragment = fragments_[sites_[site]];
    return {fragment.text.get(), fragment.size};
  }
  std::size_t AllocatedBytes() const { return facts_ ? bytes_ : 0; }

 private:
  struct Fragment {
    std::unique_ptr<char[]> text;
    std::size_t size = 0;
  };
  LeafSources() = default;
  std::unique_ptr<analysis::IndependentCreation[]> facts_;
  std::unique_ptr<std::size_t[]> sites_;
  std::unique_ptr<Fragment[]> fragments_;
  std::size_t count_ = 0;
  std::size_t bytes_ = 0;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEAF_SOURCES_H_
