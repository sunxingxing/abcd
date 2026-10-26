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
constexpr std::uint32_t kLeaf = 320, kSibling = 340;
// Dynamic FrameLayout: function, new.target, receiver precede explicit args.
constexpr std::uint8_t kImplicitArguments = 3, kReceiverArgument = 2;
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
std::string FileDraft(bool nested) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (i * kByteBits));
  if (nested) {
    for (unsigned i = 0; i < sizeof(kLeaf); ++i) {
      bytes[kPool + sizeof(kTarget) + i] = static_cast<std::uint8_t>(kLeaf >> (i * kByteBits));
      bytes[kPool + sizeof(kTarget) + sizeof(kLeaf) + i] = static_cast<std::uint8_t>(kSibling >> (i * kByteBits));
    }
  }
  const auto caller = Append(bytes, {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0,
      Op(abc::Opcode::kRETURNUNDEFINED)});
  // Boolean NEG is now covered by primitive numeric facts. Use the unknown
  // invocation receiver instead: its coercion may run user code, and the
  // raw numeric analysis has no tag-exclusion proof. Its coercion cannot be
  // discarded even when its result is unused.
  constexpr auto kLegacyNegate = static_cast<std::uint16_t>(abc::Opcode::kDEPRECATED_NEG_PREF_V8);
  const auto leaf = Append(bytes, {Op(abc::Opcode::kDEBUGGER), Op(abc::Opcode::kLDA_V8), kReceiverArgument,
      Op(abc::Opcode::kSTA_V8), 0, static_cast<std::uint8_t>(kLegacyNegate),
      static_cast<std::uint8_t>(kLegacyNegate >> kByteBits), 0, Op(abc::Opcode::kRETURNUNDEFINED)});
  const auto target = nested ? Append(bytes, {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 1, 0, 0,
      Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 2, 0, 0, Op(abc::Opcode::kRETURNUNDEFINED)}) : leaf;
  const auto sibling = nested ? Append(bytes, {Op(abc::Opcode::kLDAI_IMM32), 7, 0, 0, 0,
      Op(abc::Opcode::kRETURN)}) : leaf;
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
  index.regions.push_back({kCaller, kCodeStart, {}, {nested ? 3U : 1U, kPool}, {}, {}});
  index.methods.resize(nested ? 4 : 2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "partial"; index.methods[1].name_utf16_length = 7;
  index.methods[1].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  if (nested) {
    index.methods[2].offset = {kLeaf}; index.methods[2].code = leaf;
    index.methods[2].name = "leaf"; index.methods[2].name_utf16_length = 4;
    index.methods[3].offset = {kSibling}; index.methods[3].code = sibling;
    index.methods[3].name = "sibling"; index.methods[3].name_utf16_length = 7;
    index.methods[2].access_flags = index.methods[3].access_flags = index.methods[1].access_flags;
  }
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto identity = source::MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  identity.contract = source::kSourceContract;  // Pinned partial-analysis boundary.
  std::ostringstream positions;
  const auto report = InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
      positions, FamilyInspectMode::kSourceDraft);
  if (!report.value("draft_prepared", false)) std::cerr << report << '\n';
  test::Check(report.value("draft_prepared", false));
  test::Check(!report.value("draft_incomplete", true));
  const auto source = report.at("draft_source").get<std::string>();
  test::Check(source.find("INCOMPLETE") == std::string::npos);
  test::Check(source.find("debugger;") < source.find("-"));
  test::Check(source.find("UNRECOVERED") == std::string::npos);
  if (!nested) test::Check(source.find("return") == std::string::npos);
  else test::Check(source.find("return 7;") != std::string::npos);

  test::Check(report.contains("family_writes"));
  test::Check(report.at("family_writes").at("captured_stores") == 0);
  test::Check(positions.str().find("\"incomplete\":false") != std::string::npos);
  const auto located = nlohmann::json::parse("{" + positions.str() + "\"ready\":true}");
  test::Check(located["draft_positions"].size() == (nested ? 3 : 1));
  if (nested) {
    test::Check(located["draft_positions"][0]["incomplete"] == false);
    test::Check(located["draft_positions"][1]["incomplete"] == false);
    test::Check(located["draft_positions"][2]["incomplete"] == false);
  }
  // The values-only diagnostic keeps the previous complete-only construction.
  std::ostringstream strict_positions;
  const auto strict = InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
      strict_positions, FamilyInspectMode::kValues);
  test::Check(strict.value("family_tree_prepared", false));
  test::Check(!strict.contains("draft_source") && strict_positions.str().empty());
  test::Check(unlink(path) == 0);
  return source;
}
}  // namespace
int main(int argc, char**) {
  if (argc > 1) { std::cout << FileDraft(false); return 0; }
  FileDraft(false); FileDraft(true);
}
