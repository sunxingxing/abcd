#include "../../src/source/prepared_family.h"
#include "../../src/source/family_emission.h"
#include "../../src/source/family_route_failure.h"
#include "abcd/corpus/abc_file.h"
#include "constructor_tail_syntax.h"
#include <cstdlib>
#include <charconv>
#include <string_view>
#include <iostream>
#include <memory>
#include <span>
namespace {
void Check(bool condition) { if (!condition) std::abort(); }
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  Check(argc == 2 || argc == 3 || argc == 4);
  std::uint32_t entity = 7312;
  if (argc >= 3) {
    const std::string_view text{argv[2]};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), entity);
    Check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
  }
  auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto hash = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto creator = lookup.Find({entity}); Check(creator.has_value());
  RouteInputIdentity identity; identity.profile = header.profile;
  identity.abc_version = header.version; identity.file_size = header.file_size;
  auto caller = std::get<FileFunction>(ReadFunctionValues(file, header, index, *creator,
      identity, InputCaptureMode::kClosed));
  Check(caller.values.classes.classes.size() == 1);
  const auto occurrence = caller.values.classes.classes.front().operation;
  const FamilyDestination destination{Strictness::kStrict, nullptr, FunctionFragment::kClassMember};
  const auto wrong = ClassTreeContext::ResolveConstructor(file, header, index, lookup, roles,
      caller, {0}, identity);
  Check(std::holds_alternative<ClassContextFailure>(wrong));
  const auto limited = ClassTreeContext::ResolveConstructor(file, header, index, lookup, roles,
      caller, occurrence, identity, {0});
  Check(std::holds_alternative<ClassContextFailure>(limited));
  const auto valid_context = ClassTreeContext::ResolveConstructor(file, header, index, lookup, roles,
      caller, occurrence, identity);
  const auto& context = std::get<ClassTreeContext>(valid_context);
  if (argc == 4) {
    Check(std::string_view(argv[3]) == "--tail-syntax");
    ConstructorTailSyntax(context); Check(!file.CheckUnchanged()); return 0;
  }
  Check(context.Constructor() && context.Length() == 0);
  auto& working = std::get<analysis::ValueClassCreation>(caller.values.graph.ops[occurrence.value].operation);
  ++working.length;
  Check(std::holds_alternative<ClassContextFailure>(ClassTreeContext::ResolveConstructor(
      file, header, index, lookup, roles, caller, occurrence, identity)));
  --working.length;
  auto& use = caller.values.graph.uses[caller.values.graph.ops[occurrence.value].first_use.value];
  const auto input = use.operand;
  use.operand = analysis::SpecialValue::kNull;
  Check(std::holds_alternative<ClassContextFailure>(ClassTreeContext::ResolveConstructor(
      file, header, index, lookup, roles, caller, occurrence, identity)));
  use.operand = input;
  auto prepared = PreparedFamily::PrepareConstructor(file, header, index, lookup, roles,
      functions, caller, occurrence, identity, destination);
  if (const auto* error = std::get_if<FamilyPrepareFailure>(&prepared)) {
    const auto failure = FamilyRouteFailure(*error);
    std::cerr << "constructor preparation failure " << error->cause.index() << ' '
        << static_cast<unsigned>(failure.failure_class) << '\n'; return 1;
  }
  auto family = std::get<PreparedFamily>(std::move(prepared));
  Check(family.Family().members[family.Family().root.value].tree.callable == CallableKind::kBaseConstructor);
  Check(std::holds_alternative<FamilyPrepareFailure>(family.CheckDestination(
      {Strictness::kStrict, nullptr, FunctionFragment::kFunctionExpression})));
  const auto printed = FamilyEmission::Write(std::move(family), destination, std::cout, {0});
  Check(std::holds_alternative<FamilyEmission>(printed));
  const auto& receipts = std::get<FamilyEmission>(printed).Receipts();
  Check(receipts.size() == 1 && receipts.front().route == SourceRoute::kValueTree &&
      receipts.front().route == abcd::source::SourceRoute::kValueTree);
}
