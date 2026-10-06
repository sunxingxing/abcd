#ifndef ABCD_SOURCE_NAME_RESERVATIONS_H_
#define ABCD_SOURCE_NAME_RESERVATIONS_H_

#include <algorithm>
#include <span>
#include <string>

#include "abcd/source/source_tree.h"

namespace abcd::source {
struct NameReference {
  std::u16string name;
  FunctionOwnerId owner;
};
// A borrowed, name/owner-sorted index for one occurrence subtree. Metadata and
// generated binding names are preferences, never permission to shadow a real
// descendant reference. The family owns this index during synchronous tree
// construction; no view survives in a constructed or published tree.
struct NameReservations {
  std::span<const NameReference> references;
  FunctionOwnerId first;
  std::size_t count;
  template <class Step>
  bool Contains(std::u16string_view name, Step step) const {
    if (!count) return false;
    const auto found = std::lower_bound(references.begin(), references.end(), name,
        [&](const NameReference& reference, std::u16string_view text) {
          step(std::min(reference.name.size(), text.size()) + 1);
          return reference.name < text;
        });
    const auto last = std::upper_bound(found, references.end(), name,
        [&](std::u16string_view text, const NameReference& reference) {
          step(std::min(reference.name.size(), text.size()) + 1);
          return text < reference.name;
        });
    const auto user = std::lower_bound(found, last, first.value,
        [&](const NameReference& reference, std::uint32_t owner) {
          step(1); return reference.owner.value < owner;
        });
    step(1);
    return user != last && static_cast<std::uint64_t>(user->owner.value) <
        static_cast<std::uint64_t>(first.value) + count;
  }
};
}
#endif
