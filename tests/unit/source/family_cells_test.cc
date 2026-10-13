#include "../../../src/source/family_cells.h"
#include "../../../src/source/capture_requirement.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>
#include <unistd.h>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr std::uint32_t kPool = 64, kParent = 256, kMiddle = 300, kChild = 340, kCode = 512;
constexpr unsigned kByteBits = 8, kKindShift = 8, kUlebBits = 7;
constexpr std::uint8_t kUlebPayload = 0x7f, kContinuation = 0x80;
std::uint8_t Op(abc::Opcode value) { return static_cast<std::uint8_t>(value); }
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  do {
    auto byte = static_cast<std::uint8_t>(value & kUlebPayload); value >>= kUlebBits;
    bytes.push_back(byte | (value ? kContinuation : 0));
  } while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::vector<std::uint8_t> code, bool caught,
    std::uint32_t parameters = 0) {
  const auto handler = static_cast<std::uint32_t>(code.size());
  if (caught) code.push_back(Op(abc::Opcode::kRETURNUNDEFINED));
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  constexpr std::uint32_t kLocals = 1, kImplicitArguments = 3;
  Uleb(bytes, kLocals); Uleb(bytes, kImplicitArguments + parameters); Uleb(bytes, code.size()); Uleb(bytes, caught ? 1 : 0);
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  if (caught) {
    Uleb(bytes, 0); Uleb(bytes, handler); Uleb(bytes, 1);
    Uleb(bytes, 0); Uleb(bytes, handler); Uleb(bytes, 1);
  }
  return {{offset}, {start}, kLocals, kImplicitArguments + parameters, static_cast<std::uint32_t>(code.size()), caught ? 1U : 0U};
}
enum class Access { kNone, kGuarded, kRead, kWrite };
void AccessCode(std::vector<std::uint8_t>& code, Access access, std::uint8_t depth = 0) {
  if (access == Access::kNone) return;
  if (access == Access::kWrite) code.push_back(Op(abc::Opcode::kLDUNDEFINED));
  code.insert(code.end(), {Op(access == Access::kWrite ? abc::Opcode::kSTLEXVAR_IMM4_IMM4 :
      abc::Opcode::kLDLEXVAR_IMM4_IMM4), depth}); // imm4 depth, slot zero.
  if (access == Access::kGuarded) {
    constexpr auto guard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    code.insert(code.end(), {static_cast<std::uint8_t>(guard), static_cast<std::uint8_t>(guard >> kByteBits), 0, 0});
  }
}
void CreationCode(std::vector<std::uint8_t>& code, std::uint8_t target) {
  code.insert(code.end(), {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, target, 0, 0});
}
// File-backed input owners over hand-assembled decoded semantics. The middle
// is deliberately imported through the phased route without a user handler;
// this is the working IR shape left by async completion normalization. No
// source receipt or compiler-layout admission is manufactured by this test.
struct Fixture {
  char path[64] = "/tmp/abcd-family-cells-XXXXXX";
  std::unique_ptr<corpus::AbcFile> file;
  abc::Header header;
  abc::MetadataIndex index;
  std::optional<abc::MethodLookup> lookup;
  std::optional<MethodRoleIndex> roles;
  std::optional<FunctionCreationSites> sites;
  RouteInputIdentity identity;

  Fixture(Access own, Access child, bool caught, unsigned creations = 1,
      SourceContract contract = SourceContract::kLanguageSourceV1, bool entry = false, bool local_environment = false) {
    std::vector<std::uint8_t> bytes(kCode);
    const auto word = [&](std::size_t at, std::uint32_t value) {
      for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
    };
    word(kPool, kMiddle); word(kPool + sizeof(kMiddle), kChild);
    std::vector<std::uint8_t> parent{Op(abc::Opcode::kNEWLEXENV_IMM8), 1};
    CreationCode(parent, 0);
    parent.insert(parent.end(), {Op(abc::Opcode::kDEBUGGER), Op(abc::Opcode::kLDUNDEFINED),
        Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kRETURNUNDEFINED)});
    std::vector<std::uint8_t> middle;
    if (local_environment) middle.insert(middle.end(), {Op(abc::Opcode::kNEWLEXENV_IMM8), 1});
    constexpr std::uint8_t kFirstParameter = 4;
    if (entry) {
      middle = {Op(abc::Opcode::kLDA_V8), kFirstParameter, Op(abc::Opcode::kSTA_V8), 0,
          Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTRICTNOTEQ_IMM8_V8), 0, 0};
      const auto branch = middle.size();
      middle.insert(middle.end(), {Op(abc::Opcode::kJNEZ_IMM8), 0});
      AccessCode(middle, Access::kGuarded);
      middle.insert(middle.end(), {Op(abc::Opcode::kSTA_V8), kFirstParameter});
      middle[branch + 1] = static_cast<std::uint8_t>(middle.size() - branch);
    }
    AccessCode(middle, own, local_environment ? 1 : 0);
    for (unsigned i = 0; i < creations; ++i) CreationCode(middle, 1);
    if (entry) middle.insert(middle.end(), {Op(abc::Opcode::kLDA_V8), kFirstParameter, Op(abc::Opcode::kRETURN)});
    else middle.push_back(Op(abc::Opcode::kRETURNUNDEFINED));
    std::vector<std::uint8_t> leaf; AccessCode(leaf, child, local_environment ? 1 : 0);
    // A real throwing operation makes the handler reachable independently of
    // the lexical access (which Round B will project as an extra throw point).
    if (caught) leaf.insert(leaf.end(), {Op(abc::Opcode::kLDA_V8), 1, Op(abc::Opcode::kCALLARG0_IMM8), 0});
    leaf.push_back(Op(abc::Opcode::kRETURNUNDEFINED));
    const abc::CodeInfo codes[]{Append(bytes, parent, false), Append(bytes, middle, false, entry ? 1 : 0), Append(bytes, leaf, caught)};
    const auto fd = mkstemp(path); test::Check(fd >= 0);
    test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && close(fd) == 0);
    file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
    header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
    header.profile = abc::ProfileId::kDynamic1206; header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
    index.regions.push_back({kParent, static_cast<std::uint32_t>(bytes.size()), {}, {2, kPool}, {}, {}});
    const std::uint32_t offsets[]{kParent, kMiddle, kChild};
    for (unsigned i = 0; i < 3; ++i) {
      abc::MethodInfo method{}; method.offset = {offsets[i]}; method.owner_offset = {128}; method.code = codes[i];
      method.name = "body"; method.name_utf16_length = method.name.size();
      method.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kKindShift;
      index.methods.push_back(std::move(method));
    }
    lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
    roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(*file, header, index, *lookup));
    sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(*file, header, index, *lookup));
    identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
    identity.contract = contract;
  }
  ~Fixture() { test::Check(unlink(path) == 0); }
  FamilyFunction Read(abc::MethodPosition method, bool phased) const {
    if (!phased) return std::get<FileFunction>(ReadFunctionValues(*file, header, index, method, identity, InputCaptureMode::kConditional));
    const auto found = sites->Find(index.methods[method.value].offset);
    test::Check(found.count != 0);
    const FunctionContext context{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        *file, header, index, *lookup, *roles, *sites, found.first, identity))};
    auto result = FilePhasedFunction::Read(*file, header, index, context, {}, nullptr, InputCaptureMode::kConditional);
    test::Check(std::holds_alternative<FilePhasedFunction>(result));
    return std::get<FilePhasedFunction>(std::move(result));
  }
  FamilyCreation Edge(const FamilyFunction& parent, FamilyId owner, InstructionId site) const {
    if (const auto* phase = parent.Phase()) {
      const auto input = phase->Phases().TreeInput(phase->Body()).input;
      for (std::size_t n = 0; n < input.flow.nodes.size(); ++n) {
        const auto& node = input.flow.nodes[n];
        if (node.instruction != site || node.phase != ExceptionPhase::kBefore) continue;
        auto binding = std::get<CreationBinding>(BindExceptionCreation(*file, header, index, *lookup,
            parent.Method(), parent.Body(), input.flow, input.environments, {static_cast<std::uint32_t>(n)}));
        for (std::size_t op = 0; op < parent.Graph().ops.size(); ++op) {
          const auto& operation = parent.Graph().ops[op];
          if (std::holds_alternative<ValueFunctionCreation>(operation.operation) &&
              parent.Graph().origins[operation.origin.value].site.value == site.value)
            return {owner, site, std::move(binding), {}, OpId{static_cast<std::uint32_t>(op)}};
        }
      }
      test::Check(false);
    }
    return {owner, site, std::get<CreationBinding>(BindCreation(*file, header, index, *lookup,
        parent.Method(), parent.Body(), parent.Normal().input.Proof().environments, site.value)), {}, {}};
  }
  FileFamily Family(bool phased_middle, bool caught) const {
    FileFamily family;
    family.members.push_back({Read({0}, false)});
    auto edge = Edge(family.members[0].function, {0}, family.members[0].function.Children().front());
    family.members.push_back({Read({1}, phased_middle), {}, std::move(edge)});
    for (auto site : family.members[1].function.Children()) {
      auto child = Edge(family.members[1].function, {1}, site);
      family.members.push_back({Read({2}, caught), {}, std::move(child)});
    }
    return family;
  }
};
FamilyCellCost Forwarding(bool phase, Access own, Access child, bool caught, CaptureDomain expected, unsigned creations = 1,
    SourceContract contract = SourceContract::kLanguageSourceV1, bool local_environment = false) {
  Fixture fixture(own, child, caught, creations, contract, false, local_environment);
  auto family = fixture.Family(phase, caught);
  const auto original = family.members[1].function.Capture().OriginalInput().Captures();
  test::Check(original.size() == (own == Access::kNone ? 0U : 1U));
  if (phase) test::Check(family.members[1].function.Phase()->Body().tries.empty());
  if (caught) {
    const auto& strict = family.members[2].function;
    test::Check(strict.Phase() && !strict.Body().tries.empty());
    test::Check(strict.Capture().OriginalInput().Captures().front().domain == CaptureDomain::kLanguage);
  }
  const auto result = PlanFamilyCells(family);
  test::Check(std::holds_alternative<FamilyCellCost>(result));
  const auto& middle = family.members[1].function;
  const auto uses = middle.Capture().Input().Captures();
  test::Check(uses.size() == 1 && uses.front().domain == expected);
  test::Check(uses.front().cell.depth == CapturedDepth{0});
  test::Check(middle.Cells()->demands.size() == creations);
  for (std::size_t i = 1; i < middle.Cells()->demands.size(); ++i)
    test::Check(middle.Cells()->demands[i - 1].creation != middle.Cells()->demands[i].creation);
  if (!original.empty()) test::Check(original.front().domain ==
      (own == Access::kGuarded ? CaptureDomain::kGuarded :
       contract == SourceContract::kLanguageSourceV1 ? CaptureDomain::kSourceAccess : CaptureDomain::kLanguage));
  const auto& edge = *family.members[1].incoming;
  const auto bound = BindCaptureCells(fixture.index, family.members[0].function.NormalCapture(),
      middle.Capture(), edge.site, edge.creation);
  if (expected == CaptureDomain::kLanguage) {
    // The refusal is the parent initialization obligation, not phase import.
    const auto& error = std::get<CaptureCellFailure>(bound);
    test::Check(error.kind == CaptureCellError::kInput);
    const auto& cause = std::get<CaptureFailure>(error.cause);
    test::Check(cause.kind == CaptureError::kScope && std::get<ScopeError>(cause.cause) == ScopeError::kInitialization);
    test::Check(cause.occurrence && cause.occurrence->ordinal == 0 &&
        cause.occurrence->requested == CaptureDomain::kLanguage && cause.occurrence->source);
    // A pure forward or strengthened local use points to its strict descendant,
    // never to the parent's creation PC or the forwarding function's entry.
    if (original.empty() || original.front().domain != CaptureDomain::kLanguage) {
      test::Check(cause.occurrence->source->method == abc::EntityOffset{kChild} &&
          cause.occurrence->source->pc && cause.occurrence->source->working_pc);
    }

  } else {
    const auto& cells = std::get<CaptureCells>(bound);
    test::Check(cells.deferred.size() == 1);
    test::Check(std::holds_alternative<std::uint64_t>(VerifyCaptureCells(fixture.index,
        family.members[0].function.NormalCapture(), middle.Capture(), edge.site, edge.creation, cells)));
  }
  if (!phase && !original.empty()) {
    auto forged = *middle.Storage();
    forged.uses.front().domain = original.front().domain == CaptureDomain::kSourceAccess
        ? CaptureDomain::kGuarded : CaptureDomain::kDefined;
    const CaptureChild altered{CaptureFunction{middle.Normal().input, middle.Normal().values, &forged}};
    test::Check(std::get<CaptureCellFailure>(BindCaptureCells(fixture.index,
        family.members[0].function.NormalCapture(), altered, edge.site, edge.creation)) == CaptureCellError::kBinding);
  }
  const auto cost = std::get<FamilyCellCost>(result);
  // FamilyCellCost accounts retained products plus family scratch; upstream
  // cell builders also need transient scratch and report their own work.
  SemanticBudget budget;
  budget.work = cost.work;
  test::Check(std::holds_alternative<FamilyCellCost>(PlanFamilyCells(family, budget)));
  --budget.work;
  test::Check(std::get<CaptureCellFailure>(PlanFamilyCells(family,
      budget)) == CaptureCellError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  test::Check(std::get<CaptureCellFailure>(PlanFamilyCells(family, budget)) == CaptureCellError::kResourceLimit);
  return cost;
}
void RequirementLattice() {
  using D = CaptureDomain;
  constexpr D domains[]{D::kGuarded, D::kSourceAccess, D::kLanguage, D::kDefined};
  constexpr D joined[][4]{
      {D::kGuarded, D::kSourceAccess, D::kLanguage, D::kDefined},
      {D::kSourceAccess, D::kSourceAccess, D::kLanguage, D::kDefined},
      {D::kLanguage, D::kLanguage, D::kLanguage, D::kDefined},
      {D::kDefined, D::kDefined, D::kDefined, D::kDefined}};
  constexpr bool strengthens[][4]{{true, true, true, false}, {false, true, true, false},
      {false, false, true, false}, {false, false, false, true}};
  for (unsigned a = 0; a < std::size(domains); ++a)
    for (unsigned b = 0; b < std::size(domains); ++b) {
      test::Check(JoinCaptureRequirement(domains[a], domains[b]) == joined[a][b]);
      test::Check(StrengthensCaptureRequirement(domains[a], domains[b]) == strengthens[a][b]);
      for (auto c : domains)
        test::Check(JoinCaptureRequirement(JoinCaptureRequirement(domains[a], domains[b]), c) ==
            JoinCaptureRequirement(domains[a], JoinCaptureRequirement(domains[b], c)));
    }
}
void EntryDemand(Access own) {
  Fixture fixture(own, Access::kRead, false, 0, SourceContract::kLanguageSourceV1, true);
  auto family = fixture.Family(false, false);
  auto& function = family.members[1].function;
  auto& values = function.Normal().values;
  const auto parameters = std::get<ParameterPlan>(BuildParameterPlan(values.graph, 1));
  const auto candidates = std::get<DefaultCandidates>(FindDefaultCandidates(values.graph, parameters, 0));
  test::Check(candidates.capture.has_value());
  auto projected = DefaultBody::BuildCaptured(values.graph, *values.environments, *values.cells, parameters, candidates, 0);
  test::Check(std::holds_alternative<DefaultBody>(projected));
  function.Defaults().emplace(std::get<DefaultBody>(std::move(projected)));
  values.graph = function.Defaults()->Graph();
  values.environments = std::get<ValueEnvironments>(BuildValueEnvironments(values.graph));
  // Same residual reconstruction as family default projection. Entry demands
  // are independent of remaining body reads, and never acquire SourceAccess.
  values.cells.reset();
  test::Check(std::holds_alternative<FamilyCellCost>(PlanFamilyCells(family)));
  test::Check(function.Cells()->entry_demands.size() == 1);
  for (const auto& use : function.Capture().Input().Captures())
    test::Check(use.domain == CaptureDomain::kLanguage);
  const auto& edge = *family.members[1].incoming;
  const auto error = std::get<CaptureCellFailure>(BindCaptureCells(fixture.index,
      family.members[0].function.NormalCapture(), function.Capture(), edge.site, edge.creation));
  test::Check(error.kind == CaptureCellError::kInput);
  const auto cause = std::get<CaptureFailure>(error.cause);
  test::Check(cause.kind == CaptureError::kScope && std::get<ScopeError>(cause.cause) == ScopeError::kInitialization);
}
void MissingMapping() {
  Fixture fixture(Access::kRead, Access::kRead, true);
  auto family = fixture.Family(true, true);
  // Not a creation in the phased WORKING graph. This must fail typed instead
  // of silently dropping the descendant Language obligation.
  family.members[2].incoming->phase_operation = OpId{static_cast<std::uint32_t>(family.members[1].function.Graph().ops.size())};
  test::Check(std::get<CaptureCellFailure>(PlanFamilyCells(family)) == CaptureCellError::kCell);
}
}  // namespace
int main() {
  RequirementLattice();
  MissingMapping();
  for (const auto own : {Access::kNone, Access::kGuarded, Access::kRead}) EntryDemand(own);
  Forwarding(true, Access::kRead, Access::kRead, true, CaptureDomain::kLanguage);
  Forwarding(true, Access::kRead, Access::kWrite, true, CaptureDomain::kLanguage);
  Forwarding(false, Access::kGuarded, Access::kWrite, false, CaptureDomain::kSourceAccess);
  Forwarding(false, Access::kRead, Access::kRead, true, CaptureDomain::kLanguage);
  Forwarding(false, Access::kGuarded, Access::kGuarded, false, CaptureDomain::kGuarded);
  Forwarding(false, Access::kRead, Access::kGuarded, false, CaptureDomain::kSourceAccess);
  for (const auto contract : {SourceContract::kLanguageNumericConstructorsV1, SourceContract::kLanguageSourceV1})
    for (const auto access : {Access::kRead, Access::kWrite})
      Forwarding(true, Access::kRead, access, true, CaptureDomain::kLanguage, 1, contract);
  Forwarding(true, Access::kRead, Access::kRead, true, CaptureDomain::kLanguage, 2,
      SourceContract::kLanguageSourceV1, true);
  for (const bool phase : {false, true}) {
    Forwarding(phase, Access::kNone, Access::kGuarded, false, CaptureDomain::kGuarded);
    Forwarding(phase, Access::kNone, Access::kWrite, false, CaptureDomain::kSourceAccess);
    Forwarding(phase, Access::kNone, Access::kRead, true, CaptureDomain::kLanguage, 2);
  }
  std::optional<FamilyCellCost> previous;
  for (const auto count : {200U, 201U, 402U, 804U}) {
    const auto cost = Forwarding(true, Access::kNone, Access::kWrite, false, CaptureDomain::kSourceAccess, count);
    if (previous) {
      test::Check(cost.work < 3 * previous->work);
      test::Check(cost.allocated_bytes < 3 * previous->allocated_bytes);
    }
    previous = cost;
  }
}
