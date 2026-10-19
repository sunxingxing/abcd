#include "class_self_initialization.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <tuple>

#include "class_self_resource.h"
#include "class_tree.h"
#include "effect_order.h"
#include "family_file.h"
#include "function_captures.h"
#include "phased_capture_cells.h"
#include "tree_constants.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ClassSelfInitializationError;
class Check {
 public:
  explicit Check(const ClassSelfInitializationInput& input,
                 SemanticBudget budget)
      : input(input), initial(budget), left(budget) {
    failure.creator = input.creator.input.Method();
    failure.location = OperationLocation(input.creator.values.graph,
                                         input.call.call, failure.creator);
    failure.creation = input.call.creation;
  }
  void Require(bool ok, Error error) const {
    if (!ok) {
      auto f = failure;
      f.kind = error;
      throw f;
    }
  }
  void Step(std::uint64_t n = 1) {
    Require(n <= left.work, Error::kWorkLimit);
    left.work -= n;
  }
  void Admit(std::size_t n, std::size_t width = 1) {
    Require(width && n <= left.bytes / width, Error::kResourceLimit);
    left.bytes -= n * width;
  }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    v.reserve(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  template <class T>
  void Append(std::vector<T>& v, T value) {
    if (v.size() == v.capacity()) {
      Require(v.size() <= UINT32_MAX / 2, Error::kResourceLimit);
      Reserve(v, v.empty() ? 1 : 2 * v.size());
    }
    v.push_back(std::move(value));
  }
  template <class T, class... E>
  const T* Get(const std::variant<E...>& result, Error error) const {
    const auto* value = std::get_if<T>(&result);
    Require(value, ClassSelfResource(result).value_or(error));
    return value;
  }
  ClassSelfInitializationCost Cost() const {
    return {initial.bytes - left.bytes, initial.work - left.work};
  }
  const ClassSelfInitializationInput& input;
  SemanticBudget initial, left;
  ClassSelfInitializationFailure failure{Error::kCorrespondence};
};
OpId AtPc(const ValueProgram& graph, abc::CodeOffset pc, Check& q) {
  // Original input sites are ordered; graph.input is the retained
  // correspondence.
  q.Step(std::bit_width(graph.input.sites.size()) + 1);
  const auto found = std::lower_bound(
      graph.origins.begin(), graph.origins.end(), pc,
      [](const auto& origin, auto at) { return origin.pc < at; });
  q.Require(found != graph.origins.end() && found->pc == pc &&
                found->site.value < graph.input.sites.size() &&
                graph.input.sites[found->site.value],
            Error::kOccurrence);
  return *graph.input.sites[found->site.value];
}
abc::CodeOffset CreationPc(const FunctionContext& context, Check& q) {
  if (const auto* c = context.CreationSite()) return c->pc;
  if (const auto* c = context.ConstructorSite()) return c->pc;
  if (const auto* c = context.ClassSite()) return c->pc;
  q.Require(false, Error::kOccurrence);
  return {};
}
// Cell IDs are used only inside their owning plan. Crossing the occurrence
// boundary resolves an address through the real environment ancestry instead.
bool SelfAt(const FileFunction& parent, const CellPlan& cells, OpId creation,
            IncomingCell incoming, OpId allocation, LexicalSlot slot,
            Check& q) {
  q.Step(std::numeric_limits<std::uint32_t>::digits);
  q.Require(parent.values.environments &&
                creation.value < parent.values.environments->flow.before.size(),
            Error::kStorage);
  const auto& env = *parent.values.environments;
  q.Require(env.flow.before[creation.value].has_value(), Error::kStorage);
  const auto bound = env.flow.environments.ResolveCapture(
      *env.flow.before[creation.value], incoming.depth, incoming.slot);
  const auto* binding = std::get_if<LexicalBinding>(&bound);
  q.Require(binding, Error::kStorage);
  const auto* local = std::get_if<EnvironmentId>(&binding->environment);
  if (!local) return false;
  q.Require(local->value < cells.allocations.size(), Error::kStorage);
  return cells.allocations[local->value] == allocation &&
         binding->slot.value == slot.value;
}
enum class RootRole : std::uint8_t {
  kInitializer,
  kConstructor,
  kLiteralMember,
  kDefinedMember
};
using RootKey =
    std::tuple<RootRole, std::uint32_t, std::uint32_t, std::uint32_t>;
ClassSelfInitialization Derive(const ClassSelfInitializationInput& input,
                               Check& q) {
  const auto& parent = input.creator;
  const auto& graph = parent.values.graph;
  q.Require(parent.values.cells && parent.values.environments &&
                parent.values.method == parent.input.Method() &&
                parent.values.identity == parent.input.Identity(),
            Error::kOccurrence);
  auto discovered = FindClassSelfStores(
      graph, *parent.values.environments, *parent.values.cells,
      parent.values.initializers.calls, q.left);
  if (const auto* e = std::get_if<ClassSelfError>(&discovered)) {
    q.Require(false,
              *e == ClassSelfError::kResourceLimit ? Error::kResourceLimit
              : *e == ClassSelfError::kWorkLimit   ? Error::kWorkLimit
                                                   : Error::kInitialization);
  }
  const auto& stores = std::get<ClassSelfStores>(discovered);
  q.Admit(stores.allocated_bytes);
  q.Step(stores.work);
  std::vector<const ClassSelfStore*> indexed;
  q.Reserve(indexed, stores.stores.size());
  for (const auto& store : stores.stores) {
    q.Step();
    indexed.push_back(&store);
  }
  std::sort(indexed.begin(), indexed.end(), [&](const auto* a, const auto* b) {
    q.Step();
    return a->creation < b->creation;
  });
  const auto found =
      std::lower_bound(indexed.begin(), indexed.end(), input.call.creation,
                       [&](const auto* store, auto id) {
                         q.Step();
                         return store->creation < id;
                       });
  q.Require(found != indexed.end() &&
                (*found)->creation == input.call.creation &&
                (*found)->initializer == input.call.call,
            Error::kInitialization);
  const auto& store = **found;
  const auto* local = std::get_if<AllocatedCell>(
      &parent.values.cells->cells[store.cell.value].storage);
  q.Require(local && local->allocation == store.allocation, Error::kStorage);
  q.failure.allocation = store.allocation;
  q.failure.store = store.store;
  q.failure.slot = local->slot;
  ClassSelfInitialization result{parent.input.Identity(),
                                 input.occurrence,
                                 parent.input.Method(),
                                 {},
                                 graph.revision,
                                 graph.cfg_revision,
                                 input.call,
                                 store.allocation,
                                 store.store,
                                 local->slot};
  q.Admit(sizeof(result));
  auto flow = BuildEffectOrder(graph, q.left);
  const auto* order = q.Get<EffectOrder>(flow, Error::kOrder);
  q.Admit(order->allocated_bytes);
  q.Step(order->work);
  const auto& invocation = graph.ops[input.call.call.value];
  q.Require(graph.ops[input.call.creation.value].result == input.call.receiver,
            Error::kOccurrence);
  q.Require(invocation.use_count == 2 &&
                std::get_if<ValueInvoke>(&invocation.operation),
            Error::kOccurrence);
  const auto* callee =
      std::get_if<ValueId>(&graph.uses[invocation.first_use.value].operand);
  const auto* source =
      callee ? std::get_if<OpResult>(&graph.values[callee->value].origin)
             : nullptr;
  q.Require(source && source->op == input.call.method &&
                std::get_if<ValueId>(
                    &graph.uses[invocation.first_use.value + 1].operand) &&
                std::get<ValueId>(
                    graph.uses[invocation.first_use.value + 1].operand) ==
                    input.call.receiver,
            Error::kOccurrence);
  std::vector<bool> covered;
  q.Reserve(covered, graph.ops.size());
  covered.resize(graph.ops.size());
  covered[input.call.creation.value] = true;
  auto definitions_result = FindClassDefinitions(graph, q.left);
  const auto* definitions =
      q.Get<ClassDefinitionSites>(definitions_result, Error::kCorrespondence);
  q.Admit(definitions->allocated_bytes);
  q.Step(definitions->work);
  std::vector<ClassDefinitionSite> owned;
  q.Reserve(owned, definitions->sites.size());
  for (const auto& definition : definitions->sites) {
    q.Step();
    if (definition.creation == input.call.creation) owned.push_back(definition);
  }
  auto layout_result =
      BuildClassLayout(input.file, input.header, input.index, input.lookup,
                       input.roles, input.classes, parent, input.call.creation,
                       parent.input.Identity(), q.left, nullptr, owned);
  const auto* layout = q.Get<ClassTree>(layout_result, Error::kCorrespondence);
  q.Admit(layout->allocated_bytes);
  q.Step(layout->work);
  std::vector<RootKey> expected, actual;
  q.Reserve(expected, 2 + layout->member_sites.size() + 2 * owned.size());
  q.Reserve(actual, input.families.size());
  expected.emplace_back(RootRole::kInitializer, input.call.method.value, 0, 0);
  expected.emplace_back(RootRole::kConstructor, input.call.creation.value,
                        layout->creation.target.value, 0);
  for (const auto id : layout->member_sites) {
    q.Step();
    const auto* member = input.classes.At(id);
    q.Require(member, Error::kOccurrence);
    expected.emplace_back(RootRole::kLiteralMember, input.call.creation.value,
                          member->target.value, member->property);
  }
  std::vector<std::optional<RootKey>> defined_members;
  q.Reserve(defined_members, graph.ops.size());
  defined_members.resize(graph.ops.size());
  for (const auto& definition : owned)
    for (const auto& member : definition.Methods()) {
      q.Step();
      if (!member.operation) continue;
      q.Require(member.operation->value < graph.ops.size(), Error::kOccurrence);
      const auto* function = std::get_if<ValueFunctionCreation>(
          &graph.ops[member.operation->value].operation);
      q.Require(function, Error::kOccurrence);
      const auto resolved = abc::ResolvePoolReference(
          input.file, input.header, input.index, parent.input.Method(),
          function->method, abc::PoolEntityKind::kMethod);
      q.Step(std::bit_width(input.index.methods.size()) +
             std::bit_width(input.index.regions.size()) + 1);
      const auto* target = std::get_if<abc::PoolReference>(&resolved);
      q.Require(target, Error::kOccurrence);
      const RootKey key{RootRole::kDefinedMember, member.operation->value,
                        target->offset.value,
                        static_cast<std::uint32_t>(member.kind)};
      q.Require(!defined_members[member.operation->value], Error::kOccurrence);
      defined_members[member.operation->value] = key;
      expected.push_back(key);
    }
  bool initializer_seen = false;
  for (std::size_t f = 0; f < input.families.size(); ++f) {
    q.Step();
    const auto& root = input.families[f];
    const auto& family = root.family;
    q.Require(!family.members.empty() &&
                  root.context.Identity() == parent.input.Identity() &&
                  family.members.front().function.Identity() ==
                      parent.input.Identity(),
              Error::kOccurrence);
    const auto creation = AtPc(graph, CreationPc(root.context, q), q);
    const bool initializer = creation == input.call.method;
    if (initializer) {
      q.Require(!initializer_seen && root.context.CreationSite() &&
                    root.context.Callable() == CallableKind::kMethod &&
                    !root.context.Frame().parameters,
                Error::kOccurrence);
      initializer_seen = true;
      q.failure.initializer = root.context.Target();
      result.initializer = root.context.Target();
      actual.emplace_back(RootRole::kInitializer, creation.value, 0, 0);
    } else {
      const auto* c = root.context.ConstructorSite();
      const auto* m = root.context.ClassSite();
      const auto* d = root.context.ClassContext()
                          ? root.context.ClassContext()->DefinedMember()
                          : nullptr;
      bool defined = d && creation.value < defined_members.size() &&
                     defined_members[creation.value] &&
                     d->function.target == root.context.Target() &&
                     std::get<2>(*defined_members[creation.value]) ==
                         root.context.Target().value &&
                     std::get<3>(*defined_members[creation.value]) ==
                         static_cast<std::uint32_t>(d->kind);
      if (defined)
        actual.push_back(*defined_members[creation.value]);
      else if (c)
        actual.emplace_back(RootRole::kConstructor, creation.value,
                            c->target.value, 0);
      else if (m)
        actual.emplace_back(RootRole::kLiteralMember, creation.value,
                            m->target.value, m->property);
      q.Require(defined ||
                    (c && c->creator == parent.input.Method() &&
                     creation == input.call.creation) ||
                    (m && m->creator == parent.input.Method() &&
                     creation == input.call.creation),
                Error::kOccurrence);
    }
    q.Require(
        root.context.CreationSite()
            ? root.context.CreationSite()->creator == parent.input.Method()
            : true,
        Error::kOccurrence);
    covered[creation.value] = true;
    using ChildKey = std::pair<std::uint32_t, std::uint32_t>;
    std::vector<ChildKey> required_children, actual_children;
    std::size_t barriers = 0;
    for (const auto& member : family.members) {
      q.Step();
      q.Require(member.function.Children().size() <= SIZE_MAX - barriers,
                Error::kResourceLimit);
      barriers += member.function.Children().size();
    }
    q.Reserve(required_children, barriers);
    q.Reserve(actual_children, family.members.size() - 1);
    for (std::size_t m = 0; m < family.members.size(); ++m) {
      const auto& member = family.members[m];
      const auto original = member.function.Capture().OriginalInput();
      for (const auto site : member.function.Children()) {
        q.Step();
        q.Require(site.value < original.Instructions().size(),
                  Error::kOccurrence);
        const auto& instruction = original.Instructions()[site.value];
        if (std::holds_alternative<FunctionCreation>(instruction.operation)) {
          required_children.emplace_back(static_cast<std::uint32_t>(m),
                                         site.value);
        } else if (const auto* object = std::get_if<BufferedObjectCreation>(
                       &instruction.operation)) {
          const MethodConstants constants(input.file, input.header, input.index,
                                          member.function.Method(),
                                          {q.left.bytes});
          auto parsed = ReadObjectBuffer(
              *object, instruction.origin.value_or(instruction.pc), constants,
              {q.left.bytes, q.left.work});
          const auto* buffer = q.Get<ObjectBuffer>(parsed, Error::kWrite);
          q.Admit(buffer->allocated_bytes);
          q.Step(buffer->work);
        } else if (const auto* array = std::get_if<BufferedArrayCreation>(
                       &instruction.operation)) {
          auto parsed = ReadBufferedArray(input.file, input.header, input.index,
                                          member.function.Method(), *array,
                                          q.left.bytes, {q.left.bytes});
          const auto* buffer = q.Get<BufferedArray>(parsed, Error::kWrite);
          q.Admit(buffer->retained_bytes);
          q.Step(buffer->elements.size());
        } else
          q.Require(false, Error::kScope);  // Nested class ownership is not
                                            // this certificate's scope.
      }
      if (m) {
        q.Require(member.incoming && member.incoming->parent.value < m,
                  Error::kOccurrence);
        actual_children.emplace_back(member.incoming->parent.value,
                                     member.incoming->site.value);
      }
    }
    std::sort(required_children.begin(), required_children.end(),
              [&](const auto& a, const auto& b) {
                q.Step();
                return a < b;
              });
    std::sort(actual_children.begin(), actual_children.end(),
              [&](const auto& a, const auto& b) {
                q.Step();
                return a < b;
              });
    q.Step(required_children.size());
    q.Require(required_children == actual_children, Error::kWrite);
    std::vector<std::vector<bool>> self;
    q.Reserve(self, family.members.size());
    self.resize(family.members.size());
    for (std::size_t m = 0; m < family.members.size(); ++m) {
      q.Step();
      const auto& member = family.members[m];
      const auto& function = member.function;
      const auto view = function.Capture();
      const auto source_input = view.Input();
      const auto& g = function.Graph();
      q.Require(function.Identity() == parent.input.Identity() &&
                    function.Record() == parent.input.Record(),
                Error::kOccurrence);
      q.Append(result.owners,
               ClassSelfFamilyStamp{
                   static_cast<std::uint32_t>(f), static_cast<std::uint32_t>(m),
                   function.Method(), g.revision, g.cfg_revision});
      const auto* cells = function.Cells();
      const auto* environments = view.Environments();
      if (m == 0)
        q.Require(source_input.MatchesOwner(root.context) &&
                      root.context.Target() == function.Method(),
                  Error::kOccurrence);
      if (m == 0)
        q.Require(source_input.MatchesOwner(root.context) &&
                      root.context.Target() == function.Method(),
                  Error::kOccurrence);
      else {
        q.Require(member.incoming && member.context &&
                      member.incoming->parent.value < m,
                  Error::kOccurrence);
        const auto& edge = *member.incoming;
        const auto& predecessor = family.members[edge.parent.value].function;
        const auto* phase = predecessor.Phase();
        const auto checked =
            phase ? VerifyPhasedCaptureCells(
                        input.index, input.lookup, phase->Phases(),
                        predecessor.Body(), phase->Locals(),
                        {*edge.phase_operation, edge.site, edge.creation},
                        function.Capture(), *member.context, edge.cells, q.left,
                        predecessor.Cells())
                  : VerifyCaptureCells(input.index, predecessor.NormalCapture(),
                                       function.Capture(), edge.site,
                                       edge.creation, edge.cells, q.left);
        q.Step(*q.Get<std::uint64_t>(checked, Error::kCorrespondence));
      }
      if (!cells) {
        q.Require(source_input.Captures().empty() && !environments,
                  Error::kStorage);
        continue;
      }
      q.Require(environments, Error::kStorage);
      const auto verified =
          function.Phase()
              ? VerifyPhasedCellPlan(g, *environments, *cells, q.left)
              : VerifyCellPlan(g, *environments, *cells, q.left);
      if (const auto* e = std::get_if<CellError>(&verified))
        q.Require(false, *e == CellError::kResourceLimit ? Error::kResourceLimit
                         : *e == CellError::kWorkLimit   ? Error::kWorkLimit
                                                         : Error::kStorage);
      q.Step(std::get<std::uint64_t>(verified));
      if (!function.Phase()) {
        const auto captures = VerifyFunctionCaptures(function.Normal().input, g,
                                                     *environments, q.left);
        q.Step(*q.Get<std::uint64_t>(captures, Error::kCorrespondence));
      }
      std::vector<const ValueExceptionalExit*> exceptions;
      q.Reserve(exceptions, g.ops.size());
      exceptions.resize(g.ops.size());
      for (const auto& exit : g.exceptional_exits) {
        q.Step();
        q.Require(
            exit.op.value < exceptions.size() && !exceptions[exit.op.value],
            Error::kCatchState);
        exceptions[exit.op.value] = &exit;
      }
      std::optional<EffectOrder> read_order;
      q.Reserve(self[m], cells->cells.size());
      self[m].resize(cells->cells.size());
      for (std::size_t c = 0; c < cells->cells.size(); ++c) {
        q.Step();
        const auto& cell = cells->cells[c];
        const auto* incoming = std::get_if<IncomingCell>(&cell.storage);
        if (!incoming) continue;
        bool alias = false;
        OpId at = creation;
        if (m == 0)
          alias = SelfAt(parent, *parent.values.cells, creation, *incoming,
                         store.allocation, local->slot, q);
        else {
          const auto& edge = *member.incoming;
          const auto& predecessor = family.members[edge.parent.value].function;
          const auto& pg = predecessor.Graph();
          q.Require(edge.site.value < pg.input.sites.size() &&
                        pg.input.sites[edge.site.value],
                    Error::kOccurrence);
          at = *pg.input.sites[edge.site.value];
          const auto target = FindDemandCell(
              pg, *predecessor.Capture().Environments(), *predecessor.Cells(),
              CellDemand{at, {incoming->depth, incoming->slot}});
          q.Step(std::bit_width(predecessor.Cells()->cells.size()) +
                 std::numeric_limits<std::uint32_t>::digits);
          q.Require(target && target->value < self[edge.parent.value].size(),
                    Error::kStorage);
          alias = self[edge.parent.value][target->value];
        }
        self[m][c] = alias;
        q.Append(result.captures,
                 ClassSelfCapture{static_cast<std::uint32_t>(f),
                                  static_cast<std::uint32_t>(m), *incoming, at,
                                  alias});
        if (!alias) continue;
        for (std::uint32_t a = 0; a < cell.count; ++a) {
          q.Step();
          const auto& access = cells->accesses[cell.first + a];
          q.failure.location =
              OperationLocation(g, access.operation, function.Method());
          q.failure.read = access.operation;
          q.failure.guard.reset();
          q.failure.use.reset();
          q.Require(access.kind == CellAccessKind::kLoad, Error::kWrite);
          const auto& read = g.ops[access.operation.value];
          q.Require(read.result.has_value(), Error::kGuard);
          const auto instruction = g.origins[read.origin.value].site;
          const auto reads = source_input.CaptureAccesses();
          const auto uses = source_input.Captures();
          const auto original =
              std::lower_bound(reads.begin(), reads.end(), instruction.value,
                               [&](const auto& use, auto id) {
                                 q.Step();
                                 return use.instruction.value < id;
                               });
          q.Require(original != reads.end() &&
                        original->instruction.value == instruction.value &&
                        uses[original - reads.begin()].domain ==
                            CaptureDomain::kGuarded,
                    Error::kGuard);
          std::optional<OpId> guard;
          for (auto u = g.values[read.result->value].first_use; u;
               u = g.uses[u->value].next) {
            q.Step();
            q.Require(u->value < g.uses.size(), Error::kGuard);
            const auto& use = g.uses[u->value];
            const auto* owner = std::get_if<OpId>(&use.owner);
            if (owner && std::holds_alternative<ValueGuard>(
                             g.ops[owner->value].operation)) {
              q.Require(!guard, Error::kGuard);
              guard = *owner;
            }
          }
          q.Require(guard.has_value(), Error::kGuard);
          q.failure.guard = guard;
          if (!read_order) {
            auto checked = BuildEffectOrder(g, q.left);
            q.Get<EffectOrder>(checked, Error::kOrder);
            read_order.emplace(std::get<EffectOrder>(std::move(checked)));
            q.Admit(read_order->allocated_bytes);
            q.Step(read_order->work);
          }
          q.Require(read_order->after[access.operation.value] == guard &&
                        read_order->before[guard->value] ==
                            EffectPredecessor{access.operation},
                    Error::kGuard);
          const auto& guard_op = g.ops[guard->value];
          q.Require(guard_op.life == ValueLife::kLive &&
                        guard_op.origin.value < g.origins.size(),
                    Error::kGuard);
          q.Step();
          q.Require(guard_op.use_count == 1 && !guard_op.result &&
                        guard_op.first_use.value < g.uses.size(),
                    Error::kGuard);
          const auto& checked_value = g.uses[guard_op.first_use.value];
          const auto* value = std::get_if<ValueId>(&checked_value.operand);
          q.Require(checked_value.role == OperandRole::kGuardValue && value &&
                        *value == *read.result,
                    Error::kGuard);
          const auto* exception = exceptions[guard->value];
          q.Require(guard_op.exceptional == ExceptionalCompletion::kMayThrow &&
                        exception &&
                        (!function.Phase() ? !exception->handler : true) &&
                        exception->origin == guard_op.origin &&
                        exception->before_throw == guard_op.before_throw,
                    Error::kCatchState);
          const auto guard_site = g.origins[guard_op.origin.value].site.value;
          q.Require(guard_site < source_input.Instructions().size() &&
                        std::holds_alternative<HoleGuard>(
                            source_input.Instructions()[guard_site].operation),
                    Error::kGuard);
          q.Append(
              result.reads,
              ClassSelfRead{static_cast<std::uint32_t>(f),
                            static_cast<std::uint32_t>(m), access.operation,
                            *guard, *read.result, exception->handler});
        }
      }
    }
  }
  q.Require(initializer_seen && !result.reads.empty(), Error::kOccurrence);
  // A publication is a different event from filling the private self cell.
  // In particular, a module/outer-cell store cannot precede initialization.
  if (const auto result_value = graph.ops[input.call.creation.value].result) {
    for (auto use = graph.values[result_value->value].first_use; use;
         use = graph.uses[use->value].next) {
      q.Step();
      const auto& occurrence = graph.uses[use->value];
      const auto* owner = std::get_if<OpId>(&occurrence.owner);
      if (!owner || *owner == store.store || *owner == input.call.call ||
          *owner == input.call.method)
        continue;
      const auto& op = graph.ops[owner->value];
      if (occurrence.role == OperandRole::kStoredValue) {
        q.failure.location =
            OperationLocation(graph, *owner, parent.input.Method());
        q.failure.use = use;
        q.Require(order->Before(graph, input.call.call, op.block, op.order),
                  Error::kOrder);
      }
    }
  }
  std::sort(expected.begin(), expected.end(),
            [&](const auto& a, const auto& b) {
              q.Step();
              return a < b;
            });
  std::sort(actual.begin(), actual.end(), [&](const auto& a, const auto& b) {
    q.Step();
    return a < b;
  });
  q.Step(expected.size());
  q.Require(actual == expected, Error::kOccurrence);
  std::vector<bool> inside;
  const auto& environments = *parent.values.environments;
  q.Reserve(inside, environments.flow.environments.Size());
  inside.resize(environments.flow.environments.Size());
  for (std::size_t i = 0; i < inside.size(); ++i) {
    q.Step();
    const auto* definition = environments.flow.environments.Find(
        EnvironmentId{static_cast<std::uint32_t>(i)});
    q.Require(definition && i < parent.values.cells->allocations.size(),
              Error::kStorage);
    inside[i] = parent.values.cells->allocations[i] == store.allocation;
    if (const auto* ancestor =
            std::get_if<EnvironmentId>(&definition->parent)) {
      q.Require(ancestor->value < i, Error::kStorage);
      inside[i] = inside[i] || inside[ancestor->value];
    }
  }
  // The creator's capture barriers are a complete inventory, including class
  // literal members. No closure created in the private environment is omitted.
  for (const auto site : parent.input.Children()) {
    q.Step();
    q.Require(
        site.value < graph.input.sites.size() && graph.input.sites[site.value],
        Error::kOccurrence);
    const auto op = *graph.input.sites[site.value];
    if (covered[op.value]) continue;
    // Whether this occurrence captures the private activation is an ancestry
    // query, not a layout interval or a function-name check.
    q.Require(op.value < environments.flow.before.size() &&
                  environments.flow.before[op.value],
              Error::kStorage);
    const auto* active =
        std::get_if<EnvironmentId>(&*environments.flow.before[op.value]);
    if (active && inside[active->value]) {
      q.failure.location = OperationLocation(graph, op, parent.input.Method());
      q.Require(false, Error::kWrite);
    }
  }
  const auto method = input.lookup.Find(parent.input.Method());
  q.Require(method && method->value < input.index.methods.size() &&
                input.index.methods[method->value].code,
            Error::kOccurrence);
  q.Require(!input.index.methods[method->value].code->try_count ||
                parent.cleanup.has_value(),
            Error::kCatchState);
  if (parent.cleanup) {
    const auto& original = parent.cleanup->Original();
    auto rebuilt = EnvironmentCleanup::Build(
        original, parent.input.Identity().profile, parent.frame, q.left);
    q.Get<EnvironmentCleanup>(rebuilt, Error::kCatchState);
    const auto& cleanup = std::get<EnvironmentCleanup>(rebuilt);
    q.Admit(cleanup.AllocatedBytes());
    q.Step(cleanup.Work());
    q.Step(std::uint64_t(original.instructions.size()) *
           (std::bit_width(original.tries.size() + 1) + 1));
    const auto dispatch_result = BuildExceptionDispatch(
        original, parent.input.Identity().profile, {q.left.bytes});
    const auto* dispatch =
        q.Get<ExceptionDispatch>(dispatch_result, Error::kCatchState);
    q.Admit(dispatch->spans.capacity(), sizeof(DispatchSpan));
    result.initializer_exception =
        dispatch->Find(graph.origins[invocation.origin.value].pc);
    q.Require(result.initializer_exception.has_value(), Error::kCatchState);
    const auto allocation_site =
        graph.origins[graph.ops[store.allocation.value].origin.value].site;
    for (const auto& handler : cleanup.Handlers()) {
      q.Step();
      for (std::uint32_t p = 0; p < handler.pop_count; ++p) {
        q.Step();
        const auto& pop = cleanup.Pops()[handler.first_pop + p];
        if (pop.allocation == InstructionId{allocation_site.value})
          q.Append(result.cleanup,
                   ClassSelfCleanup{handler.handler, pop.instruction,
                                    handler.rethrow});
      }
    }
    q.Require(!result.cleanup.empty() &&
                  std::any_of(result.cleanup.begin(), result.cleanup.end(),
                              [&](const auto& cleanup) {
                                q.Step();
                                return cleanup.handler ==
                                       result.initializer_exception->handler;
                              }),
              Error::kCatchState);
  } else
    q.Require(parent.body.tries.empty(), Error::kCatchState);
  return result;
}
}  // namespace
std::variant<ClassSelfInitializationProof, ClassSelfInitializationFailure>
ClassSelfInitializationProof::Build(const ClassSelfInitializationInput& input,
                                    analysis::SemanticBudget budget) {
  Check q(input, budget);
  try {
    ClassSelfInitializationProof result;
    result.evidence_ = Derive(input, q);
    result.cost_ = q.Cost();
    return result;
  } catch (const ClassSelfInitializationFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfInitialization(const ClassSelfInitializationInput& input,
                              const ClassSelfInitialization& proposed,
                              analysis::SemanticBudget budget) {
  Check q(input, budget);
  try {
    // Re-derive from original owner inputs. No proposed membership or
    // successful builder flag is read while computing storage, guards, writes
    // or cleanup.
    const auto expected = Derive(input, q);
    q.Step(expected.reads.size() + expected.captures.size() +
           expected.cleanup.size() + 1);
    q.failure.location = OperationLocation(input.creator.values.graph,
                                           expected.store, expected.creator);
    q.failure.read.reset();
    q.failure.guard.reset();
    q.failure.use.reset();
    q.Require(expected.identity == proposed.identity &&
                  expected.occurrence == proposed.occurrence &&
                  expected.creator == proposed.creator &&
                  expected.initializer == proposed.initializer &&
                  expected.revision == proposed.revision &&
                  expected.control_revision == proposed.control_revision &&
                  expected.call == proposed.call,
              Error::kOccurrence);
    q.Require(expected.allocation == proposed.allocation &&
                  expected.slot.value == proposed.slot.value,
              Error::kStorage);
    q.Require(expected.store == proposed.store, Error::kInitialization);
    const auto locate_read = [&](const ClassSelfRead& read) {
      const auto& member =
          input.families[read.family].family.members[read.member].function;
      q.failure.location =
          OperationLocation(member.Graph(), read.guard, member.Method());
      q.failure.read = read.read;
      q.failure.guard = read.guard;
    };
    locate_read(expected.reads.front());
    q.Require(expected.reads.size() == proposed.reads.size(), Error::kGuard);
    for (std::size_t i = 0; i < expected.reads.size(); ++i) {
      const auto& a = expected.reads[i];
      const auto& b = proposed.reads[i];
      locate_read(a);
      q.Step();
      q.Require(a.family == b.family && a.member == b.member &&
                    a.read == b.read && a.guard == b.guard &&
                    a.value == b.value,
                Error::kGuard);
      q.Require(a.exception == b.exception, Error::kCatchState);
    }
    q.Require(expected.captures == proposed.captures, Error::kStorage);
    q.Require(
        expected.cleanup == proposed.cleanup &&
            expected.initializer_exception == proposed.initializer_exception,
        Error::kCatchState);
    q.Require(expected.owners == proposed.owners, Error::kCorrespondence);
    return q.Cost();
  } catch (const ClassSelfInitializationFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
}  // namespace abcd::source
