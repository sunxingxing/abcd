#include "capture_cell_targets.h"
#include "phased_function_file.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
const RouteInputIdentity& CaptureInput::Identity() const {
  return std::visit([](auto* input) -> const RouteInputIdentity& {
    if constexpr (std::is_same_v<decltype(input), const FunctionInput*>) return input->Identity();
    else return input->Phases().Identity();
  }, input_);
}
abc::EntityOffset CaptureInput::Method() const {
  return std::visit([](auto* input) {
    if constexpr (std::is_same_v<decltype(input), const FunctionInput*>) return input->Method();
    else return input->Phases().Target();
  }, input_);
}
abc::EntityOffset CaptureInput::Record() const { return std::visit([](auto* input) { return input->Record(); }, input_); }
InputCaptureMode CaptureInput::Mode() const { return std::visit([](auto* input) { return input->Mode(); }, input_); }
bool CaptureInput::MatchesOwner(const FunctionContext& owner) const {
  return std::visit([&](auto* input) { return input->MatchesOwner(owner); }, input_);
}
std::span<const CaptureUse> CaptureInput::Captures() const {
  if (captures_) return *captures_;
  return std::visit([](auto* input) { return input->Captures(); }, input_);
}
std::span<const InputCaptureAccess> CaptureInput::CaptureAccesses() const {
  return std::visit([](auto* input) { return input->CaptureAccesses(); }, input_);
}
std::span<const analysis::SemanticInstruction> CaptureInput::Instructions() const {
  return std::visit([](auto* input) -> std::span<const analysis::SemanticInstruction> {
    if constexpr (std::is_same_v<decltype(input), const FunctionInput*>) return input->Input().instructions;
    else return input->Phases().TreeInput(input->Body()).input.flow.instructions;
  }, input_);
}
CaptureInput CaptureChild::Input() const {
  auto input = OriginalInput();
  return storage_ ? input.WithCaptures(storage_->uses) : input;
}
CaptureInput CaptureChild::OriginalInput() const {
  return std::visit([](auto function) -> CaptureInput {
    if constexpr (std::is_same_v<decltype(function), CaptureFunction>) return function.input;
    else return *function;
  }, function_);
}
const analysis::ValueProgram& CaptureChild::Graph() const {
  return std::visit([](auto function) -> const analysis::ValueProgram& {
    if constexpr (std::is_same_v<decltype(function), CaptureFunction>) return function.values.graph;
    else return function->Phases().Graph();
  }, function_);
}
const analysis::ValueEnvironments* CaptureChild::Environments() const {
  return std::visit([](auto function) -> const analysis::ValueEnvironments* {
    if constexpr (std::is_same_v<decltype(function), CaptureFunction>)
      return function.values.environments ? &*function.values.environments : nullptr;
    else return function->Phases().CellEnvironments();
  }, function_);
}
const CellPlan* CaptureChild::Cells() const {
  if (storage_) return &storage_->cells;
  return std::visit([](auto function) -> const CellPlan* {
    if constexpr (std::is_same_v<decltype(function), CaptureFunction>)
      return function.values.cells ? &*function.values.cells : nullptr;
    else return function->Phases().Cells();
  }, function_);
}
bool CaptureChild::MatchesInput() const {
  return std::visit([](auto function) {
    if constexpr (std::is_same_v<decltype(function), CaptureFunction>) {
      const auto& values = function.values; const auto& input = function.input;
      return values.identity == input.Identity() && values.method == input.Method() &&
          values.record == input.Record() && values.capture_mode == input.Mode();
    } else return true;  // One immutable owner contains both original and working products.
  }, function_);
}
namespace {
using Error = CaptureCellError;
void Require(bool condition, Error error) { if (!condition) throw error; }
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : initial_(budget), left_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= left_.work, Error::kWorkLimit); left_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template <class T> void Size(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count); Admit(values.capacity() - count, sizeof(T));
  }
  analysis::SemanticBudget Remaining() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }
 private:
  analysis::SemanticBudget initial_, left_;
};
Error InputError(CaptureError error) {
  if (error == CaptureError::kResourceLimit) return Error::kResourceLimit;
  if (error == CaptureError::kWorkLimit) return Error::kWorkLimit;
  return Error::kInput;
}
struct IncomingRange { std::uint32_t first; std::span<const Cell> cells; };
IncomingRange Incoming(CaptureChild function, Quota& quota) {
  if (!function.Cells()) return {0, {}};
  const auto& cells = function.Cells()->cells;
  quota.Step(std::bit_width(cells.size()) + 1);
  // Verified CellPlan sorts allocated storage before incoming storage. Avoid
  // walking a child's unrelated locals for each parent creation context.
  const auto first = std::partition_point(cells.begin(), cells.end(), [](const Cell& cell) {
    return std::holds_alternative<AllocatedCell>(cell.storage);
  });
  const auto offset = static_cast<std::uint32_t>(first - cells.begin());
  return {offset, std::span<const Cell>(cells).subspan(offset)};
}
void Owner(CaptureChild function, Quota& quota, bool check_uses = true) {
  quota.Step();
  Require(function.MatchesInput() && function.Input().Mode() == InputCaptureMode::kConditional, Error::kOwner);
  const auto original = function.OriginalInput().Captures();
  const auto uses = function.Input().Captures();
  Require(uses.size() >= original.size(), Error::kCoverage);
  for (std::size_t i = 0; check_uses && i < original.size(); ++i) {
    quota.Step();
    Require(uses[i].cell.depth == original[i].cell.depth &&
        uses[i].cell.slot.value == original[i].cell.slot.value && uses[i].domain == original[i].domain, Error::kBinding);
  }
  const auto* cells = function.Cells(); const auto* environments = function.Environments();
  const auto& graph = function.Graph();
  Require((environments != nullptr) == (cells != nullptr), Error::kCell);
  if (cells) {
    Require(cells->revision == graph.revision && cells->cfg_revision == graph.cfg_revision &&
        environments->revision == graph.revision && environments->cfg_revision == graph.cfg_revision, Error::kRevision);
    Require(cells->operations.size() == graph.ops.size(), Error::kCell);
  } else Require(function.Input().Captures().empty(), Error::kCoverage);
}
void Owners(const abc::MetadataIndex& index, CaptureFunction parent, CaptureChild child,
    const analysis::CreationBinding& creation, Quota& quota) {
  Owner(parent, quota, false); Owner(child, quota); quota.Step();
  Require(parent.input.Identity() == child.Input().Identity() && parent.input.Record() == child.Input().Record() &&
      creation.target.position.value < index.methods.size(), Error::kOwner);
  const auto& target = index.methods[creation.target.position.value];
  Require(target.offset == child.Input().Method() && target.owner_offset == child.Input().Record(), Error::kOwner);
}
CaptureParent Parent(CaptureFunction function) {
  return {function.input.Identity(), function.input.Method(), function.values.graph,
      function.values.environments ? &*function.values.environments : nullptr,
      CaptureChild(function).Cells(), function.input.Input().instructions,
      function.input.Proof().locals.allocations, ScopeFacts(function.input.Input(), function.input.Proof())};
}
void Owner(const CaptureParent& parent, Quota& quota) {
  quota.Step();
  Require((parent.environments != nullptr) == (parent.cells != nullptr), Error::kCell);
  if (!parent.cells) return;
  Require(parent.cells->revision == parent.graph.revision && parent.cells->cfg_revision == parent.graph.cfg_revision &&
      parent.environments->revision == parent.graph.revision && parent.environments->cfg_revision == parent.graph.cfg_revision,
      Error::kRevision);
  Require(parent.cells->operations.size() == parent.graph.ops.size(), Error::kCell);
}
analysis::OpId Operation(const analysis::ValueProgram& graph,
    std::span<const analysis::SemanticInstruction> instructions, analysis::InstructionId site, Quota& quota) {
  quota.Step();
  Require(site.value < graph.input.sites.size() && graph.input.sites[site.value] &&
      site.value < instructions.size(), Error::kCreation);
  const auto id = *graph.input.sites[site.value];
  Require(id.value < graph.ops.size(), Error::kCreation);
  const auto& op = graph.ops[id.value];
  Require(op.life == analysis::ValueLife::kLive && op.origin.value < graph.origins.size(), Error::kCreation);
  const auto& origin = graph.origins[op.origin.value];
  const auto& original = instructions[site.value];
  Require(origin.site.value == site.value && origin.pc == original.pc && origin.original == original.origin &&
      origin.disposition == analysis::OriginDisposition::kRetained, Error::kCreation);
  return id;
}
analysis::OpId Operation(const CaptureParent& function, analysis::InstructionId site, Quota& quota) {
  return Operation(function.graph, function.instructions, site, quota);
}
analysis::OpId Creation(const CaptureParent& parent, analysis::InstructionId site,
    const analysis::CreationBinding& creation, Quota& quota) {
  const auto id = Operation(parent, site, quota);
  const auto* operation = std::get_if<analysis::ValueFunctionCreation>(&parent.graph.ops[id.value].operation);
  Require(operation && operation->kind == creation.creation.kind && operation->method == creation.creation.method &&
      operation->length == creation.creation.length.value && operation->home_object == creation.creation.home_object &&
      operation->cache_slot == creation.creation.cache_slot, Error::kCreation);
  Require(parent.environments && id.value < parent.environments->flow.before.size() &&
      parent.environments->flow.before[id.value], Error::kCreation);
  return id;
}
CaptureTarget Target(const CaptureParent& parent, const CellStorage& storage, Quota& quota) {
  Require(parent.cells != nullptr, Error::kCell);
  quota.Step(std::bit_width(parent.cells->cells.size()) + 1);
  if (const auto cell = FindCell(*parent.cells, storage)) return ParentCell{*cell};
  // A local capture passed original initialization proof, so its actual
  // parent store must have survived as a local cell. Never invent that cell.
  const auto* incoming = std::get_if<IncomingCell>(&storage);
  Require(incoming != nullptr, Error::kCell);
  return OuterCell{{incoming->depth, incoming->slot}};
}
CaptureTarget Project(const CaptureParent& parent, const CaptureCell& cell, Quota& quota) {
  quota.Step();
  if (const auto* outer = std::get_if<analysis::CapturedSlot>(&cell))
    return Target(parent, IncomingCell{outer->depth, outer->slot}, quota);
  const auto& local = std::get<analysis::LocalCell>(cell);
  const auto allocations = parent.allocations;
  Require(local.environment.value < allocations.size() && allocations[local.environment.value], Error::kCell);
  const auto op = Operation(parent, *allocations[local.environment.value], quota);
  Require(std::holds_alternative<analysis::ValueEnvironmentPush>(parent.graph.ops[op.value].operation), Error::kCell);
  return Target(parent, AllocatedCell{op, local.slot}, quota);
}
CaptureTarget Rebase(const CaptureParent& parent, analysis::OpId creation, IncomingCell incoming, Quota& quota) {
  // EnvironmentIndex's fixed uint32-depth ancestry bound, not lexical ISA width.
  constexpr auto kAncestryWork = std::numeric_limits<decltype(analysis::CapturedDepth{}.value)>::digits;
  quota.Step(kAncestryWork);
  const auto& flow = parent.environments->flow;
  const auto resolved = flow.environments.ResolveCapture(*flow.before[creation.value], incoming.depth, incoming.slot);
  const auto* binding = std::get_if<analysis::LexicalBinding>(&resolved);
  Require(binding != nullptr, Error::kBinding);
  if (const auto* outer = std::get_if<analysis::CapturedDepth>(&binding->environment))
    return Target(parent, IncomingCell{*outer, binding->slot}, quota);
  const auto environment = std::get<analysis::EnvironmentId>(binding->environment);
  const auto& allocations = parent.cells->allocations;
  Require(environment.value < allocations.size(), Error::kCell);
  return Target(parent, AllocatedCell{allocations[environment.value], binding->slot}, quota);
}
}  // namespace

std::variant<std::uint64_t, CaptureCellError> VerifyCaptureTargets(
    CaptureParent parent, CaptureChild child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& plan, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owner(parent, quota); Owner(child, quota);
    Require(parent.identity == child.Input().Identity(), Error::kOwner);
    Require(plan.identity == parent.identity && plan.parent == parent.method &&
        plan.child == child.Input().Method(), Error::kOwner);
    Require(plan.parent_revision == parent.graph.revision && plan.parent_cfg == parent.graph.cfg_revision &&
        plan.child_revision == child.Graph().revision && plan.child_cfg == child.Graph().cfg_revision, Error::kRevision);
    const auto operation = Creation(parent, site, creation, quota);
    Require(plan.creation == operation, Error::kCreation);
    const auto checked = VerifyCapturePlan(parent.scopes,
        site, creation, child.Input().Captures(), plan.input, quota.Remaining());
    if (const auto* error = std::get_if<CaptureError>(&checked)) return InputError(*error);
    quota.Step(std::get<std::uint64_t>(checked));
    const auto incoming = Incoming(child, quota);
    Require(plan.targets.size() == incoming.cells.size(), Error::kCoverage);
    for (std::size_t i = 0; i < incoming.cells.size(); ++i) {
      quota.Step();
      Require(plan.targets[i].child.value == incoming.first + i, Error::kCoverage);
      const auto* cell = std::get_if<IncomingCell>(&incoming.cells[i].storage);
      Require(cell && plan.targets[i].target == Rebase(parent, operation, *cell, quota), Error::kBinding);
    }
    // Independently connect each original initialized occurrence to its child
    // work cell. The planner below groups by cell, not by this occurrence walk.
    const auto accesses = child.Input().CaptureAccesses();
    Require(accesses.size() <= plan.input.bindings.size(), Error::kCoverage);
    for (std::size_t i = 0; i < accesses.size(); ++i) {
      quota.Step();
      const auto op = Operation(child.Graph(), child.Input().Instructions(), accesses[i].instruction, quota);
      Require(child.Cells() && op.value < child.Cells()->operations.size(), Error::kCell);
      const auto cell = child.Cells()->operations[op.value];
      Require(cell && cell->value >= incoming.first && cell->value - incoming.first < plan.targets.size(), Error::kCell);
      const auto& target = plan.targets[cell->value - incoming.first];
      Require(target.child == *cell && target.target == Project(parent, plan.input.bindings[i].cell, quota), Error::kBinding);
    }
    // Additional input obligations are exactly the zero-access incoming cells,
    // in storage order. Never pretend a forwarding requirement is an opcode.
    auto next = accesses.size();
    for (std::size_t i = 0; i < incoming.cells.size(); ++i) {
      quota.Step();
      if (incoming.cells[i].count != 0) continue;
      Require(next < plan.input.bindings.size(), Error::kCoverage);
      const auto& use = child.Input().Captures()[next];
      const auto& storage = std::get<IncomingCell>(incoming.cells[i].storage);
      Require(use.cell.depth == storage.depth && use.cell.slot.value == storage.slot.value &&
          use.domain == analysis::CaptureDomain::kLanguage, Error::kBinding);
      Require(plan.targets[i].target == Project(parent, plan.input.bindings[next++].cell, quota), Error::kBinding);
    }
    Require(next == plan.input.bindings.size(), Error::kCoverage);
    return quota.Work();
  } catch (Error error) { return error; }
}

std::variant<CaptureCells, CaptureCellError> BindCaptureTargets(
    CaptureParent parent, CaptureChild child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    CapturePlan input, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owner(parent, quota); Owner(child, quota);
    Require(parent.identity == child.Input().Identity(), Error::kOwner);
    quota.Admit(1, sizeof(CaptureCells));
    const auto operation = Creation(parent, site, creation, quota);
    quota.Admit(input.allocated_bytes, 1);
    CaptureCells plan{parent.identity, parent.method, child.Input().Method(),
        parent.graph.revision, parent.graph.cfg_revision,
        child.Graph().revision, child.Graph().cfg_revision, operation, std::move(input)};
    const auto incoming = Incoming(child, quota);
    quota.Size(plan.targets, incoming.cells.size());
    for (std::size_t i = 0; i < incoming.cells.size(); ++i) {
      quota.Step();
      const auto* cell = std::get_if<IncomingCell>(&incoming.cells[i].storage);
      Require(cell != nullptr, Error::kCell);
      plan.targets[i] = {CellId{incoming.first + static_cast<std::uint32_t>(i)}, Rebase(parent, operation, *cell, quota)};
    }
    const auto checked = VerifyCaptureTargets(parent, child, site, creation, plan, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked));
    plan.allocated_bytes = sizeof(plan) + plan.input.allocated_bytes +
        plan.targets.capacity() * sizeof(CaptureCellBinding);
    plan.work = quota.Work();
    return plan;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
std::variant<std::uint64_t, CaptureCellError> VerifyCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureChild child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& plan, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owners(index, parent, child, creation, quota);
    const auto checked = VerifyCaptureTargets(Parent(parent), child, site, creation, plan, quota.Remaining());
    if (const auto* error = std::get_if<Error>(&checked)) return *error;
    quota.Step(std::get<std::uint64_t>(checked)); return quota.Work();
  } catch (Error error) { return error; }
}
std::variant<CaptureCells, CaptureCellError> BindCaptureCells(
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureChild child,
    analysis::InstructionId site, const analysis::CreationBinding& creation, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owners(index, parent, child, creation, quota);
    auto bound = BuildCapturePlan(ScopeFacts(parent.input.Input(), parent.input.Proof()),
        site, creation, child.Input().Captures(), quota.Remaining());
    if (const auto* error = std::get_if<CaptureError>(&bound)) return InputError(*error);
    auto input = std::get<CapturePlan>(std::move(bound)); quota.Step(input.work);
    auto built = BindCaptureTargets(Parent(parent), child, site, creation, std::move(input), quota.Remaining());
    if (const auto* error = std::get_if<Error>(&built)) return *error;
    auto result = std::get<CaptureCells>(std::move(built)); result.work += quota.Work(); return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
