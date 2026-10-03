#include "../../../src/source/class_tree.h"
#include "analysis/ssa_fixture.h"
#include <unistd.h>
#include <algorithm>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr std::uint32_t kPool = 64, kLiteral = 160, kKey = 4096, kCaller = 256,
    kCtor = 300, kRejected = 340, kAccepted = 380, kCode = 8192;
constexpr unsigned kByteBits = 8, kKindShift = 8, kUlebBits = 7;
constexpr std::uint8_t kUlebPayload = 0x7f, kContinuation = 0x80;
std::uint8_t Op(abc::Opcode op) {return static_cast<std::uint8_t>(op);}
void Uleb(std::vector<std::uint8_t>& out, std::uint32_t value) {
  do {auto byte = static_cast<std::uint8_t>(value & kUlebPayload); value >>= kUlebBits;
    out.push_back(byte | (value ? kContinuation : 0));} while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> code,
    std::uint32_t locals, std::uint32_t args, bool caught = false) {
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  Uleb(bytes, locals); Uleb(bytes, args); Uleb(bytes, code.size()); Uleb(bytes, caught ? 1 : 0);
  const auto start = static_cast<std::uint32_t>(bytes.size()); bytes.insert(bytes.end(), code.begin(), code.end());
  if (caught) {Uleb(bytes, 0); Uleb(bytes, code.size() - 1); Uleb(bytes, 1);
    Uleb(bytes, 0); Uleb(bytes, code.size() - 1); Uleb(bytes, 1);}
  return {{offset}, {start}, locals, args, static_cast<std::uint32_t>(code.size()), caught ? 1U : 0U};
}
void Run(bool caught, bool malformed = false, std::uint32_t count = 3) {
  std::vector<std::uint8_t> bytes(kCode);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  word(kPool, kCtor); word(kPool + sizeof(kCtor), kLiteral);
  // Three real literal member occurrences: failed, supported, failed. Reusing
  // the failed template must retain both property occurrences, without a body.
  word(kLiteral, (count * 3 + 1) * 2); auto at = kLiteral + sizeof(std::uint32_t);
  for (std::uint32_t i = 0; i < count; ++i) {
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(at, kKey); at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod); word(at, i == 1 ? kAccepted : kRejected); at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate); bytes[at++] = i == 1 ? 0 : 1; bytes[at++] = 0;
    bytes[kKey] = 9; std::copy_n("body", 4, bytes.begin() + kKey + 1);
  }
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(at, count);
  const std::uint8_t caller[]{Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8), 0, 0, 0, 1, 0, 0, 0, 0,
      Op(abc::Opcode::kRETURN)};
  const std::uint8_t ctor[]{Op(abc::Opcode::kRETURNUNDEFINED)};
  std::vector<std::uint8_t> rejected{Op(abc::Opcode::kLDA_V8), 4, Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kLDA_V8), 4, Op(abc::Opcode::kADD2_IMM8_V8), 0, 0,
      Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kRETURN)};
  if (caught) rejected.push_back(Op(abc::Opcode::kRETURNUNDEFINED));
  if (malformed) rejected[1] = 250;
  const std::uint8_t accepted[]{Op(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0, Op(abc::Opcode::kRETURN)};
  const abc::CodeInfo code[]{Append(bytes, caller, 1, 3), Append(bytes, ctor, 0, 3),
      Append(bytes, rejected, 1, 4, caught), Append(bytes, accepted, 0, 3)};
  char path[] = "/tmp/abcd-class-member-gap-XXXXXX";
  auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206; header.version = {12,0,6,0}; header.file_size = bytes.size();
  abc::MetadataIndex index; index.regions.push_back({kCaller, static_cast<std::uint32_t>(bytes.size()), {}, {2, kPool}, {}, {}});
  index.methods.resize(4); const std::uint32_t offsets[]{kCaller, kCtor, kRejected, kAccepted};
  for (std::size_t i = 0; i < 4; ++i) {
    auto& method = index.methods[i]; method.offset = {offsets[i]}; method.owner_offset = {128}; method.code = code[i];
    method.name = i == 1 ? "Known" : "body"; method.name_utf16_length = method.name.size();
    method.access_flags = static_cast<std::uint32_t>(i < 2 ? abc::FunctionKind::kBaseConstructor : abc::FunctionKind::kNone) << kKindShift;
  }
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto role_result = MethodRoleIndex::Build(*file, header, index, lookup);
  if (malformed) {
    test::Check(std::holds_alternative<MethodRoleFailure>(role_result));
    test::Check(unlink(path) == 0); return;
  }
  const auto roles = std::get<MethodRoleIndex>(std::move(role_result));
  const auto functions = std::get<FunctionCreationSites>(FunctionCreationSites::Build(*file, header, index, lookup));
  const auto classes = std::get<ClassMethodSites>(ClassMethodSites::Build(*file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  const auto creator = std::get<FileFunction>(ReadFunctionValues(*file, header, index, {0}, identity, InputCaptureMode::kClosed));
  const auto creation = creator.values.classes.classes.front().operation;
  auto complete = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation, identity);
  test::Check(std::holds_alternative<ClassTreeFailure>(complete));
  const auto& refusal = std::get<FamilyPrepareFailure>(std::get<ClassTreeFailure>(complete));
  const auto& failed_family = std::get<FamilyFileFailure>(refusal.cause);
  test::Check(failed_family.method == abc::EntityOffset{kRejected} && !failed_family.parent);
  if (caught) {
    const auto& failed = std::get<PhasedInputFailure>(std::get<PhasedFileFailure>(failed_family.cause));
    test::Check(failed.cause.kind == ExceptionInputError::kInternalValue && failed.cause.pc.value == 9 &&
        failed.cause.work_units != 0 && failed.work != 0 && failed.allocated_bytes != 0);
  } else {
    const auto& failed = std::get<SemanticProofFailure>(
        std::get<FunctionInputFailure>(std::get<FunctionFileFailure>(failed_family.cause)).cause);
    test::Check(failed.kind == SemanticProofError::kInternalValue && failed.pc.value == 9 &&
        failed.work_units != 0 && failed.allocated_bytes != 0);
  }
  auto partial = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation, identity,
      nullptr, {}, nullptr, {}, RecoveryMode::kAllowPartial);
  if (malformed) test::Check(std::holds_alternative<ClassTreeFailure>(partial));
  else {
    test::Check(std::holds_alternative<ClassTree>(partial));
    auto& tree = std::get<ClassTree>(partial);
    test::Check(!tree.BodiesPrepared() && tree.DraftPrepared());
    test::Check(!std::get<ClassParentScope>(CheckClassParentScope(tree)).independent);
    auto omitted = std::move(tree.omissions);
    std::ostringstream rejected_output;
    test::Check(!tree.DraftPrepared());
    test::Check(std::holds_alternative<TreePrintError>(PrintClassTree(tree, rejected_output)) && rejected_output.str().empty());
    tree.omissions = std::move(omitted);
    const auto old_site = tree.member_sites[0]; tree.member_sites[0] = tree.member_sites[1];
    test::Check(!tree.DraftPrepared()); tree.member_sites[0] = old_site;
    test::Check(tree.DraftPrepared());
    std::ostringstream output;
    auto printed = PrintClassTree(tree, output);
    test::Check(std::holds_alternative<PrintedClassTree>(printed));
    test::Check(std::get<PrintedClassTree>(printed).incomplete);
    test::Check(output.str().starts_with("/* INCOMPLETE class:"));
    test::Check(output.str().find("return 42;") != std::string::npos);
    test::Check(output.str().find("UNRECOVERED class member 0") < output.str().find("return 42;"));
    test::Check(output.str().find("UNRECOVERED class member 2") > output.str().find("return 42;"));
  }
  test::Check(unlink(path) == 0);
}
}
int main() {Run(true); Run(false); Run(true, true); Run(true, false, 200); Run(false, false, 201);}
