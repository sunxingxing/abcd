#ifndef ABCD_SOURCE_LEAF_SOURCES_H_
#define ABCD_SOURCE_LEAF_SOURCES_H_

#include "capture_stream.h"
#include "leaf_creation.h"

namespace abcd::source {
// Prepared output artifacts, not semantic strings: only verified child source
// templates are cached. Analysis uses typed facts; emission resolves typed
// capture holes, never parses source text. Target
// sharing saves compiler work, never shares a runtime function object.
class LeafSources {
 public:
  LeafSources(LeafSources&&) noexcept = default;
  LeafSources& operator=(LeafSources&&) noexcept = default;
  static std::variant<LeafSources, BodyEmitFailure> Build(
      const abc::MethodBody& body,
      const analysis::EnvironmentFlow& environments,
      const MethodConstants& constants, const abc::MethodLookup& methods,
      LocalLexicalBudget budget);
  std::span<const analysis::ClosureCreation> Facts() const {
    return {facts_.get(), facts_ ? count_ : 0};
  }
  // Literal template bytes only; use Write to emit bound executable source.
  std::string_view Source(std::size_t site) const {
    const auto& fragment = fragments_[sites_[site]];
    return {fragment.text.get(), fragment.size};
  }
  BodyEmitResult Write(std::size_t site, std::ostream& output,
                       EmitBudget budget) const;
  std::size_t AllocatedBytes() const { return facts_ ? bytes_ : 0; }

 private:
  struct Fragment {
    std::unique_ptr<char[]> text;
    std::size_t size = 0;
    std::unique_ptr<CaptureHole[]> holes;
    std::size_t hole_count = 0;
    std::unique_ptr<analysis::LocalCell[]> bindings;
  };
  LeafSources() = default;
  std::unique_ptr<analysis::ClosureCreation[]> facts_;
  std::unique_ptr<std::size_t[]> sites_;
  std::unique_ptr<Fragment[]> fragments_;
  std::size_t count_ = 0;
  std::size_t bytes_ = 0;
};
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LEAF_SOURCES_H_
