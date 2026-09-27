#include "../../src/source/family_emission.h"
#include "analysis/ssa_fixture.h"

#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256,
    kTarget = 300, kChild = 320, kCodeStart = 400;
constexpr std::uint8_t kParameters = 3, kImplicitArguments = 3;
constexpr unsigned kByteBits = 8, kFunctionKindShift = 8;
std::uint8_t Op(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes,
                     const std::vector<std::uint8_t>& code, std::uint8_t args,
                     std::uint8_t locals = 0) {
  // All fixture lengths are one-byte ULEBs; no production encoder is reused.
  test::Check(code.size() < 128);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {locals, args, static_cast<std::uint8_t>(code.size()), 0});
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{offset}, {start}, locals, args, static_cast<std::uint32_t>(code.size()), 0};
}
enum class CapturedBase { kArgument, kReceiver, kFunction, kRealm };
void Emit(bool wide, bool captured = false, CapturedBase base = CapturedBase::kArgument,
    bool child_global = false) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  if (captured)
    for (unsigned i = 0; i < sizeof(kChild); ++i)
      bytes[kPool + sizeof(kTarget) + i] = static_cast<std::uint8_t>(kChild >> (kByteBits * i));
  if (child_global) {
    constexpr std::uint32_t kGlobalName = 80, kGlobalSlot = 2;
    constexpr std::string_view kName = "store";
    constexpr unsigned kLengthShift = 1, kAsciiFlag = 1;
    for (unsigned i = 0; i < sizeof(kGlobalName); ++i)
      bytes[kPool + sizeof(kGlobalName) * kGlobalSlot + i] =
          static_cast<std::uint8_t>(kGlobalName >> (kByteBits * i));
    bytes[kGlobalName] = static_cast<std::uint8_t>((kName.size() << kLengthShift) | kAsciiFlag);
    std::copy(kName.begin(), kName.end(), bytes.begin() + kGlobalName + 1);
  }
  const auto caller = Append(bytes,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, kParameters,
       Op(abc::Opcode::kRETURNUNDEFINED)}, 0);
  constexpr std::uint8_t kObject = kImplicitArguments, kKey = kObject + 1,
      kRhs = kKey + 1, kCache = 0;
  // function(object,key,rhs) { const value = rhs(); object[key] = value; }
  // RETURNUNDEFINED makes the call single-use, exposing unsafe RHS fusion.
  std::vector<std::uint8_t> code{Op(abc::Opcode::kLDA_V8), kRhs,
      Op(abc::Opcode::kCALLARG0_IMM8), kCache,
      Op(wide ? abc::Opcode::kSTOBJBYVALUE_IMM16_V8_V8 :
                abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8), kCache};
  if (wide) code.push_back(kCache);
  code.insert(code.end(), {kObject, kKey, Op(abc::Opcode::kRETURNUNDEFINED)});
  std::optional<abc::CodeInfo> child;
  if (captured) {
    // Compact lexical addresses put depth in the low nibble and slot above it.
    constexpr std::uint8_t kObjectCell = 0, kKeyCell = 0x10, kRhsCell = 0x20;
    code = {Op(abc::Opcode::kNEWLEXENV_IMM8), kParameters};
    switch (base) {
      case CapturedBase::kArgument: code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kObject}); break;
      case CapturedBase::kReceiver: code.push_back(Op(abc::Opcode::kLDTHIS)); break;
      case CapturedBase::kFunction: code.push_back(Op(abc::Opcode::kLDFUNCTION)); break;
      case CapturedBase::kRealm: code.push_back(Op(abc::Opcode::kLDGLOBAL)); break;
    }
    code.insert(code.end(), {Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kObjectCell,
        Op(abc::Opcode::kLDA_V8), kKey, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kKeyCell,
        Op(abc::Opcode::kLDA_V8), kRhs, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kRhsCell,
        Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), kCache, 1, 0, 0,
        Op(abc::Opcode::kPOPLEXENV), Op(abc::Opcode::kRETURN)});
  }
  const auto target = Append(bytes, code, kImplicitArguments + kParameters);
  if (captured) {
    constexpr std::uint8_t kObjectCell = 0, kKeyCell = 0x10, kRhsCell = 0x20;
    constexpr std::uint8_t kLocalObject = 0, kLocalKey = 1, kLocals = 2;
    std::vector<std::uint8_t> body{
        Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), kObjectCell, Op(abc::Opcode::kSTA_V8), kLocalObject,
        Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), kKeyCell, Op(abc::Opcode::kSTA_V8), kLocalKey,
        Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), kRhsCell, Op(abc::Opcode::kCALLARG0_IMM8), kCache,
        Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), kRhsCell,
        Op(wide ? abc::Opcode::kSTOBJBYVALUE_IMM16_V8_V8 : abc::Opcode::kSTOBJBYVALUE_IMM8_V8_V8), kCache};
    if (wide) body.push_back(kCache);
    body.insert(body.end(), {kLocalObject, kLocalKey, Op(abc::Opcode::kRETURNUNDEFINED)});
    if (child_global)
      body.insert(body.begin(), {Op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), kCache, 2, 0});
    child = Append(bytes, body, kImplicitArguments, kLocals);
  }
  char path[] = "/tmp/abcd-computed-store-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCodeStart, {}, {child_global ? 3U : captured ? 2U : 1U, kPool}, {}, {}});
  index.methods.resize(captured ? 3 : 2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "store"; index.methods[1].name_utf16_length = 5;
  index.methods[1].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  if (captured) {
    index.methods[2].offset = {kChild}; index.methods[2].code = *child;
    index.methods[2].name = "write"; index.methods[2].name_utf16_length = 5;
    index.methods[2].access_flags =
        static_cast<std::uint32_t>(abc::FunctionKind::kArrow) << kFunctionKindShift;
  }
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(
      std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  const GlobalScope globals{};
  const FamilyDestination destination{Strictness::kStrict,
      child_global || base == CapturedBase::kRealm ? &globals : nullptr};
  if (captured) {
    FunctionContext owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, sites.Find({kTarget}).first, identity))};
    auto family = ReadFunctionFamily(file, header, index, lookup, roles, sites,
        owner.Position(), identity, {}, nullptr, &owner);
    const auto& read = std::get<FileFamily>(family);
    test::Check(read.members.size() == 2);
    test::Check(read.members[0].function.input.MatchesOwner(owner));
    test::Check(read.members[1].context.has_value());
    test::Check(read.members[1].function.input.MatchesOwner(*read.members[1].context));
    test::Check(!read.members[1].function.input.MatchesOwner(owner));
  }
  auto prepared = PreparedFamily::PrepareCreated(file, header, index, lookup, roles,
      sites, sites.Find({kTarget}).first, identity, destination);
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  std::ostringstream output;
  auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)),
      destination, output, {});
  test::Check(std::holds_alternative<FamilyEmission>(emitted));
  const auto& emission = std::get<FamilyEmission>(emitted);
  const auto members = captured ? 2U : 1U;
  test::Check(emission.Receipts().size() == members);
  for (const auto& receipt : emission.Receipts())
    test::Check(receipt.stage == SourceStage::kPrinted && receipt.coverage == Coverage::kComplete);
  test::Check(emission.Positions().members.size() == members);
  const auto store_member = captured ? 1U : 0U;
  const auto& member = emission.Family().Family().members[store_member];
  const std::size_t kInstructionCount = (captured ? 9 : 4) + (child_global ? 1 : 0);
  const std::size_t kStoreSite = (captured ? 7 : 2) + (child_global ? 1 : 0);
  const abc::CodeOffset kStorePc{(captured ? 14U : 4U) + (child_global ? 4U : 0U)};
  test::Check(member.sources && member.sources->sites.size() == kInstructionCount);
  const auto& mapping = member.sources->sites[kStoreSite];
  const auto expression = std::get<ExprId>(mapping.target);
  const auto& origin = std::get<OriginalSite>(member.tree.origins[mapping.origin.value]);
  test::Check(origin.pc == kStorePc);
  const auto span = emission.Positions().members[store_member].expressions[expression.value];
  test::Check(span.begin < span.end && span.end <= output.view().size());
  const auto name = child_global ? (wide ? "selfGlobalWide" : "selfGlobalNarrow") :
      base == CapturedBase::kRealm ? (wide ? "realmWide" : "realmNarrow") :
      base == CapturedBase::kFunction ? (wide ? "selfWide" : "selfNarrow") :
      base == CapturedBase::kReceiver ? (wide ? "receiverWide" : "receiverNarrow") :
      captured ? (wide ? "closureWide" : "closureNarrow") : (wide ? "wide" : "narrow");
  std::cout << "export const " << name << " = " << output.str() << ";\n";
  // Unlink changes ctime: keep the input unchanged until all file-backed
  // preparation and fingerprint checks have finished.
  test::Check(unlink(path) == 0);
}
}  // namespace
int main() {
  Emit(false); Emit(true); Emit(false, true); Emit(true, true);
  Emit(false, true, CapturedBase::kReceiver); Emit(true, true, CapturedBase::kReceiver);
  Emit(false, true, CapturedBase::kFunction); Emit(true, true, CapturedBase::kFunction);
  Emit(false, true, CapturedBase::kFunction, true); Emit(true, true, CapturedBase::kFunction, true);
  Emit(false, true, CapturedBase::kRealm); Emit(true, true, CapturedBase::kRealm);
}
