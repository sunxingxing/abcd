#include "../../src/source/derived_projection.h"
#include "../../src/source/evaluation_plan.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/source_route.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>

namespace {
void Check(bool value, const std::source_location& where = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); }
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  using namespace abcd::analysis;
  Check(argc == 2);
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
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
  // Initialization projection does not authorize a native spread call. The
  // array-expansion and lifetime parent-brand contracts still remain open.
  Check(!DescribeEvaluation(call.operation));
  Check(!file.CheckUnchanged());
}
