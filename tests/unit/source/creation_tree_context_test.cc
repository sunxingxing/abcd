#include "../../../src/source/creation_tree_context.h"
#include "../../../src/source/function_file.h"
#include "../../../src/source/syntax_resolution.h"
#include "../../../src/source/family_file.h"
#include "abcd/source/verified_function.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/class_method.h"
#include "analysis/ssa_fixture.h"

#include <unistd.h>

#include <algorithm>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class ChildBody { kEmpty, kReceiver, kCapture, kCell, kRecursive };
enum class CallerObject { kAbsent, kPrimitive, kMethod };
constexpr std::uint32_t kPool = 64, kCaller = 256, kTarget = 300,
    kRecord = 128, kCallerCode = 400, kCodePadding = 16;
constexpr unsigned kFunctionKindShift = 8;  // Pinned dynamic method access flags.
constexpr std::uint8_t kDefineFunction = 0x33, kWideFunction = 0x74,
    kDefineMethod = 0x34, kWideMethod = 0xbe, kReturnUndefined = 0x65;
constexpr std::uint16_t kWideCache = 0x1234;

void Uleb(std::vector<std::uint8_t>& out, std::uint32_t value) {
  constexpr std::uint8_t kPayloadMask = 0x7f, kContinuation = 0x80;
  constexpr unsigned kPayloadBits = 7;
  do {
    auto byte = static_cast<std::uint8_t>(value & kPayloadMask);
    value >>= kPayloadBits;
    if (value) byte |= kContinuation;
    out.push_back(byte);
  } while (value);
}
abc::CodeInfo AppendCode(std::vector<std::uint8_t>& file,
    const std::vector<std::uint8_t>& instructions, std::uint32_t arguments) {
  const auto offset = static_cast<std::uint32_t>(file.size());
  Uleb(file, 0); Uleb(file, arguments);
  Uleb(file, static_cast<std::uint32_t>(instructions.size())); Uleb(file, 0);
  const auto start = static_cast<std::uint32_t>(file.size());
  file.insert(file.end(), instructions.begin(), instructions.end());
  return {{offset}, {start}, 0, arguments, static_cast<std::uint32_t>(instructions.size()), 0};
}

void Run(std::uint32_t count, abc::FunctionKind kind, ChildBody child,
         bool all_formats = false, std::uint8_t parameters = 0,
         CallerObject object = CallerObject::kAbsent) {
  std::vector<std::uint8_t> bytes(kCallerCode);
  constexpr unsigned kByteBits = 8;
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  std::vector<std::uint8_t> caller;
  if (object != CallerObject::kAbsent) {
    constexpr std::uint32_t kLiteral = 80, kKey = 112, kTaggedPairEntries = 4;
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (unsigned i = 0; i < sizeof(value); ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
    };
    word(kPool + sizeof(std::uint32_t), kLiteral);
    word(kLiteral, kTaggedPairEntries);
    auto at = kLiteral + sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
    word(at, kKey); at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(object == CallerObject::kMethod
        ? abc::LiteralTag::kMethod : abc::LiteralTag::kInteger);
    word(at, object == CallerObject::kMethod ? kTarget : 42);
    constexpr std::uint8_t kOneAsciiUnit = 3;
    bytes[kKey] = kOneAsciiUnit; bytes[kKey + 1] = 'x';
    constexpr std::uint8_t kObjectWithBuffer = 0x07;
    // Repeat the same primitive pool entry to exercise occurrence accounting.
    for (std::uint32_t i = 0; i < count; ++i)
      caller.insert(caller.end(), {kObjectWithBuffer, 0, 1, 0});
  }
  std::vector<std::uint32_t> pcs;
  for (std::uint32_t i = 0; i < count; ++i) {
    pcs.push_back(static_cast<std::uint32_t>(caller.size()));
    if (all_formats && i == 1)
      caller.insert(caller.end(), {kWideFunction, static_cast<std::uint8_t>(kWideCache),
          static_cast<std::uint8_t>(kWideCache >> kByteBits), 0, 0, parameters});
    else if (all_formats && i == 2) caller.insert(caller.end(), {kDefineMethod, 2, 0, 0, parameters});
    else if (all_formats && i == 3)
      caller.insert(caller.end(), {kWideMethod, static_cast<std::uint8_t>(kWideCache),
          static_cast<std::uint8_t>(kWideCache >> kByteBits), 0, 0, parameters});
    else caller.insert(caller.end(), {kDefineFunction, 1, 0, 0, parameters});
  }
  caller.push_back(kReturnUndefined);
  constexpr std::uint32_t kImplicitArguments = 3;
  const auto caller_code = AppendCode(bytes, caller, kImplicitArguments);
  bytes.resize(bytes.size() + kCodePadding);
  constexpr std::uint8_t kLoad = 0x60, kReturn = 0x64, kReceiver = 2,
      kReadCell = 0x3c, kStoreCell = 0x3d, kPush = 0x09, kUndefined = 0x00, kPop = 0x69;
  std::vector<std::uint8_t> target;
  switch (child) {
    case ChildBody::kEmpty: target = {kReturnUndefined}; break;
    case ChildBody::kReceiver: target = {kLoad, kReceiver, kReturn}; break;
    case ChildBody::kCapture: target = {kReadCell, 0, kReturn}; break;
    case ChildBody::kCell:
      target = {kPush, 1, kUndefined, kStoreCell, 0, kReadCell, 0, kPop, kReturn}; break;
    case ChildBody::kRecursive:
      target = {kDefineFunction, 0, 0, 0, parameters, kReturnUndefined}; break;
  }
  const auto target_code = AppendCode(bytes, target, kImplicitArguments + parameters);
  char path[] = "/tmp/abcd-creation-tree-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0 && write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206; header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCallerCode, {}, {object == CallerObject::kAbsent ? 1U : 2U, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller_code;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target_code;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "child"; index.methods[1].name_utf16_length = 5;
  index.methods[1].access_flags = static_cast<std::uint32_t>(kind) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  const auto range = sites.Find({kTarget});
  test::Check(range.count == count + (child == ChildBody::kRecursive ? 1 : 0) && sites.Find({kCaller}).count == 0);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto* site = sites.At({range.first.value + i});
    test::Check(site && site->target == abc::EntityOffset{kTarget} && site->creator == abc::EntityOffset{kCaller} &&
        site->pc.value == pcs[i] && site->resolved.position == abc::MethodPosition{1});
    if (all_formats && (i == 1 || i == 3)) test::Check(site->creation.cache_slot == kWideCache);
    test::Check(site->creation.kind == (all_formats && i >= 2 ? CreationKind::kMethod : CreationKind::kFunction));
    test::Check(sites.FindOccurrence({kTarget}, {kCaller}, {pcs[i]}) == FunctionSiteId{range.first.value + i});
    test::Check(!sites.FindOccurrence({kTarget}, {kCaller}, {pcs[i] + 1}));
  }
  test::Check(!sites.FindOccurrence({kCaller}, {kCaller}, {}));
  SemanticBudget limited; limited.work = sites.Work();
  test::Check(std::holds_alternative<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup, limited)));
  --limited.work;
  test::Check(std::get<FunctionSiteError>(std::get<FunctionSiteFailure>(
      FunctionCreationSites::Build(file, header, index, lookup, limited)).error) == FunctionSiteError::kWorkLimit);
  {
    const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
    const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
    const auto family_result = ReadFunctionFamily(file, header, index, lookup, roles, sites, {0}, identity);
    const bool family_refuses = all_formats || child == ChildBody::kCapture || child == ChildBody::kRecursive ||
        (object == CallerObject::kMethod && count != 0);
    test::Check(std::holds_alternative<FamilyFileFailure>(family_result) == family_refuses);
    if (object == CallerObject::kMethod && count != 0) {
      const auto& failure = std::get<FamilyFileFailure>(family_result);
      test::Check(std::get<BodyEmitFailure>(failure.cause).object_error == ObjectBufferError::kValue &&
          failure.creation_pc == abc::CodeOffset{0});
    }
    if (child == ChildBody::kRecursive) {
      const auto& failure = std::get<FamilyFileFailure>(family_result);
      test::Check(std::get<FamilyFileError>(failure.cause) == FamilyFileError::kRecursiveCreation &&
          failure.method == abc::EntityOffset{kTarget} && failure.parent == abc::EntityOffset{kTarget} && failure.creation_pc == abc::CodeOffset{0});
    }
    if (!family_refuses) {
      const auto& family = std::get<FileFamily>(family_result);
      test::Check(family.members.size() == count + 1 && family.members[0].children.size() == count);
      test::Check(!family.members[0].incoming && !family.members[0].context);
      test::Check(family.members[0].objects.size() == (object == CallerObject::kPrimitive ? count : 0));
      test::Check(family.members[0].function.input.Children().size() ==
          family.members[0].children.size() + family.members[0].objects.size());
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto& member = family.members[i + 1];
        test::Check(member.function.input.Method() == abc::EntityOffset{kTarget} && member.context && member.incoming);
        test::Check(member.incoming->parent == FileFamily::kRoot && member.incoming->creation.offset.value == pcs[i]);
        test::Check(family.members[0].children[i].child == FunctionOwnerId{i + 1});
        test::Check(member.children.empty());
      }
      SemanticBudget family_budget; family_budget.bytes = 0;
      test::Check(std::get<FamilyFileError>(std::get<FamilyFileFailure>(ReadFunctionFamily(
          file, header, index, lookup, roles, sites, {0}, identity, family_budget)).cause) == FamilyFileError::kResourceLimit);
      family_budget = {}; family_budget.work = 0;
      test::Check(std::get<FamilyFileError>(std::get<FamilyFileFailure>(ReadFunctionFamily(
          file, header, index, lookup, roles, sites, {0}, identity, family_budget)).cause) == FamilyFileError::kWorkLimit);
    }
    if (count != 1 && !all_formats) { test::Check(unlink(path) == 0); return; }
    const auto conditional = ReadFunctionValues(file, header, index, {1}, identity, InputCaptureMode::kConditional);
    test::Check(std::holds_alternative<FileFunction>(conditional));
    const auto& opened_child = std::get<FileFunction>(conditional);
    test::Check(opened_child.input.Method() == abc::EntityOffset{kTarget} &&
        opened_child.values.method == opened_child.input.Method() &&
        opened_child.input.Mode() == InputCaptureMode::kConditional);
    test::Check(opened_child.input.Captures().size() == (child == ChildBody::kCapture ? 1 : 0));
    auto created = std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, range.first, identity));
    const FunctionContext context{std::move(created)};
    constexpr FunctionOwnerId kSyntaxOwner{99};
    const auto resolve = [&](SemanticBudget budget = {}) {
      return ResolveFunctionSyntax(file, header, index, context, opened_child.body,
          opened_child.values.graph, kSyntaxOwner, budget);
    };
    const auto syntax = std::get<ResolvedSyntax>(resolve());
    test::Check(syntax.syntax.owner == kSyntaxOwner && syntax.syntax.method == context.Target() &&
        syntax.syntax.identity == identity && syntax.syntax.callable == context.Callable() &&
        syntax.syntax.strictness == context.Mode() && syntax.syntax.parameters.size() == parameters);
    test::Check(syntax.operations.size() == opened_child.values.graph.ops.size() &&
        syntax.value_revision == opened_child.values.graph.revision &&
        syntax.cfg_revision == opened_child.values.graph.cfg_revision &&
        syntax.View().name_count == syntax.syntax.names.size() &&
        syntax.View().text_count == syntax.syntax.texts.size());
    for (const auto& parameter : syntax.syntax.parameters)
      test::Check(!parameter.name && parameter.evidence == NameEvidence::kSynthesized);
    SemanticBudget syntax_budget; syntax_budget.work = syntax.work;
    test::Check(std::holds_alternative<ResolvedSyntax>(resolve(syntax_budget)));
    --syntax_budget.work;
    test::Check(std::holds_alternative<SyntaxResolutionFailure>(resolve(syntax_budget)));
    syntax_budget = {}; syntax_budget.bytes = 0;
    test::Check(std::get<SyntaxResolutionError>(std::get<SyntaxResolutionFailure>(resolve(syntax_budget))) == SyntaxResolutionError::kResourceLimit);
    auto wrong_index = index; ++wrong_index.methods[context.Position().value].offset.value;
    test::Check(std::get<SyntaxResolutionError>(std::get<SyntaxResolutionFailure>(ResolveFunctionSyntax(
        file, header, wrong_index, context, opened_child.body, opened_child.values.graph, kSyntaxOwner))) == SyntaxResolutionError::kContext);
    if (child == ChildBody::kCapture)
      test::Check(std::holds_alternative<FunctionFileFailure>(ReadFunctionValues(file, header,
          index, {1}, identity, InputCaptureMode::kClosed)));
    auto stale = identity; ++stale.file_size;
    test::Check(std::get<FunctionFileError>(std::get<FunctionFileFailure>(ReadFunctionValues(file,
        header, index, {1}, stale, InputCaptureMode::kConditional))) == FunctionFileError::kIdentity);
    SemanticBudget empty; empty.bytes = 0;
    test::Check(std::get<FunctionFileError>(std::get<FunctionFileFailure>(ReadFunctionValues(file,
        header, index, {1}, identity, InputCaptureMode::kConditional, empty))) == FunctionFileError::kResourceLimit);
    auto prepared = VerifiedFunction::PrepareCreated(file, header, index, lookup, roles, sites, range.first, identity);
    const bool refuses = child == ChildBody::kCapture || child == ChildBody::kRecursive ||
        (kind == abc::FunctionKind::kArrow && child == ChildBody::kReceiver);
    test::Check(std::holds_alternative<FunctionPrepareFailure>(prepared) == refuses);
    if (!refuses) {
      const auto& function = std::get<VerifiedFunction>(prepared);
      test::Check(!function.ClassCreation() && function.FunctionCreation() &&
          function.FunctionCreation()->pc.value == 0 && function.Receipt().stage == SourceStage::kTreeVerified);
      test::Check(function.Tree().callable == (kind == abc::FunctionKind::kArrow ? CallableKind::kArrow : CallableKind::kFunction));
      if (child == ChildBody::kCell) {
        test::Check(std::any_of(function.Tree().symbols.begin(), function.Tree().symbols.end(),
            [](const auto& symbol) { return symbol.kind == BindingKind::kLocal; }));
      }
      std::ostringstream output;
      test::Check(std::get<TreePrintError>(PrintFunctionTree(function, output)) == TreePrintError::kUnsupportedContext);
      test::Check(output.str().empty());
    }
    if (all_formats) {
      test::Check(std::get<FunctionPrepareFailure>(VerifiedFunction::PrepareCreated(
          file, header, index, lookup, roles, sites, {range.first.value + 2}, identity)).kind == FunctionPrepareError::kContext);
    }
  }
  test::Check(unlink(path) == 0);
}
}  // namespace
int main() {
  for (const auto count : {0U, 1U, 2U, 200U, 201U}) Run(count, abc::FunctionKind::kBaseConstructor, ChildBody::kEmpty);
  Run(4, abc::FunctionKind::kBaseConstructor, ChildBody::kEmpty, true);
  for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
    for (const auto child : {ChildBody::kEmpty, ChildBody::kReceiver, ChildBody::kCapture, ChildBody::kCell}) Run(1, kind, child);
  for (const auto count : {1U, 2U, 200U, 201U})
    Run(1, abc::FunctionKind::kBaseConstructor, ChildBody::kEmpty, false, static_cast<std::uint8_t>(count));
  Run(1, abc::FunctionKind::kBaseConstructor, ChildBody::kRecursive);
  for (const auto count : {0U, 1U, 2U, 200U, 201U})
    Run(count, abc::FunctionKind::kBaseConstructor, ChildBody::kEmpty, false, 0, CallerObject::kPrimitive);
  Run(1, abc::FunctionKind::kBaseConstructor, ChildBody::kEmpty, false, 0, CallerObject::kMethod);
}
