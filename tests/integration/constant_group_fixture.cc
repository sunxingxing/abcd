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
constexpr std::uint8_t kImplicitArguments = 3, kLocals = 1, kCache = 0;
constexpr unsigned kByteBits = 8, kFunctionKindShift = 8;
struct ProfileCase {
  abc::ProfileId profile;
  abc::AbcVersion version;
};
constexpr std::array kProfiles{
    ProfileCase{abc::ProfileId::kDynamic1202, {12, 0, 2, 0}},
    ProfileCase{abc::ProfileId::kDynamic1206, {12, 0, 6, 0}},
    ProfileCase{abc::ProfileId::kDynamic1301, {13, 0, 1, 0}}};
constexpr std::array kConstants{
    abc::Opcode::kLDNAN, abc::Opcode::kLDINFINITY, abc::Opcode::kLDUNDEFINED,
    abc::Opcode::kLDNULL, abc::Opcode::kLDTRUE, abc::Opcode::kLDFALSE};
enum class ExpectedSource { kPrinted, kRejected };
std::uint8_t Op(abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); }
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes,
    const std::vector<std::uint8_t>& code, std::uint8_t locals, std::uint8_t args) {
  constexpr std::size_t kSingleByteUlebLimit = 128;
  test::Check(code.size() < kSingleByteUlebLimit);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {locals, args, static_cast<std::uint8_t>(code.size()), 0});
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  return {{offset}, {start}, locals, args, static_cast<std::uint32_t>(code.size()), 0};
}
void Emit(ProfileCase profile, abc::Opcode constant, bool callback,
    ExpectedSource expected = ExpectedSource::kPrinted, bool debug = false) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  const std::uint8_t parameters = callback ? 1 : 0;
  const auto caller = Append(bytes,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), kCache, 0, 0, parameters,
       Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), kCache, 0, 0, parameters,
       Op(abc::Opcode::kRETURNUNDEFINED)}, 0, 0);
  std::vector<std::uint8_t> code{Op(constant)};
  if (debug) code.push_back(Op(abc::Opcode::kDEBUGGER));
  if (callback) {
    // Keep the loaded value live across a potentially throwing call. The
    // callback result is intentionally discarded, not substituted for it.
    constexpr std::uint8_t kValue = 0, kCallback = kLocals + kImplicitArguments;
    code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kValue,
        Op(abc::Opcode::kLDA_V8), kCallback,
        Op(abc::Opcode::kCALLARG1_IMM8_V8), kCache, kValue,
        Op(abc::Opcode::kLDA_V8), kValue});
  }
  if (debug) code.push_back(Op(abc::Opcode::kDEBUGGER));
  code.push_back(Op(abc::Opcode::kRETURN));
  const auto target = Append(bytes, code, callback ? kLocals : 0,
      kImplicitArguments + parameters);
  char path[] = "/tmp/abcd-constant-group-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = profile.profile; header.version = profile.version;
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCodeStart, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "constant"; index.methods[1].name_utf16_length = 8;
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
  if (expected == ExpectedSource::kPrinted) {
    const auto range = sites.Find({kTarget});
    test::Check(range.count == 2);
    FunctionContext first{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, range.first, identity))};
    FunctionContext second{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, {range.first.value + 1}, identity))};
    auto read = ReadFunctionValues(file, header, index, first.Position(), identity,
        InputCaptureMode::kConditional, {}, nullptr, &first);
    const auto& input = std::get<FileFunction>(read);
    test::Check(input.input.HasOwner() && input.input.MatchesOwner(first));
    test::Check(!input.input.MatchesOwner(second));
    const auto wrong_target = FunctionInput::Build(identity, {kCaller}, {kRecord},
        input.body, input.frame, InputCaptureMode::kConditional, {}, &first);
    test::Check(std::get<FunctionInputError>(
        std::get<FunctionInputFailure>(wrong_target).cause) == FunctionInputError::kOwner);
    auto wrong_frame = input.frame;
    ++wrong_frame.locals;
    const auto wrong_layout = FunctionInput::Build(identity, {kTarget}, {kRecord},
        input.body, wrong_frame, InputCaptureMode::kConditional, {}, &first);
    test::Check(std::get<FunctionInputError>(
        std::get<FunctionInputFailure>(wrong_layout).cause) == FunctionInputError::kOwner);
    auto wrong_identity = identity;
    ++wrong_identity.file_size;
    const auto wrong_file = FunctionInput::Build(wrong_identity, {kTarget}, {kRecord},
        input.body, input.frame, InputCaptureMode::kConditional, {}, &first);
    test::Check(std::get<FunctionInputError>(
        std::get<FunctionInputFailure>(wrong_file).cause) == FunctionInputError::kOwner);
    auto family = ReadFunctionFamily(file, header, index, lookup, roles, sites,
        first.Position(), identity, {}, nullptr, &first);
    auto wrong_occurrence = BuildFileFamilyTrees(file, header, index,
        std::get<FileFamily>(std::move(family)), second);
    test::Check(std::get<FamilyTreeError>(
        std::get<FamilyTreeFailure>(wrong_occurrence).cause) == FamilyTreeError::kContext);
  }
  auto prepared = PreparedFamily::PrepareCreated(file, header, index, lookup, roles,
      sites, sites.Find({kTarget}).first, identity, destination);
  if (expected == ExpectedSource::kRejected) {
    // A VM marker may be consumed internally, but neither a JS return nor a
    // host call argument may expose it under the current execution contract.
    test::Check(std::holds_alternative<FamilyPrepareFailure>(prepared));
    test::Check(unlink(path) == 0);
    return;
  }
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  std::ostringstream output;
  auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)),
      destination, output, {});
  test::Check(std::holds_alternative<FamilyEmission>(emitted));
  const auto& emission = std::get<FamilyEmission>(emitted);
  test::Check(emission.Receipts().size() == 1);
  const auto& receipt = emission.Receipts().front();
  test::Check(receipt.stage == SourceStage::kPrinted && receipt.coverage == Coverage::kComplete);
  const auto& member = emission.Family().Family().members.front();
  test::Check(member.sources && !member.sources->sites.empty());
  const auto& mapping = member.sources->sites.front();
  const auto& origin = std::get<OriginalSite>(member.tree.origins[mapping.origin.value]);
  test::Check(origin.pc == abc::CodeOffset{0});
  const auto expression = std::get<ExprId>(mapping.target);
  const auto span = emission.Positions().members.front().expressions[expression.value];
  test::Check(span.begin < span.end && span.end <= output.view().size());
  if (debug) {
    const std::array expected_pc{abc::CodeOffset{1},
        abc::CodeOffset{static_cast<std::uint32_t>(code.size() - 2)}};
    std::size_t count = 0;
    for (const auto& site : member.sources->sites) {
      const auto* statement = std::get_if<StmtId>(&site.target);
      if (!statement || !std::holds_alternative<DebuggerStmt>(
          member.tree.statements[statement->value].statement)) continue;
      test::Check(count < expected_pc.size());
      const auto& origin = std::get<OriginalSite>(member.tree.origins[site.origin.value]);
      test::Check(origin.pc == expected_pc[count++]);
      const auto range = emission.Positions().members.front().statements[statement->value];
      test::Check(range.begin < range.end && range.end <= output.view().size());
      test::Check(output.view().substr(range.begin, range.end - range.begin) == "debugger;");
    }
    test::Check(count == expected_pc.size());
  }
  std::cout << output.str() << ",\n";
  test::Check(unlink(path) == 0);
}
}  // namespace
int main() {
  std::cout << "export const cases = [\n";
  for (const auto profile : kProfiles) {
    for (const auto constant : kConstants)
      for (const bool callback : {false, true}) Emit(profile, constant, callback);
    for (const bool callback : {false, true})
      Emit(profile, abc::Opcode::kLDHOLE, callback, ExpectedSource::kRejected);
  }
  std::cout << "];\n";
  std::cout << "export const debugCases = [\n";
  for (const auto profile : kProfiles)
    for (const auto constant : kConstants)
      for (const bool callback : {false, true})
        Emit(profile, constant, callback, ExpectedSource::kPrinted, true);
  std::cout << "];\n";
}
