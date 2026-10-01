#include "../../src/source/function_file.h"
#include "../../src/source/class_tree_context.h"
#include "../../src/source/family_file.h"
#include "../../src/source/family_tree.h"
#include "../../src/source/module_aliases.h"
#include "../../src/source/function_tree.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/function_creation_sites.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"

#include <algorithm>
#include <memory>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::analysis;
  using namespace abcd::source;
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
  const auto creator = std::get<FileFunction>(ReadFunctionValues(file, header, index,
      *lookup.Find({8202}), identity, InputCaptureMode::kConditional));
  const auto found = std::find_if(creator.values.graph.ops.begin(), creator.values.graph.ops.end(),
      [&](const auto& op) {
        return std::holds_alternative<ValueClassCreation>(op.operation) &&
            creator.values.graph.origins[op.origin.value].pc == abc::CodeOffset{45};
      });
  test::Check(found != creator.values.graph.ops.end());
  const OpId operation{static_cast<std::uint32_t>(found - creator.values.graph.ops.begin())};
  FunctionContext context{std::get<ClassTreeContext>(ClassTreeContext::ResolveConstructor(
      file, header, index, lookup, roles, creator, operation, identity))};
  const InstructionId site{creator.values.graph.origins[found->origin.value].site.value};
  const ParentModuleInput module_input{creator.input, site};
  const auto& target = index.methods[context.Position().value];
  const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *target.code));
  const auto input = FunctionInput::Build(identity, target.offset, target.owner_offset, body,
      context.Frame(), InputCaptureMode::kConditional, {}, &context, &module_input);
  test::Check(std::holds_alternative<FunctionInput>(input));
  const auto& constructor = std::get<FunctionInput>(input);
  test::Check(constructor.HasInheritedModules() && constructor.InitializedModuleReads().empty());
  const auto edge_work = std::get<std::uint64_t>(VerifyInitializedModuleReads(creator.input, site, constructor));
  test::Check(edge_work != 0);
  SemanticBudget exact; exact.work = edge_work;
  test::Check(std::get<std::uint64_t>(VerifyInitializedModuleReads(creator.input, site, constructor, exact)) == edge_work);
  --exact.work;
  test::Check(std::get<FunctionInputError>(VerifyInitializedModuleReads(creator.input, site, constructor, exact)) ==
      FunctionInputError::kWorkLimit);
  test::Check(std::holds_alternative<FunctionInputError>(VerifyInitializedModuleReads(creator.input, {0}, constructor)));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  const auto occurrence = *sites.FindOccurrence({7754}, {8339}, {39});
  FunctionContext arrow{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
      file, header, index, lookup, roles, sites, occurrence, identity))};
  const auto at = std::find_if(constructor.Input().instructions.begin(), constructor.Input().instructions.end(),
      [](const auto& instruction) { return instruction.pc == abc::CodeOffset{39}; });
  test::Check(at != constructor.Input().instructions.end());
  const InstructionId creation{static_cast<std::uint32_t>(at - constructor.Input().instructions.begin())};
  const ParentModuleInput inherited{constructor, creation};
  const auto record = std::find_if(index.records.begin(), index.records.end(),
      [&](const auto& value) { return value.offset == constructor.Record(); });
  test::Check(record != index.records.end());
  const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
  const auto modules = std::get<RecordModuleContext>(ReadRecordModuleContext(file, header, index, *record, packaging));
  auto family = std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup, roles, sites,
      context.Position(), identity, {}, &modules, &context, {record->offset.value}, FamilyClassMode::kReject, &module_input));
  const auto actual = std::find_if(family.members.begin(), family.members.end(),
      [](const auto& member) { return member.function.Method() == abc::EntityOffset{7754}; });
  test::Check(actual != family.members.end() && actual->incoming && !actual->function.Phase());
  test::Check(actual->function.Normal().input.InitializedModuleReads().size() == 2);
  for (const auto& read : actual->function.Normal().input.InitializedModuleReads()) test::Check(!read.store);
  const auto actual_trees = BuildFileFamilyTrees(file, header, index, std::move(family), context,
      {}, &modules, {record->offset.value}, &lookup);
  test::Check(std::holds_alternative<ConstructedFamily>(actual_trees));

  // Keep the exact real owners but isolate decoded read lowering from the
  // unrelated call-reference failure in this constructor's full callback.
  const ModuleOwnerId owner{record->offset.value};
  for (const auto count : {1U, 200U, 201U, 400U}) {
    std::vector<std::uint8_t> code;
    for (std::uint32_t i = 0; i < count; ++i) code.insert(code.end(), {0x7d, 0});
    code.push_back(0x64);
    const auto reader = test::Body(code);
    const auto child = std::get<FunctionInput>(FunctionInput::Build(identity, arrow.Target(), constructor.Record(),
        reader, arrow.Frame(), InputCaptureMode::kConditional, {}, &arrow, &inherited));
    test::Check(child.InitializedModuleReads().size() == count && !child.InitializedModuleReads().front().store);
    test::Check(std::holds_alternative<std::uint64_t>(VerifyInitializedModuleReads(constructor, creation, child)));
    const auto values = std::get<FunctionValues>(BuildFunctionValues(child, reader, arrow.Frame()));
    const auto aliases = std::get<ModuleAliases>(BindModuleAliases(file, header, index, child, values, modules, owner));
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(values.graph, {}, IncomingCells::kConditional,
        {}, aliases.evaluations, {}, {}, ClosureWrites::kLanguage));
    const auto control = std::get<ControlPlan>(BuildControlPlan(values.graph));
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = child.Method(); syntax.owner = {0};
    syntax.callable = arrow.Callable(); syntax.strictness = Strictness::kStrict;
    syntax.names = aliases.names; syntax.modules = aliases.aliases;
    syntax.parameters.resize(arrow.Frame().parameters);
    const std::vector<ExpressionConstant> constants(values.graph.ops.size());
    const auto tree = std::get<ConstructedFunction>(BuildFunctionTree(values.graph, control, evaluation,
        std::move(syntax), {constants, aliases.names.size(), 0})).tree;
    test::Check(tree.recovery_issues.empty());
    test::Check(std::holds_alternative<TreeFlow>(VerifyTreeFlow(values.graph, control, evaluation, tree)));
    test::Check(std::holds_alternative<std::uint64_t>(VerifyModuleAliasTree(aliases, evaluation, tree)));
    const auto enclosure = std::get<ModuleEnclosure>(ModuleEnclosure::Build(file, header, modules, identity, owner));
    const std::vector<std::u16string> local_names{u"ImmersiveType", u"WindowInfo", u"WindowUtil"};
    const std::vector<std::string_view> record_names{"ImmersiveType", "WindowInfo", "WindowUtil"};
    GlobalScope scope{}; scope.module_enclosure = &enclosure;
    scope.module_local_names = local_names; scope.record_bindings = record_names;
    std::ostringstream output;
    const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree, output, {}, {}, &scope));
    test::Check(!printed.incomplete);
    if (argc == 3 && count == 1) {
      test::Check(std::string_view(argv[2]) == "--source");
      std::cout << "let ImmersiveType; let WindowInfo; let WindowUtil;\nexport const read = (" << output.str()
          << ");\nexport function set(value) { ImmersiveType = value; }\n";
    }
  }
  // A class created before initialization must not gain membership from the
  // same module's metadata or a branch store after the creation prefix.
  auto delayed = creator.body;
  for (auto& instruction : delayed.instructions)
    if (instruction.offset == abc::CodeOffset{10} || instruction.offset == abc::CodeOffset{19})
      instruction.decoded.operands[0].raw_bits = 1;
  const auto late = std::get<FunctionInput>(FunctionInput::Build(identity, creator.input.Method(), creator.input.Record(),
      delayed, creator.frame, InputCaptureMode::kConditional));
  test::Check(std::holds_alternative<FunctionInputError>(VerifyInitializedModuleReads(late, site, constructor)));
  const ParentModuleInput late_input{late, site};
  const auto late_constructor = std::get<FunctionInput>(FunctionInput::Build(identity, target.offset, target.owner_offset,
      body, context.Frame(), InputCaptureMode::kConditional, {}, &context, &late_input));
  const ParentModuleInput uninitialized{late_constructor, creation};
  const auto raw = test::Body({0x7d, 0, 0x64});
  const auto rejected = FunctionInput::Build(identity, arrow.Target(), constructor.Record(), raw,
      arrow.Frame(), InputCaptureMode::kConditional, {}, &arrow, &uninitialized);
  test::Check(std::get<SemanticProofFailure>(std::get<FunctionInputFailure>(rejected).cause).kind == SemanticProofError::kInternalValue);
  auto changed = creator.body;
  auto& changed_class = changed.instructions[site.value].decoded;
  ++changed_class.operands[0].raw_bits; // Same PC, changed class allocation.
  const auto wrong = std::get<FunctionInput>(FunctionInput::Build(identity, creator.input.Method(), creator.input.Record(),
      changed, creator.frame, InputCaptureMode::kConditional));
  test::Check(!constructor.MatchesModuleCreation(wrong, site));
  test::Check(std::holds_alternative<FunctionInputError>(VerifyInitializedModuleReads(wrong, site, constructor)));
  const ParentModuleInput wrong_input{wrong, site};
  const auto wrong_admission = FunctionInput::Build(identity, target.offset, target.owner_offset,
      body, context.Frame(), InputCaptureMode::kConditional, {}, &context, &wrong_input);
  test::Check(std::get<FunctionInputError>(std::get<FunctionInputFailure>(wrong_admission).cause) ==
      FunctionInputError::kModuleOwner);
  // A class-root family must discharge the same external edge even if the
  // root has no reads. Otherwise a transparent root could launder a premise.
  const auto seed = [&](const ParentModuleInput* publication) {
    const auto minimal = test::Body({0x65});
    auto root_input = std::get<FunctionInput>(FunctionInput::Build(identity, target.offset, target.owner_offset,
        minimal, context.Frame(), InputCaptureMode::kConditional, {}, &context, &module_input));
    auto root_values = std::get<FunctionValues>(BuildFunctionValues(root_input, minimal, context.Frame()));
    FileFamily result;
    result.members.push_back({FamilyFunction{FileFunction{minimal, context.Frame(), std::move(root_input), std::move(root_values)}}, {}, {}});
    if (publication) result.root_modules.emplace(*publication);
    return result;
  };
  const auto complete = BuildFileFamilyTrees(file, header, index, seed(&module_input), context,
      {}, &modules, owner, &lookup);
  test::Check(std::holds_alternative<ConstructedFamily>(complete) && std::get<ConstructedFamily>(complete).writes.has_value());
  for (const auto* publication : {static_cast<const ParentModuleInput*>(nullptr), &late_input}) {
    const auto invalid = BuildFileFamilyTrees(file, header, index, seed(publication), context,
        {}, &modules, owner, &lookup);
    test::Check(std::get<FamilyWriteFailure>(std::get<FamilyTreeFailure>(invalid).cause).kind == FamilyWriteError::kOwner);
  }
  test::Check(!file.CheckUnchanged());
}
