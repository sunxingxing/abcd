#include "../../src/source/derived_projection.h"
#include "../../src/source/evaluation_plan.h"
#include "../../src/source/family_tree.h"
#include "../../src/source/family_print.h"
#include "../../src/source/prepared_family.h"
#include "../../src/source/family_emission.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/source_route.h"
#include "abcd/source/record_source.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <source_location>
#include <sstream>

namespace {
void Check(bool value, const std::source_location& where = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); }
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  using namespace abcd::analysis;
  Check(argc == 2 || argc == 3);
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  auto identity = MakeRouteInputIdentity(fingerprint, header);
  identity.contract = kSourceContract;  // Native-super source target.
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  // Actual EntryAbility creation supplies the owner. Standalone constructor
  // import cannot certify this invocation's receiver or callable identity.
  const auto creator_position = lookup.Find({5001}); Check(creator_position.has_value());
  auto creator = ReadFunctionValues(file, header, index, *creator_position, identity,
      InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(creator));
  const auto& parent = std::get<FileFunction>(creator);
  std::optional<OpId> creation;
  for (std::uint32_t i = 0; i < parent.values.graph.ops.size(); ++i) {
    const auto& op = parent.values.graph.ops[i];
    if (op.life != ValueLife::kLive || !std::holds_alternative<ValueClassCreation>(op.operation)) continue;
    Check(!creation); creation = OpId{i};
  }
  Check(creation.has_value());
  auto resolved = ClassTreeContext::ResolveConstructor(file, header, index, lookup,
      roles, parent, *creation, identity);
  Check(std::holds_alternative<ClassTreeContext>(resolved));
  FunctionContext context{std::get<ClassTreeContext>(std::move(resolved))};
  Check(context.Target().value == 5083 && context.Callable() == CallableKind::kDerivedConstructor);
  const auto position = lookup.Find(context.Target()); Check(position.has_value());
  auto loaded = ReadFunctionValues(file, header, index, *position, identity,
      InputCaptureMode::kClosed, {}, nullptr, &context);
  Check(std::holds_alternative<FileFunction>(loaded));
  auto& function = std::get<FileFunction>(loaded);
  const auto proof = ProveSpreadDerivedConstructor(function.body, function.frame, {}, header.profile);
  Check(std::holds_alternative<SpreadDerivedConstructor>(proof));
  Check(std::get<SpreadDerivedConstructor>(proof).super_pc.value == 18);
  const auto projected = ProjectDerivedConstructor(function, context);
  if (const auto* error = std::get_if<DerivedProjectionError>(&projected))
    std::cerr << "spread projection error " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<DerivedProjection>(projected));
  const auto protocol = std::get<DerivedProjection>(projected);
  Check(protocol.guards.size() == 3);
  Check(!VerifyDerivedProjection(function, context, protocol));
  auto missing = protocol; missing.guards.pop_back();
  Check(VerifyDerivedProjection(function, context, missing) == DerivedProjectionError::kCoverage);

  auto& graph = function.values.graph;
  auto& call = graph.ops[protocol.call.value];
  auto& constructor = graph.uses[call.first_use.value];
  auto& array = graph.uses[call.first_use.value + 1];
  const auto saved = constructor.operand;
  constructor.operand = array.operand;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) ==
      DerivedProjectionError::kOperand);
  constructor.operand = saved;
  const auto saved_role = array.role;
  array.role = OperandRole::kArgument;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) ==
      DerivedProjectionError::kOperand);
  array.role = saved_role;
  auto& spread = std::get<ValueSuperSpread>(call.operation);
  ++spread.cache_slot;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) ==
      DerivedProjectionError::kOrigin);
  --spread.cache_slot;
  auto& guard = graph.ops[protocol.guards.front().value];
  auto& guarded = graph.uses[guard.first_use.value];
  const auto saved_guard = guarded.operand;
  guarded.operand = SpecialValue::kNull;
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) ==
      DerivedProjectionError::kOperand);
  guarded.operand = saved_guard;
  Check(!VerifyDerivedProjection(function, context, protocol));
  // Initialization alone does not grant source publication. The common
  // constructor, ABI, origin and effect proofs must all close independently.
  const auto shape = DescribeEvaluation(call.operation);
  Check(shape && shape->arguments == 0 && shape->roles.size() == 2 &&
      shape->roles[0] == OperandRole::kConstructor && shape->roles[1] == OperandRole::kInput);
  const auto scheduled = BuildEvaluationPlan(graph, {}, IncomingCells::kReject,
      {}, {}, {}, {}, ClosureWrites::kLanguage, {}, protocol.guards);
  Check(std::holds_alternative<EvaluationPlan>(scheduled));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  auto family = ReadFunctionFamily(file, header, index, lookup, roles, sites, *position,
      identity, {}, nullptr, &context);
  Check(std::holds_alternative<FileFamily>(family));
  auto partial = BuildFileFamilyTrees(file, header, index, std::get<FileFamily>(std::move(family)),
      context, {}, nullptr, {}, &lookup, RecoveryMode::kAllowPartial);
  if (const auto* error = std::get_if<FamilyTreeFailure>(&partial)) {
    std::cerr << "partial tree cause " << error->cause.index() << '\n';
    std::visit([](const auto& cause) {
      if constexpr (std::is_enum_v<std::decay_t<decltype(cause)>>)
        std::cerr << "partial tree error " << static_cast<unsigned>(cause) << '\n';
    }, error->cause);
  }
  Check(std::holds_alternative<ConstructedFamily>(partial));
  const auto& draft = std::get<ConstructedFamily>(partial);
  const auto& root = draft.members[draft.root.value];
  Check(root.tree.recovery_issues.empty() && root.abi && root.sources);
  std::ostringstream output;
  const auto printed = PrintClassFamilyDraft(draft, *context.ClassContext(), output);
  Check(std::holds_alternative<PrintedFamilyDraft>(printed));
  Check(std::get<PrintedFamilyDraft>(printed).members[draft.root.value].incomplete == false);
  Check(output.view().find("INCOMPLETE") == std::string_view::npos);
  Check(output.view().find("super(") != std::string_view::npos);
  Check(output.view().find("return ") == std::string_view::npos);
  const FamilyDestination destination{Strictness::kStrict, nullptr, FunctionFragment::kClassMember};
  auto ready = PreparedFamily::PrepareConstructor(file, header, index, lookup, roles, sites,
      parent, *creation, identity, destination, {}, RecoveryMode::kAllowPartial);
  Check(std::holds_alternative<PreparedFamily>(ready));
  auto& prepared = std::get<PreparedFamily>(ready);
  Check(prepared.Receipts().front().stage == SourceStage::kTreeVerified);
  std::ostringstream draft_output;
  Check(std::holds_alternative<PrintedFamilyDraft>(prepared.PrintDraft(destination, draft_output)));
  Check(draft_output.str() == output.str());
  std::ostringstream executable_output;
  const auto denied = FamilyEmission::Write(std::move(prepared), destination, executable_output, {});
  Check(std::holds_alternative<FamilyEmission>(denied));
  Check(executable_output.str() == output.str());
  Check(std::holds_alternative<PreparedFamily>(PreparedFamily::PrepareConstructor(
      file, header, index, lookup, roles, sites, parent, *creation, identity, destination)));
  // The complete constructor must retain the other real class members and
  // publish the same verified source target through the enclosing ESM record.
  const auto record = std::find_if(index.records.begin(), index.records.end(),
      [](const auto& candidate) { return candidate.offset.value == 5246; });
  Check(record != index.records.end());
  const auto packaging = std::get<abc::FilePackaging>(abc::DetectFilePackaging(file, header, index));
  const auto modules = std::get<RecordModuleContext>(ReadRecordModuleContext(
      file, header, index, *record, packaging));
  const auto classes = std::get<ClassMethodSites>(ClassMethodSites::Build(file, header, index, lookup));
  const auto recovered = BuildRecordSource(file, header, index, lookup, *record, modules, roles,
      {}, identity, &sites, &classes);
  Check(std::holds_alternative<PrintedSource>(recovered));
  const auto& source = std::get<PrintedSource>(recovered);
  Check(!source.incomplete);
  Check(source.text.find("INCOMPLETE") == std::string::npos);
  Check(source.text.find("UNRECOVERED") == std::string::npos);
  Check(source.text.find("async onBackup()") != std::string::npos);
  Check(source.text.find("async onRestore(bundleVersion)") != std::string::npos);
  Check(source.text.find("super(") != std::string::npos);
  Check(source.text.find("export { EntryBackupAbility as \"default\" }") != std::string::npos);
  if (argc == 3) std::cout << (std::string_view(argv[2]) == "--backup-source" ? std::string(source.text.View()) : output.str());
  Check(!file.CheckUnchanged());
}
