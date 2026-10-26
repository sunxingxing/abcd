#include <filesystem>
#include <iostream>
#include <source_location>

#include "../../../src/source/creator_classes.h"
#include "../../../src/source/family_file.h"
#include "../../../src/source/runtime_name.h"
#include "abcd/corpus/abc_file.h"
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) {
    std::cerr << "phased classes " << at.line() << '\n';
    std::abort();
  }
}
template <class T, class... E>
T Take(std::variant<E...> value,
       std::source_location at = std::source_location::current()) {
  if (auto* result = std::get_if<T>(&value)) return std::move(*result);
  std::cerr << "phased classes variant " << at.line() << '\n';
  std::abort();
}
}  // namespace
int main(int argc, char** argv) {
  Check(argc == 3);
  auto file =
      Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  auto index = Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  auto lookup =
      Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  auto roles = Take<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  auto functions = Take<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  auto classes = Take<analysis::ClassMethodSites>(
      analysis::ClassMethodSites::Build(*file, header, index, lookup));
  auto identity = MakeRouteInputIdentity(hash, header);
  identity.contract = SourceContract::kLanguageSourceV2;
  unsigned tested = 0;
  for (const auto& method : index.methods) {
    if (RuntimeName(method) != "captured" && RuntimeName(method) != "after")
      continue;
    const auto sites = functions.Find(method.offset);
    Check(sites.count == 1);
    FunctionContext context{Take<CreationTreeContext>(
        CreationTreeContext::Resolve(*file, header, index, lookup, roles,
                                     functions, sites.first, identity))};
    auto family = Take<FileFamily>(ReadFunctionFamily(
        *file, header, index, lookup, roles, functions, context.Position(),
        identity, {}, nullptr, &context, {}, FamilyClassMode::kReject, nullptr,
        {}, nullptr, &classes));
    const auto* phase = family.members.front().function.Phase();
    Check(phase != nullptr);
    const ClassCreator creator{*phase};
    const CreatorClassInput input{*file,
                                  header,
                                  index,
                                  lookup,
                                  roles,
                                  functions,
                                  classes,
                                  creator,
                                  identity,
                                  {method.offset, family.first_owner, {0}},
                                  family.members.front().function.Storage()};
    auto plan = Take<CreatorClasses>(DiscoverCreatorClasses(input));
    Take<CreatorClassCost>(VerifyCreatorClasses(input, plan));
    Check(plan.tasks.size() == 1 &&
          plan.tasks.front().tree->phase_point.has_value());
    const auto point = *plan.tasks.front().tree->phase_point;
    const auto refused = [&] {
      const auto result = VerifyCreatorClasses(input, plan);
      Check(std::holds_alternative<CreatorClassFailure>(result));
      Check(std::get<ClassTreeError>(
                std::get<CreatorClassFailure>(result).cause) ==
            ClassTreeError::kCreation);
    };
    ++plan.tasks.front().operation.value;
    refused();
    --plan.tasks.front().operation.value;
    ++plan.tasks.front().tree->creation.pc.value;
    refused();
    --plan.tasks.front().tree->creation.pc.value;
    plan.tasks.front().tree->phase_point->environment =
        analysis::EnvironmentId{UINT32_MAX};
    refused();
    plan.tasks.front().tree->phase_point = point;
    ++plan.tasks.front().tree->phase_point->before.value;
    refused();
    plan.tasks.front().tree->phase_point = point;
    ++plan.tasks.front().tree->phase_point->commit.value;
    refused();
    plan.tasks.front().tree->phase_point = point;
    ++plan.tasks.front().tree->phase_point->original.value;
    refused();
    plan.tasks.front().tree->phase_point = point;
    Take<CreatorClassCost>(VerifyCreatorClasses(input, plan));
    const auto no_bytes = DiscoverCreatorClasses(input, {0});
    Check(std::get<ClassTreeError>(
              std::get<CreatorClassFailure>(no_bytes).cause) ==
          ClassTreeError::kResourceLimit);
    const auto no_work =
        DiscoverCreatorClasses(input, {analysis::SemanticBudget{}.bytes, 0});
    Check(std::get<ClassTreeError>(
              std::get<CreatorClassFailure>(no_work).cause) ==
          ClassTreeError::kWorkLimit);
    const GlobalScope globals{};
    auto fresh = Take<FileFamily>(ReadFunctionFamily(
        *file, header, index, lookup, roles, functions, context.Position(),
        identity, {}, nullptr, &context, {}, FamilyClassMode::kReject, nullptr,
        {}, nullptr, &classes));
    auto prepared = Take<FamilyTreeInput>(FamilyTreeInput::Prepare(
        *file, header, index, std::move(fresh), context, {}, nullptr, {},
        &lookup, RecoveryMode::kCompleteOnly, nullptr, nullptr, &globals));
    auto trees = Take<ConstructedFamily>(std::move(prepared).Build(
        *file, header, index, context, {}, nullptr, {}, &lookup,
        RecoveryMode::kCompleteOnly, nullptr, &globals));
    Take<FamilyWrites>(VerifyFamilyWrites(index, family, trees, {}, &lookup));
    // Absent heritage consumes Hole as protocol state, never a source value.
    Check(!trees.members.front().tree.hole);
    unsigned mutations = 0;
    for (const auto& expression : trees.members.front().tree.expressions) {
      const auto* created = std::get_if<ClassExpr>(&expression.expression);
      if (!created) continue;
      auto definition = std::const_pointer_cast<ClassTree>(created->definition);
      Check(definition && definition->phase_binding && definition->phase_point);
      const auto binding = definition->phase_binding;
      definition->phase_binding.reset();
      const auto rejected =
          VerifyFamilyWrites(index, family, trees, {}, &lookup);
      Check(std::get<FamilyWriteFailure>(rejected).kind ==
            FamilyWriteError::kCoverage);
      definition->phase_binding = binding;
      const auto actual = definition->phase_point;
      ++definition->phase_point->commit.value;
      Check(std::get<FamilyWriteFailure>(
                VerifyFamilyWrites(index, family, trees, {}, &lookup))
                .kind == FamilyWriteError::kCoverage);
      definition->phase_point = actual;
      Take<FamilyWrites>(VerifyFamilyWrites(index, family, trees, {}, &lookup));
      ++mutations;
    }
    Check(mutations == 1);
    ++tested;
  }
  Check(tested == 2);
  std::cout << "phased class site, PC, environment, Before/Commit and budget "
               "mutations: 2 creators\n";
  std::uint64_t previous = 0;
  std::size_t previous_size = 1;
  // Exercise the same inventory/index on existing independently compiled wide
  // creators; no new source or fabricated graph becomes production evidence.
  for (const auto count : {200U, 201U, 400U, 800U}) {
    auto scale_file = Take<std::unique_ptr<corpus::AbcFile>>(
        corpus::AbcFile::Open(std::filesystem::path(argv[2]) /
                              ("width" + std::to_string(count) + ".abc")));
    auto fingerprint = Take<corpus::FileFingerprint>(scale_file->Fingerprint());
    auto scale_header = Take<abc::Header>(abc::ParseHeader(
        std::span(fingerprint.prefix).first(fingerprint.prefix_size),
        fingerprint.size));
    auto metadata = Take<abc::MetadataIndex>(
        abc::BuildMetadataIndex(*scale_file, scale_header));
    auto scale_identity = MakeRouteInputIdentity(fingerprint, scale_header);
    scale_identity.contract = identity.contract;
    unsigned found = 0;
    for (std::size_t i = 0; i < metadata.methods.size(); ++i) {
      if (RuntimeName(metadata.methods[i]) != "run") continue;
      auto normal = Take<FileFunction>(ReadFunctionValues(
          *scale_file, scale_header, metadata, {static_cast<std::uint32_t>(i)},
          scale_identity, InputCaptureMode::kConditional));
      auto inventory = Take<PhasedClassSites>(
          BuildPhasedClassSites(normal.values.graph, {}));
      Check(inventory.sources.size() == count);
      const auto size = normal.values.graph.ops.size() +
                        normal.values.graph.uses.size() +
                        normal.values.graph.values.size();
      std::cout << "inventory " << count << " inputs=" << size
                << " bytes=" << inventory.allocated_bytes
                << " work=" << inventory.work << '\n';
      // Register allocation changes the number of value/copy operations in
      // these compiled fixtures. Compare work per actual input, not class
      // count.
      Check(
          inventory.work > previous &&
          (!previous || inventory.work * previous_size < 3 * previous * size));
      previous = inventory.work;
      previous_size = size;
      const auto exact =
          analysis::SemanticBudget{inventory.allocated_bytes, inventory.work};
      Take<PhasedClassSites>(BuildPhasedClassSites(normal.values.graph, exact));
      Check(std::get<analysis::ValueHeritageError>(BuildPhasedClassSites(
                normal.values.graph, {exact.bytes, exact.work - 1})) ==
            analysis::ValueHeritageError::kWorkLimit);
      Check(std::get<analysis::ValueHeritageError>(
                BuildPhasedClassSites(normal.values.graph, {0})) ==
            analysis::ValueHeritageError::kResourceLimit);
      for (const auto& site : inventory.sources)
        Check(site.instruction.value <
                  normal.input.Input().instructions.size() &&
              normal.input.Input().instructions[site.instruction.value].pc ==
                  site.pc);
      ++found;
    }
    Check(found == 1);
  }
}
