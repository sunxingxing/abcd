#include "../../../src/source/default_candidates.h"
#include "../../../src/source/default_value_proof.h"
#include "../../../src/source/class_self_initialization_file.h"
#include "analysis/ssa_fixture.h"

#include <charconv>
#include <algorithm>
#include <iostream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
template <class T, class E>
T Take(std::variant<T, E> result) {
  test::Check(std::holds_alternative<T>(result));
  return std::get<T>(std::move(result));
}
void Run(const char* path, abc::EntityOffset requested) {
  auto file = Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto fingerprint = Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  auto identity = MakeRouteInputIdentity(fingerprint, header);
  identity.contract = SourceContract::kLanguageSourceV5;
  const auto index = Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  const auto lookup = Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto position = lookup.Find(requested);
  test::Check(position.has_value());
  const auto& method = index.methods[position->value];
  const auto record = std::find_if(index.records.begin(), index.records.end(),
      [&](const auto& r) { return r.offset == method.owner_offset; });
  test::Check(record != index.records.end());
  const auto packaging = Take<abc::FilePackaging>(abc::DetectFilePackaging(*file, header, index));
  const auto module = Take<RecordModuleContext>(
      ReadRecordModuleContext(*file, header, index, *record, packaging));
  const auto function = Take<FileFunction>(ReadFunctionValues(*file, header, index,
      *position, identity, InputCaptureMode::kConditional, {}, &module));
  const auto& graph = function.values.graph;
  // This fixture deliberately has a real guarded import-property default,
  // followed by four constants and a global-property default. Refusal must
  // not silently replace either effectful initializer with its base binding.
  test::Check(function.frame.parameters == 7);
  const auto parameters = Take<ParameterPlan>(BuildParameterPlan(graph, function.frame.parameters));
  const auto candidates = Take<DefaultCandidates>(FindDefaultCandidates(graph, parameters, 1));
  test::Check(candidates.values.empty() && !candidates.capture);
  test::Check(std::get<DefaultProofError>(DefaultValueProof::Build(
      graph, parameters, candidates, 1)) == DefaultProofError::kParameter);
  test::Check(std::get<DefaultCandidateError>(FindDefaultCandidates(
      graph, parameters, 1, {0, SemanticBudget{}.work})) == DefaultCandidateError::kResourceLimit);
  test::Check(std::get<DefaultCandidateError>(FindDefaultCandidates(
      graph, parameters, 1, {SemanticBudget{}.bytes, 0})) == DefaultCandidateError::kWorkLimit);
  const auto exact = Take<DefaultCandidates>(FindDefaultCandidates(
      graph, parameters, 1, {SemanticBudget{}.bytes, candidates.work}));
  test::Check(exact.values.empty() && !exact.capture);
  test::Check(candidates.work > 0);
  test::Check(std::get<DefaultCandidateError>(FindDefaultCandidates(
      graph, parameters, 1, {SemanticBudget{}.bytes, candidates.work - 1})) ==
      DefaultCandidateError::kWorkLimit);
  bool module_read = false, guard = false, property = false, global = false;
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    const auto& op = graph.ops[i];
    if (op.life != ValueLife::kLive) continue;
    module_read |= std::holds_alternative<ValueModuleRead>(op.operation);
    guard |= std::holds_alternative<ValueGuard>(op.operation);
    property |= std::holds_alternative<ValueNamedRead>(op.operation);
    global |= std::holds_alternative<ValueGlobalRead>(op.operation) ||
        std::holds_alternative<ValueGlobalObject>(op.operation);
    std::cout << "op" << i << " PC" << graph.origins[op.origin.value].pc.value
              << " B" << op.block.value << " variant=" << op.operation.index();
    if (op.result) std::cout << " -> v" << op.result->value;
    for (std::uint32_t n = 0; n < op.use_count; ++n) {
      const auto id = op.first_use.value + n;
      const auto& use = graph.uses[id];
      std::cout << " Use" << id << ":" << static_cast<unsigned>(use.role);
      if (const auto* value = std::get_if<ValueId>(&use.operand)) std::cout << "=v" << value->value;
    }
    std::cout << '\n';
  }
  test::Check(module_read && guard && property && global);
  for (std::size_t i = 0; i < graph.values.size(); ++i) {
    if (const auto* phi = std::get_if<PhiDefinition>(&graph.values[i].origin)) {
      std::cout << "phi v" << i << " B" << phi->block.value << '\n';
      for (auto id = graph.values[i].first_use; id; id = graph.uses[id->value].next)
        std::cout << "  consumer Use" << id->value << '\n';
    }
  }
  std::cout << "effectful parameter-expression boundary retained; declared length 1, formals 7\n";
}
}  // namespace
int main(int argc, char** argv) {
  test::Check(argc == 3);
  std::uint32_t method = 0;
  const std::string_view arg{argv[2]};
  const auto number = std::from_chars(arg.data(), arg.data() + arg.size(), method);
  test::Check(number.ec == std::errc{} && number.ptr == arg.data() + arg.size());
  Run(argv[1], {method});
}
