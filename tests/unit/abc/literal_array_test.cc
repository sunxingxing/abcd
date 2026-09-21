#include "abcd/abc/literal_array.h"

#include <unistd.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Word(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned i = 0; i < sizeof(value); ++i)
    bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
abcd::abc::LiteralResult Read(std::vector<std::uint8_t> payload,
                              std::size_t budget = 1024 * 1024) {
  using namespace abcd::abc;
  std::vector<std::uint8_t> bytes(kDynamicHeaderSize);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  char path[] = "/tmp/abcd-literal-XXXXXX";
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
  auto result = ReadLiteralArray(
      *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened), header,
      {kDynamicHeaderSize}, {budget});
  const auto& file = *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened);
  Check(std::get<LiteralFailure>(ReadLiteralArray(file, header, {0})).kind ==
        LiteralError::kReference);
  header.profile = ProfileId::kStatic0107;
  Check(std::get<LiteralFailure>(
            ReadLiteralArray(file, header, {kDynamicHeaderSize}))
            .kind == LiteralError::kUnsupportedProfile);
  unlink(path);
  return result;
}
}  // namespace

int main() {
  using namespace abcd::abc;
  Check(std::get<LiteralArray>(Read({0, 0, 0, 0})).entries.empty());
  std::vector<std::uint8_t> bytes;
  Word(bytes, 12);  // Six ordered tag/value pairs.
  bytes.push_back(0x01);
  bytes.push_back(2);  // Runtime converts nonzero to true.
  bytes.push_back(0x02);
  Word(bytes, UINT32_MAX);
  bytes.push_back(0x04);
  Word(bytes, 0);
  Word(bytes, 0x80000000);  // -0 bits.
  bytes.push_back(0x06);
  Word(bytes, kDynamicHeaderSize);
  bytes.push_back(0x09);
  bytes.push_back(3);
  bytes.push_back(0);
  bytes.push_back(0xff);
  bytes.push_back(0);
  const auto array = std::get<LiteralArray>(Read(bytes));
  Check(array.entries.size() == 6);
  Check(array.entries[0].tag == LiteralTag::kBoolean);
  Check(std::get<LiteralBits>(array.entries[0].value).value == 2);
  Check(std::get<LiteralBits>(array.entries[1].value).value == UINT32_MAX);
  Check(std::get<LiteralBits>(array.entries[2].value).value ==
        UINT64_C(0x8000000000000000));
  Check(std::get<EntityOffset>(array.entries[3].value).value ==
        kDynamicHeaderSize);
  Check(array.entries[4].tag == LiteralTag::kMethodAffiliate);
  Check(std::get<LiteralBits>(array.entries[4].value).value == 3);
  Check(array.entries[5].tag == LiteralTag::kNull);
  Check(array.end.value == kDynamicHeaderSize + bytes.size());
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    Check(std::holds_alternative<LiteralFailure>(Read(
        std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + length))));
  }
  Check(std::get<LiteralFailure>(Read(bytes, 0)).kind ==
        LiteralError::kResourceLimit);
  bytes.pop_back();
  Check(std::get<LiteralFailure>(Read(bytes)).kind == LiteralError::kTruncated);
  Check(std::get<LiteralFailure>(Read({1, 0, 0, 0, 1, 0})).kind ==
        LiteralError::kOddCount);
  Check(std::get<LiteralFailure>(Read({2, 0, 0, 0, 0xfe, 0})).kind ==
        LiteralError::kUnknownTag);
  Check(std::get<LiteralFailure>(Read({2, 0, 0, 0, 0x0a, 0})).kind ==
        LiteralError::kPackedArray);
  Check(std::get<LiteralFailure>(Read({2, 0, 0, 0, 0x05, 0, 0, 0, 0})).kind ==
        LiteralError::kReference);
  for (const auto tag : {0x05, 0x06, 0x07, 0x16, 0x18, 0x1a, 0x1b, 0x1c}) {
    bytes.clear();
    Word(bytes, 2);
    bytes.push_back(tag);
    Word(bytes, kDynamicHeaderSize);
    const auto entry = std::get<LiteralArray>(Read(bytes)).entries.front();
    Check(static_cast<unsigned>(entry.tag) == static_cast<unsigned>(tag));
    Check(std::get<EntityOffset>(entry.value).value == kDynamicHeaderSize);
  }
  for (const auto tag : {0x03, 0x17}) {
    bytes.clear();
    Word(bytes, 2);
    bytes.push_back(tag);
    Word(bytes, UINT32_MAX);
    const auto entry = std::get<LiteralArray>(Read(bytes)).entries.front();
    Check(std::get<LiteralBits>(entry.value).value == UINT32_MAX);
  }
  for (const auto tag : {0x08, 0x19}) {
    Check(std::get<LiteralBits>(
              std::get<LiteralArray>(
                  Read({2, 0, 0, 0, static_cast<std::uint8_t>(tag), 0xff}))
                  .entries.front()
                  .value)
              .value == 0xff);
  }
  Check(std::get<LiteralFailure>(Read({2, 0, 0, 0, 0, 0})).kind ==
        LiteralError::kUnsupportedTag);
  for (const std::uint32_t length : {200U, 201U, 1024U, 8192U}) {
    bytes.clear();
    Word(bytes, length * 2);
    for (std::uint32_t i = 0; i < length; ++i) {
      bytes.push_back(1);
      bytes.push_back(1);
    }
    Check(std::get<LiteralArray>(Read(bytes)).entries.size() == length);
  }
}
