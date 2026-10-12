#include "../../src/source/function_tree.h"
#include "../../src/source/syntax_resolution.h"
#include "../../src/source/function_abi.h"
#include "../../src/source/implicit_constructor.h"
#include "analysis/ssa_fixture.h"
#include <climits>
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
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  // Actual EntryAbility creation supplies the owner. Standalone constructor
  // import cannot certify this invocation's receiver or callable identity.
  const auto creator_position = lookup.Find({9460}); Check(creator_position.has_value());
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
  Check(context.Target().value == 9544 && context.Callable() == CallableKind::kDerivedConstructor);
  const auto position = lookup.Find(context.Target()); Check(position.has_value());
  auto loaded = ReadFunctionValues(file, header, index, *position, identity,
      InputCaptureMode::kClosed, {}, nullptr, &context);
  Check(std::holds_alternative<FileFunction>(loaded));
  auto& function = std::get<FileFunction>(loaded);
  std::cerr << "frame parameters=" << function.frame.parameters << " extra=" << function.frame.extra_arguments << " length=" << context.Length() << '\n';
  const auto fixture = [&](std::vector<std::uint8_t> bytes, bool accept) {
    auto body = test::Body(bytes);
    auto built = FunctionInput::Build(identity, context.Target(), function.input.Record(), body,
        function.frame, InputCaptureMode::kClosed, {}, &context);
    Check(std::holds_alternative<FunctionInput>(built));
    auto input = std::get<FunctionInput>(std::move(built));
    auto values = BuildFunctionValues(input, body, function.frame);
    Check(std::holds_alternative<FunctionValues>(values));
    FileFunction candidate{std::move(body), function.frame, std::move(input), std::get<FunctionValues>(std::move(values))};
    const auto proof = ProveImplicitConstructor(candidate);
    if (accept && std::holds_alternative<DerivedProjectionError>(proof))
      std::cerr << "fixture proof error " << static_cast<unsigned>(std::get<DerivedProjectionError>(proof)) << '\n';
    Check(std::holds_alternative<ImplicitConstructorProof>(proof) == accept);
    if (accept) {
      const auto projected = ProjectDerivedConstructor(candidate, context);
      Check(std::holds_alternative<DerivedProjection>(projected));
      const auto& protocol = std::get<DerivedProjection>(projected);
      const auto control = std::get<ControlPlan>(BuildControlPlan(candidate.values.graph));
      const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(candidate.values.graph, {}, IncomingCells::kReject,
          {}, {}, {}, {}, analysis::ClosureWrites::kUnknown, {}, protocol.guards));
      auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, context,
          candidate.body, candidate.values.graph, {0}));
      auto constants = syntax.View(); constants.receiver_guards = protocol.guards; constants.implicit_constructor = &candidate;
      const auto built = BuildFunctionTree(candidate.values.graph, control, evaluation, std::move(syntax.syntax), constants);
      if (const auto* error = std::get_if<FunctionTreeFailure>(&built)) std::cerr << "fixture tree error " << static_cast<unsigned>(*error) << '\n';
      Check(std::holds_alternative<ConstructedFunction>(built));
      const auto& tree = std::get<ConstructedFunction>(built).tree;
      Check(tree.ImplicitConstructor());
      Check(std::holds_alternative<ConstructedAbi>(BuildFunctionAbi(candidate.values.graph, tree, candidate.frame, context.Length())));
    }
  };
  const auto emit = [](std::vector<std::uint8_t>& bytes, abc::Opcode op) {
    const auto wire = static_cast<std::uint16_t>(op); bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> CHAR_BIT));
  };
  const auto constructor = static_cast<std::uint8_t>(function.frame.function->value);
  const auto parameter = static_cast<std::uint8_t>(function.frame.first_parameter.value);
  // Retain the real same-file owner while replacing only the decoded test
  // body. This checks input/current-IR projection, not native parent-brand or
  // whole-class publication. All layouts use the same common protocol proof.
  std::size_t initialization_cases = 0;
  for (const bool spread : {false, true}) for (const bool after : {false, true})
    for (const bool guards : {false, true}) for (const unsigned count : {0U, 1U, 200U, 201U, 402U}) {
      std::vector<std::uint8_t> bytes;
      const auto jump = [&](abc::Opcode opcode) {
        const auto pc = bytes.size(); emit(bytes, opcode);
        bytes.insert(bytes.end(), sizeof(std::int32_t), 0); return pc;
      };
      const auto target = [&](std::size_t pc, std::size_t label) {
        const auto delta = static_cast<std::uint32_t>(label - pc);
        for (std::size_t b = 0; b < sizeof(delta); ++b)
          bytes[pc + 1 + b] = static_cast<std::uint8_t>(delta >> (CHAR_BIT * b));
      };
      const auto loop = [&] {
        const auto begin = bytes.size();
        emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(parameter);
        const auto exit = jump(abc::Opcode::kJEQZ_IMM32);
        for (unsigned i = 0; i < count; ++i) emit(bytes, abc::Opcode::kNOP);
        const auto back = jump(abc::Opcode::kJMP_IMM32);
        target(back, begin); target(exit, bytes.size());
      };
      if (!after) loop();
      if (guards) {
        emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(function.frame.this_value->value);
        emit(bytes, abc::Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8); bytes.push_back(1);
      }
      if (spread) {
        emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(constructor);
        emit(bytes, abc::Opcode::kSUPERCALLSPREAD_IMM8_V8); bytes.insert(bytes.end(), {0, parameter});
      } else {
        emit(bytes, abc::Opcode::kSUPERCALLTHISRANGE_IMM8_IMM8_V8); bytes.insert(bytes.end(), {0, 1, parameter});
      }
      emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);
      if (guards) { emit(bytes, abc::Opcode::kTHROW_IFSUPERNOTCORRECTCALL_PREF_IMM8); bytes.push_back(0); }
      if (after) loop();
      emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(0); emit(bytes, abc::Opcode::kRETURN);
      auto body = test::Body(bytes);
      auto input = std::get<FunctionInput>(FunctionInput::Build(identity, context.Target(), function.input.Record(),
          body, function.frame, InputCaptureMode::kClosed, {}, &context));
      auto values = std::get<FunctionValues>(BuildFunctionValues(input, body, function.frame));
      FileFunction candidate{std::move(body), function.frame, std::move(input), std::move(values)};
      const auto projected = ProjectDerivedConstructor(candidate, context);
      if (const auto* error = std::get_if<DerivedProjectionError>(&projected))
        std::cerr << "initialization projection error " << static_cast<unsigned>(*error) << '\n';
      Check(std::holds_alternative<DerivedProjection>(projected));
      const auto& protocol = std::get<DerivedProjection>(projected);
      Check(protocol.guards.size() == (guards ? 2 : 0));
      Check(!VerifyDerivedProjection(candidate, context, protocol));
      if (guards) {
        auto& op = candidate.values.graph.ops[protocol.guards.front().value];
        auto& use = candidate.values.graph.uses[op.first_use.value];
        const auto saved = use.operand;
        use.operand = SpecialValue::kNull;
        Check(VerifyDerivedProjection(candidate, context, protocol) == DerivedProjectionError::kOperand);
        use.operand = saved;
      }
      ++initialization_cases;
    }
  std::cout << "working initialization cases " << initialization_cases << '\n';
  const auto forward = [&](std::vector<std::uint8_t>& bytes, std::uint8_t reg) {
    emit(bytes, abc::Opcode::kCALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8); bytes.push_back(reg);
  };
  const auto returned = [&](std::vector<std::uint8_t>& bytes) {emit(bytes, abc::Opcode::kRETURN);};
  for (const auto gap : {0U, 1U, 200U, 201U}) for (const auto saved : {0U, 1U, 3U, 5U}) {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kMOV_V8_V8); bytes.push_back(saved); bytes.push_back(constructor);
    for (unsigned i = 0; i < gap; ++i) emit(bytes, abc::Opcode::kNOP);
    forward(bytes, saved); returned(bytes); fixture(std::move(bytes), true);
  }
  {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(parameter);
    const auto branch = bytes.size(); emit(bytes, abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
    forward(bytes, constructor); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);
    const auto jump = bytes.size(); emit(bytes, abc::Opcode::kJMP_IMM8); bytes.push_back(0);
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    forward(bytes, constructor); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);
    bytes[jump + 1] = static_cast<std::uint8_t>(bytes.size() - jump);
    emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(0); returned(bytes);
    fixture(bytes, true);
    const auto second = jump + 2;
    bytes[second] = static_cast<std::uint8_t>(abc::Opcode::kNOP);
    bytes[second + 1] = static_cast<std::uint8_t>(abc::Opcode::kNOP);
    bytes[second + 2] = static_cast<std::uint8_t>(abc::Opcode::kNOP);
    fixture(bytes, false);
  }
  for (const auto slot : {parameter, static_cast<std::uint8_t>(function.frame.new_target->value)}) {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kLDUNDEFINED); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(slot);
    forward(bytes, constructor); returned(bytes); fixture(bytes, false);
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes, constructor);
    emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);
    emit(bytes, abc::Opcode::kLDUNDEFINED); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(parameter);
    emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(0); returned(bytes);
    fixture(bytes, true);
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes, parameter); returned(bytes); fixture(bytes, false);
    bytes.clear(); forward(bytes, constructor); forward(bytes, constructor); returned(bytes); fixture(bytes, false);
    bytes.clear(); emit(bytes, abc::Opcode::kDEBUGGER); forward(bytes, constructor); returned(bytes); fixture(bytes, false);
    bytes.clear(); forward(bytes, constructor); emit(bytes, abc::Opcode::kLDUNDEFINED); returned(bytes); fixture(bytes, false);
    bytes.clear(); emit(bytes, abc::Opcode::kJMP_IMM8); bytes.push_back(0); fixture(bytes, false);
  }
  const auto projected = ProjectDerivedConstructor(function, context);
  if (const auto* error = std::get_if<DerivedProjectionError>(&projected))
    std::cerr << "forward projection error " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<DerivedProjection>(projected));
}
