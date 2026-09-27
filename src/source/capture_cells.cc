#include "capture_cell_targets.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
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
IncomingRange Incoming(CaptureFunction function, Quota& quota) {
  if (!function.values.cells) return {0, {}};
  const auto& cells = function.values.cells->cells;
  quota.Step(std::bit_width(cells.size()) + 1);
  // Verified CellPlan sorts allocated storage before incoming storage. Avoid
  // walking a child's unrelated locals for each parent creation context.
  const auto first = std::partition_point(cells.begin(), cells.end(), [](const Cell& cell) {
    return std::holds_alternative<AllocatedCell>(cell.storage);
  });
  const auto offset = static_cast<std::uint32_t>(first - cells.begin());
  return {offset, std::span<const Cell>(cells).subspan(offset)};
}
void Owner(CaptureFunction function, Quota& quota) {
  quota.Step();
  const auto& values = function.values;
  Require(values.identity == function.input.Identity() && values.method == function.input.Method() &&
      values.record == function.input.Record() && values.capture_mode == function.input.Mode() &&
      values.capture_mode == InputCaptureMode::kConditional, Error::kOwner);
  Require(values.environments.has_value() == values.cells.has_value(), Error::kCell);
  if (values.cells) {
    Require(values.cells->revision == values.graph.revision && values.cells->cfg_revision == values.graph.cfg_revision &&
        values.environments->revision == values.graph.revision &&
        values.environments->cfg_revision == values.graph.cfg_revision, Error::kRevision);
    Require(values.cells->operations.size() == values.graph.ops.size(), Error::kCell);
  } else Require(function.input.Captures().empty(), Error::kCoverage);
}
void Owners(const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    const analysis::CreationBinding& creation, Quota& quota) {
  Owner(parent, quota); Owner(child, quota); quota.Step();
  Require(parent.input.Identity() == child.input.Identity() && parent.input.Record() == child.input.Record() &&
      creation.target.position.value < index.methods.size(), Error::kOwner);
  const auto& target = index.methods[creation.target.position.value];
  Require(target.offset == child.input.Method() && target.owner_offset == child.input.Record(), Error::kOwner);
}
CaptureParent Parent(CaptureFunction function) {
  return {function.input.Identity(), function.input.Method(), function.values.graph,
      function.values.environments ? &*function.values.environments : nullptr,
      function.values.cells ? &*function.values.cells : nullptr, function.input.Input().instructions,
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
analysis::OpId Operation(const CaptureParent& function, analysis::InstructionId site, Quota& quota) {
  quota.Step(); const auto& graph = function.graph;
  Require(site.value < graph.input.sites.size() && graph.input.sites[site.value] &&
      site.value < function.instructions.size(), Error::kCreation);
  const auto id = *graph.input.sites[site.value];
  Require(id.value < graph.ops.size(), Error::kCreation);
  const auto& op = graph.ops[id.value];
  Require(op.life == analysis::ValueLife::kLive && op.origin.value < graph.origins.size(), Error::kCreation);
  const auto& origin = graph.origins[op.origin.value];
  const auto& original = function.instructions[site.value];
  Require(origin.site.value == site.value && origin.pc == original.pc && origin.original == original.origin &&
      origin.disposition == analysis::OriginDisposition::kRetained, Error::kCreation);
  return id;
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
    CaptureParent parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    const CaptureCells& plan, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owner(parent, quota); Owner(child, quota);
    Require(parent.identity == child.input.Identity(), Error::kOwner);
    Require(plan.identity == parent.identity && plan.parent == parent.method &&
        plan.child == child.input.Method(), Error::kOwner);
    Require(plan.parent_revision == parent.graph.revision && plan.parent_cfg == parent.graph.cfg_revision &&
        plan.child_revision == child.values.graph.revision && plan.child_cfg == child.values.graph.cfg_revision, Error::kRevision);
    const auto operation = Creation(parent, site, creation, quota);
    Require(plan.creation == operation, Error::kCreation);
    const auto checked = VerifyCapturePlan(parent.scopes,
        site, creation, child.input.Captures(), plan.input, quota.Remaining());
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
    const auto accesses = child.input.CaptureAccesses();
    Require(accesses.size() == plan.input.bindings.size(), Error::kCoverage);
    const auto child_view = Parent(child);
    for (std::size_t i = 0; i < accesses.size(); ++i) {
      quota.Step();
      const auto op = Operation(child_view, accesses[i].instruction, quota);
      Require(child.values.cells && op.value < child.values.cells->operations.size(), Error::kCell);
      const auto cell = child.values.cells->operations[op.value];
      Require(cell && cell->value >= incoming.first && cell->value - incoming.first < plan.targets.size(), Error::kCell);
      const auto& target = plan.targets[cell->value - incoming.first];
      Require(target.child == *cell && target.target == Project(parent, plan.input.bindings[i].cell, quota), Error::kBinding);
    }
    return quota.Work();
  } catch (Error error) { return error; }
}

std::variant<CaptureCells, CaptureCellError> BindCaptureTargets(
    CaptureParent parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation,
    CapturePlan input, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owner(parent, quota); Owner(child, quota);
    Require(parent.identity == child.input.Identity(), Error::kOwner);
    quota.Admit(1, sizeof(CaptureCells));
    const auto operation = Creation(parent, site, creation, quota);
    quota.Admit(input.allocated_bytes, 1);
    CaptureCells plan{parent.identity, parent.method, child.input.Method(),
        parent.graph.revision, parent.graph.cfg_revision,
        child.values.graph.revision, child.values.graph.cfg_revision, operation, std::move(input)};
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
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
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
    const abc::MetadataIndex& index, CaptureFunction parent, CaptureFunction child,
    analysis::InstructionId site, const analysis::CreationBinding& creation, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget); Owners(index, parent, child, creation, quota);
    auto bound = BindFunctionInputs(index, parent.input, site, creation, child.input, quota.Remaining());
    if (const auto* error = std::get_if<CaptureError>(&bound)) return InputError(*error);
    if (std::holds_alternative<InputBindingError>(bound)) return Error::kOwner;
    auto input = std::get<CapturePlan>(std::move(bound)); quota.Step(input.work);
    auto built = BindCaptureTargets(Parent(parent), child, site, creation, std::move(input), quota.Remaining());
    if (const auto* error = std::get_if<Error>(&built)) return *error;
    auto result = std::get<CaptureCells>(std::move(built)); result.work += quota.Work(); return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
