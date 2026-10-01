#include "../../../src/source/derived_value_states.h"
#include "../../../src/source/prepared_family.h"
#include "../../../src/source/family_emission.h"
#include "abcd/source/capture_preservation.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <memory>
namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
void Check(bool value, const std::source_location& at = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
enum class Child { kReadOnly, kWrite, kEscape };
enum class CellBranch { kNone, kReceiver, kMixed };
std::uint64_t Run(abc::ProfileId profile, std::size_t copies, Child child, bool print,
    CellBranch branch = CellBranch::kNone) {
  // Pinned dynamic ISA fixture: actual derived class occurrence and closure,
  // receiver saved into local cell after super, read again after DEFINEFUNC.
  constexpr std::uint32_t kPool = 64, kOwner = 80, kLiteral = 128,
      kCaller = 256, kConstructor = 300, kChild = 350, kCallerCode = 400,
      kName = 500, kConstructorCode = 600;
  constexpr std::uint8_t kLoad = 0x60, kStore = 0x61, kReturn = 0x64,
      kUndefined = 0x00, kLexLoad = 0x3c, kLexStore = 0x3d, kDefine = 0x33,
      kSuper = 0x32, kCheckPrefix = 0xfe, kSuperCheck = 7;
  constexpr std::uint8_t kReceiver = 6, kArgument = 7, kSavedReceiver = 1, kResult = 0;
  std::vector<std::uint8_t> constructor{
      static_cast<std::uint8_t>(abc::Opcode::kNEWLEXENV_IMM8), 1,
      static_cast<std::uint8_t>(abc::Opcode::kMOV_V8_V8), kSavedReceiver, kReceiver,
      kSuper, 0, 1, kArgument, kStore, kResult,
      kLoad, kSavedReceiver, kCheckPrefix, kSuperCheck, 1,
      kLoad, kResult, kStore, kReceiver, kLoad, kReceiver, kLexStore, 0};
  if (branch != CellBranch::kNone) {
    std::vector<std::uint8_t> alternate = branch == CellBranch::kMixed ?
        std::vector<std::uint8_t>{kUndefined} : std::vector<std::uint8_t>{kLoad, kReceiver};
    alternate.insert(alternate.end(), {kLexStore, 0});
    constexpr std::uint8_t kToAlternate = 8; // branch + receiver load/store + jump
    const auto to_join = static_cast<std::uint8_t>(2 + alternate.size());
    constructor.insert(constructor.end(), {kLoad, kArgument,
        static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM8), kToAlternate,
        kLoad, kReceiver, kLexStore, 0,
        static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), to_join});
    constructor.insert(constructor.end(), alternate.begin(), alternate.end());
  }
  constructor.insert(constructor.end(), {kDefine, 0, 2, 0, 0,
      static_cast<std::uint8_t>(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 3, 0, kReceiver});
  for (std::size_t i = 0; i < copies; ++i) constructor.insert(constructor.end(), {kLexLoad, 0, kStore, 3});
  constructor.insert(constructor.end(), {kLexLoad, 0, kCheckPrefix, kSuperCheck, 0, kReturn});
  std::vector<std::uint8_t> descendant{kLexLoad, 0, kReturn};
  if (child == Child::kWrite) descendant.insert(descendant.begin(), {kUndefined, kLexStore, 0});
  if (child == Child::kEscape) descendant.insert(descendant.begin(), {0xfc, 0x00});
  const std::vector<std::uint8_t> caller{kLoad, 4, kStore, 0, 0x35, 0, 0, 0, 1, 0, 1, 0, 0, kReturn};
  const auto child_code = kConstructorCode + static_cast<std::uint32_t>(constructor.size()) + 32;
  std::vector<std::uint8_t> bytes(child_code + descendant.size() + 32);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = value >> (8 * i);
  };
  word(kPool, kConstructor); word(kPool + 4, kLiteral); word(kPool + 8, kChild); word(kPool + 12, kName);
  word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kLiteral + 5, 0);
  constexpr std::string_view kMemberName = "callback";
  bytes[kName] = (kMemberName.size() << 1) | 1;
  std::copy(kMemberName.begin(), kMemberName.end(), bytes.begin() + kName + 1);
  const auto write_body = [&](std::uint32_t at, std::uint32_t locals, std::uint32_t arguments,
      const std::vector<std::uint8_t>& code) {
    auto cursor = at;
    const auto uleb = [&](std::uint32_t value) {
      do { auto byte = value & 0x7f; value >>= 7; bytes[cursor++] = byte | (value ? 0x80 : 0); } while (value);
    };
    uleb(locals); uleb(arguments); uleb(code.size()); uleb(0);
    const auto start = cursor; std::copy(code.begin(), code.end(), bytes.begin() + start);
    return abc::CodeInfo{{at}, {start}, locals, arguments, static_cast<std::uint32_t>(code.size()), 0};
  };
  const auto caller_info = write_body(kCallerCode, 1, 4, caller);
  const auto constructor_info = write_body(kConstructorCode, 4, 4, constructor);
  const auto child_info = write_body(child_code, 0, 3, descendant);
  char path[] = "/tmp/abcd-derived-cells-XXXXXX"; const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  const auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = profile;
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0};
  header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index; index.regions.push_back({kCaller, kCallerCode, {}, {4, kPool}, {}, {}});
  index.methods.resize(3);
  for (auto& method : index.methods) method.owner_offset = {kOwner};
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller_info;
  index.methods[1].offset = {kConstructor}; index.methods[1].code = constructor_info;
  index.methods[1].name = "Fixture"; index.methods[1].name_utf16_length = 7;
  index.methods[2].offset = {kChild}; index.methods[2].code = child_info;
  index.methods[2].name = "#*#"; index.methods[2].name_utf16_length = 3;
  constexpr auto kKindShift = 8;
  index.methods[2].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kArrow) << kKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  auto loaded_parent = ReadFunctionValues(file, header, index, {0}, identity, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(loaded_parent));
  const auto& parent = std::get<FileFunction>(loaded_parent);
  Check(parent.values.classes.classes.size() == 1); const auto creation = parent.values.classes.classes.front().operation;
  auto resolved = ClassTreeContext::ResolveConstructor(file, header, index, lookup, roles, parent, creation, identity);
  Check(std::holds_alternative<ClassTreeContext>(resolved));
  FunctionContext context{std::get<ClassTreeContext>(std::move(resolved))};
  auto loaded = ReadFunctionValues(file, header, index, {1}, identity, InputCaptureMode::kConditional, {}, nullptr, &context);
  Check(std::holds_alternative<FileFunction>(loaded)); auto& function = std::get<FileFunction>(loaded);
  const DerivedCaptureInput captures{file, header, index, lookup};
  const auto projected = ProjectDerivedConstructor(function, context, {}, &captures);
  if (child != Child::kReadOnly || branch == CellBranch::kMixed) {
    Check(std::get<DerivedProjectionError>(projected) == DerivedProjectionError::kProtocol);
    Check(!file.CheckUnchanged()); Check(unlink(path) == 0); return 0;
  }
  if (const auto* error = std::get_if<DerivedProjectionError>(&projected)) std::cerr << "projection error " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<DerivedProjection>(projected));
  const auto& protocol = std::get<DerivedProjection>(projected);
  Check(!VerifyDerivedProjection(function, context, protocol, {}, &captures));
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) == DerivedProjectionError::kProtocol);
  MethodConstants constants(file, header, index, context.Target(), {});
  const auto preserved = std::get<CapturePreservations>(ProveCapturePreservation(function.body, constants, &lookup, {}));
  const auto classified = BuildDerivedValueStates(function, protocol.call, preserved.sites, {});
  Check(std::holds_alternative<DerivedValueStates>(classified));
  const auto work = std::get<DerivedValueStates>(classified).work;
  Check(std::holds_alternative<DerivedValueStates>(BuildDerivedValueStates(function, protocol.call,
      preserved.sites, {64 * 1024 * 1024, work})));
  Check(std::get<DerivedProjectionError>(BuildDerivedValueStates(function, protocol.call,
      preserved.sites, {64 * 1024 * 1024, work - 1})) == DerivedProjectionError::kWorkLimit);
  if (copies == 1) for (const std::size_t unused : {200U, 201U, 400U, 800U}) {
    auto unrelated = index; unrelated.methods.resize(index.methods.size() + unused);
    for (std::size_t i = index.methods.size(); i < unrelated.methods.size(); ++i)
      unrelated.methods[i].offset = {static_cast<std::uint32_t>(10000 + i)};
    const auto large_lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(unrelated.methods));
    MethodConstants large_constants(file, header, unrelated, context.Target(), {});
    const auto sparse = std::get<CapturePreservations>(ProveCapturePreservation(function.body,
        large_constants, &large_lookup, {}));
    Check(sparse.sites.size() == preserved.sites.size());
    // Only logarithmic method-target lookups change; no ambient-file census.
    Check(sparse.work_units < preserved.work_units * 2);
  }

  auto& graph = function.values.graph;
  auto load = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) { return std::holds_alternative<ValueLexicalLoad>(op.operation); });
  Check(load != graph.ops.end()); auto& address = std::get<ValueLexicalLoad>(load->operation).address;
  ++address.slot.value;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &captures)) == DerivedProjectionError::kOperand);
  --address.slot.value;
  auto store = std::find_if(graph.ops.begin(), graph.ops.end(), [](const auto& op) { return std::holds_alternative<ValueLexicalStore>(op.operation); });
  Check(store != graph.ops.end()); auto& operand = graph.uses[store->first_use.value].operand;
  const auto original = operand; operand = SpecialValue::kUndefined;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &captures)) == DerivedProjectionError::kOperand);
  operand = original;
  // A later cell read cannot retroactively initialize the store that defines
  // that cell. Reject working-graph feedback before solving receiver origins.
  Check(load->result.has_value()); operand = *load->result;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &captures)) == DerivedProjectionError::kOperand);
  operand = original;
  auto& stored_address = std::get<ValueLexicalStore>(store->operation).address;
  ++stored_address.slot.value;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &captures)) == DerivedProjectionError::kOperand);
  --stored_address.slot.value;
  const auto saved_origin = graph.origins[load->origin.value].pc;
  ++graph.origins[load->origin.value].pc.value;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &captures)) == DerivedProjectionError::kOrigin);
  graph.origins[load->origin.value].pc = saved_origin;
  auto wrong_header = header; ++wrong_header.version.bytes[0];
  const DerivedCaptureInput wrong_input{file, wrong_header, index, lookup};
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {}, &wrong_input)) == DerivedProjectionError::kOwner);

  auto missing = protocol; missing.guards.pop_back();
  Check(VerifyDerivedProjection(function, context, missing, {}, &captures) == DerivedProjectionError::kCoverage);
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {0}, &captures)) == DerivedProjectionError::kResourceLimit);
  Check(std::holds_alternative<DerivedProjectionError>(ProjectDerivedConstructor(function, context, {64 * 1024 * 1024, 0}, &captures)));
  const auto functions = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  auto prepared = PreparedFamily::PrepareConstructor(file, header, index, lookup, roles, functions, parent, creation,
      identity, {Strictness::kStrict, nullptr, FunctionFragment::kClassMember});
  if (const auto* error = std::get_if<FamilyPrepareFailure>(&prepared)) std::cerr << "family failure " << error->cause.index() << '\n';
  Check(std::holds_alternative<PreparedFamily>(prepared));
  std::ostringstream output;
  const auto emitted = FamilyEmission::Write(std::get<PreparedFamily>(std::move(prepared)),
      {Strictness::kStrict, nullptr, FunctionFragment::kClassMember}, output, {});
  Check(std::holds_alternative<FamilyEmission>(emitted));
  if (print) std::cout << nlohmann::json{{"source", output.str()}}.dump() << '\n';
  Check(!file.CheckUnchanged()); Check(unlink(path) == 0); return work;
}
}
int main(int argc, char** argv) {
  Check(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--source"));
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    if (argc == 2) { Run(profile, 1, Child::kReadOnly, true); continue; }
    std::uint64_t previous = 0;
    for (const auto count : {1U, 200U, 201U, 400U, 800U}) {
      const auto work = Run(profile, count, Child::kReadOnly, false);
      if (count > 201) Check(work < previous * 3); previous = work;
    }
    Run(profile, 1, Child::kWrite, false); Run(profile, 1, Child::kEscape, false);
    Run(profile, 1, Child::kReadOnly, false, CellBranch::kReceiver);
    Run(profile, 1, Child::kReadOnly, false, CellBranch::kMixed);
  }
}
