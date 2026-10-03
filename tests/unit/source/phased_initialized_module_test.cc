#include "../../../src/source/class_tree.h"
#include "../../../src/source/phased_function_file.h"
#include "../../../src/analysis/phased_value_import.h"
#include "analysis/ssa_fixture.h"
#include <unistd.h>
#include <algorithm>
#include <sstream>

namespace {
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
constexpr std::uint32_t kPool = 64, kLiteral = 160, kKey = 4096, kCaller = 256,
    kCtor = 300, kReader = 340, kCode = 8192;
constexpr unsigned kByteBits = 8, kKindShift = 8, kUlebBits = 7;
constexpr std::uint8_t kUlebPayload = 0x7f, kContinuation = 0x80;
std::uint8_t Op(abc::Opcode op) {return static_cast<std::uint8_t>(op);}
void Uleb(std::vector<std::uint8_t>& out, std::uint32_t value) {
  do {auto byte = static_cast<std::uint8_t>(value & kUlebPayload); value >>= kUlebBits;
    out.push_back(byte | (value ? kContinuation : 0));} while (value);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> code,
    std::uint32_t locals, std::uint32_t args, bool caught = false) {
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  Uleb(bytes, locals); Uleb(bytes, args); Uleb(bytes, code.size()); Uleb(bytes, caught ? 1 : 0);
  const auto start = static_cast<std::uint32_t>(bytes.size()); bytes.insert(bytes.end(), code.begin(), code.end());
  if (caught) {Uleb(bytes, 0); Uleb(bytes, code.size() - 1); Uleb(bytes, 1);
    Uleb(bytes, 0); Uleb(bytes, code.size() - 1); Uleb(bytes, 1);}
  return {{offset}, {start}, locals, args, static_cast<std::uint32_t>(code.size()), caught ? 1U : 0U};
}
void Run(bool initialized, bool late = false, std::uint32_t count = 1) {
  std::vector<std::uint8_t> bytes(kCode);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (kByteBits * i));
  };
  word(kPool, kCtor); word(kPool + sizeof(kCtor), kLiteral);
  // An actual class member reads a local module slot inside its own catch.
  // Initializing after class creation never grants an entry premise.
  word(kLiteral, 8); auto at = kLiteral + sizeof(std::uint32_t);
  for (std::uint32_t i = 0; i < 1; ++i) {
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(at, kKey); at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod); word(at, kReader); at += sizeof(std::uint32_t);
    bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate); bytes[at++] = 1; bytes[at++] = 0;
    bytes[kKey] = 9; std::copy_n("body", 4, bytes.begin() + kKey + 1);
  }
  bytes[at++] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(at, 1);
  std::vector<std::uint8_t> caller{Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8), 0, 0, 0, 1, 0, 0, 0, 0,
      Op(abc::Opcode::kRETURN)};
  constexpr std::uint8_t kModuleSlot = 7;
  const std::uint8_t store[]{Op(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0,
      Op(abc::Opcode::kSTMODULEVAR_IMM8), kModuleSlot};
  if (initialized && !late) caller.insert(caller.begin(), std::begin(store), std::end(store));
  if (late) caller.insert(caller.end() - 1, std::begin(store), std::end(store));
  const std::uint8_t ctor[]{Op(abc::Opcode::kRETURNUNDEFINED)};
  std::vector<std::uint8_t> reader;
  for (std::uint32_t read = 0; read < count; ++read)
    reader.insert(reader.end(), {Op(abc::Opcode::kLDLOCALMODULEVAR_IMM8), kModuleSlot});
  reader.insert(reader.end(), {Op(abc::Opcode::kRETURN), Op(abc::Opcode::kRETURNUNDEFINED)});
  const abc::CodeInfo code[]{Append(bytes, caller, 1, 3), Append(bytes, ctor, 0, 3),
      Append(bytes, reader, 1, 4, true)};
  char path[] = "/tmp/abcd-class-member-gap-XXXXXX";
  auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) && close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206; header.version = {12,0,6,0}; header.file_size = bytes.size();
  abc::MetadataIndex index; index.regions.push_back({kCaller, static_cast<std::uint32_t>(bytes.size()), {}, {2, kPool}, {}, {}});
  index.methods.resize(3); const std::uint32_t offsets[]{kCaller, kCtor, kReader};
  for (std::size_t i = 0; i < 3; ++i) {
    auto& method = index.methods[i]; method.offset = {offsets[i]}; method.owner_offset = {128}; method.code = code[i];
    method.name = i == 1 ? "Known" : "body"; method.name_utf16_length = method.name.size();
    method.access_flags = static_cast<std::uint32_t>(i < 2 ? abc::FunctionKind::kBaseConstructor : abc::FunctionKind::kNone) << kKindShift;
  }
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(*file, header, index, lookup));
  const auto functions = std::get<FunctionCreationSites>(FunctionCreationSites::Build(*file, header, index, lookup));
  const auto classes = std::get<ClassMethodSites>(ClassMethodSites::Build(*file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file->Fingerprint()), header);
  const auto creator = std::get<FileFunction>(ReadFunctionValues(*file, header, index, {0}, identity, InputCaptureMode::kClosed));
  const auto creation = creator.values.classes.classes.front().operation;
  const auto occurrence = classes.Find({kReader}); test::Check(occurrence.count == 1);
  const FunctionContext context{std::get<ClassTreeContext>(ClassTreeContext::Resolve(
      *file, header, index, lookup, roles, classes, occurrence.first, identity))};
  const ParentModuleInput parent{creator.input, {creator.values.graph.origins[
      creator.values.graph.ops[creation.value].origin.value].site.value}};
  auto result = FilePhasedFunction::Read(*file, header, index, context, {}, nullptr,
      InputCaptureMode::kConditional, &parent);
  if (!initialized || late) test::Check(std::holds_alternative<PhasedFileFailure>(result));
  else {
    test::Check(std::holds_alternative<FilePhasedFunction>(result));
    const auto& child = std::get<FilePhasedFunction>(result);
    test::Check(child.InitializedModuleReads().size() == count);
    test::Check(std::holds_alternative<std::uint64_t>(VerifyInitializedModuleReads(
        parent.parent, parent.creation, child)));
    const auto& graph = child.Phases().Graph();
    const auto phases = child.Phases().TreeInput(child.Body()).input;
    auto guards = std::get<ModuleGuards>(BuildPhasedModuleGuards(phases, graph));
    test::Check(guards.pairs.size() == count);
    for (const auto& read : guards.pairs) test::Check(!read.guard);
    std::vector<ModuleEvaluation> bindings;
    for (const auto& read : guards.pairs)
      bindings.push_back({read, std::get<ValueModuleRead>(graph.ops[read.read.value].operation), {{1}, {7}}});
    const auto evaluation = BuildPhasedEvaluationPlan(graph, {}, IncomingCells::kReject, {}, bindings);
    test::Check(std::holds_alternative<EvaluationPlan>(evaluation));
    auto altered_proof = phases.proof; altered_proof.initialized_modules = {};
    const PhasedValueInput altered{phases.flow, phases.ssa, phases.environments, phases.values, altered_proof, phases.frame};
    test::Check(std::holds_alternative<ValueImportFailure>(VerifyImportedPhases(graph, altered)));
    const auto work = std::get<std::uint64_t>(VerifyInitializedModuleReads(parent.parent, parent.creation, child));
    test::Check(std::holds_alternative<FunctionInputError>(VerifyInitializedModuleReads(
        parent.parent, parent.creation, child, {SemanticBudget{}.bytes, work - 1})));
    guards.pairs.pop_back();
    test::Check(std::holds_alternative<ModuleGuardError>(VerifyPhasedModuleGuards(phases, graph, guards)));
    test::Check(std::holds_alternative<PhasedFileFailure>(FilePhasedFunction::Read(*file, header, index,
        context, {}, nullptr, InputCaptureMode::kClosed, &parent)));
    auto missing = parent; missing.creation = {0};
    test::Check(std::holds_alternative<PhasedFileFailure>(FilePhasedFunction::Read(*file, header, index,
        context, {}, nullptr, InputCaptureMode::kConditional, &missing)));
  }
  test::Check(unlink(path) == 0);
}
}
int main() {Run(true); Run(false); Run(true, true); Run(true, false, 200); Run(true, false, 201);}
