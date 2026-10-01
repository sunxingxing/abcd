#include "../../../src/source/module_binding_name.h"
#include "abcd/corpus/abc_file.h"

#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool value) { if (!value) std::abort(); }
std::uint64_t Run(std::size_t count, bool invalid = false) {
  std::vector<std::uint8_t> bytes(abc::kDynamicHeaderSize);
  const auto text = [&](std::string_view name) {
    const abc::EntityOffset at{static_cast<std::uint32_t>(bytes.size())};
    auto length = name.size() * 2 + 1;
    constexpr auto kLebPayloadBits = 7U;
    constexpr auto kLebPayloadMask = (1U << kLebPayloadBits) - 1;
    constexpr auto kLebContinue = 1U << kLebPayloadBits;
    do {
      auto part = static_cast<std::uint8_t>(length & kLebPayloadMask);
      length >>= kLebPayloadBits;
      bytes.push_back(part | (length ? kLebContinue : 0));
    } while (length);
    bytes.insert(bytes.end(), name.begin(), name.end()); bytes.push_back(0);
    return at;
  };
  // One long entity used by many aliases: input and output are linear in N.
  const auto named = text(std::string(count, 'a'));
  const auto named_equal = text(std::string(count, 'a'));
  const auto collision = text("$default$$$$user");
  const auto sentinel = text(invalid ? "*unknown*" : "*default*");
  const auto sentinel_equal = text(invalid ? "*unknown*" : "*default*");
  abc::ModuleInfo module;
  for (std::size_t i = 0; i < count; ++i) module.local_exports.push_back({i % 2 ? named_equal : named, {}});
  module.local_exports.push_back({collision, {}});
  module.local_exports.push_back({sentinel, {}});
  module.local_exports.push_back({sentinel_equal, {}}); // Adjacent text equality, distinct entities.
  module.local_exports.push_back({named, {}});
  module.local_exports.push_back({sentinel, {}}); // Nonadjacent equality retains a separate slot.
  char path[] = "/tmp/abcd-module-binding-name-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206; header.file_size = bytes.size();
  auto built = ResolveModuleBindingNames(*file, header, module);
  if (invalid) {
    Check(std::get<ModuleBindingNameError>(std::get<ModuleBindingNameFailure>(built)) == ModuleBindingNameError::kName);
    Check(unlink(path) == 0); return 0;
  }
  const auto& result = std::get<ModuleBindingNames>(built);
  Check(result.locals.size() == 5 && result.evidence.size() == 5);
  Check(result.locals[0] == result.locals[3] && result.locals[1] == u"$default$$$$user");
  Check(result.locals[2] == u"$default$$$$$2" && result.locals[4] == u"$default$$$$$4");
  Check(result.evidence[0] == NameEvidence::kMetadata && result.evidence[2] == NameEvidence::kSynthesized);
  Check(std::all_of(result.rows.begin(), result.rows.begin() + count, [](auto slot) { return slot == 0; }));
  Check(result.rows[count + 1] == 2 && result.rows[count + 2] == 2 && result.rows.back() == 4);
  Check(std::holds_alternative<ModuleBindingNames>(ResolveModuleBindingNames(*file, header, module, {result.allocated_bytes, result.work})));
  Check(std::get<ModuleBindingNameError>(std::get<ModuleBindingNameFailure>(
      ResolveModuleBindingNames(*file, header, module, {0}))) == ModuleBindingNameError::kResourceLimit);
  Check(std::get<ModuleBindingNameError>(std::get<ModuleBindingNameFailure>(
      ResolveModuleBindingNames(*file, header, module, {result.allocated_bytes, result.work - 1}))) == ModuleBindingNameError::kWorkLimit);
  Check(unlink(path) == 0); return result.work;
}
}
int main() {
  Run(1); Run(1, true);
  const auto a = Run(200), b = Run(201), c = Run(400);
  std::cout << "work " << a << ' ' << b << ' ' << c << '\n';
  Check(b < a * 2 && c < b * 3);
}
