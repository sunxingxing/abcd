#include "../../../tools/family_value_inspect.h"
#include "abcd/abc/method_attributes.h"
#include "analysis/ssa_fixture.h"

#include <nlohmann/json.hpp>
#include <unistd.h>

#include <sstream>

namespace {
using namespace abcd;
// Synthetic metadata fixture with file-backed Code entities and a real
// DEFINEFUNC occurrence. It is not a full-file metadata parser acceptance test.
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256, kTarget = 300, kCodeStart = 400;
constexpr std::uint8_t kImplicitArguments = 3;
constexpr unsigned kByteBits = 8, kFunctionKindShift = 8;
std::uint8_t Op(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::initializer_list<std::uint8_t> code) {
  constexpr std::size_t kSingleByteUlebLimit = 128;
  test::Check(code.size() < kSingleByteUlebLimit);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {0, kImplicitArguments, static_cast<std::uint8_t>(code.size()), 0});
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{offset}, {start}, 0, kImplicitArguments, static_cast<std::uint32_t>(code.size()), 0};
}
void FileDraft() {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (i * kByteBits));
  const auto caller = Append(bytes, {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0,
      Op(abc::Opcode::kRETURNUNDEFINED)});
  const auto target = Append(bytes, {Op(abc::Opcode::kDEBUGGER), Op(abc::Opcode::kLDAI_IMM32),
      7, 0, 0, 0, Op(abc::Opcode::kINC_IMM8), 0, Op(abc::Opcode::kRETURN)});
  char path[] = "/tmp/abcd-partial-file-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0 && write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  test::Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206; header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCodeStart, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "partial"; index.methods[1].name_utf16_length = 7;
  index.methods[1].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto identity = source::MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  std::ostringstream positions;
  const auto report = InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
      positions, FamilyInspectMode::kSourceDraft);
  if (!report.value("draft_prepared", false)) std::cerr << report << '\n';
  test::Check(report.value("draft_prepared", false));
  test::Check(report.value("draft_incomplete", false));
  const auto source = report.at("draft_source").get<std::string>();
  test::Check(source.starts_with("/* INCOMPLETE"));
  test::Check(source.find("debugger;") < source.find("UNRECOVERED"));
  test::Check(source.find("return") == std::string::npos);
  test::Check(source.find("PC 1") != std::string::npos);
  test::Check(!report.contains("family_writes"));
  test::Check(positions.str().find("\"incomplete\":true") != std::string::npos);
  // The values-only diagnostic keeps the previous complete-only construction.
  std::ostringstream strict_positions;
  const auto strict = InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
      strict_positions, FamilyInspectMode::kValues);
  test::Check(!strict.value("family_tree_prepared", false));
  test::Check(!strict.contains("draft_source") && strict_positions.str().empty());
  test::Check(unlink(path) == 0);
}
}  // namespace
int main() { FileDraft(); }
