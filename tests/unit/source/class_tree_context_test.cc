#include "../../../src/source/class_tree_context.h"
#include "../../../src/source/prepared_family.h"
#include "../../../src/source/family_emission.h"
#include "abcd/source/verified_function.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/class_method.h"
#include "abcd/source/global_reference.h"
#include "../../support/function_segments.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
static_assert(!std::is_copy_constructible_v<VerifiedFunction>);
static_assert(!std::is_copy_constructible_v<FunctionEmission>);
static_assert(!std::is_copy_constructible_v<FamilyEmission>);
static_assert(!std::is_constructible_v<FunctionEmission, VerifiedFunction,
                                     PrintedFunction, GeneratedRange, MethodRouteReceipt, std::size_t>);
static_assert(!std::is_constructible_v<VerifiedFunction, FunctionTree>);
static_assert(std::is_same_v<decltype(std::declval<VerifiedFunction&>().Tree()),
                             const FunctionTree&>);
void Check(bool condition) { if (!condition) std::abort(); }
enum class DefaultEncoding { kNone, kInteger, kNegation };
void Run(DefaultEncoding encoding = DefaultEncoding::kNone, bool global_typeof = false,
         bool object_initializer = false) {
  const bool defaults = encoding != DefaultEncoding::kNone;
  constexpr std::uint32_t kPool = 64, kOwner = 80, kLiteral = 128, kCaller = 256,
      kConstructor = 300, kMember = 350, kCallerCode = 400, kMemberCode = 430, kName = 500;
  std::vector<std::uint8_t> bytes(640);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor);
  word(kPool + sizeof(std::uint32_t), kLiteral);
  word(kPool + 2 * sizeof(std::uint32_t), kName);
  word(kLiteral, 8);  // Four tag/value entries, including prototype split.
  bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kString);
  word(kLiteral + 5, kName);
  bytes[kLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod);
  word(kLiteral + 10, kMember);
  bytes[kLiteral + 14] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
  const std::uint8_t encoded_length = defaults ? 0 : 1;
  bytes[kLiteral + 15] = encoded_length;
  bytes[kLiteral + 17] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 18, 1);
  bytes[kName] = 7;
  std::copy_n("run", 3, bytes.begin() + kName + 1);
  const std::vector<std::uint8_t> caller{1, 0, 10, 0, 0x35, 0, 0, 0, 1, 0, 0, 0, 0, 0x65};
  // OpenHarmony-v7.0-Release isa.yaml: dynamic loads, strict comparison,
  // relative conditional jump and return. Header encodes locals/args/bytes/tries.
  constexpr std::uint8_t kUndefined = 0x00, kStrictNotEqual = 0x27,
      kJumpNonzero = 0x51, kLoad = 0x60, kStore = 0x61,
      kInteger = 0x62, kReturn = 0x64, kParameter = 3, kNegate = 0x1f;
  constexpr std::uint8_t kDefaultBodyBytes = 16, kJoinDelta = 9, kMinusOneByte = 0xff;
  std::vector<std::uint8_t> member = defaults
      ? std::vector<std::uint8_t>{0, 4, kDefaultBodyBytes, 0,
          kUndefined, kStrictNotEqual, 0, kParameter, kJumpNonzero, kJoinDelta,
          kInteger, kMinusOneByte, kMinusOneByte, kMinusOneByte, kMinusOneByte,
          kStore, kParameter, kLoad, kParameter, kReturn}
      : std::vector<std::uint8_t>{0, 4, 3, 0, kLoad, kParameter, kReturn};
  constexpr std::size_t kHeaderBytes = 4, kIntegerPc = 6, kStorePc = 11,
      kBranchPc = 4, kCodeSizeOffset = 2;
  constexpr std::uint8_t kNegateBytes = 2;
  if (encoding == DefaultEncoding::kNegation) {
    member[kHeaderBytes + kIntegerPc + 1] = 1;
    for (std::size_t i = 2; i <= sizeof(std::int32_t); ++i)
      member[kHeaderBytes + kIntegerPc + i] = 0;
    member.insert(member.begin() + kHeaderBytes + kStorePc, {kNegate, 0});
    member[kHeaderBytes + kBranchPc + 1] += kNegateBytes;
    member[kCodeSizeOffset] += kNegateBytes;
  }
  if (global_typeof) {
    constexpr std::uint8_t kReadGlobal = 0x3f, kTypeof = 0x1c, kGlobalBodyBytes = 7;
    member = {0, 4, kGlobalBodyBytes, 0, kReadGlobal, 0, 2, 0, kTypeof, 0, kReturn};
  }
  if (object_initializer) {
    // Pinned v7 ISA: createemptyobject; sta; lda; definefieldbyname; lda; return.
    constexpr std::uint8_t kEmptyObject = 0x04, kDefineField = 0xdb,
        kObjectBodyBytes = 13, kObjectRegister = 0, kObjectParameter = 4,
        kNamePoolIndex = 2;
    member = {1, 4, kObjectBodyBytes, 0, kEmptyObject, kStore, kObjectRegister,
        kLoad, kObjectParameter, kDefineField, 0, kNamePoolIndex, 0,
        kObjectRegister, kLoad, kObjectRegister, kReturn};
  }
  std::copy(caller.begin(), caller.end(), bytes.begin() + kCallerCode);
  std::copy(member.begin(), member.end(), bytes.begin() + kMemberCode);
  char path[] = "/tmp/abcd-class-tree-context-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0};
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCallerCode, {}, {3, kPool}, {}, {}});
  index.methods.resize(3);
  for (auto& method : index.methods) method.owner_offset = {kOwner};
  index.methods[0].offset = {kCaller};
  index.methods[0].code = abc::CodeInfo{{kCallerCode}, {kCallerCode + 4}, 1, 0, 10, 0};
  index.methods[1].offset = {kConstructor};
  index.methods[2].offset = {kMember};
  index.methods[2].code = abc::CodeInfo{{kMemberCode}, {kMemberCode + 4}, member[0], 4,
                                      member[kCodeSizeOffset], 0};
  index.methods[2].name = "run";
  index.methods[2].name_utf16_length = 3;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto range = sites.Find({kMember});
  Check(range.count == 1 && sites.Find({kConstructor}).count == 0);
  const auto* site = sites.At(range.first);
  Check(site && site->creator == abc::EntityOffset{kCaller} && site->pc.value == 0 && site->length == encoded_length);
  auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  auto context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::holds_alternative<ClassTreeContext>(context));
  const auto& value = std::get<ClassTreeContext>(context);
  Check(value.Name() == u"run" && value.Frame().parameters == 1);
  Check(value.Callable() == CallableKind::kMethod && value.Mode() == Strictness::kStrict);
  if (global_typeof) {
    const GlobalScope scope{}; // Complete enclosure of this synthetic fixture.
    const auto denied = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
    Check(std::get<FunctionPrepareFailure>(denied).kind == FunctionPrepareError::kContext);
    auto ready = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity, {}, &scope);
    Check(std::holds_alternative<VerifiedFunction>(ready));
    const auto& function = std::get<VerifiedFunction>(ready);
    std::ostringstream output;
    Check(std::get<TreePrintError>(PrintFunctionTree(function, output)) == TreePrintError::kUnsupportedContext);
    Check(output.view().empty());
    auto shadow = scope;
    shadow.class_binding = "run";
    Check(std::get<TreePrintError>(PrintFunctionTree(function, output, {}, {}, &shadow)) == TreePrintError::kUnsupportedContext);
    Check(output.view().empty());
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(function, output, {}, {}, &scope)));
    Check(output.view().find("return (typeof (0, run));") != std::string_view::npos);
    auto rejected = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity, {}, &shadow);
    Check(std::get<FunctionPrepareFailure>(rejected).kind == FunctionPrepareError::kContext);
    auto family = PreparedFamily::PrepareClass(file, header, index, lookup, roles, functions,
        sites, range.first, identity, {Strictness::kStrict, &scope, FunctionFragment::kClassMember});
    Check(std::holds_alternative<PreparedFamily>(family));
    std::ostringstream changed_destination;
    const auto family_refused = std::get<PreparedFamily>(family).PrintDraft(
        {Strictness::kStrict, &shadow, FunctionFragment::kClassMember}, changed_destination);
    Check(std::get<FamilyEnclosureFailure>(std::get<FamilyPrepareFailure>(family_refused).cause).kind ==
        FamilyEnclosureError::kShadowedGlobal && changed_destination.str().empty());
    Check(unlink(path) == 0);
    return;
  }
  auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::holds_alternative<VerifiedFunction>(prepared));
  const auto& function = std::get<VerifiedFunction>(prepared);
  Check(function.Name() == u"run" && function.ClassCreation() &&
      function.ClassCreation()->target == abc::EntityOffset{kMember} && !function.FunctionCreation());
  Check(function.Tree().parameters.size() == 1);
  Check(function.Tree().callable == CallableKind::kMethod);
  Check(function.Tree().strictness == Strictness::kStrict);
  Check(function.Tree().identity == identity);
  Check(function.Receipt().stage == SourceStage::kTreeVerified);
  Check(function.Receipt().coverage == Coverage::kIncomplete);
  Check(!function.Receipt().failure);
  Check(function.Abi().length == encoded_length && function.Abi().parameters.size() == 1);
  Check(function.Abi().layout == (defaults ? ParameterLayout::kLiteralDefaults : ParameterLayout::kSimple));
  Check(function.Receipt().work_revision == function.Tree().value_revision.value);
  Check(function.Tree().signature.empty() != defaults);
  Check(function.Abi().parameters[0].reads.operands == 1);
  Check(function.Abi().parameters[0].reads.references == 1);
  Check(function.Abi().receiver.operands == 0 && function.Abi().receiver.references == 0);
  std::ostringstream output;
  const auto printing = PrintFunctionTree(function, output);
  Check(std::holds_alternative<PrintedFunction>(printing));
  const auto& printed = std::get<PrintedFunction>(printing);
  auto family_result = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
      functions, sites, range.first, identity, {Strictness::kStrict, nullptr, FunctionFragment::kClassMember});
  Check(std::holds_alternative<PreparedFamily>(family_result));
  const auto& family = std::get<PreparedFamily>(family_result);
  std::ostringstream family_output;
  const auto family_print = family.PrintDraft({Strictness::kStrict, nullptr, FunctionFragment::kClassMember}, family_output);
  Check(std::holds_alternative<PrintedFamilyDraft>(family_print));
  const auto& family_positions = std::get<PrintedFamilyDraft>(family_print);
  Check(family_positions.members.size() == 1 &&
      family_positions.members[0].fragment == FunctionFragment::kClassMember);
  Check(family_output.str() == output.str());
  Check(family_positions.members[0].parameters == printed.parameters);
  std::ostringstream wrong_context;
  Check(std::get<TreePrintError>(PrintFamilyDraft(family.Family(), wrong_context)) ==
      TreePrintError::kUnsupportedContext && wrong_context.str().empty());
  auto wrong_owner = family.Family(); wrong_owner.members[0].tree.method.value += 1;
  Check(std::get<TreePrintError>(PrintClassFamilyDraft(wrong_owner, value, wrong_context)) ==
      TreePrintError::kUnsupportedContext && wrong_context.str().empty());
  const auto placement_refusal = family.PrintDraft({Strictness::kStrict, nullptr}, wrong_context);
  Check(std::get<FamilyPrepareError>(std::get<FamilyPrepareFailure>(placement_refusal).cause) ==
      FamilyPrepareError::kPlacement && wrong_context.str().empty());
  std::ostringstream emitted_output;
  auto family_emission = FamilyEmission::Write(std::get<PreparedFamily>(std::move(family_result)),
      {Strictness::kStrict, nullptr, FunctionFragment::kClassMember}, emitted_output, {});
  Check(std::holds_alternative<FamilyEmission>(family_emission));
  const auto& published_family = std::get<FamilyEmission>(family_emission);
  Check(published_family.Receipts().size() == 1 && emitted_output.str() == output.str());
  Check(published_family.Receipts()[0].stage == SourceStage::kPrinted &&
      published_family.Receipts()[0].coverage == Coverage::kComplete &&
      !ValidateRouteReceipt(published_family.Receipts()[0]));
  Check(printed.fragment == FunctionFragment::kClassMember);
  Check(printed.bytes == output.view().size());
  Check(output.view().starts_with(defaults ? "run($arg0 = (-1)) {\n" : "run($arg0) {\n"));
  Check(printed.parameters.size() == 1);
  const auto parameter = printed.parameters[0];
  Check(parameter.begin < parameter.end && parameter.end <= printed.bytes);
  Check(output.view().substr(parameter.begin, parameter.end - parameter.begin) ==
        (defaults ? "$arg0 = (-1)" : "$arg0"));
  for (const auto placement : function.Tree().signature) {
    Check(placement.parameter == ParameterId{0});
    const auto* origin = std::get_if<OriginalSite>(&function.Tree().origins[placement.origin.value]);
    constexpr std::uint32_t kDirectJoinPc = 13;
    const auto join = kDirectJoinPc + (encoding == DefaultEncoding::kNegation ? kNegateBytes : 0);
    Check(origin && origin->pc.value < join);
  }
  Check(printed.expressions.size() == function.Tree().expressions.size());
  Check(printed.properties.size() == function.Tree().properties.size());
  if (object_initializer) {
    Check(printed.properties.size() == 1);
    const auto property = printed.properties[0];
    Check(property.begin < property.end && property.end <= printed.bytes);
    Check(output.view().substr(property.begin, property.end - property.begin) == "[\"run\"]: $arg0");
    const auto origin = function.Tree().properties[0].origin;
    const auto* site = std::get_if<OriginalSite>(&function.Tree().origins[origin.value]);
    constexpr std::uint32_t kDefinitionPc = 5;
    Check(site && site->pc.value == kDefinitionPc);
  }
  Check(printed.statements.size() == function.Tree().statements.size());
  for (const auto range : printed.expressions)
    Check(range.begin < range.end && range.end <= printed.bytes);
  for (const auto range : printed.statements)
    Check(range.begin < range.end && range.end <= printed.bytes);
  for (const auto offset : printed.anchors) Check(offset <= printed.bytes);
  std::ostringstream denied;
  Check(std::get<TreePrintError>(PrintFunctionTree(function, denied, {0})) == TreePrintError::kOutputLimit);
  Check(denied.view().empty());
  Check(std::get<TreePrintError>(PrintFunctionTree(function, denied, {}, {0, 0})) == TreePrintError::kWorkLimit);
  std::ostringstream broken;
  broken.setstate(std::ios::badbit);
  Check(std::get<TreePrintError>(PrintFunctionTree(function, broken)) == TreePrintError::kWrite);
  const auto fresh = [&]() {
    return std::get<VerifiedFunction>(VerifiedFunction::PrepareClass(
        file, header, index, lookup, roles, sites, range.first, identity));
  };
  ClassMethod shape{{{kMember}, abc::ClassMethodKind::kOrdinary, encoded_length},
      {abc::FunctionKind::kNone}, function.Frame(), abc::ClassPlacement::kPrototype,
      u"run", {}, EffectiveMethodKind::kOrdinary};
  constexpr std::string_view prefix = "class Fixture {\n";
  std::ostringstream composed;
  composed << prefix;
  auto segment = WriteClassMethod(shape, fresh(), composed, GeneratedOffset{prefix.size()});
  Check(std::holds_alternative<FunctionEmission>(segment));
  const auto& emission = std::get<FunctionEmission>(segment);
  Check(emission.Destination().begin == prefix.size());
  Check(emission.Destination().end == composed.view().size());
  Check(composed.view().substr(emission.Destination().begin) == output.view());
  Check(emission.Receipt().stage == SourceStage::kPrinted && emission.Receipt().coverage == Coverage::kComplete);
  Check(!emission.Receipt().legacy_writer_used && !ValidateRouteReceipt(emission.Receipt()));
  Check(emission.Function().Abi().parameters[0].reads.references == 1);
  Check(emission.Positions().parameters == printed.parameters);
  Check(emission.Positions().properties == printed.properties);
  for (const auto span : emission.Positions().expressions) {
    const auto begin = emission.Destination().begin + span.begin;
    const auto end = emission.Destination().begin + span.end;
    Check(begin < end && end <= emission.Destination().end);
  }
  test::CheckFunctionTransport(std::get<FunctionEmission>(std::move(segment)), output.view());
  auto mismatched = shape;
  mismatched.member.target.value++;
  Check(std::get<FunctionEmitError>(WriteClassMethod(mismatched, fresh(), denied, {})) == FunctionEmitError::kCreation);
  mismatched = shape;
  mismatched.frame.parameters++;
  Check(std::get<FunctionEmitError>(WriteClassMethod(mismatched, fresh(), denied, {})) == FunctionEmitError::kFrame);
  mismatched = shape;
  mismatched.key = u"wrong";
  Check(std::get<FunctionEmitError>(WriteClassMethod(mismatched, fresh(), denied, {})) == FunctionEmitError::kName);
  Check(denied.view().empty());
  Check(std::get<TreePrintError>(WriteClassMethod(shape, fresh(), denied,
      GeneratedOffset{std::numeric_limits<std::size_t>::max()})) == TreePrintError::kOutputLimit);
  Check(denied.view().empty());
  auto limited = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity, {0, 0});
  Check(std::get<FunctionPrepareFailure>(limited).kind == FunctionPrepareError::kResourceLimit);
  if (defaults) { Check(unlink(path) == 0); return; }
  index.methods[2].code->arguments = 5;
  context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::holds_alternative<ClassTreeContext>(context));
  auto rejected = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<FunctionPrepareFailure>(rejected).kind == FunctionPrepareError::kParameters);
  index.methods[2].code->arguments = 4;
  index.methods[2].name = "bad";
  context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<ClassContextError>(std::get<ClassContextFailure>(context).error) == ClassContextError::kName);
  Check(unlink(path) == 0);
}
}  // namespace
int main() {
  Run(); Run(DefaultEncoding::kInteger); Run(DefaultEncoding::kNegation);
  Run(DefaultEncoding::kNone, true);
  Run(DefaultEncoding::kNone, false, true);
}
