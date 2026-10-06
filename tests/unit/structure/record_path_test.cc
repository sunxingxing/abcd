#include "abcd/structure/record_path.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <source_location>

#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd;
void Check(bool value,
           std::source_location location = std::source_location::current()) {
  if (!value) {
    std::fprintf(stderr, "record path assertion at line %u\n", location.line());
    std::abort();
  }
}

// Exercise the production ABC string reader and source-identity gatherer.
// UTF-16 units use Panda's length ULEB and MUTF-8, including surrogate units;
// output expectations below independently specify canonical UTF-8 paths.
class SourceFile {
 public:
  explicit SourceFile(std::span<const std::u16string> names) {
    std::vector<std::uint8_t> bytes(abc::kDynamicHeaderSize);
    constexpr unsigned kLebBits = 7, kUtf8Bits = 6;
    constexpr std::uint32_t kLebMask = 0x7f, kUtf8Mask = 0x3f;
    for (const auto& name : names) {
      abc::RecordInfo record;
      record.offset = {static_cast<std::uint32_t>(bytes.size())};
      record.source_file = record.offset;
      record.descriptor =
          "Lfallback/r" + std::to_string(index.records.size()) + ";";
      index.records.push_back(record);
      const bool ascii =
          std::all_of(name.begin(), name.end(),
                      [](char16_t unit) { return unit != 0 && unit < 0x80; });
      auto length = static_cast<std::uint32_t>((name.size() << 1) | ascii);
      do {
        const auto part = length & kLebMask;
        length >>= kLebBits;
        bytes.push_back(static_cast<std::uint8_t>(part | (length ? 0x80 : 0)));
      } while (length);
      for (const auto unit : name) {
        if (unit != 0 && unit < 0x80)
          bytes.push_back(unit);
        else if (unit < 0x800) {
          bytes.push_back(0xc0 | (unit >> kUtf8Bits));
          bytes.push_back(0x80 | (unit & kUtf8Mask));
        } else {
          bytes.push_back(0xe0 | (unit >> (2 * kUtf8Bits)));
          bytes.push_back(0x80 | ((unit >> kUtf8Bits) & kUtf8Mask));
          bytes.push_back(0x80 | (unit & kUtf8Mask));
        }
      }
      bytes.push_back(0);
    }
    const auto fd = mkstemp(path);
    Check(fd >= 0);
    Check(write(fd, bytes.data(), bytes.size()) ==
          static_cast<ssize_t>(bytes.size()));
    Check(close(fd) == 0);
    auto opened = corpus::AbcFile::Open(path);
    Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
    file = std::move(std::get<std::unique_ptr<corpus::AbcFile>>(opened));
    header.kind = abc::AbcKind::kDynamic;
    header.profile = abc::ProfileId::kDynamic1206;
    header.file_size = bytes.size();
    header.layout = abc::DynamicLayout{};
  }
  SourceFile(std::initializer_list<std::u16string> names)
      : SourceFile(std::span(names.begin(), names.size())) {}
  ~SourceFile() {
    file.reset();
    unlink(path);
  }
  auto Plan(std::size_t budget = 16 * 1024 * 1024) const {
    return structure::PlanRecordSourcePaths(*file, header, index, budget);
  }
  abc::MetadataIndex index;

 private:
  char path[sizeof("/tmp/abcd-record-path-XXXXXX")] =
      "/tmp/abcd-record-path-XXXXXX";
  abc::Header header;
  std::unique_ptr<corpus::AbcFile> file;
};

void EmbeddedPaths() {
  using structure::PathError;
  using structure::RecordPath;
  using structure::SourcePathOrigin;
  SourceFile ordinary{u"app|lib|1.0.0|src/Foo.ts", u"app|app|1.0.0|src/Bar.ets",
                      u"app|@scope/lib|1.0.0|src/Baz.mjs"};
  const auto paths = std::get<std::vector<RecordPath>>(ordinary.Plan());
  Check(paths[0].stem == "lib/src/Foo");
  Check(paths[1].stem == "app/src/Bar");
  Check(paths[2].stem == "%40scope/lib/src/Baz");
  for (const auto& path : paths)
    Check(path.origin == SourcePathOrigin::kPackageIdentity);
  std::reverse(ordinary.index.records.begin(), ordinary.index.records.end());
  const auto reversed = std::get<std::vector<RecordPath>>(ordinary.Plan());
  for (std::size_t i = 0; i < paths.size(); ++i)
    Check(paths[i].stem == reversed[paths.size() - i - 1].stem);
  Check(std::get<PathError>(ordinary.Plan(0)) == PathError::kResourceLimit);
  Check(std::get<PathError>(ordinary.Plan(2100)) == PathError::kResourceLimit);

  SourceFile versions{u"app|lib|1.0.0|src/Foo.ts", u"app|lib|2.0.0|src/Foo.ts"};
  auto distinct = std::get<std::vector<RecordPath>>(versions.Plan());
  Check(distinct[0].stem == "lib/1.0.0/src/Foo");
  Check(distinct[1].stem == "lib/2.0.0/src/Foo");
  SourceFile entries{u"phone|lib|1.0.0|src/Foo.ts",
                     u"tablet|lib|1.0.0|src/Foo.ts",
                     u"phone|lib|2.0.0|src/Foo.ts"};
  distinct = std::get<std::vector<RecordPath>>(entries.Plan());
  Check(distinct[0].stem == "phone/lib/1.0.0/src/Foo");
  Check(distinct[1].stem == "tablet/lib/src/Foo");
  Check(distinct[2].stem == "phone/lib/2.0.0/src/Foo");

  SourceFile unicode{u"app|\u5305|1.0.0|\u76ee\u5f55/\U0001f600.ts",
                     u"app|%e5%8c%85|1.0.0|src/Foo.ts"};
  const auto encoded = std::get<std::vector<RecordPath>>(unicode.Plan());
  Check(encoded[0].stem == "%e5%8c%85/%e7%9b%ae%e5%bd%95/%f0%9f%98%80");
  Check(encoded[1].stem == "%25e5%258c%2585/src/Foo");

  const std::array invalid{
      std::u16string(u"not-a-package-file.ts"),
      std::u16string(u"app|lib|v|../Foo.ts"),
      std::u16string(u"app|lib|v|/src/Foo.ts"),
      std::u16string(u"app|lib|v|src//Foo.ts"),
      std::u16string(u"app|lib|v|src/./Foo.ts"),
      std::u16string(u"app|lib|v|src/Foo.cpp"),
      std::u16string(u"app|lib|v|src\\Foo.ts"),
      std::u16string(u"app|lib|v|src/Foo.ts|extra"),
      std::u16string(u"app|lib|v|src/\u0001.ts"),
      std::u16string(u"app|lib|v|src/") + char16_t{0xd800} + u".ts",
      std::u16string(u"app|lib|v|src/") + char16_t{0xdc00} + u".ts",
      std::u16string(u"app|lib|v|src/") + char16_t{0xd800} + u"x.ts",
      std::u16string(u"app|lib|v|src/") + char16_t{0} + u".ts"};
  SourceFile unsupported(invalid);
  const auto fallback = std::get<std::vector<RecordPath>>(unsupported.Plan());
  for (std::size_t i = 0; i < fallback.size(); ++i) {
    Check(fallback[i].stem == "fallback/r" + std::to_string(i));
    Check(fallback[i].origin == SourcePathOrigin::kRecordDescriptor);
  }
  SourceFile missing{u""};
  missing.index.records[0].source_file.reset();
  Check(std::get<std::vector<RecordPath>>(missing.Plan())[0].stem ==
        "fallback/r0");
  // Ownership failures remain fatal. Unsupported filename spelling does not
  // grant authority to another record's debug info.
  missing.index.records[0].method_count = 1;
  missing.index.methods.push_back({});
  Check(std::get<PathError>(missing.Plan()) == PathError::kSourceEvidence);

  for (const auto& other : {u"app|lib|v|src/Foo.ts", u"app|lib|v|src/foo.ts",
                            u"app|lib|v|src/Foo.js"}) {
    SourceFile collision{u"app|lib|v|src/Foo.ts", other};
    Check(std::get<PathError>(collision.Plan()) == PathError::kCollision);
  }
  // Namespace qualification may itself meet a literal path. Reject the real
  // collision; record order and entity IDs cannot silently select a winner.
  SourceFile qualified_collision{u"app|lib|1|src/Foo.ts",
                                 u"app|lib|2|src/Foo.ts",
                                 u"app|lib/1|v|src/Foo.ts"};
  Check(std::get<PathError>(qualified_collision.Plan()) ==
        PathError::kCollision);
  SourceFile component_limit{u"app|lib|v|src/" + std::u16string(23, u'\u5305') +
                             u".ts"};
  Check(std::get<PathError>(component_limit.Plan()) ==
        PathError::kComponentLimit);
  std::u16string long_path = u"app|lib|v|";
  for (std::size_t i = 0; i < 21; ++i)
    long_path += std::u16string(100, u'x') + u'/';
  SourceFile path_limit{long_path + u"Foo.ts"};
  Check(std::get<PathError>(path_limit.Plan()) == PathError::kPathLimit);
  for (const std::size_t count : {200U, 201U, 400U, 800U}) {
    std::vector<std::u16string> names;
    for (std::size_t i = 0; i < count; ++i) {
      const auto digits = std::to_string(i);
      names.push_back(u"app|lib|" +
                      std::u16string(digits.begin(), digits.end()) +
                      u"|src/Foo.ts");
    }
    SourceFile large(names);
    const auto result = std::get<std::vector<RecordPath>>(large.Plan());
    Check(result.size() == count);
    for (std::size_t i = 0; i < count; ++i)
      Check(result[i].stem == "lib/" + std::to_string(i) + "/src/Foo");
  }
}
}  // namespace

int main() {
  EmbeddedPaths();
  using namespace abcd;
  const auto check = [](bool value) {
    if (!value) std::abort();
  };
  abc::RecordInfo record;
  record.offset = {123};
  record.descriptor = "Lentry/ets/pages/Index;";
  const auto path = structure::PlanRecordPath(record);
  check(std::get<structure::RecordPath>(path).stem ==
        "entry/ets/pages/Index~r0000007b");
  record.descriptor = "L/../a\\b/%;";
  check(
      std::get<structure::RecordPath>(structure::PlanRecordPath(record)).stem ==
      "%00/%2e%2e/a%5cb/%25~r0000007b");
  record.offset = {124};
  check(std::get<structure::RecordPath>(structure::PlanRecordPath(record))
            .stem.ends_with("~r0000007c"));
  record.descriptor = "invalid";
  check(std::get<structure::PathError>(structure::PlanRecordPath(record)) ==
        structure::PathError::kDescriptor);
  record.descriptor = "L" + std::string(256, 'a') + ";";
  check(std::get<structure::PathError>(structure::PlanRecordPath(record)) ==
        structure::PathError::kComponentLimit);

  std::array<abc::RecordInfo, 2> records;
  records[0].descriptor = "Lcom.example.pasteboard/entry/ets/pages/Home;";
  records[1].descriptor = "Lcom.example.pasteboard/entry/ets/pages/PageA;";
  const auto sources = structure::PlanRecordSourcePaths(records);
  check(std::get<std::vector<structure::RecordPath>>(sources)[0].stem ==
        "com.example.pasteboard/entry/ets/pages/Home");
  check(std::get<std::vector<structure::RecordPath>>(sources)[1].stem ==
        "com.example.pasteboard/entry/ets/pages/PageA");
  records[1].descriptor = records[0].descriptor;
  records[1].offset = {124};
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(
            records)) == structure::PathError::kCollision);
  records[1].descriptor = "Lcom.example.pasteboard/entry/ets/pages/home;";
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(
            records)) == structure::PathError::kCollision);
  records[0].descriptor = "L/../a\\b/%;";
  records[1].descriptor = "Lx/a.b;";
  const auto escaped = structure::PlanRecordSourcePaths(records);
  check(std::get<std::vector<structure::RecordPath>>(escaped)[0].stem ==
        "%00/.%2e/a%5cb/%25");
  check(std::get<std::vector<structure::RecordPath>>(escaped)[1].stem ==
        "x/a.b");
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(
            records, 0)) == structure::PathError::kResourceLimit);
}
