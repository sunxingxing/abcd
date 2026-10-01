#include "../../src/source/family_emission.h"
#include "../../src/source/family_writes.h"
#include "analysis/ssa_fixture.h"
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr unsigned kByteBits = 8, kUlebBits = 7, kFunctionKindShift = 8;
constexpr std::uint32_t kPool = 64, kRecord = 128, kLiteral = 160, kText = 220,
    kCaller = 256, kTarget = 300, kCodeStart = 400;
constexpr std::uint8_t kLocals = 1, kImplicit = 3, kInput = kLocals + kImplicit;
std::uint8_t Op(abc::Opcode value) { return static_cast<std::uint8_t>(value); }
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  constexpr std::uint8_t kContinue = 1U << kUlebBits;
  do {
    auto byte = static_cast<std::uint8_t>(value & (kContinue - 1)); value >>= kUlebBits;
    bytes.push_back(byte | (value ? kContinue : 0));
  } while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& code,
    std::uint8_t locals, std::uint8_t args) {
  const auto start = static_cast<std::uint32_t>(bytes.size());
  Uleb(bytes, locals); Uleb(bytes, args); Uleb(bytes, code.size()); Uleb(bytes, 0);
  const auto body = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{start}, {body}, locals, args, static_cast<std::uint32_t>(code.size()), 0};
}
std::uint64_t Run(abc::ProfileId profile, std::uint32_t count, bool print, bool callable = false) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  const auto word = [&](std::uint32_t at, std::uint64_t value, unsigned width = sizeof(std::uint32_t)) {
    for (unsigned i = 0; i < width; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  word(kPool, kTarget); word(kPool + sizeof(kTarget), kLiteral);
  // Five primitive tagged entries: string, integer, null, boolean, negative zero.
  // A method tag deliberately refuses, rather than inventing a closure owner.
  word(kLiteral, 10); auto at = kLiteral + sizeof(std::uint32_t);
  const auto entry = [&](abc::LiteralTag tag, std::uint64_t value, unsigned width) {
    bytes[at++] = static_cast<std::uint8_t>(tag); word(at, value, width); at += width;
  };
  entry(callable ? abc::LiteralTag::kMethod : abc::LiteralTag::kString,
      callable ? kTarget : kText, sizeof(std::uint32_t));
  entry(abc::LiteralTag::kInteger, 42, sizeof(std::uint32_t));
  entry(abc::LiteralTag::kNull, 0, 1); entry(abc::LiteralTag::kBoolean, 1, 1);
  entry(abc::LiteralTag::kDouble, std::uint64_t{1} << 63, sizeof(std::uint64_t));
  bytes[kText] = 7; bytes[kText + 1] = 'r'; bytes[kText + 2] = 'u'; bytes[kText + 3] = 'n';
  const auto caller = Append(bytes, {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 1,
      Op(abc::Opcode::kRETURNUNDEFINED)}, 0, 0);
  std::vector<std::uint8_t> code;
  for (std::uint32_t i = 0; i < count; ++i)
    code.insert(code.end(), {Op(abc::Opcode::kCREATEARRAYWITHBUFFER_IMM8_ID16), 0, 1, 0});
  code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kLDA_V8), kInput,
      Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 0, Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRETURN)});
  const auto target = Append(bytes, code, kLocals, kImplicit + 1);
  char path[] = "/tmp/abcd-family-array-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = profile; header.file_size = bytes.size();
  if (profile == abc::ProfileId::kDynamic1202) header.version = {12, 0, 2, 0};
  else if (profile == abc::ProfileId::kDynamic1206) header.version = {12, 0, 6, 0};
  else header.version = {13, 0, 1, 0};
  abc::MetadataIndex index; index.regions.push_back({kCaller, kCodeStart, {}, {2, kPool}, {}, {}});
  index.methods.resize(2); index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  index.methods[1].name = "array"; index.methods[1].name_utf16_length = 5;
  index.methods[1].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  const auto occurrence = sites.Find({kTarget}).first;
  if (callable) {
    // An ownerless diagnostic read never publishes source. Test the original
    // array barrier without pretending its embedded method has a JS owner.
    const auto read = ReadFunctionFamily(*file, header, index, lookup, roles, sites, {1}, identity);
    test::Check(std::holds_alternative<FamilyFileFailure>(read));
    test::Check(std::get<BufferedArrayError>(std::get<FamilyFileFailure>(read).cause) == BufferedArrayError::kValue);
    test::Check(unlink(path) == 0); return 0;
  }
  const FunctionContext owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
      *file, header, index, lookup, roles, sites, occurrence, identity))};
  auto read = ReadFunctionFamily(*file, header, index, lookup, roles, sites, owner.Position(), identity, {}, nullptr, &owner);
  if (const auto* error = std::get_if<FamilyFileFailure>(&read)) std::cerr << "array read failure " << error->cause.index() << '\n';
  test::Check(std::holds_alternative<FileFamily>(read));
  auto inputs = std::get<FileFamily>(std::move(read));
  test::Check(inputs.members.size() == 1 && inputs.members[0].arrays.size() == count && inputs.members[0].children.empty());
  const auto built = BuildFileFamilyTrees(*file, header, index,
      std::get<FileFamily>(ReadFunctionFamily(*file, header, index, lookup, roles, sites, owner.Position(), identity, {}, nullptr, &owner)), owner);
  if (const auto* failure = std::get_if<FamilyTreeFailure>(&built)) std::cerr << "array tree failure " << failure->cause.index() << '\n';
  test::Check(std::holds_alternative<ConstructedFamily>(built));
  const auto& tree = std::get<ConstructedFamily>(built);
  test::Check(tree.writes && tree.writes->primitive_arrays == count && tree.members[0].tree.recovery_issues.empty());
  const auto checked = std::get<FamilyWrites>(VerifyFamilyWrites(index, inputs, tree));
  test::Check(checked.facts.primitive_arrays == count);
  auto changed = inputs.members[0].arrays[0].creation; inputs.members[0].arrays[0].creation.literal = {0};
  test::Check(std::get<FamilyWriteFailure>(VerifyFamilyWrites(index, inputs, tree)).kind == FamilyWriteError::kCoverage);
  inputs.members[0].arrays[0].creation = changed;
  inputs.members[0].arrays.push_back(inputs.members[0].arrays[0]);
  test::Check(std::get<FamilyWriteFailure>(VerifyFamilyWrites(index, inputs, tree)).kind == FamilyWriteError::kCoverage);
  inputs.members[0].arrays.pop_back(); inputs.members[0].arrays.clear();
  test::Check(std::get<FamilyWriteFailure>(VerifyFamilyWrites(index, inputs, tree)).kind == FamilyWriteError::kCoverage);
  const FamilyDestination destination{Strictness::kStrict, nullptr};
  auto prepared = PreparedFamily::PrepareCreated(*file, header, index, lookup, roles, sites, occurrence, identity, destination);
  test::Check(std::holds_alternative<PreparedFamily>(prepared)); std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(std::get<PreparedFamily>(prepared).PrintDraft(destination, output)));
  if (print) std::cout << nlohmann::json{{"source", output.str()}}.dump() << '\n';
  test::Check(unlink(path) == 0);
  return inputs.work + checked.work + tree.work;
}
}
int main(int argc, char** argv) {
  const bool print = argc == 2 && std::string_view{argv[1]} == "--source";
  test::Check(argc == 1 || print);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    if (print) Run(profile, 1, true);
    else {
      std::uint64_t previous = 0;
      for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
        const auto work = Run(profile, count, false);
        if (count == 400 || count == 800) test::Check(work < previous * 3);
        previous = work;
      }
      Run(profile, 1, false, true);
    }
  }
}
