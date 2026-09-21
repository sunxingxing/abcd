#include "abcd/abc/string_entity.h"

#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
abcd::abc::StringResult Read(std::vector<std::uint8_t> payload,
                             std::size_t budget = 1024) {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(kDynamicHeaderSize);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  char path[] = "/tmp/abcd-string-XXXXXX";
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
  auto result = ReadStringEntity(file, header, {kDynamicHeaderSize}, {budget});
  Check(std::get<MetadataError>(ReadStringEntity(file, header, {0})) ==
        MetadataError::kReference);
  Check(std::get<MetadataError>(ReadStringEntity(
            file, header, {header.file_size})) == MetadataError::kReference);
  header.profile = ProfileId::kStatic0107;
  Check(std::get<MetadataError>(
            ReadStringEntity(file, header, {kDynamicHeaderSize})) ==
        MetadataError::kUnsupportedProfile);
  unlink(path);
  return result;
}
}  // namespace

int main() {
  using namespace abcd::abc;
  Check(std::get<std::u16string>(Read({1, 0})).empty());
  Check(std::get<std::u16string>(Read({7, 'a', 'b', 'c', 0})) == u"abc");
  Check(std::get<std::u16string>(Read({4, 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0})) ==
        std::u16string({0x00e9, 0x20ac}));
  Check(std::get<std::u16string>(Read({2, 0xc0, 0x80, 0})) ==
        std::u16string(1, 0));
  Check(std::get<std::u16string>(Read({2, 0xed, 0xa0, 0x80, 0})) ==
        std::u16string(1, 0xd800));
  Check(std::get<std::u16string>(Read({2, 0xed, 0xbf, 0xbf, 0})) ==
        std::u16string(1, 0xdfff));
  const std::u16string pair{0xd83d, 0xde00};
  Check(std::get<std::u16string>(Read({4, 0xf0, 0x9f, 0x98, 0x80, 0})) == pair);
  Check(std::get<std::u16string>(
            Read({4, 0xed, 0xa0, 0xbd, 0xed, 0xb8, 0x80, 0})) == pair);
  for (const auto& payload :
       std::vector<std::vector<std::uint8_t>>{{3, 0xc0, 0x80, 0},
                                              {2, 0xc0, 0x81, 0},
                                              {2, 0x80, 0},
                                              {2, 0xe0, 0x80, 0x80, 0},
                                              {2, 0xf4, 0x90, 0x80, 0x80, 0},
                                              {4, 'a', 0},
                                              {2, 0xe1, 'a', 0}}) {
    Check(std::get<MetadataError>(Read(payload)) == MetadataError::kBadString);
  }
  Check(std::get<MetadataError>(Read({2, 'a'})) == MetadataError::kTruncated);
  Check(std::get<MetadataError>(Read({7, 'a', 'b', 'c', 0}, 0)) ==
        MetadataError::kResourceLimit);
  std::vector<std::uint8_t> longer{0x91, 0x03};  // 200 UTF-16 ASCII units.
  longer.insert(longer.end(), 200, 'x');
  longer.push_back(0);
  Check(std::get<std::u16string>(Read(longer, 4096)) ==
        std::u16string(200, u'x'));
  Check(std::get<MetadataError>(Read(longer, 200)) ==
        MetadataError::kResourceLimit);
  for (const std::uint32_t length : {201U, 1024U, 8192U}) {
    std::vector<std::uint8_t> payload;
    std::uint32_t prefix = (length << 1) | 1;
    do {
      const auto part = prefix & 0x7f;
      prefix >>= 7;
      payload.push_back(static_cast<std::uint8_t>(part | (prefix ? 0x80 : 0)));
    } while (prefix);
    payload.insert(payload.end(), length, 'x');
    payload.push_back(0);
    Check(std::get<std::u16string>(Read(payload, 128 * 1024)) ==
          std::u16string(length, u'x'));
  }
}
