#include "../../../src/source/class_tree_context.h"
#include "../../../src/source/prepared_family.h"
#include "../../../src/source/family_emission.h"
#include "../../../src/source/phased_method.h"
#include "../../../src/source/module_aliases.h"
#include "../../../src/source/syntax_resolution.h"
#include "abcd/source/verified_function.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/tree_printer.h"
#include "abcd/source/class_method.h"
#include "abcd/source/global_reference.h"
#include "../../support/function_segments.h"
#include "../../../tools/tree_position_json.h"
#include <nlohmann/json.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
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
enum class ExceptionShape {
  kNone, kBranch, kModule, kForwarder, kDeadCopies, kLiveCopies, kReceiver, kReceiverCatch,
  kReceiverArgument, kReceiverPair, kReceiverNumericPair, kReceiverTypeof
};
void Run(DefaultEncoding encoding = DefaultEncoding::kNone, bool global_typeof = false,
         bool object_initializer = false, bool snapshot = false, std::ostream* evidence = nullptr,
         ExceptionShape exception = ExceptionShape::kNone) {
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
  constexpr std::uint32_t kModuleType = 520, kModuleField = 530, kRequest = 548, kModule = 560;
  if (exception == ExceptionShape::kModule) {
    constexpr std::uint8_t kI32 = 5, kAscii = 1;
    bytes[kModuleType] = kI32;
    constexpr std::string_view kModuleFieldName = "moduleRecordIdx", kRequestName = "dep";
    bytes[kModuleField] = (kModuleFieldName.size() << 1) | kAscii;
    std::copy(kModuleFieldName.begin(), kModuleFieldName.end(), bytes.begin() + kModuleField + 1);
    bytes[kRequest] = (kRequestName.size() << 1) | kAscii;
    std::copy(kRequestName.begin(), kRequestName.end(), bytes.begin() + kRequest + 1);
    auto cursor = kModule;
    const auto emit = [&](std::uint32_t value) { word(cursor, value); cursor += sizeof(value); };
    emit(0); emit(1); emit(kRequest);
    emit(1); emit(kName); emit(kName); cursor += sizeof(std::uint16_t);
    emit(0); emit(0); emit(0); emit(0);
  }
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
  if (snapshot) {
    // Replace the final two-byte parameter load, preserving the default join PC.
    member.erase(member.end() - 3, member.end() - 1);
    member.insert(member.end() - 1, static_cast<std::uint8_t>(abc::Opcode::kGETUNMAPPEDARGS));
    --member[kCodeSizeOffset];
  }
  if (exception != ExceptionShape::kNone) {
    const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
    constexpr std::uint8_t kBodyBytes = 15, kProtectedBytes = 14,
        kBranchDelta = 4, kNamePool = 2, kSingleTry = 1, kCatchAll = 0;
    member = {0, 4, kBodyBytes, kSingleTry, kLoad, kParameter,
        op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
        op(abc::Opcode::kCALLARG0_IMM8), 0, op(abc::Opcode::kJEQZ_IMM8), kBranchDelta,
        op(abc::Opcode::kLDTRUE), kReturn, op(abc::Opcode::kLDFALSE), kReturn, kReturn,
        0, kProtectedBytes, kSingleTry, kCatchAll, kProtectedBytes, 1};
    if (exception == ExceptionShape::kDeadCopies) {
      // Unused callable/new-target copies precede the real branch/call/catch.
      // Keep ABI entries real: these cannot be printed as undefined locals.
      constexpr std::uint8_t kLocals = 2, kCallable = 2, kNewTarget = 3,
          kFirstLocal = 0, kSecondLocal = 1, kPrologueBytes = 9;
      member[0] = kLocals;
      member[kHeaderBytes + 1] = kParameter + kLocals;
      member.insert(member.begin() + kHeaderBytes, {
          op(abc::Opcode::kMOV_V8_V8), kFirstLocal, kCallable,
          op(abc::Opcode::kMOV_V8_V8), kSecondLocal, kNewTarget,
          op(abc::Opcode::kMOV_V8_V8), kFirstLocal, kFirstLocal});
      member[kCodeSizeOffset] += kPrologueBytes;
      const auto table = kHeaderBytes + member[kCodeSizeOffset];
      member[table + 1] += kPrologueBytes;
      member[table + 4] += kPrologueBytes;
    }
    if (exception == ExceptionShape::kLiveCopies) {
      constexpr std::uint8_t kLocals = 2, kFirstLocal = 0, kSecondLocal = 1, kPrologueBytes = 6;
      member[0] = kLocals;
      member[kHeaderBytes + 1] = kSecondLocal;
      member.insert(member.begin() + kHeaderBytes, {
          op(abc::Opcode::kMOV_V8_V8), kFirstLocal, kParameter + kLocals,
          op(abc::Opcode::kMOV_V8_V8), kSecondLocal, kFirstLocal});
      member[kCodeSizeOffset] += kPrologueBytes;
      const auto table = kHeaderBytes + member[kCodeSizeOffset];
      member[table + 1] += kPrologueBytes;
      member[table + 4] += kPrologueBytes;
    }
    if (exception == ExceptionShape::kModule) {
      constexpr std::uint8_t kModuleBodyBytes = 8, kModuleProtectedBytes = 7, kModuleSlot = 0;
      constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
      constexpr unsigned kByteBits = 8;
      member = {0, 4, kModuleBodyBytes, kSingleTry,
          op(abc::Opcode::kLDEXTERNALMODULEVAR_IMM8), kModuleSlot,
          op(kGuard), static_cast<std::uint16_t>(kGuard) >> kByteBits, kNamePool, 0,
          kReturn, kReturn, 0, kModuleProtectedBytes, kSingleTry, kCatchAll, kModuleProtectedBytes, 1};
    }
    if (exception == ExceptionShape::kReceiver) {
      constexpr std::uint8_t kLocals = 2, kReceiver = 5, kCallee = 0, kArgument = 1,
          kValue = 7, kReceiverBytes = 28, kReceiverProtected = 27;
      member = {kLocals, 4, kReceiverBytes, kSingleTry,
          kLoad, kReceiver, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
          kStore, kCallee, kInteger, kValue, 0, 0, 0, kStore, kArgument, kLoad, kCallee,
          op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kReceiver, kArgument,
          op(abc::Opcode::kJEQZ_IMM8), kBranchDelta,
          op(abc::Opcode::kLDTRUE), kReturn, op(abc::Opcode::kLDFALSE), kReturn, kReturn,
          0, kReceiverProtected, kSingleTry, kCatchAll, kReceiverProtected, 1};
    }
    if (exception == ExceptionShape::kReceiverCatch) {
      constexpr std::uint8_t kLocals = 2, kReceiver = 5, kCallee = 0, kArgument = 1,
          kReceiverBytes = 26, kReceiverProtected = 23, kHandlerBytes = 3;
      member = {kLocals, 4, kReceiverBytes, kSingleTry,
          op(abc::Opcode::kMOV_V8_V8), kArgument, kReceiver,
          kLoad, kReceiver, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
          kStore, kCallee, kLoad, kCallee,
          op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kReceiver, kArgument,
          op(abc::Opcode::kJEQZ_IMM8), kBranchDelta,
          op(abc::Opcode::kLDTRUE), kReturn, op(abc::Opcode::kLDFALSE), kReturn,
          kLoad, kArgument, kReturn,
          0, kReceiverProtected, kSingleTry, kCatchAll, kReceiverProtected, kHandlerBytes};
    }
    if (exception == ExceptionShape::kReceiverArgument || exception == ExceptionShape::kReceiverPair ||
        exception == ExceptionShape::kReceiverNumericPair || exception == ExceptionShape::kReceiverTypeof) {
      constexpr std::uint8_t kLocals = 2, kReceiver = 5, kCallee = 0, kArgument = 1,
          kBodyBytes = 29, kProtectedBytes = 28;
      member = {kLocals, 4, kBodyBytes, kSingleTry,
          kLoad, kReceiver, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
          kStore, kCallee,
          kLoad, kReceiver, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
          kStore, kArgument, kLoad, kCallee,
          op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8), 0, kReceiver, kArgument,
          op(abc::Opcode::kJEQZ_IMM8), kBranchDelta,
          op(abc::Opcode::kLDTRUE), kReturn, op(abc::Opcode::kLDFALSE), kReturn, kReturn,
          0, kProtectedBytes, kSingleTry, kCatchAll, kProtectedBytes, 1};
      if (exception == ExceptionShape::kReceiverTypeof) {
        constexpr std::size_t kStoreArgumentPc = 14;
        constexpr std::uint8_t kTypeofBytes = 2;
        member.insert(member.begin() + kHeaderBytes + kStoreArgumentPc, {op(abc::Opcode::kTYPEOF_IMM8), 0});
        member[kCodeSizeOffset] += kTypeofBytes;
        const auto table = kHeaderBytes + member[kCodeSizeOffset];
        member[table + 1] += kTypeofBytes;
        member[table + 4] += kTypeofBytes;
      }
      if (exception == ExceptionShape::kReceiverPair || exception == ExceptionShape::kReceiverNumericPair) {
        constexpr std::size_t kArgumentReadyPc = 16;
        constexpr std::uint8_t kSecondArgumentBytes = 11;
        member.insert(member.begin() + kHeaderBytes + kArgumentReadyPc, {
            kLoad, kReceiver, op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
            op(abc::Opcode::kADD2_IMM8_V8), 0, kArgument, kStore, kArgument});
        member[kCodeSizeOffset] += kSecondArgumentBytes;
        const auto table = kHeaderBytes + member[kCodeSizeOffset];
        member[table + 1] += kSecondArgumentBytes;
        member[table + 4] += kSecondArgumentBytes;
        if (exception == ExceptionShape::kReceiverPair) {
          // A string operand proves ADD's normal result is a Language value.
          // Arbitrary numeric ADD still needs the separate NaN-payload proof.
          constexpr std::uint8_t kStringPrefixBytes = 8;
          member.insert(member.begin() + kHeaderBytes + kArgumentReadyPc, {
              op(abc::Opcode::kLDA_STR_ID16), kNamePool, 0,
              op(abc::Opcode::kADD2_IMM8_V8), 0, kArgument, kStore, kArgument});
          member[kCodeSizeOffset] += kStringPrefixBytes;
          const auto updated_table = kHeaderBytes + member[kCodeSizeOffset];
          member[updated_table + 1] += kStringPrefixBytes;
          member[updated_table + 4] += kStringPrefixBytes;
        }
      }
    }
    if (exception == ExceptionShape::kForwarder) {
      constexpr std::uint8_t kForwardBodyBytes = 19, kTryEnd = 14, kHandlerPc = 16,
          kHandlerBytes = 2, kToFalse = 5, kToJoin = 5, kToGap = 2, kToReturn = 4;
      member = {0, 4, kForwardBodyBytes, kSingleTry, kLoad, kParameter,
          op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, kNamePool, 0,
          op(abc::Opcode::kJEQZ_IMM8), kToFalse, op(abc::Opcode::kLDTRUE),
          op(abc::Opcode::kJMP_IMM8), kToJoin, op(abc::Opcode::kLDFALSE),
          op(abc::Opcode::kJMP_IMM8), kToGap, op(abc::Opcode::kJMP_IMM8), kToReturn,
          op(abc::Opcode::kJMP_IMM8), kHandlerBytes, kReturn,
          0, kTryEnd, kSingleTry, kCatchAll, kHandlerPc, kHandlerBytes};
    }
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
                                      member[kCodeSizeOffset], member[3]};
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
  if (exception != ExceptionShape::kNone) {
    const FunctionContext owner{std::get<ClassTreeContext>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, sites, range.first, identity))};
    const auto decoded = std::get<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *index.methods[2].code));
    auto analyzed = PhasedMethod::Build(owner, decoded);
    if (exception == ExceptionShape::kReceiverNumericPair) {
      Check(std::get<PhasedMethodError>(analyzed) == PhasedMethodError::kValues);
      Check(unlink(path) == 0);
      return;
    }
    if (exception == ExceptionShape::kModule) {
      // No import enclosure means no executable module read, even when the
      // guarded pair is valid. Names alone must not open publication.
      Check(std::get<PhasedMethodError>(analyzed) == PhasedMethodError::kEvaluation);
      auto refused = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites,
          range.first, identity, {});
      Check(std::get<FunctionPrepareFailure>(refused).kind == FunctionPrepareError::kEvaluation);
      auto module_index = index;
      module_index.regions[0].classes = {1, kModuleType};
      abc::RecordInfo record; record.offset = {kOwner}; record.field_count = 1;
      constexpr abc::EntityOffset kField{320};
      module_index.fields.push_back({kField, record.offset, {kModuleField}, {}, 0,
          abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
      const auto module_context = std::get<RecordModuleContext>(ReadRecordModuleContext(
          file, header, module_index, record, abc::FilePackaging::kMerged));
      GlobalScope module_scope{};
      auto enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(
          file, header, module_context, identity, {1}));
      module_scope.module_enclosure = &enclosure;
      auto other_identity = identity; other_identity.contract = SourceContract::kLanguageAndHostV2;
      auto other_enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(
          file, header, module_context, other_identity, {1}));
      GlobalScope foreign_scope{}; foreign_scope.module_enclosure = &other_enclosure;
      const auto foreign_result = VerifiedFunction::PrepareClass(file, header, module_index, lookup,
          roles, sites, range.first, identity, {}, &foreign_scope);
      const auto& foreign_failure = std::get<FunctionPrepareFailure>(foreign_result);
      Check(foreign_failure.kind == FunctionPrepareError::kContext && foreign_failure.values &&
          !ValidateRouteReceipt(*foreign_failure.values));
      auto published = VerifiedFunction::PrepareClass(file, header, module_index, lookup, roles, sites,
          range.first, identity, {}, &module_scope);
      Check(std::holds_alternative<VerifiedFunction>(published));
      const auto& verified = std::get<VerifiedFunction>(published);
      std::ostringstream denied;
      Check(std::get<TreePrintError>(PrintFunctionTree(verified, denied)) == TreePrintError::kUnsupportedContext);
      GlobalScope names_only{};
      names_only.import_bindings = enclosure.Names(); names_only.import_names = enclosure.SortedNames();
      Check(std::get<TreePrintError>(PrintFunctionTree(verified, denied, {}, {}, &names_only)) ==
          TreePrintError::kUnsupportedContext);
      GlobalScope shadowed = module_scope; shadowed.class_binding = "run";
      Check(std::get<TreePrintError>(PrintFunctionTree(verified, denied, {}, {}, &shadowed)) ==
          TreePrintError::kUnsupportedContext);
      Check(std::get<FunctionPrepareFailure>(VerifiedFunction::PrepareClass(file, header, module_index,
          lookup, roles, sites, range.first, identity, {}, &shadowed)).kind == FunctionPrepareError::kContext);
      Check(denied.str().empty());
      std::ostringstream output, imports;
      Check(std::holds_alternative<std::size_t>(enclosure.WriteImports(file, header, imports)));
      auto emitted = FunctionEmission::Write(std::get<VerifiedFunction>(std::move(published)), output, {}, {}, {}, &module_scope);
      Check(std::holds_alternative<FunctionEmission>(emitted));
      const auto& emission = std::get<FunctionEmission>(emitted);
      Check(emission.Positions().module_binding && emission.Positions().module_binding->owner == enclosure.Owner());
      Check(emission.Receipt().coverage == Coverage::kComplete && !ValidateRouteReceipt(emission.Receipt()));
      if (evidence) {
        std::ostringstream mapping;
        tools::WriteTreePositions(mapping, emission.Function().Tree(), emission.Positions());
        *evidence << nlohmann::json{{"source", output.str()}, {"imports", imports.str()},
            {"positions", nlohmann::json::parse(mapping.str())}}.dump() << '\n';
      }
      const PhasedModuleBinding binding{file, header, module_index, module_context, {1}};
      const auto bound = PhasedMethod::Build(owner, decoded, {}, &binding);
      Check(std::holds_alternative<PhasedMethod>(bound));
      const auto& method = std::get<PhasedMethod>(bound);
      Check(method.Modules() && method.Modules()->names == std::vector<std::u16string>{u"run"});
      Check(method.Evaluation().modules.size() == 1 &&
          method.Evaluation().modules[0].target == ModuleSymbol{ModuleOwnerId{1}, SymbolId{0}});
      Check(std::holds_alternative<PhasedMethod>(PhasedMethod::Build(owner, decoded,
          {analysis::SemanticBudget{}.bytes, method.Work()}, &binding)));
      Check(std::get<PhasedMethodError>(PhasedMethod::Build(owner, decoded,
          {analysis::SemanticBudget{}.bytes, method.Work() - 1}, &binding)) == PhasedMethodError::kWorkLimit);
      auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(
          file, header, module_index, owner, decoded, method.Graph(), {1}));
      const auto first_name = static_cast<std::uint32_t>(syntax.syntax.names.size());
      syntax.syntax.names.insert(syntax.syntax.names.end(), method.Modules()->names.begin(), method.Modules()->names.end());
      syntax.syntax.modules = method.Modules()->aliases;
      for (auto& alias : syntax.syntax.modules) alias.name.value += first_name;
      const auto constants = syntax.View();
      const auto tree = std::get<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(method.Graph(),
          method.TreeInput(decoded), method.Evaluation(), std::move(syntax.syntax), constants));
      Check(std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(
          *method.Modules(), method.Evaluation(), tree.tree)));
      auto wrong_index = module_index;
      ++wrong_index.methods[owner.Position().value].owner_offset.value;
      const PhasedModuleBinding wrong_binding{file, header, wrong_index, module_context, {1}};
      Check(std::get<PhasedMethodError>(PhasedMethod::Build(owner, decoded, {}, &wrong_binding)) ==
          PhasedMethodError::kModules);
      Check(unlink(path) == 0);
      return;
    }
    Check(std::holds_alternative<PhasedMethod>(analyzed));
    const auto moved = std::get<PhasedMethod>(std::move(analyzed));
    if (exception == ExceptionShape::kLiveCopies) {
      constexpr std::size_t kParameterCopies = 3;
      for (std::size_t i = 0; i < kParameterCopies; ++i)
        Check(!moved.Evaluation().operations[i].temporary);
    }
    const auto view = moved.TreeInput(decoded);
    Check(std::holds_alternative<std::uint64_t>(VerifyExceptionControl(decoded, moved.Graph(),
        view.input, view.regions, view.control)));
    Check(std::get<PhasedMethodError>(PhasedMethod::Build(owner, decoded, {0})) ==
        PhasedMethodError::kResourceLimit);
    Check(std::holds_alternative<PhasedMethod>(PhasedMethod::Build(owner, decoded,
        {analysis::SemanticBudget{}.bytes, moved.Work()})));
    Check(std::get<PhasedMethodError>(PhasedMethod::Build(owner, decoded,
        {analysis::SemanticBudget{}.bytes, moved.Work() - 1})) == PhasedMethodError::kWorkLimit);
    auto old_contract = identity; old_contract.contract = SourceContract::kLanguageAndHostV1;
    const FunctionContext old_owner{std::get<ClassTreeContext>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, sites, range.first, old_contract))};
    Check(std::get<PhasedMethodError>(PhasedMethod::Build(old_owner, decoded)) == PhasedMethodError::kContract);
    const auto contract_refusal = VerifiedFunction::PrepareClass(
        file, header, index, lookup, roles, sites, range.first, old_contract);
    const auto& rejected_contract = std::get<FunctionPrepareFailure>(contract_refusal);
    Check(rejected_contract.values && rejected_contract.values->failure &&
        !ValidateRouteReceipt(*rejected_contract.values));
    auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup, roles, sites, range.first, identity);
    if (const auto* error = std::get_if<FunctionPrepareFailure>(&prepared))
      std::cerr << "exception preparation error " << static_cast<unsigned>(error->kind) << '\n';
    Check(std::holds_alternative<VerifiedFunction>(prepared));
    std::ostringstream output;
    auto written = FunctionEmission::Write(std::get<VerifiedFunction>(std::move(prepared)), output, {});
    Check(std::holds_alternative<FunctionEmission>(written));
    const auto& emission = std::get<FunctionEmission>(written);
    Check(emission.Function().Abi().parameters.size() == 1 && emission.Function().Abi().length == 1);
    auto mismatched = index;
    mismatched.methods[2].code->try_count = 0;
    Check(std::get<FunctionPrepareFailure>(VerifiedFunction::PrepareClass(
        file, header, mismatched, lookup, roles, sites, range.first, identity)).kind == FunctionPrepareError::kBody);
    Check(emission.Receipt().coverage == Coverage::kComplete &&
        emission.Receipt().stage == SourceStage::kPrinted && !ValidateRouteReceipt(emission.Receipt()));
    if (evidence) {
      std::ostringstream mapping;
      tools::WriteTreePositions(mapping, emission.Function().Tree(), emission.Positions());
      *evidence << nlohmann::json{{"source", output.str()}, {"positions", nlohmann::json::parse(mapping.str())}}.dump() << '\n';
    }
    Check(unlink(path) == 0);
    return;
  }
  if (snapshot) {
    const HostArgumentSnapshotBinding factory{u"snapshotArguments", ArgumentSnapshotAbi::kV7CurrentSlots};
    GlobalScope globals{}; globals.argument_snapshot_binding = &factory;
    const FamilyDestination destination{Strictness::kStrict, &globals, FunctionFragment::kClassMember};
    auto prepared = PreparedFamily::PrepareClass(file, header, index, lookup, roles,
        functions, sites, range.first, identity, destination);
    Check(std::holds_alternative<PreparedFamily>(prepared));
    const auto& ready = std::get<PreparedFamily>(prepared);
    const auto& root = ready.Family().members.front();
    Check(root.tree.parameters.size() == 1 && root.tree.parameters[0].initializer.has_value() == defaults);
    Check(root.abi && root.abi->length == encoded_length && root.sources);
    std::ostringstream denied;
    Check(std::holds_alternative<FamilyPrepareFailure>(ready.PrintDraft(
        {Strictness::kStrict, nullptr, FunctionFragment::kClassMember}, denied)) && denied.str().empty());
    std::ostringstream output;
    const auto printed = ready.PrintDraft(destination, output);
    Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    const auto& positions = std::get<PrintedFamilyDraft>(printed).members.front();
    Check(positions.argument_snapshot_binding && positions.argument_snapshot_binding->name == factory.name);
    if (evidence) {
      std::ostringstream mapping;
      tools::WriteTreePositions(mapping, root.tree, positions, &*root.sources);
      auto mapped = nlohmann::json::parse(mapping.str());
      auto snapshots = nlohmann::json::array();
      const auto decoded = std::get<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *index.methods[2].code));
      for (std::size_t i = 0; i < decoded.instructions.size(); ++i)
        if (decoded.instructions[i].decoded.opcode == abc::Opcode::kGETUNMAPPEDARGS)
          snapshots.push_back({{"instruction", i}, {"pc", decoded.instructions[i].offset.value}});
      Check(snapshots.size() == 1);
      *evidence << nlohmann::json{{"source", output.str()}, {"parameters", 1},
          {"method", true}, {"default_parameter", defaults}, {"writes_parameter", false},
          {"branch_parameter", false}, {"loop_snapshots", false}, {"nested_snapshot", false},
          {"snapshots", snapshots}, {"positions", std::move(mapped)}} << '\n';
    }
    std::ostringstream sealed;
    auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)), destination, sealed, {});
    Check(std::holds_alternative<FamilyEmission>(emitted) && sealed.str() == output.str());
    const auto& receipt = std::get<FamilyEmission>(emitted).Receipts().front();
    Check(receipt.coverage == Coverage::kComplete && !ValidateRouteReceipt(receipt));
    Check(unlink(path) == 0);
    return;
  }
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
  test::CheckFunctionTransport(std::get<FamilyEmission>(std::move(family_emission)), emitted_output.str());
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
  // Metadata no longer matches the encoded Code header; reject before ABI proof.
  Check(std::get<FunctionPrepareFailure>(rejected).kind == FunctionPrepareError::kBody);
  index.methods[2].code->arguments = 4;
  index.methods[2].name = "bad";
  context = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, range.first, identity);
  Check(std::get<ClassContextError>(std::get<ClassContextFailure>(context).error) == ClassContextError::kName);
  Check(unlink(path) == 0);
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--module-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kModule);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--receiver-typeof-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kReceiverTypeof);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--receiver-pair-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kReceiverPair);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--receiver-argument-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kReceiverArgument);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--receiver-catch-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kReceiverCatch);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--receiver-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kReceiver);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--live-copies-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kLiveCopies);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--dead-copies-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kDeadCopies);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--forwarder-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kForwarder);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--exception-json") {
    Run(DefaultEncoding::kNone, false, false, false, &std::cout, ExceptionShape::kBranch);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--snapshot-json") {
    for (const auto encoding : {DefaultEncoding::kNone, DefaultEncoding::kInteger, DefaultEncoding::kNegation})
      Run(encoding, false, false, true, &std::cout);
    return 0;
  }
  Check(argc == 1);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiverTypeof);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiverNumericPair);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiverPair);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiverArgument);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiverCatch);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kReceiver);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kLiveCopies);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kDeadCopies);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kModule);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kForwarder);
  Run(DefaultEncoding::kNone, false, false, false, nullptr, ExceptionShape::kBranch);
  Run(); Run(DefaultEncoding::kInteger); Run(DefaultEncoding::kNegation);
  Run(DefaultEncoding::kNone, true);
  Run(DefaultEncoding::kNone, false, true);
}
