#include "../../../src/source/function_input.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/capture_plan.h"
#include "abcd/source/tree_printer.h"
#include "abcd/analysis/dominators.h"
#include "analysis/ssa_fixture.h"

#include <bit>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Layout { kStoreAfterRead, kAllocationAfterStore };
abc::MethodBody Body(std::uint32_t count, Layout layout, bool capture = false, bool earlier = false,
    bool guard = false, bool old_capture = false,
    abc::ProfileId profile = abc::ProfileId::kDynamic1206) {
  std::vector<std::uint8_t> bytes;
  const auto jump = [&] {
    const auto pc = static_cast<std::uint32_t>(bytes.size());
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM32));
    bytes.resize(bytes.size() + sizeof(std::int32_t)); return pc;
  };
  const auto patch = [&](std::uint32_t pc, std::uint32_t target) {
    const auto delta = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(pc));
    for (std::size_t i = 0; i < sizeof(delta); ++i) bytes[pc + 1 + i] = static_cast<std::uint8_t>(delta >> (8 * i));
  };
  if (old_capture) bytes.insert(bytes.end(), {0x33, 0, 0, 0, 0});
  if (layout == Layout::kStoreAfterRead) bytes.insert(bytes.end(), {0x09, 1});
  const auto entry = jump();
  const auto read = static_cast<std::uint32_t>(bytes.size());
  if (capture) bytes.insert(bytes.end(), {0x33, 0, 0, 0, 0});
  else for (std::uint32_t i = 0; i < count; ++i) bytes.insert(bytes.end(), {0x3c, 0});
  bytes.push_back(0x64);
  const auto store = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), {0x60, 0, 0x3d, 0});
  if (guard) bytes.insert(bytes.end(), {0x3c, 0, 0xfe, 7, 0});
  if (earlier) bytes.insert(bytes.end(), {0x33, 0, 0, 0, 0});
  patch(jump(), read);
  if (layout == Layout::kStoreAfterRead) patch(entry, store);
  else {
    const auto allocation = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), {0x09, 1}); patch(jump(), store); patch(entry, allocation);
  }
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(std::span(bytes).subspan(pc), profile));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta)
      instruction.branch_target = abc::CodeOffset{
          static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  return body;
}
abc::FrameLayout Frame() {
  return std::get<abc::FrameLayout>(abc::BuildFrameLayout(0, 1, {false, false, false, false}));
}
RouteInputIdentity Identity(abc::ProfileId profile = abc::ProfileId::kDynamic1206) {
  RouteInputIdentity identity; identity.profile = profile;
  switch (profile) {
    case abc::ProfileId::kDynamic1202: identity.abc_version = {12, 0, 2, 0}; break;
    case abc::ProfileId::kDynamic1206: identity.abc_version = {12, 0, 6, 0}; break;
    case abc::ProfileId::kDynamic1301: identity.abc_version = {13, 0, 1, 0}; break;
    default: test::Check(false);
  }
  return identity;
}
void Branches() {
  // Both successors initialize the same allocated cell independently. A
  // closure on one successor must not expose the environment on its sibling.
  std::vector<std::uint8_t> bytes{0x09, 1, 0x60, 0,
      static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM32), 0, 0, 0, 0};
  constexpr std::uint32_t branch = 4;
  const auto patch = [&](std::uint32_t target) {
    const auto delta = target - branch;
    for (std::size_t i = 0; i < sizeof(delta); ++i)
      bytes[branch + 1 + i] = static_cast<std::uint8_t>(delta >> (8 * i));
  };
  bytes.insert(bytes.end(), {0x60, 0, 0x3d, 0, 0x33, 0, 0, 0, 0, 0x64});
  patch(static_cast<std::uint32_t>(bytes.size()));
  bytes.insert(bytes.end(), {0x60, 0, 0x3d, 0, 0x33, 0, 0, 0, 0, 0x64});
  const auto body = test::Body(bytes);
  auto built = FunctionInput::Build(Identity(), {200}, {100}, body, Frame(), InputCaptureMode::kConditional);
  test::Check(std::holds_alternative<FunctionInput>(built));
  const auto& input = std::get<FunctionInput>(built);
  test::Check(input.Proof().locals.initializations.size() == 2);
  const ScopeFacts scopes(input.Input(), input.Proof().environments, input.Proof().locals,
      input.Proof().defined_captures, ClosureWrites::kUnknown);
  test::Check(input.Children().size() == 2);
  for (const auto site : input.Children())
    test::Check(std::holds_alternative<CaptureCell>(scopes.Rebase(site, {{0}, {0}}, CaptureDomain::kLanguage)));

  // One successor bypasses the store and reaches a read of Hole. Address
  // order and a store elsewhere in the function cannot establish initialization.
  bytes = {0x09, 1, 0x60, 0, static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM32), 0, 0, 0, 0,
      0x60, 0, 0x3d, 0};
  patch(static_cast<std::uint32_t>(bytes.size()));
  bytes.insert(bytes.end(), {0x3c, 0, 0x64});
  const auto bypass = test::Body(bytes);
  test::Check(std::holds_alternative<FunctionInputFailure>(
      FunctionInput::Build(Identity(), {200}, {100}, bypass, Frame(), InputCaptureMode::kClosed)));
}
void InitializationRegions() {
  for (const auto count : {1U, 200U, 201U, 402U}) {
    std::vector<std::uint8_t> bytes{0x09, 1};
    for (std::uint32_t i = 0; i < count; ++i) {
      std::optional<std::uint32_t> branch;
      if (i + 1 < count) {
        bytes.insert(bytes.end(), {0x60, 0});
        branch = static_cast<std::uint32_t>(bytes.size());
        bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kJEQZ_IMM32));
        bytes.resize(bytes.size() + sizeof(std::int32_t));
      }
      bytes.insert(bytes.end(), {0x60, 0, 0x3d, 0, 0x33, 0, 0, 0, 0, 0x64});
      if (branch) {
        const auto delta = static_cast<std::uint32_t>(bytes.size()) - *branch;
        for (std::size_t b = 0; b < sizeof(delta); ++b)
          bytes[*branch + 1 + b] = static_cast<std::uint8_t>(delta >> (8 * b));
      }
    }
    const auto body = test::Body(bytes);
    auto built = FunctionInput::Build(Identity(), {200}, {100}, body, Frame(), InputCaptureMode::kConditional);
    test::Check(std::holds_alternative<FunctionInput>(built));
    const auto& input = std::get<FunctionInput>(built);
    test::Check(input.Proof().locals.initializations.size() == count && input.Children().size() == count);
    const ScopeFacts scopes(input.Input(), input.Proof().environments, input.Proof().locals,
        input.Proof().defined_captures, ClosureWrites::kUnknown);
    for (const auto site : input.Children())
      test::Check(std::holds_alternative<CaptureCell>(scopes.Rebase(site, {{0}, {0}}, CaptureDomain::kLanguage)));
  }
}
void Captures() {
  for (const auto layout : {Layout::kStoreAfterRead, Layout::kAllocationAfterStore})
    for (const bool earlier : {false, true}) for (const bool old_capture : {false, true}) {
      const auto body = Body(1, layout, true, earlier, true, old_capture);
      auto built = FunctionInput::Build(Identity(), {200}, {100}, body, Frame(), InputCaptureMode::kConditional);
      test::Check(std::holds_alternative<FunctionInput>(built));
      const auto& input = std::get<FunctionInput>(built);
      const auto& method = input.Input(); const auto& proof = input.Proof();
      // The lower-address creation belongs to the block entered after storage
      // and guard. Pool/template binding is tested separately; this view checks
      // only original decoded scope facts and creation identity.
      const auto site = input.Children()[old_capture ? 1 : 0];
      const CreationBinding creation{method.instructions[site.value].pc,
          std::get<FunctionCreation>(method.instructions[site.value].operation),
          {{1}, {abc::FunctionKind::kBaseConstructor, false, 0, {}}},
          OrdinaryCapture{*proof.environments.before[site.value]}};
      const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage};
      const ScopeFacts scopes(method, proof);
      test::Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, site, creation, {&use, 1})));
      const ScopeFacts unknown(method, proof.environments, proof.locals, proof.defined_captures, ClosureWrites::kUnknown);
      const auto exposed = unknown.Rebase(site, use.cell, use.domain);
      if (earlier) test::Check(std::get<ScopeError>(exposed) == ScopeError::kWrites);
      else test::Check(std::holds_alternative<CaptureCell>(exposed));
      auto defined = proof.defined_captures;
      const auto dom = std::get<DominatorTree>(BuildNormalDominators(method.flow));
      for (const auto& node : dom.nodes) defined.block_entries.push_back(node.begin);
      for (std::uint32_t b = 0; b < method.flow.blocks.size(); ++b) {
        const auto& block = method.flow.blocks[b];
        for (std::uint32_t i = block.first_instruction; i < block.first_instruction + block.instruction_count; ++i)
          if (std::holds_alternative<SuperCheck>(method.instructions[i].operation))
            defined.guards.push_back({{0}, {i}, dom.nodes[b].begin, dom.nodes[b].end});
      }
      const ScopeFacts guarded(method, proof.environments, proof.locals, defined, ClosureWrites::kLanguage);
      test::Check(std::holds_alternative<CaptureCell>(guarded.Rebase(site, use.cell, CaptureDomain::kDefined)));
      defined.guards[0].instruction = site;
      test::Check(std::get<ScopeError>(guarded.Rebase(site, use.cell, CaptureDomain::kDefined)) == ScopeError::kDomain);
    }
  for (const auto layout : {Layout::kStoreAfterRead, Layout::kAllocationAfterStore}) {
    const auto body = Body(1, layout, false, true);
    const auto rejected = FunctionInput::Build(Identity(), {200}, {100}, body, Frame(), InputCaptureMode::kClosed);
    test::Check(std::holds_alternative<FunctionInputFailure>(rejected));
  }
}
std::string Recover(std::uint32_t count, Layout layout, abc::ProfileId profile) {
  const auto body = Body(count, layout, false, false, false, false, profile);
  const auto frame = Frame(); const auto identity = Identity(profile);
  auto built = FunctionInput::Build(identity, {200}, {100}, body, frame, InputCaptureMode::kConditional);
  test::Check(std::holds_alternative<FunctionInput>(built));
  const auto& input = std::get<FunctionInput>(built);
  auto lifted = BuildFunctionValues(input, body, frame);
  test::Check(std::holds_alternative<FunctionValues>(lifted));
  const auto& values = std::get<FunctionValues>(lifted);
  const auto control = std::get<ControlPlan>(BuildControlPlan(values.graph));
  const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(values.graph));
  FunctionSyntax syntax;
  syntax.identity = identity; syntax.method = {200}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(frame.parameters);
  const std::vector<ExpressionConstant> constants(values.graph.ops.size());
  const ExpressionConstants expressions{constants, 0, 0};
  auto recovered = BuildFunctionTree(values.graph, control, evaluation, syntax, expressions);
  test::Check(std::holds_alternative<ConstructedFunction>(recovered));
  const auto& tree = std::get<ConstructedFunction>(recovered).tree;
  test::Check(tree.recovery_issues.empty());
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(values.graph, control, evaluation, tree, expressions)));
  std::ostringstream output;
  test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree, output)));
  return output.str();
}
}
int main(int argc, char** argv) {
  Branches();
  InitializationRegions();
  Captures();
  const bool json = argc == 2 && std::string_view(argv[1]) == "--json";
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
   for (const auto count : {1U, 200U, 201U, 402U})
    for (const auto layout : {Layout::kStoreAfterRead, Layout::kAllocationAfterStore}) {
      const auto source = Recover(count, layout, profile);
      if (json) std::cout << nlohmann::json{{"source", source}, {"count", count}}.dump() << '\n';
    }
}
