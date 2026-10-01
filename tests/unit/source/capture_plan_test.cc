#include "../../../src/source/capture_plan.h"

#include <cstdlib>
#include <utility>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value) { if (!value) std::abort(); }
SemanticMethod Owner() {
  // Scope-only synthetic facts below do not claim a complete input certificate.
  constexpr std::uint8_t kReturnUndefined = 0x65;
  const std::uint8_t code[]{kReturnUndefined};
  abc::MethodBody body;
  body.instructions.push_back({{0}, std::get<abc::DecodedInstruction>(
      abc::DecodeInstruction(code, abc::ProfileId::kDynamic1206)), {}});
  return std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 3));
}
void Run() {
  auto method = Owner();
  method.instructions.resize(5);
  method.flow.blocks[0].instruction_count = 5;
  const FunctionCreation operation{CreationKind::kFunction, {0}, {0}, HomeObjectInput::kCurrentFunction, 0};
  method.instructions[2] = {{2}, operation, {}, {}};
  method.instructions[4] = {{4}, operation, {}, {}};
  const EnvironmentDefinition definitions[]{
      {CapturedDepth{0}, PushEnvironment{1, {}}},
      {CapturedDepth{0}, PushEnvironment{1, {}}}};
  EnvironmentFlow environments{std::get<EnvironmentIndex>(EnvironmentIndex::Build(definitions))};
  environments.before.resize(5);
  environments.before[2] = EnvironmentId{0}; environments.before[4] = EnvironmentId{1};
  LocalAccessPlan locals;
  locals.cells = {{{0}, {0}}, {{1}, {0}}};
  locals.stores = {{{0}, {1}}, {{1}, {3}}};
  locals.allocations = {InstructionId{0}, InstructionId{2}};
  DefinedCaptures defined;
  ScopeFacts scopes(method, environments, locals, defined, ClosureWrites::kLanguage);
  CreationBinding creation{{2}, operation, {{0}, {}}, OrdinaryCapture{EnvironmentId{0}}};
  const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage};
  for (const auto count : {0U, 1U, 200U, 201U, 4096U}) {
    std::vector<CaptureUse> uses(count, use);
    const auto result = BuildCapturePlan(scopes, {2}, creation, uses);
    const auto* plan = std::get_if<CapturePlan>(&result);
    Check(plan && plan->bindings.size() == count);
    Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes, {2}, creation, uses, *plan)));
    SemanticBudget budget; budget.work = plan->work;
    Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, uses, budget)));
    --budget.work;
    Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, uses, budget)) == CaptureError::kWorkLimit);
    budget = {}; budget.bytes = 0;
    Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, uses, budget)) == CaptureError::kResourceLimit);
    if (!count) continue;
    auto forged = *plan;
    forged.bindings[0].domain = CaptureDomain::kDefined;
    Check(std::get<CaptureError>(VerifyCapturePlan(scopes, {2}, creation, uses, forged)) == CaptureError::kBinding);
    forged = *plan; std::get<LocalCell>(forged.bindings[0].cell).environment = {1};
    Check(std::get<CaptureError>(VerifyCapturePlan(scopes, {2}, creation, uses, forged)) == CaptureError::kBinding);
  }
  const CaptureUse outer{{{1}, {0}}, CaptureDomain::kDefined};
  const auto outside = std::get<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&outer, 1}));
  Check(std::get<CapturedSlot>(outside.bindings[0].cell).depth.value == 0 &&
      outside.bindings[0].domain == CaptureDomain::kDefined);
  auto second = creation; second.offset = {4}; second.capture = OrdinaryCapture{EnvironmentId{1}};
  const auto other = std::get<CapturePlan>(BuildCapturePlan(scopes, {4}, second, {&use, 1}));
  Check(std::get<LocalCell>(other.bindings[0].cell).environment.value == 1);
  Check(std::get<CaptureError>(VerifyCapturePlan(scopes, {2}, creation, {&use, 1}, other)) == CaptureError::kCreation);
  second.capture = SendableCapture{};
  Check(std::get<CaptureError>(BuildCapturePlan(scopes, {4}, second, {&use, 1})) == CaptureError::kCreation);
  const CaptureUse needs_defined{{{0}, {0}}, CaptureDomain::kDefined};
  Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})) == CaptureError::kScope);
  defined.block_entries = {1};
  defined.guards = {{{0}, {1}, 1, 2}};
  Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})));
  defined.guards[0].instruction = {2};
  Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})) == CaptureError::kScope);
  locals.capture_barriers = {{1}};
  ScopeFacts unknown(method, environments, locals, defined, ClosureWrites::kUnknown);
  Check(std::get<CaptureError>(BuildCapturePlan(unknown, {2}, creation, {&use, 1})) == CaptureError::kScope);
  locals.stores.clear();
  Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, {&use, 1})) == CaptureError::kScope);
  // A closure may capture a cell before its first write, but that does not
  // establish this planner's Language-at-creation premise. A future dominating
  // store (or one at the creation site) must not discharge a child's read.
  // Categories' decoded parent creates at site 9 and initializes at site 18.
  locals.capture_barriers.clear();
  locals.block_entries = {1};
  locals.initializations.resize(2);
  for (const auto store : {InstructionId{2}, InstructionId{3}}) {
    locals.stores = {{{0}, store}};
    locals.initializations[0] = LocalInitialization{store, 1, 2};
    Check(std::get<ScopeError>(scopes.Rebase({2}, use.cell, use.domain)) == ScopeError::kInitialization);
    Check(std::get<CaptureError>(BuildCapturePlan(scopes, {2}, creation, {&use, 1})) == CaptureError::kScope);
  }
  locals.stores = {{{0}, {1}}};
  locals.initializations[0] = LocalInitialization{{1}, 1, 2};
  Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&use, 1})));
}
}  // namespace
int main() { Run(); }
