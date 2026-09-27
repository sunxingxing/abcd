#include "../../src/source/family_emission.h"
#include "analysis/ssa_fixture.h"

#include <unistd.h>

#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256,
    kTarget = 300, kCodeStart = 400;
constexpr std::uint8_t kParameters = 3, kImplicitArguments = 3;
constexpr unsigned kByteBits = 8, kFunctionKindShift = 8;
std::uint8_t Op(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes,
                     const std::vector<std::uint8_t>& code, std::uint8_t args) {
  // All fixture lengths are one-byte ULEBs; no production encoder is reused.
  test::Check(code.size() < 128);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {0, args, static_cast<std::uint8_t>(code.size()), 0});
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{offset}, {start}, 0, args, static_cast<std::uint32_t>(code.size()), 0};
}
void Emit(bool wide) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
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
  const auto target = Append(bytes, code, kImplicitArguments + kParameters);
  char path[] = "/tmp/abcd-computed-store-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  test::Check(unlink(path) == 0);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCodeStart, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "store"; index.methods[1].name_utf16_length = 5;
  index.methods[1].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(
      std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  const FamilyDestination destination{Strictness::kStrict, nullptr};
  auto prepared = PreparedFamily::PrepareCreated(file, header, index, lookup, roles,
      sites, sites.Find({kTarget}).first, identity, destination);
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  std::ostringstream output;
  auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)),
      destination, output, {});
  test::Check(std::holds_alternative<FamilyEmission>(emitted));
  const auto& emission = std::get<FamilyEmission>(emitted);
  test::Check(emission.Receipts().size() == 1 &&
      emission.Receipts()[0].stage == SourceStage::kPrinted &&
      emission.Receipts()[0].coverage == Coverage::kComplete);
  test::Check(emission.Positions().members.size() == 1);
  const auto& member = emission.Family().Family().members[0];
  constexpr std::size_t kInstructionCount = 4, kStoreSite = 2;
  constexpr abc::CodeOffset kStorePc{4};
  test::Check(member.sources && member.sources->sites.size() == kInstructionCount);
  const auto& mapping = member.sources->sites[kStoreSite];
  const auto expression = std::get<ExprId>(mapping.target);
  const auto& origin = std::get<OriginalSite>(member.tree.origins[mapping.origin.value]);
  test::Check(origin.pc == kStorePc);
  const auto span = emission.Positions().members[0].expressions[expression.value];
  test::Check(span.begin < span.end && span.end <= output.view().size());
  std::cout << "export const " << (wide ? "wide" : "narrow") << " = " << output.str() << ";\n";
}
}  // namespace
int main() { Emit(false); Emit(true); }
