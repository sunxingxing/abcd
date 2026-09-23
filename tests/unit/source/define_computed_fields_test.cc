#include "abcd/source/static_fields.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd;
  constexpr std::size_t kFileBytes = 1024;
  constexpr std::size_t kMethod = 200;
  constexpr std::size_t kPool = 300;
  constexpr std::size_t kCode = 400;
  constexpr std::size_t kKey = 500;
  constexpr std::size_t kValue = 520;
  constexpr std::uint8_t kLocals = 2;
  constexpr std::uint8_t kArguments = 3;
  constexpr std::uint8_t kReceiver = kLocals + kArguments - 1;
  constexpr std::uint8_t kObject = 0;
  constexpr std::uint8_t kKeyRegister = 1;
  constexpr std::uint32_t kIndexedKey = 201;
  constexpr std::int32_t kIndexedValue = 42;
  constexpr std::size_t kCodeHeaderBytes = 4;
  constexpr std::array<std::uint8_t, 31> kInstructions{
      0x60, kReceiver, 0x61, kObject,       // this -> object register
      0x3e, 0, 0, 0x61, kKeyRegister,       // string pool slot 0 -> key
      0x3e, 1, 0,                          // string pool slot 1 -> accumulator
      0xfb, 1, 0, kKeyRegister, kObject,   // DefineFieldByValue
      0x62, kIndexedValue, 0, 0, 0,        // integer value
      0xfb, 2, 0, kIndexedKey, 0, 0, 0, kObject,
      0x65};                              // DefineFieldByIndex, return
  static_assert(kInstructions.size() < UINT8_MAX);
  std::array<std::uint8_t, kFileBytes> bytes{};
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i)
      bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
  };
  word(kPool, kKey);
  word(kPool + sizeof(std::uint32_t), kValue);
  const auto string = [&](std::size_t at, std::string_view text) {
    bytes[at] = static_cast<std::uint8_t>(text.size() * 2 + 1);
    std::copy(text.begin(), text.end(), bytes.begin() + at + 1);
  };
  string(kKey, "total");
  string(kValue, "ready");
  bytes[kCode] = kLocals;
  bytes[kCode + 1] = kArguments;
  bytes[kCode + 2] = kInstructions.size();
  std::copy(kInstructions.begin(), kInstructions.end(),
            bytes.begin() + kCode + kCodeHeaderBytes);
  char path[] = "/tmp/abcd-define-computed-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1301;
  header.file_size = bytes.size();
  header.layout = abc::DynamicLayout{};
  abc::MetadataIndex index;
  index.regions.push_back({100, kFileBytes, {}, {2, kPool}, {}, {}});
  abc::MethodInfo method;
  method.offset = {kMethod};
  method.code = abc::CodeInfo{{kCode}, {kCode + kCodeHeaderBytes},
                              kLocals, kArguments, kInstructions.size(), 0};
  const auto recovered = source::RecoverStaticFields(
      file, header, index, method, 1024 * 1024);
  const auto* fields = std::get_if<source::StaticFields>(&recovered);
  Check(fields != nullptr && fields->fields.size() == 2);
  Check(fields->fields[0].name == u"total" &&
        fields->fields[1].name == u"201");
  Check(std::get<std::u16string>(std::get<source::StaticAtom>(
            fields->values[fields->fields[0].value])) == u"ready");
  Check(std::get<std::int32_t>(std::get<source::StaticAtom>(
            fields->values[fields->fields[1].value])) == kIndexedValue);
  Check(!file.CheckUnchanged());
  Check(unlink(path) == 0);
}
