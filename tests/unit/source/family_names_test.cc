#include "prepared_family.h"
#include "abcd/source/module_enclosure.h"
#include "analysis/ssa_fixture.h"

#include <unistd.h>
#include <array>
#include <bit>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr std::uint32_t kRecord = 400, kCaller = 420, kOuter = 440, kChild = 460;
constexpr std::uint32_t kPool = 100, kScope = 150, kName = 200, kModule = 260;
constexpr std::uint32_t kType = 350, kModuleName = 370, kModuleField = 480;
constexpr std::uint32_t kCode = 500, kWord = sizeof(std::uint32_t), kByteBits = 8;
constexpr std::uint32_t kImplicitArguments = 3, kFunctionKindShift = 8;

std::string Source(std::string_view free_name, bool module_read, bool named, bool split = false) {
  std::vector<std::uint8_t> bytes(kCode);
  auto word = [&](std::uint32_t at, std::uint32_t value) {
    for (unsigned i = 0; i < kWord; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  auto text = [&](std::uint32_t at, std::string_view value) {
    bytes[at] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at + 1);
  };
  word(kPool, kOuter); word(kPool + kWord, kChild);
  word(kPool + 2 * kWord, kScope); word(kPool + 3 * kWord, kName);
  // Valid pinned named-scope literal: integer count, string/slot pair.
  word(kScope, 6);
  bytes[kScope + kWord] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kScope + kWord + 1, 1);
  bytes[kScope + 2 * kWord + 1] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kScope + 2 * kWord + 2, kName);
  bytes[kScope + 3 * kWord + 2] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kScope + 3 * kWord + 3, 0);
  text(kName, free_name);
  if (module_read) {
    constexpr std::uint32_t kRequest = 225, kImported = 235;
    text(kRequest, "dep"); text(kImported, "fn"); text(kModuleName, "moduleRecordIdx");
    constexpr std::uint32_t kI32FieldEncoding = 5; // types.yaml: i32 7 minus u1 2.
    word(kType, kI32FieldEncoding);
    // Encoded count, one request, one import, then empty namespace/exports.
    word(kModule + kWord, 1); word(kModule + 2 * kWord, kRequest);
    word(kModule + 3 * kWord, 1); word(kModule + 4 * kWord, kName);
    word(kModule + 5 * kWord, kImported);
  }
  auto code = [&](std::vector<std::uint8_t> body, std::uint8_t registers, std::uint8_t arguments) {
    const auto start = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {registers, arguments, static_cast<std::uint8_t>(body.size()), 0});
    bytes.insert(bytes.end(), body.begin(), body.end());
    return abc::CodeInfo{{start}, {start + kWord}, registers, arguments, static_cast<std::uint32_t>(body.size()), 0};
  };
  const auto caller = code({0x33, 0, 0, 0, 1, 0x65}, 0, kImplicitArguments);
  std::vector<std::uint8_t> outer = named ?
      std::vector<std::uint8_t>{static_cast<std::uint8_t>(abc::Opcode::kNEWLEXENVWITHNAME_IMM8_ID16), 1, 2, 0} :
      std::vector<std::uint8_t>{0x09, 1};
  const std::uint8_t outer_registers = split ? 2 : 1;
  if (split) outer.insert(outer.end(), {0x60, 5, 0x61, 1, 0x4d, 3,
      static_cast<std::uint8_t>(abc::Opcode::kNOP)});
  outer.insert(outer.end(), {0x60, static_cast<std::uint8_t>(split ? 1 : 4), 0x3d, 0, 0x33, 0, 1, 0, 0, 0x61, 0, 0x60, 0, 0x69, 0x64});
  const auto outer_code = code(outer, outer_registers, kImplicitArguments + 1);
  // Original lexical value remains a call argument; callee is an independent
  // global or module binding with the same spelling as an ancestor name hint.
  std::vector<std::uint8_t> child{0x3c, 0, 0x61, 1};
  if (module_read) child.insert(child.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16),
      static_cast<std::uint8_t>(static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16) >> kByteBits), 3, 0});
  else child.insert(child.end(), {static_cast<std::uint8_t>(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 3, 0});
  child.insert(child.end(), {static_cast<std::uint8_t>(abc::Opcode::kCALLARG1_IMM8_V8), 0, 1, 0x64});
  const auto child_code = code(child, 2, kImplicitArguments);
  char path[] = "/tmp/abcd-family-names-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, kCode, {module_read ? 1U : 0U, kType}, {4, kPool}, {}, {}});
  abc::RecordInfo record; record.offset = {kRecord}; record.method_count = 3;
  if (module_read) {
    record.field_count = 1;
    index.fields.push_back({{kModuleField}, record.offset, {kModuleName}, {}, 0,
        abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  }
  index.records.push_back(record);
  for (const auto [entity, body, name] : std::array{
      std::tuple{kCaller, caller, "caller"}, std::tuple{kOuter, outer_code, "outer"}, std::tuple{kChild, child_code, "child"}}) {
    abc::MethodInfo method; method.offset = {entity}; method.owner_offset = record.offset; method.code = body;
    method.name = name; method.name_utf16_length = method.name.size();
    method.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
    index.methods.push_back(method);
  }
  auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  RouteInputIdentity identity; identity.profile = header.profile; identity.abc_version = header.version; identity.file_size = header.file_size;
  std::optional<ModuleEnclosure> enclosure;
  GlobalScope globals{};
  if (module_read) {
    auto context = std::get<RecordModuleContext>(ReadRecordModuleContext(*file, header, index, record, abc::FilePackaging::kMerged));
    enclosure.emplace(std::get<ModuleEnclosure>(ModuleEnclosure::Build(*file, header, context, identity, {})));
    globals.module_enclosure = &*enclosure;
  }
  const auto range = sites.Find({kOuter});
  auto prepared = PreparedFamily::PrepareCreated(*file, header, index, lookup, roles, sites, range.first, identity,
      {Strictness::kStrict, &globals});
  if (const auto* failure = std::get_if<FamilyPrepareFailure>(&prepared)) {
    std::cerr << "prepare variant=" << failure->cause.index() << '\n';
    if (const auto* tree = std::get_if<FamilyTreeFailure>(&failure->cause)) {
      std::cerr << "tree variant=" << tree->cause.index() << " member=" << tree->member.value << '\n';
      if (const auto* build = std::get_if<FunctionTreeError>(&tree->cause)) std::cerr << "build=" << static_cast<unsigned>(*build) << '\n';
      if (const auto* scope = std::get_if<FamilyEnclosureFailure>(&tree->cause)) std::cerr << "enclosure=" << static_cast<unsigned>(scope->kind) << '\n';
    }
  }
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  auto family = std::get<PreparedFamily>(std::move(prepared));
  test::Check(family.Family().members.size() == 2);
  // Print the checked family with its actual destination, not a raw tree.
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(family.PrintDraft({Strictness::kStrict, &globals}, output)));
  GlobalScope shadowed = globals;
  const std::array<std::string_view, 1> bindings{free_name};
  shadowed.enclosing_bindings = bindings;
  std::ostringstream refused;
  test::Check(std::holds_alternative<FamilyPrepareFailure>(
      family.PrintDraft({Strictness::kStrict, &shadowed}, refused)));
  test::Check(refused.str().empty());
  test::Check(unlink(path) == 0);
  return output.str();
}
void QueryIndex() {
  for (const std::uint32_t count : {0U, 1U, 200U, 201U, 402U, 804U}) {
    std::vector<NameReference> references;
    constexpr FunctionOwnerId first{37};
    for (std::uint32_t i = 1; i < count; i += 2) references.push_back({u"shared", {first.value + i}});
    std::uint64_t work = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      const NameReservations view{references, {first.value + i}, 1};
      test::Check(view.Contains(u"shared", [&](auto step) {work += step;}) == (i % 2 != 0));
      test::Check(!view.Contains(u"other", [&](auto step) {work += step;}));
    }
    const NameReservations empty{references, first, 0};
    test::Check(!empty.Contains(u"shared", [&](auto step) {work += step;}));
    // Callback accounting makes lookup growth explicit, not a timing benchmark.
    const auto height = std::bit_width(count);
    constexpr auto kQueryCount = 2;
    constexpr auto kBinarySearches = 3;
    constexpr auto kComparisonWidth = std::u16string_view{u"shared"}.size() + 1;
    test::Check(work <= static_cast<std::uint64_t>(count) * kQueryCount *
        (kBinarySearches * height + 1) * kComparisonWidth);
  }
  const std::array<NameReference, 1> last{{{u"last", {UINT32_MAX}}}};
  const NameReservations view{last, {UINT32_MAX}, 1};
  test::Check(view.Contains(u"last", [](auto) {}));
  enum class QueryFailure { kWorkLimit };
  bool refused = false;
  std::uint64_t work = 0;
  try { view.Contains(u"last", [&](auto step) {if (step > work) throw QueryFailure::kWorkLimit; work -= step;}); }
  catch (QueryFailure error) { refused = error == QueryFailure::kWorkLimit; }
  test::Check(refused);
}

}
int main(int argc, char** argv) {
  if (argc == 5) {
    std::cout << Source(argv[1], std::string_view(argv[2]) == "module", std::string_view(argv[3]) == "named", std::string_view(argv[4]) == "split"); return 0;
  }
  QueryIndex();
  for (const auto name : {"restoredValue", "$arg0", "$local0", "$binding0", "ClassValue", "MethodSelfValue"})
    for (const auto named : {false, true}) for (const auto module : {false, true})
      for (const auto split : {false, true}) Source(name, module, named, split);
}
