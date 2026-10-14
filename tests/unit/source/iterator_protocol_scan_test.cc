#include "../../../src/source/iterator_protocol_scan.h"
#include "abcd/analysis/exception_locals.h"
#include "abcd/corpus/abc_file.h"
#include "analysis/ssa_fixture.h"
#include <algorithm>
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;

namespace {
void Keys(const IteratorKeyInput& file, abc::EntityOffset method, const ValueProgram& original) {
  auto result = ResolveIteratorKeys(file, method, original);
  Check(std::holds_alternative<IteratorKeys>(result));
  const auto& keys = std::get<IteratorKeys>(result);
  for (const auto role : {IteratorKey::kNext, IteratorKey::kDone, IteratorKey::kValue, IteratorKey::kReturn})
    Check(std::any_of(keys.origins.begin(), keys.origins.end(), [&](const auto& v) { return v.key == role; }));
  Check(std::get<IteratorKeyError>(ResolveIteratorKeys(file, method, original, {0})) == IteratorKeyError::kResourceLimit);
  Check(std::get<IteratorKeyError>(ResolveIteratorKeys(file, method, original, {64 * 1024 * 1024, 0})) == IteratorKeyError::kWorkLimit);
  ValueProgram graph;
  for (const unsigned count : {200, 201, 400, 800}) {
    graph.ops.clear();
    const auto read = original.ops[keys.uses.front().operation.value];
    for (unsigned n = 0; n < count; ++n) graph.ops.push_back(read);
    const auto scaled = std::get<IteratorKeys>(ResolveIteratorKeys(file, method, graph));
    Check(scaled.uses.size() == count && scaled.origins.size() == 1);
    std::size_t low = 0, high = scaled.allocated_bytes;
    while (low < high) {
      const auto mid = low + (high - low) / 2;
      const auto trial = ResolveIteratorKeys(file, method, graph, {mid});
      if (std::holds_alternative<IteratorKeys>(trial)) high = mid; else low = mid + 1;
    }
    Check(std::holds_alternative<IteratorKeys>(ResolveIteratorKeys(file, method, graph, {low})));
    Check(low && !std::holds_alternative<IteratorKeys>(ResolveIteratorKeys(file, method, graph, {low - 1})));
    std::cout << "keys " << count << " budget " << low << " work " << scaled.work << '\n';
  }
  graph = original;
  std::get<ValueNamedRead>(graph.ops[keys.uses.front().operation.value].operation).name = {UINT32_MAX};
  Check(std::holds_alternative<abc::MetadataError>(ResolveIteratorKeys(file, method, graph)));
}
void Method(const IteratorKeyInput& file, const abc::MethodInfo& method) {
  const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(file.file, file.header.profile, *method.code));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(method.code->registers, method.code->arguments, {}));
  const auto flow = std::get<ExceptionFlow>(BuildExceptionFlow(body, file.header.profile, frame.registers, {}, {}, &frame));
  IdfQuota quota{kDefaultSemanticWork};
  const auto ssa = std::get<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
  std::vector<EntryValueFact> entries;
  for (std::uint32_t v = 0; v < ssa.Values().size(); ++v) {
    const auto& value = ssa.Values()[v];
    if (std::holds_alternative<MethodEntry>(value.origin))
      if (const auto* reg = std::get_if<abc::RegisterId>(&value.slot); reg && reg->value >= method.code->registers)
        entries.push_back({{v}, ValueKinds::Language()});
  }
  const auto env = std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto values = std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow, ssa, entries, quota, {},
      PendingExceptionValues::kLanguage, {}, {}, CallResultValues::kLanguage, FrameReceiver::kInitialized));
  const auto proof = std::get<ExceptionInputProof>(VerifyExceptionInputs(flow, ssa, env, values,
      kDefaultSemanticWork, nullptr, ExceptionInputDomain::kHoleTransport));
  const PhasedValueInput input{flow, ssa, env, values, proof, frame};
  const auto graph = std::get<ValueProgram>(ImportPhasedValues(input));
  const auto regions = std::get<ExceptionPlan>(BuildExceptionPlan(body, graph, input));
  const auto keys = std::get<IteratorKeys>(ResolveIteratorKeys(file, method.offset, graph));
  Keys(file, method.offset, graph);
  const auto result = ScanIteratorProtocols(graph, input, regions, keys);
  if (const auto* e = std::get_if<IteratorScanFailure>(&result)) std::cerr << "scan " << unsigned(e->kind) << '\n';
  Check(std::holds_alternative<IteratorProtocolScan>(result));
  const auto scan = std::get<IteratorProtocolScan>(result);
  Check(!scan.candidates.empty() && !scan.regions.empty());
  Check(std::holds_alternative<std::uint64_t>(VerifyIteratorCandidates(graph, input, regions, keys, scan)));
  const auto reject = [&](const auto& mutation) {
    Check(std::holds_alternative<IteratorScanFailure>(VerifyIteratorCandidates(graph, input, regions, keys, mutation)));
  };
  auto changed = scan; changed.candidates[0].iterator = {UINT32_MAX}; reject(changed);
  changed = scan; changed.candidates[0].next = OpId{UINT32_MAX}; reject(changed);
  changed = scan; changed.candidates[0].acquisition.working_pc.value++; reject(changed);
  changed = scan; changed.regions[0].region = TryId{UINT32_MAX}; reject(changed);
  changed = scan; changed.regions.clear(); reject(changed);
  changed = scan; changed.regions.push_back(scan.regions[0]); reject(changed);
  changed = scan; changed.candidates.pop_back(); reject(changed);
  for (const auto obligation : {IteratorObligation::kCloseGuard, IteratorObligation::kCompletionPriority,
      IteratorObligation::kNestedClose, IteratorObligation::kClosedUse}) {
    changed = scan;
    changed.candidates[0].obligations[static_cast<unsigned>(obligation)] = IteratorEvidenceState::kVerified;
    reject(changed);
  }
  auto forged = graph;
  forged.exceptional_exits.erase(forged.exceptional_exits.begin());
  Check(std::holds_alternative<IteratorScanFailure>(VerifyIteratorCandidates(forged, input, regions, keys, scan)));
  Check(std::get<IteratorScanFailure>(ScanIteratorProtocols(graph,input,regions,keys,{0})).kind == IteratorScanError::kResourceLimit);
  Check(std::get<IteratorScanFailure>(ScanIteratorProtocols(graph,input,regions,keys,{64*1024*1024,0})).kind == IteratorScanError::kWorkLimit);
  std::size_t low = 0, high = scan.allocated_bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    if (std::holds_alternative<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,regions,keys,{mid}))) high = mid;
    else low = mid + 1;
  }
  Check(std::holds_alternative<IteratorProtocolScan>(ScanIteratorProtocols(graph,input,regions,keys,{low})));
  Check(low && std::holds_alternative<IteratorScanFailure>(ScanIteratorProtocols(graph,input,regions,keys,{low-1})));
  std::cout << "method " << method.offset.value << " acquisitions " << scan.candidates.size() << " region associations " << scan.regions.size() << '\n';
}
}
int main(int argc, char** argv) {
  Check(argc == 2);
  const auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
  const auto hash = std::get<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned methods = 0;
  for (const auto& method : index.methods) {
    if (!method.code) continue;
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(*file, header.profile, *method.code));
    if (std::none_of(body.instructions.begin(), body.instructions.end(), [](const auto& i) {
          return i.decoded.opcode == abc::Opcode::kGETITERATOR_IMM8 || i.decoded.opcode == abc::Opcode::kGETITERATOR_IMM16;
        })) continue;
    ++methods; Method({*file, header, index}, method);
  }
  Check(methods == 3);
}
