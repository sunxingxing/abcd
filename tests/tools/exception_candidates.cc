#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/module_enclosure.h"
#include "abcd/source/function_emission.h"
#include "../../tools/tree_position_json.h"
#include "../../src/source/phased_method.h"
#include "../../src/source/module_aliases.h"
#include "../../src/source/syntax_resolution.h"

namespace {
int Inspect(const abcd::corpus::AbcFile& file, const abcd::abc::Header& header,
    const abcd::abc::MetadataIndex& index, const abcd::abc::MethodLookup& lookup,
    const abcd::analysis::ClassMethodSites& sites, const abcd::corpus::FileFingerprint& hash,
    abcd::abc::EntityOffset entity, bool emit) {
  using namespace abcd;
  using namespace abcd::source;
  const auto position = lookup.Find(entity);
  if (!position || sites.Find(entity).count != 1) return 1;
  const auto& method = index.methods[position->value];
  const auto owner = std::find_if(index.records.begin(), index.records.end(),
      [&](const auto& record) { return record.offset == method.owner_offset; });
  if (owner == index.records.end() || !method.code) return 1;
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  const auto* packed = std::get_if<abc::FilePackaging>(&packaging);
  if (!packed) return 1;
  const auto modules = ReadRecordModuleContext(file, header, index, *owner, *packed);
  const auto* record = std::get_if<RecordModuleContext>(&modules);
  if (!record) return 1;
  const auto roles = analysis::MethodRoleIndex::Build(file, header, index, lookup);
  const auto* indexed_roles = std::get_if<analysis::MethodRoleIndex>(&roles);
  if (!indexed_roles) return 1;
  auto selected = ClassTreeContext::Resolve(file, header, index, lookup, *indexed_roles,
      sites, sites.Find(entity).first, MakeRouteInputIdentity(hash, header));
  const auto* ready = std::get_if<ClassTreeContext>(&selected);
  if (!ready) return 1;
  const FunctionContext context{std::move(std::get<ClassTreeContext>(selected))};
  const auto decoded = abc::DecodeMethodBody(file, header.profile, *method.code);
  const auto* body = std::get_if<abc::MethodBody>(&decoded);
  if (!body) return 1;
  // A single explicitly selected method, not record publication or a corpus
  // scan. Its module owner is local to this diagnostic invocation.
  constexpr ModuleOwnerId kSelectedRecord{0};
  if (emit) {
    const auto identity = MakeRouteInputIdentity(hash, header);
    auto planned = ModuleEnclosure::Build(file, header, *record, identity, kSelectedRecord);
    const auto* enclosure = std::get_if<ModuleEnclosure>(&planned);
    if (!enclosure) { std::cerr << "module enclosure error " << static_cast<unsigned>(std::get<ModuleEnclosureError>(planned)) << '\n'; return 1; }
    GlobalScope scope{}; scope.module_enclosure = enclosure;
    auto prepared = VerifiedFunction::PrepareClass(file, header, index, lookup, *indexed_roles,
        sites, sites.Find(entity).first, identity, {}, &scope);
    if (const auto* error = std::get_if<FunctionPrepareFailure>(&prepared)) {
      std::cerr << "preparation error " << static_cast<unsigned>(error->kind) << '\n'; return 1;
    }
    std::ostringstream output, imports, mapping;
    if (!std::holds_alternative<std::size_t>(enclosure->WriteImports(file, header, imports))) return 1;
    auto printed = FunctionEmission::Write(std::get<VerifiedFunction>(std::move(prepared)), output, {}, {}, {}, &scope);
    const auto* emission = std::get_if<FunctionEmission>(&printed);
    if (!emission) { std::cerr << "print error " << static_cast<unsigned>(std::get<TreePrintError>(printed)) << '\n'; return 1; }
    tools::WriteTreePositions(mapping, emission->Function().Tree(), emission->Positions());
    std::cout << nlohmann::json{{"method", entity.value}, {"record_complete", false},
        {"fragment_complete", emission->Receipt().coverage == Coverage::kComplete},
        {"source", output.str()}, {"imports", imports.str()},
        {"positions", nlohmann::json::parse(mapping.str())}} << '\n';
    return file.CheckUnchanged() ? 1 : 0;
  }
  const PhasedModuleBinding binding{file, header, index, *record, kSelectedRecord};
  const auto built = PhasedMethod::Build(context, *body, {}, &binding);
  nlohmann::json report{{"method", entity.value}, {"source_complete", false}, {"executable", false}};
  if (const auto* error = std::get_if<PhasedMethodError>(&built)) {
    report["phase_error"] = static_cast<unsigned>(*error);
  } else {
    const auto& phased = std::get<PhasedMethod>(built);
    report["phased_ready"] = true;
    report["module_aliases"] = phased.Modules() ? phased.Modules()->aliases.size() : 0;
    auto resolved = ResolveFunctionSyntax(file, header, index, context, *body, phased.Graph(), {0});
    if (auto* syntax = std::get_if<ResolvedSyntax>(&resolved)) {
      if (phased.Modules()) {
        const auto first = static_cast<std::uint32_t>(syntax->syntax.names.size());
        syntax->syntax.names.insert(syntax->syntax.names.end(), phased.Modules()->names.begin(), phased.Modules()->names.end());
        syntax->syntax.modules = phased.Modules()->aliases;
        for (auto& alias : syntax->syntax.modules) alias.name.value += first;
      }
      const auto constants = syntax->View();
      const auto tree = BuildExceptionFunctionDraft(phased.Graph(), phased.TreeInput(*body),
          phased.Evaluation(), std::move(syntax->syntax), constants);
      if (const auto* error = std::get_if<FunctionTreeError>(&tree)) report["tree_error"] = static_cast<unsigned>(*error);
      else {
        const auto& draft = std::get<ExceptionFunctionDraft>(tree);
        if (phased.Modules()) {
          const auto verified = VerifyModuleAliasTree(*phased.Modules(), phased.Evaluation(), draft.tree);
          if (const auto* error = std::get_if<ModuleAliasTreeError>(&verified))
            report["module_tree_error"] = static_cast<unsigned>(*error);
          else report["tree_ready"] = true;
        } else report["tree_ready"] = true;
      }
    } else report["syntax_error"] = std::get<SyntaxResolutionFailure>(resolved).index();
  }
  std::cout << report << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
}  // namespace

// Candidate discovery or one explicit method's phase diagnostic. Not a corpus
// decompilation/support census. No source/sidecar inputs are read.
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc < 2 || argc > 4 || (argc == 4 && std::string_view(argv[3]) != "--emit")) return 1;
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto* owner = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owner) return 1;
  const auto& file = **owner;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return 1;
  const auto parsed = abc::ParseHeader(std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->kind != abc::AbcKind::kDynamic || header->checksum != hash->adler32) return 1;
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return 1;
  const auto lookup = abc::MethodLookup::Build(index->methods);
  const auto* methods = std::get_if<abc::MethodLookup>(&lookup);
  if (!methods) return 1;
  const auto bound = analysis::ClassMethodSites::Build(file, *header, *index, *methods);
  const auto* sites = std::get_if<analysis::ClassMethodSites>(&bound);
  if (!sites) return 1;
  if (argc >= 3) {
    const std::string_view text{argv[2]};
    abc::EntityOffset entity;
    const auto parsed_id = std::from_chars(text.data(), text.data() + text.size(), entity.value);
    if (parsed_id.ec != std::errc{} || parsed_id.ptr != text.data() + text.size()) return 1;
    return Inspect(file, *header, *index, *methods, *sites, *hash, entity, argc == 4);
  }
  std::vector<const abc::MethodInfo*> candidates;
  for (const auto& method : index->methods)
    if (method.code && method.code->try_count && sites->Find(method.offset).count == 1)
      candidates.push_back(&method);
  std::sort(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
    return left->code->instruction_bytes < right->code->instruction_bytes;
  });
  constexpr std::size_t kCandidateLimit = 20;
  if (candidates.size() > kCandidateLimit) candidates.resize(kCandidateLimit);
  for (const auto* method : candidates)
    std::cout << nlohmann::json{{"entity", method->offset.value}, {"name", method->name},
        {"bytes", method->code->instruction_bytes}, {"tries", method->code->try_count}} << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
