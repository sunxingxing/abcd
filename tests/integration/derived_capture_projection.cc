#include "../../src/source/derived_projection.h"
#include "abcd/source/capture_preservation.h"
#include "abcd/corpus/abc_file.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>
namespace {
void Check(bool value, const std::source_location& at = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
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
  const auto parent_position = lookup.Find({6100}); Check(parent_position.has_value());
  auto read_parent = ReadFunctionValues(file, header, index, *parent_position, identity, InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(read_parent));
  const auto& parent = std::get<FileFunction>(read_parent);
  Check(parent.values.classes.classes.size() == 1);
  const auto creation = parent.values.classes.classes.front().operation;
  auto resolved = ClassTreeContext::ResolveConstructor(file, header, index, lookup, roles, parent, creation, identity);
  Check(std::holds_alternative<ClassTreeContext>(resolved));
  FunctionContext context{std::get<ClassTreeContext>(std::move(resolved))};
  Check(context.Target().value == 5851 && context.Callable() == CallableKind::kDerivedConstructor);
  auto loaded = ReadFunctionValues(file, header, index, context.Position(), identity,
      InputCaptureMode::kConditional, {}, nullptr, &context);
  Check(std::holds_alternative<FileFunction>(loaded));
  auto& function = std::get<FileFunction>(loaded);
  MethodConstants constants(file, header, index, context.Target(), {});
  const auto preserved = ProveCapturePreservation(function.body, constants, &lookup, {});
  Check(std::holds_alternative<CapturePreservations>(preserved));
  const auto& sites = std::get<CapturePreservations>(preserved);
  Check(!sites.sites.empty());
  // Original protocol is provable only when the real closure is inspected.
  Check(std::get<DerivedConstructorError>(ProveRangeDerivedConstructor(function.body,
      function.frame, {}, header.profile)) == DerivedConstructorError::kGuard);
  const auto original = ProveRangeDerivedConstructor(function.body, function.frame, {}, header.profile, sites.sites);
  if (const auto* error = std::get_if<DerivedConstructorError>(&original))
    std::cerr << "original preserved protocol error " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<RangeDerivedConstructor>(original));
  const DerivedCaptureInput captures{file, header, index, lookup};
  const auto projected = ProjectDerivedConstructor(function, context, {}, &captures);
  if (const auto* error = std::get_if<DerivedProjectionError>(&projected))
    std::cerr << "derived captured receiver error " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<DerivedProjection>(projected));
  Check(!VerifyDerivedProjection(function, context, std::get<DerivedProjection>(projected), {}, &captures));
  Check(std::get<DerivedProjectionError>(ProjectDerivedConstructor(function, context)) == DerivedProjectionError::kProtocol);
  Check(!file.CheckUnchanged());
}
