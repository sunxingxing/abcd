#include "../../src/source/function_file.h"
#include "../../src/source/family_file.h"
#include "../../src/source/module_aliases.h"
#include "../../src/source/module_evaluation.h"
#include "../../src/source/function_tree.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/function_creation_sites.h"
#include "abcd/analysis/value_import.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>
#include <memory>
#include <iostream>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
void Scaling() {
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    const auto op = [](abc::Opcode opcode) { return static_cast<std::uint8_t>(opcode); };
    std::vector<std::uint8_t> code{op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0,
        op(abc::Opcode::kLDAI_IMM32), 1, 0, 0, 0};
    for (std::uint32_t i = 0; i < count; ++i)
      code.insert(code.end(), {op(abc::Opcode::kSTMODULEVAR_IMM8), static_cast<std::uint8_t>(i % 2)});
    for (std::uint32_t i = 0; i < count; ++i)
      code.insert(code.end(), {op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0, 0, 0});
    code.push_back(op(abc::Opcode::kRETURNUNDEFINED));
    const auto body = test::Body(code);
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {}));
    RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
    const auto parent = std::get<FunctionInput>(FunctionInput::Build(identity, {300}, {100}, body,
        frame, InputCaptureMode::kConditional));
    test::Check(!parent.ModuleStoreBefore({0}, {0})); // Future initialization.
    for (std::uint32_t i = 0; i < count; ++i) {
      test::Check(parent.ModuleStoreBefore({0}, {2 + count + i}) == InstructionId{2});
      test::Check(parent.ModuleStoreBefore({1}, {2 + count + i}) ==
          (count > 1 ? std::optional<InstructionId>{{3}} : std::nullopt));
      test::Check(!parent.ModuleStoreBefore({2}, {2 + count + i}));
    }
  }
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 0, {}));
  RouteInputIdentity identity; identity.profile = abc::ProfileId::kDynamic1206;
  const auto hole = test::Body({static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
      static_cast<std::uint8_t>(abc::Opcode::kSTMODULEVAR_IMM8), 0,
      static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)});
  const auto rejected = FunctionInput::Build(identity, {300}, {100}, hole, frame, InputCaptureMode::kConditional);
  test::Check(std::get<SemanticProofFailure>(std::get<FunctionInputFailure>(rejected).cause).kind ==
      SemanticProofError::kInternalValue);
}
}  // namespace

int main(int argc, char** argv) {
  Scaling();
  test::Check(argc == 2 || argc == 3);
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  const auto creator_position = *lookup.Find({7283});
  const auto parent = std::get<FileFunction>(ReadFunctionValues(file, header, index, creator_position,
      identity, InputCaptureMode::kConditional));
  const auto occurrence = *sites.FindOccurrence({7061}, {7283}, {196});
  FunctionContext context{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
      file, header, index, lookup, roles, sites, occurrence, identity))};
  const auto& target = index.methods[context.Position().value];
  const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *target.code));
  const auto found = std::find_if(parent.input.Input().instructions.begin(), parent.input.Input().instructions.end(),
      [](const auto& instruction) { return instruction.pc == abc::CodeOffset{196}; });
  test::Check(found != parent.input.Input().instructions.end());
  const InstructionId creation{static_cast<std::uint32_t>(found - parent.input.Input().instructions.begin())};
  const ParentModuleInput modules{parent.input, creation};
  const auto build = [&](const ParentModuleInput* premise, const FunctionContext* owner = nullptr) {
    return FunctionInput::Build(identity, target.offset, target.owner_offset, body, context.Frame(),
        InputCaptureMode::kConditional, {}, owner, premise);
  };
  const auto unproven = build(nullptr, &context);
  const auto refusal = std::get<SemanticProofFailure>(std::get<FunctionInputFailure>(unproven).cause);
  test::Check(refusal.kind == SemanticProofError::kInternalValue && refusal.pc == abc::CodeOffset{46});
  const auto child = std::get<FunctionInput>(build(&modules, &context));
  test::Check(child.InitializedModuleReads().size() == 1);
  const auto& read = child.InitializedModuleReads().front();
  test::Check(read.slot == ModuleSlot{1} && read.creator == abc::EntityOffset{7283} &&
      read.creation_pc == abc::CodeOffset{196} && read.store && parent.body.instructions[read.store->value].offset == abc::CodeOffset{11});
  test::Check(child.Input().instructions[read.instruction.value].pc == abc::CodeOffset{44});
  test::Check(child.Proof().module_reads.size() == 1 &&
      child.Proof().module_reads[0].proof == ModuleReadProof::kInitialized);
  const auto graph = std::get<ValueProgram>(ImportNormalValues(body, child.Input(), child.Proof(), context.Frame()));
  test::Check(!VerifyImportedValues(graph, child.Input(), child.Proof(), context.Frame()));
  const auto operation = *graph.input.sites[read.instruction.value];
  test::Check(std::get<ValueModuleRead>(graph.ops[operation.value].operation).slot == ModuleSlot{1});
  test::Check(graph.ops[operation.value].normal == LanguageEffect::kModuleRead &&
      graph.ops[operation.value].exceptional == ExceptionalCompletion::kMayThrow);
  test::Check(graph.values[graph.ops[operation.value].result->value].domain.IsLanguage());
  const auto work = std::get<std::uint64_t>(VerifyInitializedModuleReads(parent.input, creation, child));
  test::Check(work != 0);
  SemanticBudget exact; exact.work = work;
  test::Check(std::get<std::uint64_t>(VerifyInitializedModuleReads(parent.input, creation, child, exact)) == work);
  --exact.work;
  test::Check(std::get<FunctionInputError>(VerifyInitializedModuleReads(parent.input, creation, child, exact)) ==
      FunctionInputError::kWorkLimit);
  test::Check(std::get<FunctionInputError>(VerifyInitializedModuleReads(parent.input, {0}, child)) ==
      FunctionInputError::kModuleOwner);
  // Genuine context, record/file identity and exact creation are mandatory.
  test::Check(std::get<FunctionInputError>(std::get<FunctionInputFailure>(build(&modules)).cause) ==
      FunctionInputError::kModuleOwner);
  const ParentModuleInput wrong_site{parent.input, {0}};
  test::Check(std::get<FunctionInputError>(std::get<FunctionInputFailure>(build(&wrong_site, &context)).cause) ==
      FunctionInputError::kModuleOwner);
  const auto wrong_record = std::get<FunctionInput>(FunctionInput::Build(identity, parent.input.Method(), {100},
      parent.body, parent.frame, InputCaptureMode::kConditional));
  const ParentModuleInput wrong_owner{wrong_record, creation};
  test::Check(std::get<FunctionInputError>(std::get<FunctionInputFailure>(build(&wrong_owner, &context)).cause) ==
      FunctionInputError::kModuleOwner);
  test::Check(std::get<FunctionInputError>(VerifyInitializedModuleReads(wrong_record, creation, child)) ==
      FunctionInputError::kModuleOwner);
  // Initialization before the genuine creation is independent of entry-block
  // layout. The changed input needs its own immutable state premise.
  auto later_body = parent.body;
  for (auto& instruction : later_body.instructions) {
    if (instruction.offset == abc::CodeOffset{11}) instruction.decoded.operands[0].raw_bits = 0;
    if (instruction.offset == abc::CodeOffset{194}) instruction.decoded.operands[0].raw_bits = 1;
  }
  const auto later = std::get<FunctionInput>(FunctionInput::Build(identity, parent.input.Method(), parent.input.Record(),
      later_body, parent.frame, InputCaptureMode::kConditional));
  const ParentModuleInput late{later, creation};
  test::Check(!later.ModuleStoreBefore({1}, creation));
  test::Check(later.InitializedModulesBefore(creation).Contains({1}));
  const auto late_child = std::get<FunctionInput>(build(&late, &context));
  test::Check(late_child.InitializedModuleReads().size() == 1 &&
      !late_child.InitializedModuleReads()[0].store);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyInitializedModuleReads(later, creation, late_child)));
  test::Check(std::get<FunctionInputError>(VerifyInitializedModuleReads(later, creation, child)) ==
      FunctionInputError::kModuleOwner);
  for (auto& instruction : later_body.instructions)
    if (instruction.offset == abc::CodeOffset{194}) instruction.decoded.operands[0].raw_bits = 0;
  const auto absent = std::get<FunctionInput>(FunctionInput::Build(identity, parent.input.Method(), parent.input.Record(),
      later_body, parent.frame, InputCaptureMode::kConditional));
  const ParentModuleInput missing{absent, creation};
  test::Check(!absent.InitializedModulesBefore(creation).Contains({1}));
  test::Check(std::get<SemanticProofFailure>(std::get<FunctionInputFailure>(build(&missing, &context)).cause).kind ==
      SemanticProofError::kInternalValue);
  // Real file preparation, metadata aliases and effect indexing use the same
  // guard-free read; parent evidence does not invent an absent TDZ guard.
  const auto record = std::find_if(index.records.begin(), index.records.end(),
      [&](const auto& candidate) { return candidate.offset == parent.input.Record(); });
  test::Check(record != index.records.end());
  const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
  const auto module_context = std::get<RecordModuleContext>(ReadRecordModuleContext(
      file, header, index, *record, packaging));
  const auto prepared = std::get<FileFunction>(ReadFunctionValues(file, header, index, context.Position(), identity,
      InputCaptureMode::kConditional, {}, &module_context, &context, &modules));
  test::Check(prepared.values.modules.pairs.size() == 1 && !prepared.values.modules.pairs[0].guard);
  const auto aliases = std::get<ModuleAliases>(BindModuleAliases(file, header, index, prepared.input,
      prepared.values, module_context, {record->offset.value}));
  test::Check(aliases.evaluations.size() == 1 && !aliases.evaluations[0].pair.guard &&
      aliases.aliases.size() == 1 && aliases.names[0] == u"PageABuilder");
  const auto effects = std::get<ModuleEvaluationIndex>(IndexModuleEvaluations(
      prepared.values.graph, aliases.evaluations));
  test::Check(!effects.peers[aliases.evaluations[0].pair.read.value]);
  auto changed = prepared.values.modules;
  changed.pairs[0].guard = changed.pairs[0].read;
  test::Check(std::holds_alternative<ModuleGuardError>(VerifyModuleGuards(
      prepared.input, prepared.values.graph, changed)));
  auto schedule = aliases.evaluations;
  schedule[0].pair.guard = schedule[0].pair.read;
  test::Check(std::holds_alternative<EvaluationError>(IndexModuleEvaluations(prepared.values.graph, schedule)));
  auto wrong_graph = prepared.values.graph;
  std::get<ValueModuleRead>(wrong_graph.ops[schedule[0].pair.read.value].operation).space = ModuleSpace::kExternal;
  test::Check(std::holds_alternative<EvaluationError>(IndexModuleEvaluations(wrong_graph, aliases.evaluations)));
  const auto family = std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup, roles, sites,
      creator_position, identity, {}, &module_context, nullptr, {record->offset.value}, FamilyClassMode::kRetainRoot));
  const auto actual_child = std::find_if(family.members.begin(), family.members.end(), [&](const auto& member) {
    return !member.function.Phase() && member.function.Normal().input.Method() == child.Method();
  });
  test::Check(actual_child != family.members.end() && actual_child->incoming &&
      actual_child->function.Normal().input.InitializedModuleReads().size() == 1);
  // A small decoded body isolates the new lowering from other unsupported
  // class members. Its genuine occurrence/slot metadata come from this ABC;
  // runtime expectations and the embedding declarations stay in the test.
  for (const auto count : {1U, 2U, 200U, 201U, 400U, 800U}) {
    std::vector<std::uint8_t> reader_code;
    for (std::uint32_t i = 0; i < count; ++i)
      reader_code.insert(reader_code.end(), {static_cast<std::uint8_t>(abc::Opcode::kLDLOCALMODULEVAR_IMM8), 1});
    reader_code.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
    const auto reader_body = test::Body(reader_code);
    const auto reader_input = std::get<FunctionInput>(FunctionInput::Build(identity, child.Method(), child.Record(),
        reader_body, context.Frame(), InputCaptureMode::kConditional, {}, &context, &modules));
    const auto reader_values = std::get<FunctionValues>(BuildFunctionValues(reader_input, reader_body, context.Frame()));
    const ModuleOwnerId module_owner{record->offset.value};
    const auto reader_aliases = std::get<ModuleAliases>(BindModuleAliases(file, header, index, reader_input,
        reader_values, module_context, module_owner));
    test::Check(reader_aliases.evaluations.size() == count && reader_aliases.aliases.size() == 1);
    for (const auto& evaluated : reader_aliases.evaluations) test::Check(!evaluated.pair.guard);
    test::Check(reader_input.Children().empty() && reader_input.Captures().empty());
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(reader_values.graph, {},
        IncomingCells::kConditional, {}, reader_aliases.evaluations, {}, {}, ClosureWrites::kLanguage));
    const auto control = std::get<ControlPlan>(BuildControlPlan(reader_values.graph));
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = child.Method(); syntax.owner = {0};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.names = reader_aliases.names; syntax.modules = reader_aliases.aliases;
    syntax.parameters.resize(context.Frame().parameters);
    const std::vector<ExpressionConstant> constants(reader_values.graph.ops.size());
    const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(reader_values.graph, control, evaluation,
        std::move(syntax), {constants, reader_aliases.names.size(), 0})).tree;
    test::Check(tree.recovery_issues.empty());
    const auto flow = std::get<TreeFlow>(VerifyTreeFlow(reader_values.graph, control, evaluation, tree));
    for (const auto& evaluated : reader_aliases.evaluations)
      test::Check(flow.expressions[evaluated.pair.read.value].has_value());
    test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(reader_aliases, evaluation, tree)));
    const auto enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(file, header, module_context,
        identity, module_owner));
    const std::vector<std::u16string> local_names{u"PageA", u"PageABuilder"};
    const std::vector<std::string_view> record_names{"PageA", "PageABuilder"};
    GlobalScope scope{};
    scope.module_enclosure = &enclosure; scope.module_local_names = local_names; scope.record_bindings = record_names;
    test::Check(std::holds_alternative<ModuleDestination>(VerifyModuleDestination(tree, record->offset, &scope)));
    std::ostringstream output;
    const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &scope));
    test::Check(!printed.incomplete && output.str().find("PageABuilder") != std::string::npos);
    if (argc == 3 && count == 1) {
      test::Check(std::string_view(argv[2]) == "--source");
      std::cout << "let PageA; let PageABuilder;\nexport const read = (" << output.str()
          << ");\nexport function set(value) { PageABuilder = value; }\n";
    }
  }
  test::Check(!file.CheckUnchanged());
}
