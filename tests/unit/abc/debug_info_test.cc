#include "abcd/abc/debug_info.h"

#include <unistd.h>

#include <cstdlib>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  do {
    auto byte = static_cast<std::uint8_t>(value & 0x7f);
    value >>= 7;
    bytes.push_back(byte | (value ? 0x80 : 0));
  } while (value);
}
abcd::abc::DebugResult Read(
    std::vector<std::uint8_t> payload, std::size_t budget = 1024 * 1024,
    abcd::abc::ProfileId profile = abcd::abc::ProfileId::kDynamic1206,
    abcd::abc::IndexSection table = {1, 64}, std::uint32_t program = 62) {
  using namespace abcd;
  // Offset 60 is an empty string, 62 is an END_SEQUENCE program,
  // offset 64 holds its global index entry; debug entity begins at 68.
  constexpr std::uint32_t kTable = 64, kDebug = 68;
  std::vector<std::uint8_t> bytes(kDebug);
  bytes[60] = 1;
  for (std::size_t i = 0; i < sizeof(program); ++i)
    bytes[kTable + i] = static_cast<std::uint8_t>(program >> (8 * i));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  char path[] = "/tmp/abcd-debug-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = profile;
  header.file_size = bytes.size();
  abc::DynamicLayout layout;
  layout.line_number_programs = table;
  header.layout = layout;
  // Keep the input unchanged until all lazy reads and identity checks finish.
  auto result = abc::ReadDebugInfo(file, header, {kDebug}, {budget});
  unlink(path);
  return result;
}
}  // namespace
int main() {
  using namespace abcd::abc;
  const std::vector<std::uint8_t> payload{7, 2, 60, 0, 2, 0x12, 0x34, 0};
  const auto info = std::get<DebugInfo>(Read(payload));
  Check(info.line_start == 7 && info.parameters.size() == 2);
  Check(info.parameters[0]->value == 60 && !info.parameters[1]);
  Check(info.constant_pool.value == 73 && info.constant_pool_bytes == 2);
  Check(info.line_program.value == 62 && info.end.value == 76);
  constexpr auto kParameterBytes = 2 * sizeof(std::optional<EntityOffset>);
  Check(std::holds_alternative<DebugInfo>(Read(payload, kParameterBytes)));
  Check(std::get<MetadataError>(Read(payload, kParameterBytes - 1)) ==
        MetadataError::kResourceLimit);
  for (const IndexSection table :
       {IndexSection{1, 65}, {1, 80}, {0, 64}, {100, 64}})
    Check(std::get<MetadataError>(Read(payload, 1024, ProfileId::kDynamic1206,
                                       table)) == MetadataError::kIndexRange);
  for (const std::uint32_t program : {0U, 76U, 0xffffffffU})
    Check(std::get<MetadataError>(
              Read(payload, 1024, ProfileId::kDynamic1206, {1, 64}, program)) ==
          MetadataError::kReference);
  for (std::size_t size = 0; size < payload.size(); ++size)
    Check(std::holds_alternative<MetadataError>(
        Read({payload.begin(), payload.begin() + size})));
  Check(std::get<MetadataError>(Read(payload, 0)) ==
        MetadataError::kResourceLimit);
  Check(std::get<MetadataError>(Read(payload, 1024, ProfileId::kUnknown)) ==
        MetadataError::kUnsupportedProfile);
  auto invalid = payload;
  invalid.back() = 1;
  Check(std::get<MetadataError>(Read(invalid)) == MetadataError::kIndexRange);
  invalid = payload;
  invalid[2] = 1;
  Check(std::get<MetadataError>(Read(invalid)) == MetadataError::kReference);
  Check(std::get<MetadataError>(Read({0xff, 0xff, 0xff, 0xff, 0xff, 0})) ==
        MetadataError::kMalformedLeb);
  for (const std::uint32_t count : {0U, 200U, 201U, 8192U}) {
    std::vector<std::uint8_t> many{1};
    Uleb(many, count);
    many.insert(many.end(), count, 0);
    many.insert(many.end(), {0, 0});
    Check(std::get<DebugInfo>(Read(many)).parameters.size() == count);
  }
}
