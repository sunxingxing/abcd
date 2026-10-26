#include "../../src/source/family_emission.h"
#include "../../tools/tree_position_json.h"
#include "analysis/ssa_fixture.h"
#include "source/input_actions.h"

#include <unistd.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <string_view>

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
    ExpectedSource expected = ExpectedSource::kPrinted, bool debug = false,
    bool overwrite_abi = false,
    abc::FunctionKind kind = abc::FunctionKind::kBaseConstructor,
    std::string_view name = "constant", bool global_lookup = false,
    const HostRealmBinding* realm_binding = nullptr,
    const HostSymbolBinding* symbol_binding = nullptr, bool throws_value = false) {
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  if (global_lookup) {
    constexpr std::uint32_t kGlobalName = 80;
    constexpr std::uint8_t kAsciiFlag = 1, kLengthShift = 1;
    for (unsigned i = 0; i < sizeof(kGlobalName); ++i)
      bytes[kPool + sizeof(kTarget) + i] = static_cast<std::uint8_t>(kGlobalName >> (kByteBits * i));
    bytes[kGlobalName] = static_cast<std::uint8_t>((name.size() << kLengthShift) | kAsciiFlag);
    std::copy(name.begin(), name.end(), bytes.begin() + kGlobalName + 1);
  }
  const std::uint8_t parameters = callback ? 1 : 0;
  const auto caller = Append(bytes,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), kCache, 0, 0, parameters,
       Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), kCache, 0, 0, parameters,
       Op(abc::Opcode::kRETURNUNDEFINED)}, 0, 0);
  std::vector<std::uint8_t> code{Op(constant)};
  if (overwrite_abi) {
    constexpr std::uint8_t kReceiverArgument = kImplicitArguments - 1;
    constexpr std::uint8_t kFunctionArgument = 0;
    const auto argument = constant == abc::Opcode::kLDFUNCTION ? kFunctionArgument : kReceiverArgument;
    const auto slot = static_cast<std::uint8_t>((callback ? kLocals : 0) + argument);
    code = {Op(abc::Opcode::kLDNULL), Op(abc::Opcode::kSTA_V8), slot, Op(constant)};
  }
  if (global_lookup)
    code.insert(code.begin(), {Op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), kCache, 1, 0});
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
  if (throws_value) {
    // Pinned THROW_PREF_NONE is the throw prefix followed by subopcode zero.
    constexpr std::uint8_t kThrowPrefix = 0xfe, kThrowValue = 0x00;
    code.insert(code.end(), {kThrowPrefix, kThrowValue});
  } else code.push_back(Op(abc::Opcode::kRETURN));
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
  index.regions.push_back({kCaller, kCodeStart, {}, {global_lookup ? 2U : 1U, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  // The pinned name parser maps a present mangled "#" to an empty runtime
  // name. An absent metadata name remains unproven and is not this case.
  index.methods[1].name = name.empty() ? "#" : name;
  index.methods[1].name_utf16_length = index.methods[1].name.size();
  index.methods[1].access_flags =
      static_cast<std::uint32_t>(kind) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(
      std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  GlobalScope globals{};
  globals.realm_binding = realm_binding;
  globals.symbol_binding = symbol_binding;
  const FamilyDestination destination{Strictness::kStrict,
      global_lookup || constant == abc::Opcode::kLDGLOBAL || constant == abc::Opcode::kLDSYMBOL ? &globals : nullptr};
  if (expected == ExpectedSource::kPrinted) {
    const auto range = sites.Find({kTarget});
    test::Check(range.count == 2);
    FunctionContext first{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, range.first, identity))};
    FunctionContext second{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, {range.first.value + 1}, identity))};
    auto read = ReadFunctionValues(file, header, index, first.Position(), identity,
        InputCaptureMode::kConditional, {}, nullptr, &first);
    test::Check(std::holds_alternative<FileFunction>(read));
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
    // Internal markers cannot escape to JS. A raw arrow LDTHIS also lacks the
    // distinct lexical-owner proof needed to replace it by a source this.
    test::Check(std::holds_alternative<FamilyPrepareFailure>(prepared));
    test::Check(unlink(path) == 0);
    return;
  }
  test::Check(std::holds_alternative<PreparedFamily>(prepared));
  if (constant == abc::Opcode::kLDGLOBAL || constant == abc::Opcode::kLDSYMBOL) {
    const auto& family = std::get<PreparedFamily>(prepared);
    test::Check(std::holds_alternative<FamilyPrepareFailure>(
        family.CheckDestination({Strictness::kStrict, nullptr})));
    auto shadowed = globals;
    const std::array<std::string_view, 1> names{symbol_binding ? "hostSymbol" : realm_binding ? "hostRealm" : "globalThis"};
    shadowed.parameter_bindings = names;
    test::Check(std::holds_alternative<FamilyPrepareFailure>(
        family.CheckDestination({Strictness::kStrict, &shadowed})));
    std::ostringstream rejected;
    test::Check(std::holds_alternative<FamilyPrepareFailure>(
        family.PrintDraft({Strictness::kStrict, &shadowed}, rejected)));
    test::Check(rejected.str().empty());
    if (constant == abc::Opcode::kLDSYMBOL) {
      const GlobalScope no_intrinsic{};
      test::Check(std::holds_alternative<FamilyPrepareFailure>(
          family.PrintDraft({Strictness::kStrict, &no_intrinsic}, rejected)));
      test::Check(rejected.str().empty());
    }
    test::Check(std::holds_alternative<TreePrintFailure>(PrintFamilyDraft(family.Family(), rejected)));
    test::Check(rejected.str().empty());
  }
  std::ostringstream output;
  auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)),
      destination, output, {});
  test::Check(std::holds_alternative<FamilyEmission>(emitted));
  const auto& emission = std::get<FamilyEmission>(emitted);
  test::Check(emission.Receipts().size() == 1);
  const auto& receipt = emission.Receipts().front();
  test::Check(receipt.stage == SourceStage::kPrinted && receipt.coverage == Coverage::kComplete);
  const auto& member = emission.Family().Family().members.front();
  test::Check(member.flow.original!=nullptr);
  test::CheckInputActions(member.tree,*member.flow.original);
  test::Check(member.sources && !member.sources->occurrences.empty());
  const auto& mapping = member.sources->occurrences[(overwrite_abi ? 2 : 0) + (global_lookup ? 1 : 0)];
  if (std::holds_alternative<ElidedInstruction>(mapping.target)) {
    // Primitive copy propagation leaves a source elision; execution below still
    // checks the consumer's value. Intrinsic/ABI reads must retain real nodes.
    test::Check(constant != abc::Opcode::kLDGLOBAL && constant != abc::Opcode::kLDSYMBOL &&
        constant != abc::Opcode::kLDFUNCTION);
    const auto& origin = std::get<ElidedOrigin>(member.tree.origins[mapping.origin.value]);
    test::Check(origin.pc == abc::CodeOffset{(overwrite_abi ? 3U : 0U) + (global_lookup ? 4U : 0U)});
  } else {
    const auto& origin = std::get<OriginalSite>(member.tree.origins[mapping.origin.value]);
    test::Check(origin.pc == abc::CodeOffset{(overwrite_abi ? 3U : 0U) + (global_lookup ? 4U : 0U)});
    const auto expression = std::get<ExprId>(mapping.target);
    const auto span = emission.Positions().members.front().expressions[expression.value];
    test::Check(span.begin < span.end && span.end <= output.view().size());
    if (constant == abc::Opcode::kLDSYMBOL) {
      test::Check(std::holds_alternative<SymbolConstructorExpr>(member.tree.expressions[expression.value].expression));
      const auto& binding = emission.Positions().members.front().symbol_binding;
      test::Check(symbol_binding && binding && binding->name == symbol_binding->name);
      test::Check(output.view().substr(span.begin, span.end - span.begin) == "hostSymbol");
      std::ostringstream serialized;
      tools::WriteTreePositions(serialized, member.tree, emission.Positions().members.front(), &*member.sources);
      const auto json = nlohmann::json::parse(serialized.str());
      test::Check(json.at("symbol_binding").at("name") == "hostSymbol");
      test::Check(json.at("symbol_binding").at("policy") == "explicit_host");
      test::Check(json.at("symbol_binding").at("contract") == SourceContractName(member.tree.identity.contract));
    }
    if (constant == abc::Opcode::kLDGLOBAL) {
      test::Check(std::holds_alternative<RealmObjectExpr>(member.tree.expressions[expression.value].expression));
      const auto& binding = emission.Positions().members.front().realm_binding;
      test::Check(binding && binding->origin == (realm_binding ?
          RealmBindingOrigin::kExplicitHost : RealmBindingOrigin::kStableGlobalThis));
      test::Check(binding->name == (realm_binding ? realm_binding->name : u"globalThis"));
      test::Check(output.view().substr(span.begin, span.end - span.begin) ==
          std::string(binding->name.begin(), binding->name.end()));
      std::ostringstream serialized;
      tools::WriteTreePositions(serialized, member.tree, emission.Positions().members.front(), &*member.sources);
      const auto json = nlohmann::json::parse(serialized.str());
      test::Check(json.at("realm_binding").at("name") == (realm_binding ? "hostRealm" : "globalThis"));
      test::Check(json.at("realm_binding").at("policy") == (realm_binding ? "explicit_host" : "stable_global_this"));
      test::Check(json.at("realm_binding").at("contract") == SourceContractName(member.tree.identity.contract));
    }
    if (constant == abc::Opcode::kLDFUNCTION) {
      test::Check(member.tree.self.has_value());
      test::Check(std::get<SymbolExpr>(member.tree.expressions[expression.value].expression).symbol == member.tree.self);
      const auto& self_name = member.tree.names[member.tree.symbols[member.tree.self->value].name.value];
      test::Check(output.view().substr(span.begin, span.end - span.begin) ==
          std::string(self_name.begin(), self_name.end()));
      test::Check(member.abi && member.abi->callable == AbiObservation::kFrameIdentity &&
          member.abi->frame_function_reads == 1);
    }
  }
  if (debug) {
    const std::array expected_pc{abc::CodeOffset{1},
        abc::CodeOffset{static_cast<std::uint32_t>(code.size() - 2)}};
    std::size_t count = 0;
    for (const auto& site : member.sources->occurrences) {
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
  if (throws_value) {
    const abc::CodeOffset throw_pc{static_cast<std::uint32_t>(code.size() - 2)};
    std::size_t throws = 0;
    for (const auto& site : member.sources->occurrences) {
      const auto* statement = std::get_if<StmtId>(&site.target);
      if (!statement || !std::holds_alternative<ThrowStmt>(member.tree.statements[statement->value].statement))
        continue;
      ++throws;
      test::Check(std::get<OriginalSite>(member.tree.origins[site.origin.value]).pc == throw_pc);
      const auto range = emission.Positions().members.front().statements[statement->value];
      test::Check(range.begin < range.end && range.end <= output.view().size());
      const auto text = output.view().substr(range.begin, range.end - range.begin);
      test::Check(text.starts_with("throw ") && text.ends_with(";"));
    }
    test::Check(throws == 1);
  }
  std::cout << output.str() << ",\n";
  test::Check(unlink(path) == 0);
}
}  // namespace
int main() {
  std::cout << "export const thrownValues = [\n";
  for (const auto profile : kProfiles)
    for (const auto constant : kConstants)
      for (const bool callback : {false, true})
        Emit(profile, constant, callback, ExpectedSource::kPrinted, false, false,
            abc::FunctionKind::kBaseConstructor, "throwValue", false, nullptr, nullptr, true);
  std::cout << "];\nexport const thrownReceivers = [\n";
  for (const auto profile : kProfiles)
    Emit(profile, abc::Opcode::kLDTHIS, false, ExpectedSource::kPrinted, false, false,
        abc::FunctionKind::kBaseConstructor, "throwReceiver", false, nullptr, nullptr, true);
  std::cout << "];\n";
  std::cout << "const hostSymbol = Symbol;\nexport const symbols = [\n";
  const HostSymbolBinding symbol{u"hostSymbol"};
  for (const auto profile : kProfiles)
    for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
      for (const bool callback : {false, true})
        Emit(profile, abc::Opcode::kLDSYMBOL, callback, ExpectedSource::kPrinted,
            false, false, kind, "constant", false, nullptr, &symbol);
  std::cout << "];\n";
  std::cout << "const hostRealm = globalThis;\n";
  std::cout << "export const realms = [\n";
  for (const auto profile : kProfiles)
    for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
      for (const bool callback : {false, true})
        Emit(profile, abc::Opcode::kLDGLOBAL, callback, ExpectedSource::kPrinted, false, false, kind);
  std::cout << "];\n";
  std::cout << "export const hostRealms = [\n";
  const HostRealmBinding host{u"hostRealm"};
  for (const auto profile : kProfiles)
    for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
      for (const bool callback : {false, true})
        Emit(profile, abc::Opcode::kLDGLOBAL, callback, ExpectedSource::kPrinted,
            false, false, kind, "constant", false, &host);
  std::cout << "];\n";
  std::cout << "export const cases = [\n";
  for (const auto profile : kProfiles) {
    for (const auto constant : kConstants)
      for (const bool callback : {false, true}) Emit(profile, constant, callback);
    for (const bool callback : {false, true})
      Emit(profile, abc::Opcode::kLDHOLE, callback, ExpectedSource::kRejected);
    // Neither V2's realm policy nor a mutable Symbol property proves the
    // interpreter's original intrinsic constructor binding.
    for (const bool callback : {false, true})
      Emit(profile, abc::Opcode::kLDSYMBOL, callback, ExpectedSource::kRejected);
  }
  std::cout << "];\n";
  std::cout << "export const debugCases = [\n";
  for (const auto profile : kProfiles)
    for (const auto constant : kConstants)
      for (const bool callback : {false, true})
        Emit(profile, constant, callback, ExpectedSource::kPrinted, true);
  std::cout << "];\n";
  std::cout << "export const receivers = [\n";
  for (const auto profile : kProfiles)
    for (const bool callback : {false, true})
      for (const bool overwritten : {false, true})
        Emit(profile, abc::Opcode::kLDTHIS, callback, ExpectedSource::kPrinted, false, overwritten);
  std::cout << "];\n";
  for (const auto profile : kProfiles)
    Emit(profile, abc::Opcode::kLDTHIS, false, ExpectedSource::kRejected,
        false, false, abc::FunctionKind::kArrow);
  std::cout << "export const functions = [\n";
  for (const auto profile : kProfiles)
    for (const bool callback : {false, true})
      for (const bool overwritten : {false, true})
        Emit(profile, abc::Opcode::kLDFUNCTION, callback, ExpectedSource::kPrinted, false, overwritten);
  std::cout << "];\n";
  std::cout << "export const namedFunctions = [\n";
  for (const std::string_view name : {"$arg0", "__proto__", "MethodSelf1", "ClassValue"})
    Emit(kProfiles.front(), abc::Opcode::kLDFUNCTION, true, ExpectedSource::kPrinted,
        false, false, abc::FunctionKind::kBaseConstructor, name);
  std::cout << "];\n";
  std::cout << "export const createdFunctions = [\n";
  for (const auto profile : kProfiles)
    for (const std::string_view name : {"", "eval", "with space", "$self0", "__proto__"})
      for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
        for (const bool callback : {false, true})
          Emit(profile, abc::Opcode::kLDFUNCTION, callback, ExpectedSource::kPrinted,
              false, true, kind, name);
  std::cout << "];\n";
  std::cout << "export const globalFunctions = [\n";
  for (const auto profile : kProfiles)
    for (const auto kind : {abc::FunctionKind::kBaseConstructor, abc::FunctionKind::kArrow})
      for (const bool callback : {false, true})
        Emit(profile, abc::Opcode::kLDFUNCTION, callback, ExpectedSource::kPrinted,
            false, true, kind, "constant", true);
  std::cout << "];\n";
}
