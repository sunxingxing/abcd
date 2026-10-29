#include "class_creator.h"
#include "class_initializers.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "function_file.h"
#include "phased_function_file.h"
namespace abcd::source {
const FileFunction* ClassCreator::Normal() const {
  const auto p = std::get_if<const FileFunction*>(&input_);
  return p ? *p : nullptr;
}
const FilePhasedFunction* ClassCreator::Phase() const {
  const auto p = std::get_if<const FilePhasedFunction*>(&input_);
  return p ? *p : nullptr;
}
const analysis::ValueProgram& ClassCreator::Graph() const {
  return Phase() ? Phase()->Phases().Graph() : Normal()->values.graph;
}
const RouteInputIdentity& ClassCreator::Identity() const {
  return Phase() ? Phase()->Phases().Identity() : Normal()->input.Identity();
}
abc::EntityOffset ClassCreator::Method() const {
  return Phase() ? Phase()->Phases().Target() : Normal()->input.Method();
}
abc::EntityOffset ClassCreator::Record() const {
  return Phase() ? Phase()->Record() : Normal()->input.Record();
}
const abc::MethodBody& ClassCreator::Body() const {
  return Phase() ? Phase()->Body() : Normal()->body;
}
std::span<const analysis::SemanticInstruction> ClassCreator::Instructions()
    const {
  return Phase()
             ? std::span<const analysis::SemanticInstruction>(
                   Phase()->Phases().TreeInput(Body()).input.flow.instructions)
             : std::span<const analysis::SemanticInstruction>(
                   Normal()->input.Input().instructions);
}
std::span<const analysis::ValueClassHeritage> ClassCreator::Classes() const {
  return Phase() ? std::span<const analysis::ValueClassHeritage>(
                       Phase()->Phases().Classes().heritage.classes)
                 : std::span<const analysis::ValueClassHeritage>(
                       Normal()->values.classes.classes);
}
// Phased member-installation fusion has no exception-port proof. Keep the
// original operations for the ordinary verifier to admit or refuse separately.
std::span<const ClassDefinitionSite> ClassCreator::Definitions() const {
  return Phase() ? std::span<const ClassDefinitionSite>{}
                 : Normal()->values.definitions.sites;
}
std::span<const ClassInitializerCall> ClassCreator::Initializers() const {
  return Phase() ? std::span<const ClassInitializerCall>{Phase()->Phases().Classes().initializers}
                 : Normal()->values.initializers.calls;
}
std::span<const ClassInstanceInitializer> ClassCreator::Instances() const {
  return Phase() ? std::span<const ClassInstanceInitializer>{}
                 : Normal()->values.initializers.instances;
}
const ClassSelfOrder* ClassCreator::SelfOrder() const {
  if (Phase()) return Phase()->SelfOrder();
  return Normal() && Normal()->self_order ? &*Normal()->self_order : nullptr;
}
std::shared_ptr<ClassSelfDeferred> ClassCreator::Self() const {
  return Normal() ? Normal()->class_self : nullptr;
}
const analysis::ValueEnvironments* ClassCreator::Environments() const {
  return Phase()                         ? Phase()->Phases().CellEnvironments()
         : Normal()->values.environments ? &*Normal()->values.environments
                                         : nullptr;
}
const CellPlan* ClassCreator::Cells() const {
  return Phase()                  ? Phase()->Phases().Cells()
         : Normal()->values.cells ? &*Normal()->values.cells
                                  : nullptr;
}
std::optional<analysis::InstructionId> ClassCreator::FindSite(
    abc::CodeOffset pc) const {
  if (Phase()) {
    const auto& sources = Phase()->Phases().Classes().sources;
    const auto it =
        std::lower_bound(sources.begin(), sources.end(), pc,
                         [](const auto& a, auto b) { return a.pc < b; });
    if (it == sources.end() || it->pc != pc) return {};
    return it->instruction;
  }
  const auto instructions = Instructions();
  const auto it =
      std::lower_bound(instructions.begin(), instructions.end(), pc,
                       [](const auto& a, auto b) { return a.pc < b; });
  if (it == instructions.end() || it->pc != pc) return {};
  return analysis::InstructionId{
      static_cast<std::uint32_t>(it - instructions.begin())};
}
std::optional<analysis::InstructionId> ClassCreator::Site(
    const FunctionContext& owner) const {
  if (const auto* c = owner.ConstructorSite())
    return FindSite(c->original_pc.value_or(c->pc));
  if (const auto* c = owner.ClassSite()) return FindSite(c->pc);
  if (const auto* c = owner.CreationSite()) return FindSite(c->pc);
  return {};
}
std::optional<PhasedClassPoint> ClassCreator::Point(analysis::OpId id) const {
  if (!Phase()) return {};
  const auto& graph = Graph();
  if (id.value >= graph.ops.size()) return {};
  const auto& op = graph.ops[id.value];
  if (op.life != analysis::ValueLife::kLive ||
      !std::holds_alternative<analysis::ValueClassCreation>(op.operation) ||
      !op.result || op.result->value >= graph.values.size() ||
      op.origin.value >= graph.origins.size() ||
      op.block.value >= graph.phases.size())
    return {};
  const auto& origin = graph.origins[op.origin.value];
  const auto& phase = graph.phases[op.block.value];
  if (origin.site.value >= graph.input.sites.size() ||
      graph.input.sites[origin.site.value] != id ||
      phase.phase != analysis::ExceptionPhase::kBefore ||
      phase.operation != id || phase.instruction != origin.site)
    return {};
  const auto* result =
      std::get_if<analysis::OpResult>(&graph.values[op.result->value].origin);
  if (!result || result->op != id || !result->commit ||
      result->commit->value >= graph.phases.size())
    return {};
  const auto& commit = graph.phases[result->commit->value];
  if (commit.phase != analysis::ExceptionPhase::kCommit ||
      commit.operation != id || commit.instruction != origin.site)
    return {};
  const auto input = Phase()->Phases().TreeInput(Body()).input;
  if (phase.input.value >= input.environments.before.size() ||
      !input.environments.before[phase.input.value] ||
      phase.input.value >= input.flow.nodes.size() ||
      commit.input.value >= input.flow.nodes.size())
    return {};
  const auto& node = input.flow.nodes[phase.input.value];
  const auto& committed = input.flow.nodes[commit.input.value];
  if (node.phase != analysis::ExceptionPhase::kBefore ||
      node.instruction.value != origin.site.value ||
      committed.phase != analysis::ExceptionPhase::kCommit ||
      committed.instruction != node.instruction)
    return {};
  return PhasedClassPoint{
      id,
      {origin.site.value},
      origin.pc,
      origin.original.value_or(origin.pc),
      phase.input,
      commit.input,
      *input.environments.before[phase.input.value],
      node.exception ? node.exception->target : std::nullopt};
}
bool PhasedClassBinding::Matches(ClassCreator c, analysis::OpId op) const {
  return c.Phase() && c.Identity() == identity_ && c.Method() == method_ &&
         c.Graph().revision == revision_ && c.Graph().cfg_revision == cfg_ &&
         op == point_.operation && c.Point(op) == point_;
}
std::variant<PhasedClassSites, analysis::ValueHeritageError>
BuildPhasedClassSites(const analysis::ValueProgram& graph,
                      analysis::SemanticBudget budget) {
  using Error = analysis::ValueHeritageError;
  const auto initial = budget;
  try {
    auto found = analysis::AnalyzeValueHeritage(graph, budget);
    if (const auto* e = std::get_if<Error>(&found)) return *e;
    PhasedClassSites sites;
    sites.heritage = std::get<analysis::ValueHeritages>(std::move(found));
    if (sites.heritage.allocated_bytes > budget.bytes)
      return Error::kResourceLimit;
    if (sites.heritage.work > budget.work) return Error::kWorkLimit;
    budget.bytes -= sites.heritage.allocated_bytes;
    budget.work -= sites.heritage.work;
    const auto n = sites.heritage.classes.size();
    if (n > budget.bytes / sizeof(PhasedClassSites::Source))
      return Error::kResourceLimit;
    budget.bytes -= n * sizeof(PhasedClassSites::Source);
    sites.sources.reserve(n);
    if (sites.sources.capacity() - n >
        budget.bytes / sizeof(PhasedClassSites::Source))
      return Error::kResourceLimit;
    budget.bytes -=
        (sites.sources.capacity() - n) * sizeof(PhasedClassSites::Source);
    const auto step = [&] {
      if (!budget.work) throw Error::kWorkLimit;
      --budget.work;
    };
    for (const auto& site : sites.heritage.classes) {
      step();
      const auto& op = graph.ops[site.operation.value];
      if (op.origin.value >= graph.origins.size()) return Error::kInput;
      const auto& origin = graph.origins[op.origin.value];
      if (origin.site.value >= graph.input.sites.size() ||
          graph.input.sites[origin.site.value] != site.operation)
        return Error::kInput;
      sites.sources.push_back(
          {origin.original.value_or(origin.pc), {origin.site.value}});
    }
    std::sort(sites.sources.begin(), sites.sources.end(),
              [&](const auto& a, const auto& b) {
                step();
                return a.pc < b.pc;
              });
    for (std::size_t i = 1; i < n; ++i) {
      step();
      if (sites.sources[i - 1].pc == sites.sources[i].pc) return Error::kInput;
    }
    auto calls = FindClassInitializerCalls(graph, budget);
    if (const auto* error = std::get_if<ClassInitializerError>(&calls))
      return *error == ClassInitializerError::kResourceLimit ? Error::kResourceLimit :
          *error == ClassInitializerError::kWorkLimit ? Error::kWorkLimit : Error::kInput;
    auto& initializers = std::get<ClassInitializerCalls>(calls);
    if (initializers.allocated_bytes > budget.bytes) return Error::kResourceLimit;
    if (initializers.work > budget.work) return Error::kWorkLimit;
    budget.bytes -= initializers.allocated_bytes; budget.work -= initializers.work;
    sites.initializers = std::move(initializers.calls);
    sites.allocated_bytes = initial.bytes - budget.bytes;
    sites.work = initial.work - budget.work;
    return sites;
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  }
}
}  // namespace abcd::source
