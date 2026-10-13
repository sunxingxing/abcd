#include "../../src/source/function_values.h"
#include "../../src/source/function_context.h"
#include "../../src/source/function_tree.h"
#include "../../src/source/function_abi.h"
#include "../../src/source/instruction_sources.h"
#include "../../src/source/tree_semantics.h"
#include "../../src/analysis/value_edits.h"
#include "../../src/analysis/value_flow.h"
#include "abcd/source/tree_printer.h"
#include "analysis/ssa_fixture.h"

#include <array>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256, kTarget = 300, kCode = 400;
constexpr std::uint32_t kLocals = 4, kImplicitArguments = 3;
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  constexpr unsigned kPayloadBits = 7;
  constexpr std::uint8_t kPayloadMask = 0x7f, kContinuation = 0x80;
  do {
    auto byte = static_cast<std::uint8_t>(value & kPayloadMask); value >>= kPayloadBits;
    bytes.push_back(value ? byte | kContinuation : byte);
  } while (value);
}
FunctionContext Owner(const std::vector<std::uint8_t>& code) {
  std::vector<std::uint8_t> bytes(kCode);
  for (unsigned i = 0; i < sizeof(kTarget); ++i)
    bytes[kPool + i] = static_cast<std::uint8_t>(kTarget >> (kByteBits * i));
  const auto append = [&](std::span<const std::uint8_t> body, std::uint32_t locals, std::uint32_t args) {
    const auto offset = static_cast<std::uint32_t>(bytes.size());
    Uleb(bytes, locals); Uleb(bytes, args); Uleb(bytes, body.size()); Uleb(bytes, 0);
    const auto start = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), body.begin(), body.end());
    return abc::CodeInfo{{offset}, {start}, locals, args, static_cast<std::uint32_t>(body.size()), 0};
  };
  const std::array caller{
      static_cast<std::uint8_t>(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), std::uint8_t{0},
      std::uint8_t{0}, std::uint8_t{0}, std::uint8_t{1}, static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)};
  const auto caller_code = append(caller, 0, 0);
  const auto target_code = append(code, kLocals, kImplicitArguments + 1);
  char path[] = "/tmp/abcd-literal-intrinsic-XXXXXX";
  const auto fd = mkstemp(path); test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.layout = abc::DynamicLayout{};
  header.profile = abc::ProfileId::kDynamic1206; header.version = {12, 0, 6, 0}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCode, {}, {1, kPool}, {}, {}});
  index.methods.resize(2);
  index.methods[0].offset = {kCaller}; index.methods[0].code = caller_code;
  index.methods[1].offset = {kTarget}; index.methods[1].code = target_code;
  for (auto& method : index.methods) method.owner_offset = {kRecord};
  index.methods[1].name = "recovered"; index.methods[1].name_utf16_length = index.methods[1].name.size();
  constexpr unsigned kFunctionKindShift = 8;
  index.methods[1].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  const auto sites = std::get<FunctionCreationSites>(FunctionCreationSites::Build(file, header, index, lookup));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  auto context = CreationTreeContext::Resolve(file, header, index, lookup, roles, sites, sites.Find({kTarget}).first, identity);
  test::Check(std::holds_alternative<CreationTreeContext>(context));
  test::Check(unlink(path) == 0);
  return FunctionContext{std::get<CreationTreeContext>(std::move(context))};
}
std::uint64_t Run(std::uint32_t count, bool print, bool copies = false, bool barrier = false) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 0};
  const auto store = [&](std::uint32_t key, std::uint8_t object) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8), 0,
        static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(key >> kByteBits), object});
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    bytes.insert(bytes.end(), {op(abc::Opcode::kCREATEEMPTYOBJECT), op(abc::Opcode::kSTA_V8), 1,
        op(abc::Opcode::kLDTHIS)});
    if (copies) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 2, op(abc::Opcode::kNOP), op(abc::Opcode::kLDA_V8), 2});
    store(0, 1);
    bytes.push_back(op(abc::Opcode::kLDFUNCTION));
    if (copies) bytes.insert(bytes.end(), {op(abc::Opcode::kSTA_V8), 3, op(abc::Opcode::kNOP), op(abc::Opcode::kLDA_V8), 3});
    store(1, 1);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDTHIS), op(abc::Opcode::kSTA_V8), 2,
        op(abc::Opcode::kLDFUNCTION), op(abc::Opcode::kSTA_V8), 3,
        op(abc::Opcode::kLDA_V8), kLocals + kImplicitArguments, op(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, 2, 3});
    if (barrier && i == 0) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    store(2, 1);
    bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 1});
    store(i + 3, 0);
  }
  bytes.insert(bytes.end(), {op(abc::Opcode::kLDA_V8), 0, op(abc::Opcode::kRETURN)});
  const auto body = test::Body(bytes);
  const auto owner = Owner(bytes);
  const auto& frame = owner.Frame(); const auto identity = owner.Identity();
  const auto prepared = FunctionInput::Build(identity, {kTarget}, {kRecord}, body, frame, InputCaptureMode::kClosed, {}, &owner);
  test::Check(std::holds_alternative<FunctionInput>(prepared));
  const auto& input = std::get<FunctionInput>(prepared);
  const auto imported = BuildFunctionValues(input, body, frame);
  test::Check(std::holds_alternative<FunctionValues>(imported));
  const auto& values = std::get<FunctionValues>(imported);
  const auto& graph = values.graph;
  const auto built_plan = BuildEvaluationPlan(graph);
  if (barrier) { test::Check(std::holds_alternative<EvaluationFailure>(built_plan)); return 0; }
  test::Check(std::holds_alternative<EvaluationPlan>(built_plan));
  const auto& plan = std::get<EvaluationPlan>(built_plan);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph, plan)));
  test::Check(plan.literals && plan.literals->objects.size() == count + 1);
  for (std::size_t i = 0; i < graph.ops.size(); ++i) {
    if (graph.ops[i].life != ValueLife::kLive) continue;
    if (std::holds_alternative<ValueFrameThis>(graph.ops[i].operation) ||
        std::holds_alternative<ValueFrameFunction>(graph.ops[i].operation)) {
      test::Check(plan.operations[i].kind == EvaluationKind::kNestedLiteral);
      if (count == 1) {
        auto shared = graph;
        for (const auto& node : graph.ops) if (std::holds_alternative<ValueReturn>(node.operation))
          test::Check(!ReplaceUse(shared, node.first_use, *graph.ops[i].result));
        test::Check(std::holds_alternative<ValueFlowProof>(VerifyValueFlow(shared)));
        test::Check(std::holds_alternative<LiteralError>(BuildLiteralPlan(shared)));
        auto forged = plan; forged.literals->nested[i].reset();
        test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph, forged)));
      }
    }
  }
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = input.Method();
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.names = {u"receiver", u"self", u"effect", u"probe", u"recovered"};
  syntax.inferred_name = NameId{4}; syntax.parameters = {{NameId{3}, NameEvidence::kDebugRange}};
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto name = std::string{"field"} + std::to_string(i);
    syntax.names.emplace_back(name.begin(), name.end());
  }
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for (std::size_t i = 0; i < graph.ops.size(); ++i)
    if (const auto* field = std::get_if<ValueOwnWrite>(&graph.ops[i].operation))
      constants[i] = NameId{field->name.value < 3 ? field->name.value : field->name.value + 2};
  const ExpressionConstants resolved{constants, syntax.names.size(), 0};
  const auto built = BuildFunctionTree(graph, control, plan, std::move(syntax), resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree = std::get<ConstructedFunction>(built).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph, control, plan, tree, resolved)));
  test::Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(graph, tree, frame, 1)));
  test::Check(std::holds_alternative<InstructionSources>(BuildInstructionSources(input, graph, tree)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  if (print) std::cout << output.str();
  return plan.work;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view option{argv[1]};
    test::Check(option == "--source" || option == "--copy-source");
    Run(2, true, option == "--copy-source"); return 0;
  }
  for (const bool copies : {false, true}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U, 2U, 200U, 201U, 402U}) {
      const auto work = Run(count, false, copies);
      if (count > 201) test::Check(work < 3 * previous);
      previous = work;
    }
  }
  Run(1, false, false, true);
}
