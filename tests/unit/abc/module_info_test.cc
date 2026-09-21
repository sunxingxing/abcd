#include "abcd/abc/module_info.h"

#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
abcd::abc::ModuleResult Read(std::vector<std::uint8_t> payload,
                             std::size_t budget = 1024 * 1024) {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(kDynamicHeaderSize);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  char path[] = "/tmp/abcd-module-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = abcd::corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
  Header header;
  header.kind = AbcKind::kDynamic;
  header.profile = ProfileId::kDynamic1206;
  header.layout = DynamicLayout{};
  header.file_size = bytes.size();
  const auto& file = *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened);
  auto result = ReadModuleInfo(file, header, {kDynamicHeaderSize}, {budget});
  Check(std::get<ModuleFailure>(ReadModuleInfo(file, header, {0})).kind ==
        ModuleError::kReference);
  header.profile = ProfileId::kStatic0107;
  Check(std::get<ModuleFailure>(
            ReadModuleInfo(file, header, {kDynamicHeaderSize}))
            .kind == ModuleError::kUnsupportedProfile);
  unlink(path);
  return result;
}
}  // namespace

int main() {
  using namespace abcd::abc;
  std::vector<std::uint8_t> empty(7 * sizeof(std::uint32_t));
  Check(std::get<ModuleInfo>(Read(empty, 0)).requests.empty());
  std::vector<std::uint8_t> bytes;
  Word(bytes, 123);  // Retained prefix; the pinned VM skips this word.
  Word(bytes, 2);
  Word(bytes, 60);
  Word(bytes, 61);
  Word(bytes, 1);
  Word(bytes, 62);
  Word(bytes, 63);
  bytes.insert(bytes.end(), {1, 0});
  Word(bytes, 1);
  Word(bytes, 64);
  bytes.insert(bytes.end(), {0, 0});
  Word(bytes, 1);
  Word(bytes, 65);
  Word(bytes, 66);
  Word(bytes, 1);
  Word(bytes, 67);
  Word(bytes, 68);
  bytes.insert(bytes.end(), {1, 0});
  Word(bytes, 1);
  bytes.insert(bytes.end(), {0, 0});
  const auto module = std::get<ModuleInfo>(Read(bytes));
  Check(module.encoded_count == 123);
  Check(module.requests.size() == 2);
  Check(module.requests[1].value == 61);
  Check(module.imports.size() == 1 && module.imports[0].local.value == 62);
  Check(module.imports[0].imported.value == 63 &&
        module.imports[0].request.value == 1);
  Check(module.namespace_imports[0].local.value == 64);
  Check(module.namespace_imports[0].request.value == 0);
  Check(module.local_exports[0].local.value == 65 &&
        module.local_exports[0].exported.value == 66);
  Check(module.indirect_exports[0].exported.value == 67 &&
        module.indirect_exports[0].imported.value == 68);
  Check(module.indirect_exports[0].request.value == 1);
  Check(module.star_exports[0].value == 0);
  Check(module.end.value == kDynamicHeaderSize + bytes.size());
  const auto required = 2 * sizeof(EntityOffset) + sizeof(ModuleImport) +
                        sizeof(NamespaceImport) + sizeof(LocalExport) +
                        sizeof(IndirectExport) + sizeof(ModuleRequestId);
  Check(std::holds_alternative<ModuleInfo>(Read(bytes, required)));
  Check(std::get<ModuleFailure>(Read(bytes, required - 1)).kind ==
        ModuleError::kResourceLimit);
  Check(std::get<ModuleFailure>(Read(bytes, 0)).kind ==
        ModuleError::kResourceLimit);
  for (std::size_t size = 0; size < bytes.size(); ++size)
    Check(std::holds_alternative<ModuleFailure>(
        Read({bytes.begin(), bytes.begin() + size})));
  auto bad = bytes;
  // Request-index fields in regular/namespace imports and indirect/star
  // exports.
  for (const auto position : {28U, 38U, 64U, 70U}) {
    bad = bytes;
    bad[position] = 2;
    Check(std::get<ModuleFailure>(Read(bad)).kind ==
          ModuleError::kRequestIndex);
    bad[position] = 0;
    bad[position + 1] = 1;
    Check(std::get<ModuleFailure>(Read(bad)).kind ==
          ModuleError::kRequestIndex);
  }
  bad = bytes;
  bad[8] = 0;
  Check(std::get<ModuleFailure>(Read(bad)).kind == ModuleError::kReference);
  for (const std::uint32_t count : {200U, 201U, 1024U, 8192U}) {
    std::vector<std::uint8_t> many;
    Word(many, 0);
    Word(many, count);
    for (std::uint32_t i = 0; i < count; ++i) Word(many, 60);
    for (unsigned section = 0; section < 5; ++section) Word(many, 0);
    Check(std::get<ModuleInfo>(Read(many)).requests.size() == count);
  }
}
