#include "abcd/abc/method_lookup.h"

#include <cstdlib>
#include <utility>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd::abc;
  for (const std::size_t count : {0U, 1U, 200U, 201U, 4096U, 65536U}) {
    std::vector<MethodInfo> methods(count);
    for (std::size_t i = 0; i < count; ++i)
      methods[i].offset.value = static_cast<std::uint32_t>(2 * (count - i));
    auto lookup = std::get<MethodLookup>(MethodLookup::Build(methods));
    Check(lookup.Size() == count);
    Check(!lookup.Find({1}));
    for (std::size_t i = 0; i < count; ++i) {
      const auto found = lookup.Find(methods[i].offset);
      Check(found && found->value == i);
    }
    Check(std::holds_alternative<MethodLookup>(
        MethodLookup::Build(methods, {lookup.AllocatedBytes()})));
    if (count) {
      Check(std::get<MethodLookupError>(
                MethodLookup::Build(methods, {lookup.AllocatedBytes() - 1})) ==
            MethodLookupError::kResourceLimit);
      Check(std::get<MethodLookupError>(MethodLookup::Build(methods, {0})) ==
            MethodLookupError::kResourceLimit);
      auto moved = std::move(lookup);
      Check(lookup.Size() == 0 && !lookup.Find(methods[0].offset));
      Check(moved.Find(methods[0].offset)->value == 0);
    }
    if (count > 1) {
      methods.back().offset = methods.front().offset;
      Check(std::get<MethodLookupError>(MethodLookup::Build(methods)) ==
            MethodLookupError::kDuplicateOffset);
    }
  }
}
