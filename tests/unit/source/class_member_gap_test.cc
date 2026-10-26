#include "../../../src/source/class_tree.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/tree_semantics.h"
#include <unistd.h>
#include <algorithm>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr std::uint32_t kPool = 64, kLiteral = 160, kKey = 4096, kCaller = 256,
    kCtor = 300, kRejected = 340, kAccepted = 380, kChild = 420, kCode = 8192, kMethodStride = 40;
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
void Run(bool caught, bool malformed = false, std::uint32_t count = 3, std::uint32_t depth = 0, bool defined = false,
    abc::ProfileId profile = abc::ProfileId::kDynamic1206) {
  const bool nested = depth != 0;
  const auto children = std::max(depth, 1U);
  const auto key_slot = 4 + children;
  const auto failed_method = nested ? kChild + (depth - 1) * kMethodStride : kRejected;
  std::vector<std::uint8_t> bytes(kCode);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  word(kPool, kCtor); word(kPool + sizeof(kCtor), kLiteral);
  word(kPool + 2 * sizeof(kCtor), kRejected); word(kPool + 3 * sizeof(kCtor), kAccepted);
  for (std::uint32_t i = 0; i < children; ++i) word(kPool + (4 + i) * sizeof(kCtor), kChild + i * kMethodStride);
  word(kPool + key_slot * sizeof(kCtor), kKey);
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
  if (defined) {word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    word(kLiteral + 5, 0);}
  std::vector<std::uint8_t> caller{Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8), 0, 0, 0, 1, 0, 0, 0, 0,
      Op(abc::Opcode::kRETURN)};
  if (defined) {
    caller.pop_back(); caller.insert(caller.end(), {Op(abc::Opcode::kSTA_V8), 1});
    for (std::uint32_t i = 0; i < count; ++i) {
      caller.insert(caller.end(), {Op(abc::Opcode::kLDA_V8), 1,
          Op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8), 0,
          static_cast<std::uint8_t>(i == 1 ? 3 : 2), 0, static_cast<std::uint8_t>(i == 1 ? 0 : 1),
          Op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0, static_cast<std::uint8_t>(key_slot), 0, 1});
    }
    caller.insert(caller.end(), {Op(abc::Opcode::kLDA_V8), 1, Op(abc::Opcode::kRETURN)});
  }
  const std::uint8_t ctor[]{Op(abc::Opcode::kRETURNUNDEFINED)};
  std::vector<std::uint8_t> rejected{Op(abc::Opcode::kLDA_V8), 4, Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kNOP), Op(abc::Opcode::kADD2_IMM8_V8), 0, 0,
      Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kRETURN)};
  if (caught) rejected.push_back(Op(abc::Opcode::kRETURNUNDEFINED));
  if (malformed) rejected[1] = 250;
  const std::uint8_t accepted[]{Op(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0, Op(abc::Opcode::kRETURN)};
  const std::uint8_t enclosing[]{Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 4, 0, 1,
      Op(abc::Opcode::kRETURN)};
  std::vector<abc::CodeInfo> code{Append(bytes, caller, defined ? 2 : 1, 3), Append(bytes, ctor, 0, 3),
      nested ? Append(bytes, enclosing, 0, 4) : Append(bytes, rejected, 1, 4, caught),
      Append(bytes, accepted, 0, 3)};
  for (std::uint32_t i = 0; i < children; ++i) {
    if (i + 1 == children) code.push_back(Append(bytes, rejected, 1, 4, caught));
    else {
      const std::uint8_t next[]{Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0,
          static_cast<std::uint8_t>(5 + i), 0, 1, Op(abc::Opcode::kRETURN)};
      code.push_back(Append(bytes, next, 0, 4));
    }
  }
  char path[] = "/tmp/abcd-class-member-gap-XXXXXX";
  auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = profile;
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12,0,2,0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13,0,1,0} : abc::AbcVersion{12,0,6,0}; header.file_size = bytes.size();
  abc::MetadataIndex index; index.regions.push_back({kCaller, static_cast<std::uint32_t>(bytes.size()), {}, {key_slot + 1, kPool}, {}, {}});
  index.methods.resize(4 + children);
  std::vector<std::uint32_t> offsets{kCaller, kCtor, kRejected, kAccepted};
  for (std::uint32_t i = 0; i < children; ++i) offsets.push_back(kChild + i * kMethodStride);
  for (std::size_t i = 0; i < index.methods.size(); ++i) {
    auto& method = index.methods[i]; method.offset = {offsets[i]}; method.owner_offset = {128}; method.code = code[i];
    method.name = i == 1 ? "Known" : "body"; method.name_utf16_length = method.name.size();
    method.access_flags = static_cast<std::uint32_t>(i < 2 || i >= 4 ? abc::FunctionKind::kBaseConstructor : abc::FunctionKind::kNone) << kKindShift;
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
  auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  identity.contract = kSourceContract;  // A genuine private-value input refusal.
  const auto creator = std::get<FileFunction>(ReadFunctionValues(*file, header, index, {0}, identity, InputCaptureMode::kClosed));
  const auto creation = creator.values.classes.classes.front().operation;
  const auto definitions = defined ? std::span<const ClassDefinitionSite>(creator.values.definitions.sites) : std::span<const ClassDefinitionSite>{};
  auto complete = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation, identity, nullptr, {}, nullptr, definitions);
  test::Check(std::holds_alternative<ClassTreeFailure>(complete));
  const auto& refusal = std::get<FamilyPrepareFailure>(std::get<ClassTreeFailure>(complete));
  const auto& failed_family = std::get<FamilyFileFailure>(refusal.cause);
  test::Check(failed_family.root == abc::EntityOffset{kRejected});
  test::Check(failed_family.method == abc::EntityOffset{failed_method});
  test::Check(failed_family.parent == (nested ? std::optional<abc::EntityOffset>{{depth == 1 ? kRejected : failed_method - kMethodStride}} : std::nullopt));
  if (caught) {
    const auto& failed = std::get<PhasedInputFailure>(std::get<PhasedFileFailure>(failed_family.cause));
    test::Check(failed.cause.kind == ExceptionInputError::kInternalValue && failed.cause.pc.value == 9 &&
        failed.cause.work_units != 0 && failed.work != 0 && failed.allocated_bytes != 0);
  } else {
    const auto& failed = std::get<SemanticProofFailure>(
        std::get<FunctionInputFailure>(std::get<FunctionFileFailure>(failed_family.cause)).cause);
    test::Check(failed.kind == SemanticProofError::kInternalValue && failed.pc.value == 6 &&
        failed.work_units != 0 && failed.allocated_bytes != 0);
  }
  auto partial = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation, identity,
      nullptr, {}, nullptr, definitions, RecoveryMode::kAllowPartial);
  {
    test::Check(std::holds_alternative<ClassTree>(partial));
    auto& tree = std::get<ClassTree>(partial);
    test::Check(!tree.BodiesPrepared() && tree.DraftPrepared());
    test::Check(!std::get<ClassParentScope>(CheckClassParentScope(tree)).independent);
    if (defined) {
      test::Check(tree.members.empty() && tree.definitions.size() == count);
    } else {
      auto omitted = std::move(tree.omissions);
      std::ostringstream rejected_output;
      test::Check(!tree.DraftPrepared());
      test::Check(std::holds_alternative<TreePrintFailure>(PrintClassTree(tree, rejected_output)) && rejected_output.str().empty());
      tree.omissions = std::move(omitted);
      const auto old_site = tree.member_sites[0]; tree.member_sites[0] = tree.member_sites[1];
      test::Check(!tree.DraftPrepared()); tree.member_sites[0] = old_site;
      test::Check(tree.DraftPrepared());
    }
    std::ostringstream output;
    auto printed = PrintClassTree(tree, output);
    test::Check(std::holds_alternative<PrintedClassTree>(printed));
    test::Check(std::get<PrintedClassTree>(printed).incomplete);
    test::Check(output.str().starts_with("/* INCOMPLETE class:"));
    test::Check(output.str().find("return 42;") != std::string::npos);
    const auto gap_label = defined ? "UNRECOVERED class definition" : "UNRECOVERED class member";
    test::Check(output.str().find(gap_label) < output.str().find("return 42;"));
    test::Check(output.str().rfind(gap_label) > output.str().find("return 42;"));
    test::Check(output.str().find("failed method " + std::to_string(failed_method)) != std::string::npos);
    const auto memory_limited = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation,
        identity, nullptr, {tree.allocated_bytes - 1}, nullptr, definitions, RecoveryMode::kAllowPartial);
    test::Check(std::holds_alternative<ClassTreeFailure>(memory_limited));
    const auto work_limited = BuildClassTree(*file, header, index, lookup, roles, functions, classes, creator, creation,
        identity, nullptr, {analysis::SemanticBudget{}.bytes, tree.work - 1}, nullptr, definitions, RecoveryMode::kAllowPartial);
    test::Check(std::holds_alternative<ClassTreeFailure>(work_limited));
    // Final parent semantics must still check every original definition even
    // when its complete member family is absent from the draft.
    auto constants = std::get<TreeConstants>(ResolveTreeConstants(*file, header, index, creator.input.Method(), creator.values.graph));
    auto layout = std::make_shared<ClassTree>(std::move(tree));
    constants.operations[creation.value] = layout;
    FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = creator.input.Method();
    syntax.owner = {0}; syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = constants.names; syntax.texts = constants.texts;
    const auto& graph = creator.values.graph;
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject,
        {}, {}, {}, {}, analysis::ClosureWrites::kLanguage, definitions));
    const auto built = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation,
        std::move(syntax), constants.View()));
    test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, built.tree, constants.View())));
    auto& gap = defined ? *layout->definitions[0].omissions[0] : layout->omissions[0];
    ++gap.root.value;
    test::Check(!layout->DraftPrepared());
    if (defined) test::Check(std::get<TreeSemanticError>(VerifyTreeSemantics(graph, control, evaluation, built.tree, constants.View())) ==
        TreeSemanticError::kConstant);
    --gap.root.value;
    gap.parent.reset(); gap.creation_pc = abc::CodeOffset{0};
    test::Check(!layout->DraftPrepared());
  }
  test::Check(unlink(path) == 0);
}
}
int main() {
  Run(true); Run(false); Run(true, true); Run(true, false, 200); Run(false, false, 201);
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    for (const bool caught : {false, true}) for (const bool defined : {false, true}) {
      for (const auto depth : {1U, 2U, 3U}) Run(caught, false, 3, depth, defined, profile);
      Run(caught, false, 3, 0, defined, profile);
    }
  }
  Run(false, false, 201, 3, true); Run(true, false, 200, 2, true);
  Run(false, true, 3, 2, true);
}
