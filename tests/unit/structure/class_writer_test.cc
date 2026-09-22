#include <unistd.h>

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sstream>

#include "abcd/corpus/abc_file.h"
#include "record_text.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace
int main() {
  using namespace abcd;
  for (const bool bad_key : {false, true}) {
    std::vector<std::uint8_t> bytes(256);
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (std::size_t i = 0; i < sizeof(value); ++i)
        bytes[at + i] = value >> (8 * i);
    };
    // Regional pool: constructor, malformed buffer, valid buffer.
    word(64, 100);
    word(68, 128);
    word(72, 160);
    bytes[80] = 3;
    bytes[81] = 'x';
    bytes[82] = 0;
    bytes[84] = 1;
    bytes[85] = 0xff;
    bytes[86] = 0;
    const auto literal = [&](std::size_t at, std::uint32_t key,
                             std::uint32_t value) {
      word(at, 6);
      bytes[at + 4] = 5;
      word(at + 5, key);
      bytes[at + 9] = 5;
      word(at + 10, value);
      bytes[at + 14] = 2;
      word(at + 15, 0);
    };
    literal(128, bad_key ? 84 : 80, bad_key ? 80 : 84);
    literal(160, 80, 80);
    constexpr std::uint8_t kDefineClass = 0x35, kReturnUndefined = 0x65;
    const std::vector<std::uint8_t> code{
        1, 0, 19, 0, kDefineClass,    0, 0, 0, 1,
        0, 0, 0,  0, kDefineClass,    0, 0, 0, 2,
        0, 0, 0,  0, kReturnUndefined};
    std::copy(code.begin(), code.end(), bytes.begin() + 200);
    char path[] = "/tmp/abcd-class-writer-XXXXXX";
    const auto fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) ==
          static_cast<ssize_t>(bytes.size()));
    close(fd);
    auto opened = corpus::AbcFile::Open(path);
    const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
    unlink(path);
    abc::Header header;
    header.kind = abc::AbcKind::kDynamic;
    header.profile = abc::ProfileId::kDynamic1206;
    header.layout = abc::DynamicLayout{};
    header.file_size = bytes.size();
    abc::MetadataIndex index;
    abc::MethodInfo caller, ctor;
    caller.offset = {96};
    caller.code = abc::CodeInfo{{200}, {204}, 1, 0, 19, 0};
    ctor.offset = {100};
    index.methods = {caller, ctor};
    abc::IndexRegion region;
    region.start = 60;
    region.end = bytes.size();
    region.methods = {3, 64};
    index.regions.push_back(region);
    const auto lookup =
        std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
    structure::RecordText text(file, header, {});
    std::ostringstream out;
    out << "{\"known_module\":true";
    structure::WriteClassSites(text, index, lookup, caller, out);
    out << '}';
    const auto json = nlohmann::json::parse(out.str());
    Check(json["known_module"] == true);
    Check(json["class_sites"].size() == 2);
    Check(json["class_sites"][0].contains("error"));
    Check(!json["class_sites"][0].contains("constructor"));
    Check(json["class_sites"][1]["constructor"] == 100);
    Check(json["class_sites"][1]["properties"].size() == 1);
  }
}
