#include "../../../src/source/capture_plan.h"
#include "../../support/analysis/ssa_fixture.h"

#include <cstdlib>
#include <utility>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value) { if (!value) std::abort(); }
void Deferred() {
  // Real decoded private-environment construction. Debugger is an observation
  // barrier; an outer-cell store is an escape even without a direct call.
  for (const unsigned barrier : {0U, 1U, 2U}) {
    std::vector<std::uint8_t> bytes{0x09, 1, 0x33, 0, 0, 0, 0};
    if (barrier == 1) bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kDEBUGGER));
    if (barrier == 2) bytes.insert(bytes.end(), {0x00, 0x3d, 0x01});
    bytes.insert(bytes.end(), {0x00, 0x3d, 0, 0x65});
    const auto body = test::Body(bytes);
    const auto method = std::get<SemanticMethod>(BuildSemanticMethod(body, abc::ProfileId::kDynamic1206, 3));
    const auto proof = std::get<SemanticProof>(VerifySemanticSource(body, method, 3, 0,
        {}, {}, {}, ClosureWrites::kLanguage));
    const ScopeFacts scopes(method, proof);
    const CaptureUse use{{{0}, {0}}, CaptureDomain::kLanguage, CaptureOrigin{{77}, abc::CodeOffset{91}, abc::CodeOffset{83}}};
    const CreationBinding creation{{2}, std::get<FunctionCreation>(method.instructions[1].operation),
        {{0}, {}}, OrdinaryCapture{EnvironmentId{0}}};
    const auto result = BuildCapturePlan(scopes, {1}, creation, {&use, 1});
    // A Guarded use only ever observes the cell through its own Hole guard. It
    // is admitted before the first store, which is recorded as the deferred
    // TDZ declaration site; an already initialized cell defers nothing.
    const CaptureUse guarded{use.cell, CaptureDomain::kGuarded};
    const auto deferred = BuildCapturePlan(scopes, {1}, creation, {&guarded, 1});
    Check(std::holds_alternative<CapturePlan>(deferred));
    const auto& binding = std::get<CapturePlan>(deferred).bindings.at(0);
    const InstructionId store{static_cast<std::uint32_t>(method.instructions.size() - 2)};
    Check(std::holds_alternative<LexicalStore>(method.instructions[store.value].operation));
    Check(binding.deferred == (barrier ? std::optional{store} : std::nullopt));
    Check(std::holds_alternative<std::uint64_t>(
        VerifyCapturePlan(scopes, {1}, creation, {&guarded, 1}, std::get<CapturePlan>(deferred))));
    auto forged = std::get<CapturePlan>(deferred);
    forged.bindings[0].deferred = barrier ? std::nullopt : std::optional{store};
    Check(std::get<CaptureFailure>(VerifyCapturePlan(scopes, {1}, creation, {&guarded, 1}, forged)) == CaptureError::kBinding);
    // Unknown transitive child writes might store Hole: never deferred.
    const ScopeFacts unknown_writes(method, proof.environments, proof.locals, proof.defined_captures, ClosureWrites::kUnknown);
    if (barrier) Check(std::get<CaptureFailure>(BuildCapturePlan(unknown_writes, {1}, creation, {&guarded, 1})) == CaptureError::kScope);
    if (!barrier) {
      Check(std::holds_alternative<CapturePlan>(result));
      const auto& plan = std::get<CapturePlan>(result);
      Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes, {1}, creation, {&use, 1}, plan)));
      const CaptureUse defined{use.cell, CaptureDomain::kDefined};
      Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {1}, creation, {&defined, 1})) == CaptureError::kScope);
      const ScopeFacts unknown(method, proof.environments, proof.locals, proof.defined_captures, ClosureWrites::kUnknown);
      Check(std::get<CaptureFailure>(BuildCapturePlan(unknown, {1}, creation, {&use, 1})) == CaptureError::kScope);
    } else {
      const auto& failure = std::get<CaptureFailure>(result);
      Check(failure == CaptureError::kScope && failure.occurrence.has_value());
      Check(*failure.occurrence == CaptureOccurrence{0, use.cell, use.domain, use.source});
    }
  }
}
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
    Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, uses, budget)) == CaptureError::kWorkLimit);
    budget = {}; budget.bytes = 0;
    Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, uses, budget)) == CaptureError::kResourceLimit);
    if (!count) continue;
    auto forged = *plan;
    forged.bindings[0].domain = CaptureDomain::kDefined;
    Check(std::get<CaptureFailure>(VerifyCapturePlan(scopes, {2}, creation, uses, forged)) == CaptureError::kBinding);
    forged = *plan; std::get<LocalCell>(forged.bindings[0].cell).environment = {1};
    Check(std::get<CaptureFailure>(VerifyCapturePlan(scopes, {2}, creation, uses, forged)) == CaptureError::kBinding);
  }
  const CaptureUse outer{{{1}, {0}}, CaptureDomain::kDefined};
  const auto outside = std::get<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&outer, 1}));
  Check(std::get<CapturedSlot>(outside.bindings[0].cell).depth.value == 0 &&
      outside.bindings[0].domain == CaptureDomain::kDefined);
  auto second = creation; second.offset = {4}; second.capture = OrdinaryCapture{EnvironmentId{1}};
  const auto other = std::get<CapturePlan>(BuildCapturePlan(scopes, {4}, second, {&use, 1}));
  Check(std::get<LocalCell>(other.bindings[0].cell).environment.value == 1);
  Check(std::get<CaptureFailure>(VerifyCapturePlan(scopes, {2}, creation, {&use, 1}, other)) == CaptureError::kCreation);
  second.capture = SendableCapture{};
  Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {4}, second, {&use, 1})) == CaptureError::kCreation);
  const CaptureUse needs_defined{{{0}, {0}}, CaptureDomain::kDefined};
  Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})) == CaptureError::kScope);
  defined.block_entries = {1};
  defined.guards = {{{0}, {1}, 1, 2}};
  Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})));
  defined.guards[0].instruction = {2};
  Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, {&needs_defined, 1})) == CaptureError::kScope);
  locals.capture_barriers = {{1}};
  ScopeFacts unknown(method, environments, locals, defined, ClosureWrites::kUnknown);
  Check(std::get<CaptureFailure>(BuildCapturePlan(unknown, {2}, creation, {&use, 1})) == CaptureError::kScope);
  locals.stores.clear();
  Check(std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, {&use, 1})) == CaptureError::kScope);
  // A closure may capture a cell before its first write, but that does not
  // establish a Language read premise. Without an unexposed-prefix proof, a
  // future dominating store (or one at creation) cannot discharge that read.
  // Categories' decoded parent creates at site 9 and initializes at site 18.
  locals.capture_barriers.clear();
  locals.block_entries = {1};
  for (const auto store : {InstructionId{2}, InstructionId{3}}) {
    locals.stores = {{{0}, store}};
    locals.initializations = {{{0}, store, {0}, 1, 2}};
    Check(std::get<ScopeError>(scopes.Rebase({2}, use.cell, use.domain)) == ScopeError::kInitialization);
    const auto refused = std::get<CaptureFailure>(BuildCapturePlan(scopes, {2}, creation, {&use, 1}));
    // The rejected rebase stays visible as the typed cause of kScope.
    Check(refused == CaptureError::kScope &&
        std::get<ScopeError>(refused.cause) == ScopeError::kInitialization);
  }
  // Outside the creation's block, only the cell's single store is certainly
  // its first write; two stores there leave the order unproved and refuse.
  {
    const CaptureUse guarded{use.cell, CaptureDomain::kGuarded};
    method.flow.blocks[0].instruction_count = 3;
    FlowBlock later{};
    later.first_instruction = 3; later.instruction_count = 2;
    method.flow.blocks.push_back(later);
    std::optional<InstructionId> deferred;
    locals.stores = {{{0}, {3}}, {{0}, {4}}};
    Check(std::get<ScopeError>(scopes.Rebase({2}, guarded.cell, guarded.domain, &deferred)) == ScopeError::kInitialization &&
        !deferred);
    locals.stores = {{{0}, {4}}};
    const auto only = scopes.Rebase({2}, guarded.cell, guarded.domain, &deferred);
    Check(std::holds_alternative<CaptureCell>(only) == deferred.has_value());
    if (deferred) Check(*deferred == InstructionId{4});
    method.flow.blocks.pop_back();
    method.flow.blocks[0].instruction_count = 5;
  }
  locals.stores = {{{0}, {1}}};
  locals.initializations = {{{0}, {1}, {0}, 1, 2}};
  Check(std::holds_alternative<CapturePlan>(BuildCapturePlan(scopes, {2}, creation, {&use, 1})));
}
}  // namespace
int main() { Run(); Deferred(); }
