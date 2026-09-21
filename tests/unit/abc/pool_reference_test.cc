#include "abcd/abc/pool_reference.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <variant>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Put32(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
}  // namespace

int main() {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(512);
  Header header;
  header.kind = AbcKind::kDynamic;
  header.profile = ProfileId::kDynamic1206;
  header.file_size = bytes.size();
  header.layout = DynamicLayout{{}, {}, {}, {2, 64}};
  // Same pool slots in adjacent regions have different targets. The pool
  // contains heterogeneous entities, not just methods.
  for (std::uint32_t region = 0; region < 2; ++region) {
    const auto base = 64 + region * 40;
    Put32(bytes, base, 200 + region * 100);
    Put32(bytes, base + 4, 300 + region * 100);
    Put32(bytes, base + 16, 4);
    Put32(bytes, base + 20, 144 + region * 16);
    for (std::uint32_t slot = 0; slot < 3; ++slot)
      Put32(bytes, 144 + region * 16 + slot * 4, 400 + region * 30 + slot * 10);
    Put32(bytes, 144 + region * 16 + 12, region == 0 ? 0 : bytes.size());
  }
  char path[] = "/tmp/abcd-pool-reference-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  auto opened = abcd::corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened);
  auto built = BuildMetadataIndex(file, header);
  Check(std::holds_alternative<MetadataIndex>(built));
  const auto& index = std::get<MetadataIndex>(built);
  Check(index.regions.size() == 2);
  const std::array kinds{PoolEntityKind::kMethod, PoolEntityKind::kString,
                         PoolEntityKind::kLiteralArray};
  for (std::uint32_t region = 0; region < 2; ++region) {
    for (std::uint32_t slot = 0; slot < kinds.size(); ++slot) {
      const auto result = ResolvePoolReference(
          file, header, index, {200 + region * 100}, {slot}, kinds[slot]);
      Check(std::holds_alternative<PoolReference>(result));
      const auto ref = std::get<PoolReference>(result);
      Check(ref.kind == kinds[slot]);
      Check(ref.offset.value == 400 + region * 30 + slot * 10);
    }
    Check(std::get<MetadataError>(ResolvePoolReference(
              file, header, index, {200 + region * 100}, {3},
              PoolEntityKind::kString)) == MetadataError::kReference);
  }
  for (const auto pc : {199U, 400U}) {
    const auto result = ResolvePoolReference(file, header, index, {pc}, {0},
                                             PoolEntityKind::kString);
    Check(std::get<MetadataError>(result) == MetadataError::kRegionRange);
  }
  for (const auto slot : {4U, UINT32_MAX}) {
    const auto result = ResolvePoolReference(file, header, index, {200}, {slot},
                                             PoolEntityKind::kString);
    Check(std::get<MetadataError>(result) == MetadataError::kIndexRange);
  }
  Header unsupported = header;
  unsupported.profile = ProfileId::kStatic0107;
  Check(std::get<MetadataError>(ResolvePoolReference(
            file, unsupported, index, {200}, {0}, PoolEntityKind::kString)) ==
        MetadataError::kUnsupportedProfile);
  // A size change is deterministic even on filesystems whose timestamps
  // coalesce two writes within the same clock tick.
  const std::uint8_t changed = 1;
  Check(pwrite(fd, &changed, 1, bytes.size()) == 1);
  Check(std::get<MetadataError>(ResolvePoolReference(
            file, header, index, {200}, {0}, PoolEntityKind::kString)) ==
        MetadataError::kFileChanged);
  close(fd);
  unlink(path);
}
