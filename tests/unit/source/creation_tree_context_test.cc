#include "../../../src/source/creation_tree_context.h"
#include "../../../src/source/function_file.h"
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
enum class ChildBody { kEmpty, kReceiver, kCapture, kCell };
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

void Run(std::uint32_t count, abc::FunctionKind kind, ChildBody child, bool all_formats = false) {
  std::vector<std::uint8_t> bytes(kCallerCode);
  constexpr unsigned kByteBits = 8;
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  std::vector<std::uint8_t> caller;
  std::vector<std::uint32_t> pcs;
  for (std::uint32_t i = 0; i < count; ++i) {
    pcs.push_back(static_cast<std::uint32_t>(caller.size()));
    if (all_formats && i == 1)
      caller.insert(caller.end(), {kWideFunction, static_cast<std::uint8_t>(kWideCache),
          static_cast<std::uint8_t>(kWideCache >> kByteBits), 0, 0, 0});
    else if (all_formats && i == 2) caller.insert(caller.end(), {kDefineMethod, 2, 0, 0, 0});
    else if (all_formats && i == 3)
      caller.insert(caller.end(), {kWideMethod, static_cast<std::uint8_t>(kWideCache),
          static_cast<std::uint8_t>(kWideCache >> kByteBits), 0, 0, 0});
    else caller.insert(caller.end(), {kDefineFunction, 1, 0, 0, 0});
  }
  caller.push_back(kReturnUndefined);
  const auto caller_code = AppendCode(bytes, caller, 0);
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
  }
  constexpr std::uint32_t kImplicitArguments = 3;
  const auto target_code = AppendCode(bytes, target, kImplicitArguments);
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
  index.regions.push_back({kCaller, kCallerCode, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller_code;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target_code;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "child"; index.methods[1].name_utf16_length = 5;
  index.methods[1].access_flags = static_cast<std::uint32_t>(kind) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  const auto range = sites.Find({kTarget});
  test::Check(range.count == count && sites.Find({kCaller}).count == 0);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto* site = sites.At({range.first.value + i});
    test::Check(site && site->target == abc::EntityOffset{kTarget} && site->creator == abc::EntityOffset{kCaller} &&
        site->pc.value == pcs[i] && site->resolved.position == abc::MethodPosition{1});
    if (all_formats && (i == 1 || i == 3)) test::Check(site->creation.cache_slot == kWideCache);
    test::Check(site->creation.kind == (all_formats && i >= 2 ? CreationKind::kMethod : CreationKind::kFunction));
  }
  SemanticBudget limited; limited.work = sites.Work();
  test::Check(std::holds_alternative<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup, limited)));
  --limited.work;
  test::Check(std::get<FunctionSiteError>(std::get<FunctionSiteFailure>(
      FunctionCreationSites::Build(file, header, index, lookup, limited)).error) == FunctionSiteError::kWorkLimit);
  if (count == 1 || all_formats) {
    const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
    const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
    const auto conditional = ReadFunctionValues(file, header, index, {1}, identity, InputCaptureMode::kConditional);
    test::Check(std::holds_alternative<FileFunction>(conditional));
    const auto& opened_child = std::get<FileFunction>(conditional);
    test::Check(opened_child.input.Method() == abc::EntityOffset{kTarget} &&
        opened_child.values.method == opened_child.input.Method() &&
        opened_child.input.Mode() == InputCaptureMode::kConditional);
    test::Check(opened_child.input.Captures().size() == (child == ChildBody::kCapture ? 1 : 0));
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
    const bool refuses = child == ChildBody::kCapture || (kind == abc::FunctionKind::kArrow && child == ChildBody::kReceiver);
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
}
