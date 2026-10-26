#include "../../../src/source/prepared_family.h"
#include "abcd/abc/method_attributes.h"
#include "analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256, kFactory = 300,
    kConstructor = 340, kKey = 160, kCode = 400;
constexpr unsigned kKindShift = 8, kByteBits = 8;
constexpr std::uint8_t kUlebPayload = 0x7f, kUlebContinuation = 0x80;
constexpr unsigned kUlebBits = 7;
constexpr std::uint32_t kPoolWidth = sizeof(std::uint32_t);
std::uint8_t Op(abc::Opcode value) {return static_cast<std::uint8_t>(value);}
void Uleb(std::vector<std::uint8_t>& out, std::uint32_t value) {
  do {auto byte = static_cast<std::uint8_t>(value & kUlebPayload); value >>= kUlebBits;
    out.push_back(byte | (value ? kUlebContinuation : 0));} while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> code,
    std::uint32_t locals, std::uint32_t arguments, bool caught = false) {
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  Uleb(bytes, locals); Uleb(bytes, arguments); Uleb(bytes, code.size()); Uleb(bytes, caught ? 1 : 0);
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  if (caught) {
    // The original creation precedes the protected argument/construct protocol.
    constexpr std::uint32_t kProtectedStart = 7;
    Uleb(bytes, kProtectedStart); Uleb(bytes, code.size() - 1 - kProtectedStart);
    Uleb(bytes, 1); Uleb(bytes, 0); Uleb(bytes, code.size() - 1); Uleb(bytes, 1);
  }
  return {{offset}, {start}, locals, arguments, static_cast<std::uint32_t>(code.size()), caught ? 1U : 0U};
}
void Run(abc::ProfileId profile, bool wide, bool caught, bool empty, std::uint32_t count,
    std::ostream* output = nullptr, bool arrow = false, bool new_target = false) {
  std::vector<std::uint8_t> bytes(kCode);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  word(kPool, kFactory); word(kPool + kPoolWidth, kConstructor); word(kPool + 2 * kPoolWidth, kKey);
  bytes[kKey] = 11; std::copy_n("value", 5, bytes.begin() + kKey + 1);
  const std::uint8_t caller[]{Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 1, Op(abc::Opcode::kRETURN)};
  const auto caller_code = Append(bytes, caller, 0, 3);
  // Real DEFINEFUNC occurrence; the target body is read from this same file.
  // Two locals own constructor and argument-array identities. Input is physical r5.
  std::vector<std::uint8_t> factory{Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 1, 0, 2,
      Op(abc::Opcode::kSTA_V8), 0};
  for (std::uint32_t c = 0; c < count; ++c) {
    factory.insert(factory.end(), {Op(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0, Op(abc::Opcode::kSTA_V8), 1});
    if (!empty) for (std::uint8_t index = 0; index < 2; ++index)
      factory.insert(factory.end(), {Op(abc::Opcode::kLDA_V8), 5, Op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 2, 0,
          Op(abc::Opcode::kSTOWNBYINDEX_IMM8_V8_IMM16), 0, 1, index, 0});
    factory.insert(factory.end(), {Op(abc::Opcode::kLDA_V8), 1,
        Op(wide ? abc::Opcode::kNEWOBJAPPLY_IMM16_V8 : abc::Opcode::kNEWOBJAPPLY_IMM8_V8), 0});
    if (wide) factory.push_back(1);
    factory.push_back(0);
  }
  factory.push_back(Op(abc::Opcode::kRETURN));
  if (caught) factory.push_back(Op(abc::Opcode::kRETURN));
  const auto factory_code = Append(bytes, factory, 2, 4, caught);
  constexpr std::uint8_t kNewTarget = 1, kFirstParameter = 3;
  const std::uint8_t ctor[]{Op(abc::Opcode::kLDA_V8), new_target ? kNewTarget : kFirstParameter, Op(abc::Opcode::kRETURN)};
  const auto ctor_code = Append(bytes, ctor, 0, 5);
  char path[] = "/tmp/abcd-apply-function-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = profile; header.file_size = bytes.size();
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12,0,2,0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13,0,1,0} : abc::AbcVersion{12,0,6,0};
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, static_cast<std::uint32_t>(bytes.size()), {}, {3, kPool}, {}, {}});
  index.methods.resize(3);
  const abc::EntityOffset offsets[]{{kCaller}, {kFactory}, {kConstructor}};
  const abc::CodeInfo codes[]{caller_code, factory_code, ctor_code};
  const std::string names[]{"caller", "factory", "Known"};
  for (std::size_t i = 0; i < 3; ++i) {
    auto& method = index.methods[i]; method.offset = offsets[i]; method.owner_offset = {kRecord};
    method.code = codes[i]; method.name = names[i]; method.name_utf16_length = names[i].size();
    method.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kKindShift;
  }
  if (arrow) index.methods[2].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kArrow) << kKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(*file, header, index, lookup));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(*file, header, index, lookup));
  const auto site = sites.FindOccurrence({kFactory}, {kCaller}, {0}); test::Check(site.has_value());
  auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  identity.contract = SourceContract::kLanguageAndHostV3;  // Pinned ABI counterexamples.
  const FamilyDestination destination{Strictness::kStrict, nullptr};
  auto prepared = PreparedFamily::PrepareCreated(*file, header, index, lookup, roles, sites, *site, identity, destination);
  if (new_target) {
    // Reading the implicit new.target slot still needs its separate owner/ABI
    // proof. Direct function brand does not discharge that body obligation.
    test::Check(std::holds_alternative<FamilyPrepareFailure>(prepared));
    test::Check(unlink(path) == 0); return;
  }
  if (!std::holds_alternative<PreparedFamily>(prepared)) std::cerr << "apply function refused: " << wide << ' ' << caught << ' ' << empty << ' ' << count << '\n';
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  const auto& ready = std::get<PreparedFamily>(prepared);
  test::Check(ready.Family().members.size() == 2);
  std::ostringstream source;
  test::Check(std::holds_alternative<PrintedFamilyDraft>(ready.PrintDraft(destination, source)));
  test::Check(source.str().find("new ") != std::string::npos && source.str().find("...") == std::string::npos);
  if (output) *output << nlohmann::json{{"source", source.str()}, {"caught", caught}, {"empty", empty}, {"arrow", arrow}, {"new_target", new_target}} << '\n';
  test::Check(unlink(path) == 0);
}
}
int main(int argc, char**) {
  if (argc == 2) {
    for (bool arrow : {false, true}) for (bool caught : {false, true}) for (bool empty : {false, true})
      Run(abc::ProfileId::kDynamic1206, false, caught, empty, 1, &std::cout, arrow);
    return 0;
  }
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (bool wide : {false, true}) for (bool caught : {false, true}) for (bool empty : {false, true})
      for (auto count : {1U, 2U, 200U, 201U}) Run(profile, wide, caught, empty, count);
  for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (bool wide : {false, true}) for (bool caught : {false, true}) for (bool empty : {false, true})
      Run(profile, wide, caught, empty, 1, nullptr, true);
  for (bool wide : {false, true}) for (bool caught : {false, true})
    Run(abc::ProfileId::kDynamic1206, wide, caught, false, 1, nullptr, false, true);
}
