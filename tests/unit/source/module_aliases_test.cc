#include "../../../src/source/module_aliases.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include "abcd/corpus/abc_file.h"

#include <unistd.h>

#include <algorithm>
#include <limits>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr abc::EntityOffset kFirst{96}, kEqual{112}, kOther{128};
constexpr abc::EntityOffset kLocal{144}, kLocalEqual{160};
constexpr abc::EntityOffset kMethod{300}, kRecord{500}, kModule{420}, kRequest{560};
constexpr std::uint32_t kTypes = 200, kPool = 204, kFileBytes = 640;
constexpr ModuleOwnerId kOwner{7};
constexpr NameId kFirstName{1};
constexpr unsigned kByteBits = 8;
struct Access { ModuleSpace space; std::uint8_t slot; std::uint16_t guard; };

class Fixture {
 public:
  Fixture() {
    std::vector<std::uint8_t> bytes(kFileBytes);
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (unsigned i = 0; i < sizeof(value); ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (i * kByteBits));
    };
    const auto text = [&](std::size_t at, std::string_view value) {
      constexpr auto kAscii = 1U;
      bytes[at++] = static_cast<std::uint8_t>((value.size() << 1) | kAscii);
      std::copy(value.begin(), value.end(), bytes.begin() + at);
    };
    text(abc::kDynamicHeaderSize, "moduleRecordIdx");
    text(kFirst.value, "first"); text(kEqual.value, "first");
    text(kOther.value, "other"); text(kLocal.value, "localValue");
    text(kLocalEqual.value, "localValue"); text(kRequest.value, "./dependency.mjs");
    constexpr std::uint8_t kI32Type = 5;
    bytes[kTypes] = kI32Type;
    word(kPool, kEqual.value); word(kPool + sizeof(std::uint32_t), kOther.value);
    word(kPool + 2 * sizeof(std::uint32_t), kLocalEqual.value);
    std::size_t cursor = kModule.value;
    const auto emit = [&](std::uint32_t value) { word(cursor, value); cursor += sizeof(value); };
    const auto request = [&] { cursor += sizeof(std::uint16_t); }; // Zero-filled request ordinal 0.
    emit(0); emit(1); emit(kRequest.value); // Header and requests.
    emit(1); emit(kFirst.value); emit(kFirst.value); request();
    emit(1); emit(kOther.value); request();
    // Adjacent equal decoded local names are ONE slot, not two source bindings.
    emit(2); emit(kLocal.value); emit(kFirst.value); emit(kLocalEqual.value); emit(kOther.value);
    emit(0); emit(0); // Indirect and star exports.
    const int fd = mkstemp(path_);
    test::Check(fd >= 0);
    test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
    test::Check(close(fd) == 0);
    file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path_));
    header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
    header.version = {12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
    constexpr std::uint32_t kPoolEntries = 3;
    index.regions.push_back({250, 600, {1, kTypes}, {kPoolEntries, kPool}, {}, {}});
    abc::RecordInfo record;
    record.offset = kRecord; record.field_count = 1;
    index.fields.push_back({{320}, record.offset, {abc::kDynamicHeaderSize}, {}, 0,
        abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule.value}});
    context = std::get<RecordModuleContext>(ReadRecordModuleContext(*file, header, index,
        record, abc::FilePackaging::kMerged));
  }
  ~Fixture() { test::Check(unlink(path_) == 0); }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  std::unique_ptr<corpus::AbcFile> file;
  abc::Header header;
  abc::MetadataIndex index;
  std::optional<RecordModuleContext> context;
 private:
  char path_[sizeof("/tmp/abcd-module-aliases-XXXXXX")] = "/tmp/abcd-module-aliases-XXXXXX";
};
struct Function {
  FunctionInput input;
  FunctionValues values;
};
Function Make(const Fixture& fixture, std::span<const Access> accesses,
              abc::EntityOffset method = kMethod) {
  constexpr auto kGuard = abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
  std::vector<std::uint8_t> bytes;
  for (const auto& access : accesses) {
    const auto load = access.space == ModuleSpace::kExternal ? abc::Opcode::kLDEXTERNALMODULEVAR_IMM8
                                                          : abc::Opcode::kLDLOCALMODULEVAR_IMM8;
    bytes.insert(bytes.end(), {static_cast<std::uint8_t>(load), access.slot,
        static_cast<std::uint8_t>(kGuard), static_cast<std::uint8_t>(static_cast<std::uint16_t>(kGuard) >> kByteBits),
        static_cast<std::uint8_t>(access.guard), static_cast<std::uint8_t>(access.guard >> kByteBits)});
  }
  bytes.push_back(static_cast<std::uint8_t>(accesses.empty() ? abc::Opcode::kRETURNUNDEFINED : abc::Opcode::kRETURN));
  const auto body = test::Body(bytes);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {false, false, false, false}));
  RouteInputIdentity identity;
  identity.profile = fixture.header.profile; identity.abc_version = fixture.header.version;
  identity.file_size = fixture.header.file_size;
  auto input = std::get<FunctionInput>(FunctionInput::Build(identity, method, kRecord, body, frame, InputCaptureMode::kClosed));
  auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, frame));
  return {std::move(input), std::move(values)};
}
auto Bind(const Fixture& fixture, const Function& function, SemanticBudget budget = {}, NameId name = kFirstName) {
  return BindModuleAliases(*fixture.file, fixture.header, fixture.index, function.input,
      function.values, *fixture.context, kOwner, name, budget);
}
void Run(const Fixture& fixture, std::uint32_t count, bool local) {
  const Access access{local ? ModuleSpace::kLocal : ModuleSpace::kExternal, 0,
      static_cast<std::uint16_t>(local ? 2 : 0)};
  const std::vector<Access> accesses(count, access);
  auto function = Make(fixture, accesses);
  const auto bound = std::get<ModuleAliases>(Bind(fixture, function));
  test::Check(bound.evaluations.size() == count && bound.aliases.size() == (count ? 1 : 0));
  test::Check(bound.record == kRecord && bound.owner == kOwner && bound.first_name == kFirstName);
  for (const auto& evaluation : bound.evaluations)
    test::Check(evaluation.target == ModuleSymbol{kOwner, {local ? 2U : 0U}});
  if (count) {
    test::Check(bound.names[0] == (local ? u"localValue" : u"first"));
    test::Check(bound.aliases[0].name == kFirstName && bound.aliases[0].evidence == NameEvidence::kMetadata);
    test::Check(bound.aliases[0].access == ModuleAccess::kReadOnly);
  }
  // Real metadata + original pair proofs feed the SAME evaluation/AST route;
  // no manually supplied ModuleEvaluation or ModuleSyntax rows in this slice.
  const auto& graph = function.values.graph;
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, bound.evaluations));
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};
  syntax.identity = function.input.Identity(); syntax.method = kMethod; syntax.owner = {1};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names.push_back(u"existing");
  syntax.names.insert(syntax.names.end(), bound.names.begin(), bound.names.end());
  syntax.modules = bound.aliases;
  std::vector<ExpressionConstant> operations(graph.ops.size());
  const ExpressionConstants constants{operations, syntax.names.size(), 0};
  const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, constants)).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, evaluation, tree, constants)));
  test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(bound, evaluation, tree)));
  if (count) {
    auto changed = tree;
    changed.names[changed.symbols[changed.modules[0].symbol.value].name.value] = u"wrongName";
    test::Check(std::get<ModuleAliasTreeError>(VerifyModuleAliasTree(bound, evaluation, changed)) == ModuleAliasTreeError::kName);
    changed = tree; ++changed.modules[0].target.symbol.value;
    test::Check(std::get<ModuleAliasTreeError>(VerifyModuleAliasTree(bound, evaluation, changed)) == ModuleAliasTreeError::kTarget);
    changed = tree;
    changed.modules[0].access = ModuleAccess::kMutable;
    test::Check(std::get<ModuleAliasTreeError>(VerifyModuleAliasTree(bound, evaluation, changed)) == ModuleAliasTreeError::kAccess);
    auto changed_plan = evaluation; ++changed_plan.modules[0].target.owner.value;
    test::Check(std::get<ModuleAliasTreeError>(VerifyModuleAliasTree(bound, changed_plan, tree)) == ModuleAliasTreeError::kTarget);
  }
  SemanticBudget budget; budget.work = bound.work;
  test::Check(std::holds_alternative<ModuleAliases>(Bind(fixture, function, budget)));
  --budget.work;
  test::Check(std::get<ModuleAliasError>(std::get<ModuleAliasFailure>(Bind(fixture, function, budget)).cause) == ModuleAliasError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::holds_alternative<ModuleAliasFailure>(Bind(fixture, function, budget)));
  ++function.values.record.value;
  test::Check(std::get<ModuleAliasError>(std::get<ModuleAliasFailure>(Bind(fixture, function)).cause) == ModuleAliasError::kIdentity);
}
}  // namespace
int main() {
  const Fixture fixture;
  for (auto count : {0U, 1U, 2U, 200U, 201U, 4096U})
    for (bool local : {false, true}) Run(fixture, count, local);
  constexpr Access accesses[] = {{ModuleSpace::kLocal, 0, 2}, {ModuleSpace::kExternal, 1, 1}, {ModuleSpace::kExternal, 0, 0}};
  const auto mixed = Make(fixture, accesses);
  const auto bound = std::get<ModuleAliases>(Bind(fixture, mixed));
  test::Check(bound.aliases.size() == 3 && bound.names == std::vector<std::u16string>{u"first", u"other", u"localValue"});
  for (std::uint32_t i = 0; i < bound.aliases.size(); ++i) test::Check(bound.aliases[i].target.symbol == SymbolId{i});
  const auto sibling = Make(fixture, std::span(accesses).first(1), {304});
  test::Check(std::get<ModuleAliases>(Bind(fixture, sibling)).aliases[0].target == bound.aliases[2].target);
  constexpr Access bad_name[] = {{ModuleSpace::kExternal, 0, 1}};
  const auto wrong = Make(fixture, bad_name);
  const auto failed = std::get<ModuleAliasFailure>(Bind(fixture, wrong));
  test::Check(std::get<ModuleAliasError>(failed.cause) == ModuleAliasError::kGuardName && failed.pc == abc::CodeOffset{2});
  test::Check(std::get<ModuleAliasError>(std::get<ModuleAliasFailure>(Bind(fixture, sibling, {},
      {std::numeric_limits<std::uint32_t>::max()})).cause) == ModuleAliasError::kResourceLimit);
  auto changed = Make(fixture, accesses);
  ++changed.values.modules.revision.value;
  test::Check(std::get<ModuleGuardError>(std::get<ModuleAliasFailure>(Bind(fixture, changed)).cause) == ModuleGuardError::kRevision);
}
