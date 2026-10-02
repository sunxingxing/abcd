#include "../../../src/source/class_tree.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/family_cells.h"
#include "abcd/source/global_reference.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/exception_locals.h"
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <source_location>
#include <nlohmann/json.hpp>
namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok, const std::source_location& at = std::source_location::current()) {
  if (!ok) { std::cerr << "accessor tree check " << at.line() << '\n'; std::abort(); }
}
std::string Recover(bool is_static, bool return_prototype = false, int ordinary_parity = -1,
    bool async_method = false, abc::ProfileId profile = abc::ProfileId::kDynamic1206,
    bool capture_cells = false, bool initialized = true) {
  constexpr std::uint32_t kPool = 64, kOwner = 128, kLiteral = 160,
      kCreator = 256, kConstructor = 300, kGetter = 340, kSetter = 380,
      kBufferMethod = 420, kBufferCode = 864, kNestedMethod = 460, kNestedCode = 1000,
      kCreatorCode = 512, kConstructorCode = 768, kGetterCode = 800, kSetterCode = 832,
      kKey = 900, kPrototype = 920, kField = 944;
  std::vector<std::uint8_t> bytes(1100);
  const auto word = [&](std::size_t at, std::uint32_t n) {
    for (unsigned i = 0; i < sizeof(n); ++i) bytes[at + i] = static_cast<std::uint8_t>(n >> (8 * i));
  };
  const std::uint32_t pool[] = {kConstructor, kLiteral, kGetter, kSetter, kKey, kPrototype, kField, kBufferMethod, kNestedMethod};
  for (std::size_t i = 0; i < std::size(pool); ++i) word(kPool + i * sizeof(std::uint32_t), pool[i]);
  const auto text = [&](std::size_t at, std::string_view value) {
    bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at);
  };
  text(kKey, "value"); text(kPrototype, "prototype"); text(kField, "_value");
  word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 5, 0); // Empty class buffer, prototype split zero.
  if (capture_cells) {
    word(kLiteral, 8);
    bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(kLiteral + 5, kKey);
    bytes[kLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod); word(kLiteral + 10, kBufferMethod);
    bytes[kLiteral + 14] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
    bytes[kLiteral + 15] = 0; bytes[kLiteral + 16] = 0;
    bytes[kLiteral + 17] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kLiteral + 18, 1);
  }
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode value) { code.push_back(static_cast<std::uint8_t>(value)); };
  const auto load = [&](std::uint8_t r) { op(abc::Opcode::kLDA_V8); code.push_back(r); };
  const auto save = [&](std::uint8_t r) { op(abc::Opcode::kSTA_V8); code.push_back(r); };
  if (capture_cells) {
    op(abc::Opcode::kNEWLEXENV_IMM8); code.push_back(1);
    if (initialized) {
      op(abc::Opcode::kLDAI_IMM32); code.insert(code.end(), {42, 0, 0, 0});
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); code.push_back(0);
    }
  }
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  code.insert(code.end(), {0, 0, 0, 1, 0, 0, 0, 0}); save(1);
  if (!is_static) { op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); code.insert(code.end(), {0, 5, 0}); }
  save(2);
  // Repeated definitions test ordering and preserving the opposite descriptor
  // half. Every original method occurrence must still have a prepared body.
  for (unsigned i = 0; i < 4; ++i) {
    const bool setter = i % 2;
    op(abc::Opcode::kLDA_STR_ID16); code.insert(code.end(), {4, 0}); save(3);
    load(2); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8);
    code.insert(code.end(), {0, static_cast<std::uint8_t>(setter ? 3 : 2), 0, static_cast<std::uint8_t>(setter)});
    save(setter ? 5 : 4);
    if (static_cast<int>(i % 2) == ordinary_parity) {
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 4, 0, 2});
    } else {
      op(abc::Opcode::kLDUNDEFINED); save(setter ? 4 : 5);
      op(abc::Opcode::kLDFALSE); op(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8);
      code.insert(code.end(), {2, 3, 4, 5});
    }
  }
  // A distinct ordinary object literal in the same graph must still be
  // planned; class method installations must not be claimed as its fields.
  op(abc::Opcode::kCREATEEMPTYOBJECT); save(0); load(3);
  op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 6, 0, 0});
  load(return_prototype ? 2 : 1); op(abc::Opcode::kRETURN);
  const auto body = [&](std::uint32_t at, std::uint8_t locals, std::uint8_t args, std::vector<std::uint8_t> data,
      std::uint8_t tries = 0, std::uint8_t try_bytes = 0) {
    constexpr std::uint8_t kPayload = 0x7f, kContinuation = 0x80;
    constexpr unsigned kPayloadBits = 7;
    auto cursor = at;
    const auto uleb = [&](std::uint32_t value) {
      do {
        const auto part = static_cast<std::uint8_t>(value & kPayload); value >>= kPayloadBits;
        bytes[cursor++] = part | (value ? kContinuation : 0);
      } while (value);
    };
    const auto size = static_cast<std::uint32_t>(data.size() - try_bytes);
    uleb(locals); uleb(args); uleb(size); uleb(tries);
    std::copy(data.begin(), data.end(), bytes.begin() + cursor);
    return abc::CodeInfo{{at}, {cursor}, locals, args, size, tries};
  };
  const auto opcode = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  abc::MetadataIndex index; index.regions.push_back({kCreator, kCreatorCode, {}, {std::size(pool), kPool}, {}, {}});
  index.methods.resize(capture_cells ? 6 : 4);
  const std::uint32_t offsets[] = {kCreator, kConstructor, kGetter, kSetter, kBufferMethod, kNestedMethod};
  const std::string names[] = {"factory", "Accessors", "value", "value", "value", "nested"};
  for (std::size_t i = 0; i < index.methods.size(); ++i) {
    index.methods[i].offset = {offsets[i]}; index.methods[i].owner_offset = {kOwner};
    index.methods[i].name = names[i]; index.methods[i].name_utf16_length = names[i].size();
  }
  index.methods[0].code = body(kCreatorCode, 6, 3, code);
  index.methods[1].code = body(kConstructorCode, 0, 3, {opcode(abc::Opcode::kRETURNUNDEFINED)});
  index.methods[2].code = body(kGetterCode, 0, 3, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 6, 0, opcode(abc::Opcode::kRETURN)});
  index.methods[3].code = body(kSetterCode, 1, 4, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kSTA_V8), 0, opcode(abc::Opcode::kLDA_V8), 4,
      opcode(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 6, 0, 0, opcode(abc::Opcode::kRETURNUNDEFINED)});
  if (capture_cells) {
    const std::vector<std::uint8_t> captured{opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, opcode(abc::Opcode::kRETURN)};
    index.methods[1].code = body(kConstructorCode, 0, 3, captured);
    index.methods[2].code = body(kGetterCode, 0, 3, captured);
    index.methods[4].code = body(kBufferCode, 0, 3,
        {opcode(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 8, 0, 0, opcode(abc::Opcode::kRETURN)});
    index.methods[5].code = body(kNestedCode, 0, 3, captured);
    constexpr unsigned kFunctionKindShift = 8; // Pinned dynamic method access flags.
    index.methods[5].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  }
  if (async_method) {
    // Same private async await/resume wrapper as the class-buffer fixture,
    // now created by actual DEFINE METHOD and installed by STOWNBYNAME.
    constexpr unsigned kFunctionKindShift = 8, kByteBits = 8; // Pinned Panda attrs/encoding.
    constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    constexpr auto kStore = static_cast<std::uint8_t>(abc::Opcode::kSTA_V8);
    constexpr auto kLoad = static_cast<std::uint8_t>(abc::Opcode::kLDA_V8);
    constexpr auto kReturn = static_cast<std::uint8_t>(abc::Opcode::kRETURN);
    constexpr auto kInteger = static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32);
    index.methods[3].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kAsync) << kFunctionKindShift;
    index.methods[3].code = body(kSetterCode, 3, 4, {
        opcode(abc::Opcode::kASYNCFUNCTIONENTER), kStore, 0,
        kLoad, 6, opcode(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
        opcode(abc::Opcode::kSUSPENDGENERATOR_V8), 0, kLoad, 0,
        opcode(abc::Opcode::kRESUMEGENERATOR), kStore, 1, kLoad, 0,
        opcode(abc::Opcode::kGETRESUMEMODE), kStore, 2,
        kInteger, static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
        opcode(abc::Opcode::kEQ_IMM8_V8), 0, 2, opcode(abc::Opcode::kJEQZ_IMM8), 6,
        kLoad, 1, static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
        kLoad, 1, opcode(abc::Opcode::kLDTHIS), opcode(abc::Opcode::kNOP),
        opcode(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, kReturn,
        kStore, 1, opcode(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, kReturn,
        3, 37, 1, 0, 40, 5}, 1, 6);
  }
  char path[] = "/tmp/abcd-accessor-tree-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path); const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = profile;
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  auto loaded = ReadFunctionValues(file, header, index, {0}, identity,
      capture_cells ? InputCaptureMode::kConditional : InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(loaded)); const auto& creator = std::get<FileFunction>(loaded);
  const auto& graph = creator.values.graph; const auto& definitions = creator.values.definitions.sites;
  Check(definitions.size() == 4);
  if (capture_cells) {
    const auto class_operation = definitions.front().creation;
    const auto& class_origin = graph.origins[graph.ops[class_operation.value].origin.value];
    const analysis::InstructionId site{class_origin.site.value};
    Check(site.value < creator.input.Proof().environments.before.size() &&
        creator.input.Proof().environments.before[site.value]);
    const auto environment = *creator.input.Proof().environments.before[site.value];
    const auto check = [&](const FunctionContext& owner, analysis::InstructionId at, CaptureSite occurrence) {
      auto values = ReadFunctionValues(file, header, index, owner.Position(), identity,
          InputCaptureMode::kConditional, {}, nullptr, &owner);
      Check(std::holds_alternative<FileFunction>(values));
      const auto& child = std::get<FileFunction>(values);
      std::optional<FileFamily> forwarding;
      const auto work = [&]() -> CaptureChild {
        if (child.input.Method() != abc::EntityOffset{kBufferMethod}) return {child.input, child.values};
        // Genuine nested creation drives PlanFamilyCells. The class member has
        // no lexical opcode; its incoming cell is exclusively a descendant demand.
        Check(child.input.Captures().empty() && child.input.Children().size() == 1);
        const auto nested_site = child.input.Children().front();
        auto binding = std::get<analysis::CreationBinding>(analysis::BindCreation(file, header, index, lookup,
            child.input.Method(), child.body, child.input.Proof().environments, nested_site.value));
        const auto sites = functions.Find({kNestedMethod}); Check(sites.count == 1);
        FunctionContext nested_owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
            file, header, index, lookup, roles, functions, sites.first, identity))};
        auto nested = std::get<FileFunction>(ReadFunctionValues(file, header, index, nested_owner.Position(), identity,
            InputCaptureMode::kConditional, {}, nullptr, &nested_owner));
        auto incoming = std::get<CaptureCells>(BindCaptureCells(index, {child.input, child.values},
            {nested.input, nested.values}, nested_site, binding));
        forwarding.emplace(); forwarding->members.reserve(2);
        forwarding->members.push_back({FamilyFunction{std::get<FileFunction>(std::move(values))}});
        forwarding->members.push_back({FamilyFunction{std::move(nested)}, std::move(nested_owner),
            FamilyCreation{FileFamily::kRoot, nested_site, binding, std::move(incoming)}});
        Check(std::holds_alternative<FamilyCellCost>(PlanFamilyCells(*forwarding)));
        const auto prepared = forwarding->members[0].function.Capture();
        Check(prepared.OriginalInput().Captures().empty() && prepared.Input().Captures().size() == 1 &&
            prepared.Cells()->cells.size() == 1 && prepared.Cells()->cells[0].count == 0);
        return prepared;
      }();
      auto bound = BindCaptureCells(index, {creator.input, creator.values}, work, at, occurrence);
      if (!initialized) { Check(std::get<CaptureCellError>(bound) == CaptureCellError::kInput); return; }
      Check(std::holds_alternative<CaptureCells>(bound));
      const auto& cells = std::get<CaptureCells>(bound);
      Check(cells.targets.size() == 1 && std::holds_alternative<ParentCell>(cells.targets[0].target));
      Check(std::holds_alternative<std::uint64_t>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, cells)));
      auto changed = cells; ++changed.input.pc.value;
      Check(std::get<CaptureCellError>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, changed)) == CaptureCellError::kInput);
      changed = cells; ++std::get<ParentCell>(changed.targets[0].target).cell.value;
      Check(std::get<CaptureCellError>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, changed)) == CaptureCellError::kBinding);
      if (const auto* cls = std::get_if<ClassCaptureSite>(&occurrence)) {
        const ClassCaptureSite wrong{cls->owner, analysis::CapturedDepth{1}};
        Check(std::get<CaptureCellError>(BindCaptureCells(index,
            {creator.input, creator.values}, work, at, wrong)) == CaptureCellError::kInput);
      }
    };
    const FunctionContext constructor{std::get<ClassTreeContext>(ClassTreeContext::ResolveConstructor(
        file, header, index, lookup, roles, creator, class_operation, identity))};
    check(constructor, site, ClassCaptureSite{constructor, environment});
    // The original phase matcher must use this class instruction's Before,
    // never Commit or a neighboring instruction. This is scope-plan evidence,
    // not a claim that a whole phased class-containing body can be emitted.
    const auto phase = std::get<analysis::ExceptionFlow>(analysis::BuildExceptionFlow(
        creator.body, profile, creator.frame.registers));
    const auto phase_env = std::get<analysis::EnvironmentFlow>(analysis::AnalyzeExceptionEnvironments(phase));
    const auto phase_locals = std::get<analysis::ExceptionLocals>(analysis::ProveExceptionLocals(
        phase, phase_env, {}, {}, analysis::ClosureWrites::kLanguage));
    bool before_found = false, commit_found = false;
    const CaptureUse use{{{0}, {0}}, analysis::CaptureDomain::kLanguage};
    for (std::size_t n = 0; n < phase.nodes.size(); ++n) {
      const auto& node = phase.nodes[n];
      if (node.instruction != site) continue;
      const ScopeFacts scopes(ExceptionScopeFacts{phase, phase_env, phase_locals, {static_cast<std::uint32_t>(n)}});
      const ClassCaptureSite occurrence{constructor, environment};
      auto plan = BuildCapturePlan(scopes, site, occurrence, {&use, 1});
      if (node.phase == analysis::ExceptionPhase::kBefore) {
        before_found = true;
        if (initialized) {
          Check(std::holds_alternative<CapturePlan>(plan));
          Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes, site, occurrence,
              {&use, 1}, std::get<CapturePlan>(plan))));
        } else Check(std::get<CaptureError>(plan) == CaptureError::kScope);
      } else {
        commit_found |= node.phase == analysis::ExceptionPhase::kCommit;
        Check(std::get<CaptureError>(plan) == CaptureError::kCreation);
      }
    }
    Check(before_found && commit_found);
    const auto range = classes.Find({kBufferMethod}); Check(range.count == 1);
    const FunctionContext member{std::get<ClassTreeContext>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, classes, range.first, identity))};
    check(member, site, ClassCaptureSite{member, environment});
    const auto& definition = definitions.front();
    const FunctionContext defined{std::get<ClassTreeContext>(ClassTreeContext::ResolveDefinedMember(
        file, header, index, lookup, roles, functions, creator, definition.definition,
        abc::ClassMethodKind::kGetter, identity))};
    const auto created = *definition.Methods()[0].operation;
    const analysis::InstructionId instruction{graph.origins[graph.ops[created.value].origin.value].site.value};
    const auto binding = std::get<analysis::CreationBinding>(analysis::BindCreation(file, header, index, lookup,
        creator.input.Method(), creator.body, creator.input.Proof().environments, instruction.value));
    check(defined, instruction, binding);
    Check(unlink(path) == 0); return {};
  }
  auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, analysis::ClosureWrites::kUnknown, definitions);
  if (const auto* error = std::get_if<EvaluationError>(&evaluated)) std::cerr << "evaluation " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<EvaluationPlan>(evaluated)); const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  GlobalScope globals{};
  auto layout = BuildClassLayout(file, header, index, lookup, roles, classes, creator,
      definitions.front().creation, identity, {}, nullptr, definitions);
  Check(std::holds_alternative<ClassTree>(layout));
  std::ostringstream pending;
  Check(std::get<TreePrintError>(PrintClassTree(std::get<ClassTree>(layout), pending, {}, {}, &globals)) ==
      TreePrintError::kUnsupportedContext);
  Check(pending.str().empty());
  auto definition = std::make_shared<ClassTree>(std::get<ClassTree>(std::move(layout)));
  auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, {kCreator}, graph));
  constants.operations[definitions.front().creation.value] = definition;
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {kCreator}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = constants.names; syntax.texts = constants.texts;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  auto tree = BuildFunctionTree(graph, control, evaluation, syntax, constants.View());
  if (const auto* error = std::get_if<FunctionTreeError>(&tree)) std::cerr << "tree failure " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<ConstructedFunction>(tree));
  auto built = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
      definitions.front().creation, identity, &globals, {}, nullptr, definitions, RecoveryMode::kCompleteOnly, {37});
  if (async_method && ordinary_parity != 1) {
    const auto& failure = std::get<ClassTreeFailure>(built);
    const auto& family = std::get<FamilyPrepareFailure>(failure);
    const auto& context = std::get<ClassContextFailure>(family.cause);
    Check(std::get<ClassContextError>(context.error) == ClassContextError::kKind);
    Check(unlink(path) == 0); return {};
  }
  if (const auto* error = std::get_if<ClassTreeFailure>(&built)) std::cerr << "class failure " << error->index() << '\n';
  Check(std::holds_alternative<ClassTree>(built));
  *definition = std::get<ClassTree>(std::move(built));
  // All source occurrences reserve disjoint owners, including phase members.
  // Parent owner zero is never reused for a class root or its descendants.
  std::uint32_t next_owner = 37;
  const auto check_owners = [&](const PreparedFamily& prepared) {
    const auto& family = prepared.Family();
    Check(family.root == FileFamily::kRoot);
    for (const auto& member : family.members) Check(member.tree.owner.value == next_owner++);
  };
  check_owners(*definition->constructor);
  for (const auto& member : definition->members) check_owners(member);
  for (const auto& site : definition->definitions)
    for (const auto& member : site.bodies) if (member) check_owners(*member);
  // A complete class cannot lose an actual definition body or its proof.
  for (auto& site : definition->definitions) {
    for (auto& body : site.bodies) {
      if (!body) continue;
      auto missing_body = std::move(body); body.reset();
      std::ostringstream rejected;
      Check(std::get<TreePrintError>(PrintClassTree(*definition, rejected, {}, {}, &globals)) ==
          TreePrintError::kUnsupportedContext);
      Check(rejected.str().empty());
      Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
      body = std::move(missing_body);
    }
  }
  // Independent operand checking must reject removal or substitution of the
  // prepared body behind an otherwise well-formed projection certificate.
  auto missing = std::move(definition->definitions.back()); definition->definitions.pop_back();
  Check(std::holds_alternative<FunctionTreeError>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
  definition->definitions.push_back(std::move(missing));
  std::ostringstream output;
  auto printed = PrintFunctionTree(std::get<ConstructedFunction>(tree).tree, output, {}, {}, &globals);
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check(std::get<PrintedFunction>(printed).class_members.size() == 5);
  const auto read_family = [&] {
    return std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup, roles, functions, {0}, identity,
        {}, nullptr, nullptr, {}, FamilyClassMode::kRetainRoot));
  };
  const auto& root_tree = std::get<ConstructedFunction>(tree).tree;
  const auto flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, root_tree));
  auto incomplete = read_family(); incomplete.members[0].classes.pop_back();
  const auto denied_family = BuildRecordFamilyTrees(file, header, index, std::move(incomplete),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  Check(std::holds_alternative<FamilyTreeFailure>(denied_family));
  auto composed = BuildRecordFamilyTrees(file, header, index, read_family(),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  if (const auto* error = std::get_if<FamilyTreeFailure>(&composed)) std::cerr << "family failure " << error->cause.index() << '\n';
  Check(std::holds_alternative<ConstructedFamily>(composed));
  std::ostringstream module;
  auto module_print = PrintModuleFamilyDraft(std::get<ConstructedFamily>(composed), module, {}, {}, &globals);
  Check(std::holds_alternative<PrintedFamilyDraft>(module_print));
  Check(std::get<PrintedFamilyDraft>(module_print).members[0].class_members.size() == 5);
  Check(unlink(path) == 0); return output.str();
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--async-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool is_static : {false, true})
        std::cout << nlohmann::json{{"source", Recover(is_static, false, 1, true, profile)}, {"static", is_static}}.dump() << '\n';
    return 0;
  }
  if (argc == 2) { std::cout << Recover(std::string_view(argv[1]) == "--static", std::string_view(argv[1]) == "--retained",
      std::string_view(argv[1]) == "--method-last" ? 1 : std::string_view(argv[1]) == "--accessor-last" ? 0 : -1); return 0; }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool is_static : {false, true}) {
      Recover(is_static, false, -1, false, profile, true);
      Recover(is_static, false, -1, false, profile, true, false);
      Recover(is_static, false, 1, true, profile);
      Recover(is_static, false, -1, true, profile); // Async setter cannot become native set syntax.
    }
  Recover(false); Recover(true); Recover(false, true); Recover(false, false, 0); Recover(false, false, 1);
}
