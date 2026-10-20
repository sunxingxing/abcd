#include "class_self_scope.h"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <tuple>

#include "capture_requirement.h"
#include "class_self_resource.h"
#include "effect_order.h"
#include "family_file.h"
#include "tree_constants.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = ClassSelfInitializationError;
class Check {
 public:
  Check(const ClassSelfInitializationInput& input, SemanticBudget budget)
      : initial(budget), left(budget) {
    failure.creator = input.creator.input.Method();
    failure.creation = input.call.creation;
    failure.location = OperationLocation(input.creator.values.graph,
                                         input.call.call, failure.creator);
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
  ClassSelfInitializationCost Cost() const {
    return {initial.bytes - left.bytes, initial.work - left.work};
  }
  SemanticBudget initial, left;
  ClassSelfInitializationFailure failure{Error::kCorrespondence};
};
using Address = std::tuple<std::uint32_t, std::uint16_t>;
Address Key(IncomingCell cell) { return {cell.depth.value, cell.slot.value}; }
ClassSelfScope Derive(const ClassSelfInitializationInput& input,
                      const ClassSelfInitialization& raw, Check& q) {
  // The raw seal is not a builder flag. Re-derive original storage, guard,
  // descendant and cleanup correspondence before interpreting a single read.
  auto verified = VerifyClassSelfInitialization(input, raw, q.left);
  if (const auto* f = std::get_if<ClassSelfInitializationFailure>(&verified))
    throw *f;
  const auto cost = std::get<ClassSelfInitializationCost>(verified);
  q.Admit(cost.bytes);
  q.Step(cost.work);
  q.failure.initializer = raw.initializer;
  q.failure.allocation = raw.allocation;
  q.failure.store = raw.store;
  q.failure.slot = raw.slot;
  ClassSelfScope scope{
      {raw.occurrence, raw.call.creation, raw.allocation, raw.slot}};
  q.Admit(sizeof(scope));
  q.Reserve(scope.ranges, input.families.size());
  q.Reserve(scope.owners, raw.owners.size());
  q.Reserve(scope.requirements, raw.captures.size());
  q.Reserve(scope.references, raw.reads.size());
  q.Reserve(scope.cleanup, raw.cleanup.size());
  std::uint64_t first = 0;
  for (std::size_t f = 0; f < input.families.size(); ++f) {
    q.Step();
    const auto& family = input.families[f].family;
    q.Require(f <= UINT32_MAX && first <= UINT32_MAX &&
                  family.members.size() <= UINT32_MAX - first,
              Error::kScope);
    scope.ranges.push_back({static_cast<std::uint32_t>(f),
                            static_cast<std::uint32_t>(first),
                            static_cast<std::uint32_t>(family.members.size())});
    for (std::size_t m = 0; m < family.members.size(); ++m) {
      q.Step();
      const auto& function = family.members[m].function;
      scope.owners.push_back({static_cast<std::uint32_t>(f),
                              static_cast<std::uint32_t>(m), function.Method(),
                              function.Graph().revision,
                              function.Graph().cfg_revision});
    }
    first += family.members.size();
  }
  q.Step(scope.owners.size());
  q.Require(scope.owners == raw.owners, Error::kScope);
  const auto owner = [&](std::uint32_t f,
                         std::uint32_t m) -> const ClassSelfFamilyStamp& {
    q.Step();
    q.Require(f < scope.ranges.size() && m < scope.ranges[f].count,
              Error::kScope);
    return scope.owners[scope.ranges[f].first + m];
  };
  // Join each occurrence's real requirements once. Domain forwarding remains
  // the ordinary Guarded/SourceAccess/Language lattice; an initialized self
  // reference is not permission to strengthen any other outer cell.
  std::size_t next = 0;
  for (const auto& stamp : scope.owners) {
    q.Step();
    const auto& function =
        input.families[stamp.family].family.members[stamp.member].function;
    const auto uses = function.Capture().Input().Captures();
    std::vector<std::pair<Address, CaptureDomain>> domains;
    q.Reserve(domains, uses.size());
    for (const auto& use : uses) {
      q.Step();
      domains.emplace_back(Key({use.cell.depth, use.cell.slot}), use.domain);
    }
    std::sort(domains.begin(), domains.end(),
              [&](const auto& a, const auto& b) {
                q.Step();
                return a.first < b.first;
              });
    std::size_t count = 0;
    for (const auto& item : domains) {
      q.Step();
      if (count && domains[count - 1].first == item.first)
        domains[count - 1].second =
            JoinCaptureRequirement(domains[count - 1].second, item.second);
      else
        domains[count++] = item;
    }
    domains.resize(count);
    while (next < raw.captures.size() &&
           raw.captures[next].family == stamp.family &&
           raw.captures[next].member == stamp.member) {
      q.Step();
      const auto& capture = raw.captures[next++];
      const auto key = Key(capture.incoming);
      const auto found = std::lower_bound(domains.begin(), domains.end(), key,
                                          [&](const auto& a, auto k) {
                                            q.Step();
                                            return a.first < k;
                                          });
      // A pure forwarding cell need not have an original instruction access;
      // its requirement is carried by the child's already verified storage.
      q.Require(found != domains.end() && found->first == key, Error::kScope);
      scope.requirements.push_back({stamp, capture.incoming, capture.creation,
                                    capture.self
                                        ? ClassSelfRequirementKind::kClassInner
                                        : ClassSelfRequirementKind::kOuter,
                                    found->second});
    }
  }
  q.Require(next == raw.captures.size(), Error::kScope);
  // Resolve read -> incoming address in THAT owner's plan, then join on the
  // address and occurrence. No numeric CellId crosses an owner boundary.
  std::vector<const ClassSelfRequirement*> requirements;
  q.Reserve(requirements, scope.requirements.size());
  for (const auto& r : scope.requirements) {
    q.Step();
    requirements.push_back(&r);
  }
  const auto key = [](const ClassSelfRequirement& r) {
    return std::tuple{r.owner.family, r.owner.member, Key(r.incoming)};
  };
  std::sort(requirements.begin(), requirements.end(), [&](auto a, auto b) {
    q.Step();
    return key(*a) < key(*b);
  });
  for (const auto& read : raw.reads) {
    q.Step();
    const auto& stamp = owner(read.family, read.member);
    const auto& function =
        input.families[read.family].family.members[read.member].function;
    const auto* cells = function.Cells();
    q.failure.location =
        OperationLocation(function.Graph(), read.read, stamp.method);
    q.failure.read = read.read;
    q.failure.guard = read.guard;
    q.Require(cells && read.read.value < cells->operations.size() &&
                  cells->operations[read.read.value],
              Error::kStorage);
    const auto id = *cells->operations[read.read.value];
    q.Require(id.value < cells->cells.size(), Error::kStorage);
    const auto* address =
        std::get_if<IncomingCell>(&cells->cells[id.value].storage);
    q.Require(address, Error::kStorage);
    const auto at = std::tuple{read.family, read.member, Key(*address)};
    const auto found =
        std::lower_bound(requirements.begin(), requirements.end(), at,
                         [&](const auto* r, auto k) {
                           q.Step();
                           return key(*r) < k;
                         });
    q.Require(found != requirements.end() && key(**found) == at &&
                  (*found)->kind == ClassSelfRequirementKind::kClassInner,
              Error::kScope);
    scope.references.push_back(
        {stamp, *address, read.read, read.guard, read.value, read.exception});
  }
  std::sort(scope.references.begin(), scope.references.end(),
            [&](const auto& a, const auto& b) {
              q.Step();
              return std::tuple{a.owner.family, a.owner.member, a.read} <
                     std::tuple{b.owner.family, b.owner.member, b.read};
            });
  const auto& graph = input.creator.values.graph;
  q.failure.location = OperationLocation(graph, raw.call.call, raw.creator);
  q.failure.read.reset();
  q.failure.guard.reset();
  auto ordered = BuildEffectOrder(graph, q.left);
  if (const auto* error = std::get_if<ValueAvailabilityError>(&ordered)) {
    q.Require(false, *error == ValueAvailabilityError::kResourceLimit
                         ? Error::kResourceLimit
                     : *error == ValueAvailabilityError::kWorkLimit
                         ? Error::kWorkLimit
                         : Error::kOrder);
  }
  const auto& order = std::get<EffectOrder>(ordered);
  q.Admit(order.allocated_bytes);
  q.Step(order.work);
  auto defined = FindClassDefinitions(graph, q.left);
  const auto* definitions = std::get_if<ClassDefinitionSites>(&defined);
  q.Require(definitions,
            ClassSelfResource(defined).value_or(Error::kCorrespondence));
  q.Admit(definitions->allocated_bytes);
  q.Step(definitions->work);
  q.Reserve(scope.installations, definitions->sites.size());
  std::vector<bool> corridor;
  q.Reserve(corridor, graph.ops.size());
  corridor.resize(graph.ops.size());
  std::size_t required = 0;
  const auto include = [&](OpId id) {
    q.Step();
    q.Require(
        id.value < graph.ops.size() && HasOrderedEffect(graph.ops[id.value]),
        Error::kOrder);
    if (!corridor[id.value]) {
      corridor[id.value] = true;
      ++required;
    }
  };
  include(raw.call.method);
  include(raw.call.call);
  std::optional<TreeConstants> constants;
  for (const auto& definition : definitions->sites) {
    q.Step();
    if (definition.creation != raw.call.creation) continue;
    q.failure.location =
        OperationLocation(graph, definition.definition, raw.creator);
    q.Require(order.Before(graph, raw.store,
                           graph.ops[definition.definition.value].block,
                           graph.ops[definition.definition.value].order) &&
                  order.Before(graph, definition.definition,
                               graph.ops[raw.call.call.value].block,
                               graph.ops[raw.call.call.value].order),
              Error::kOrder);
    include(definition.definition);
    for (const auto& method : definition.Methods()) {
      q.Step();
      if (method.operation) include(*method.operation);
    }
    if (definition.prototype) {
      if (!constants) {
        auto resolved = ResolveTreeConstants(
            input.file, input.header, input.index, raw.creator, graph, q.left);
        const auto* c = std::get_if<TreeConstants>(&resolved);
        q.Require(c, ClassSelfResource(resolved).value_or(Error::kName));
        constants.emplace(std::get<TreeConstants>(std::move(resolved)));
        q.Admit(constants->allocated_bytes);
        q.Step(constants->work);
      }
      const ClassPrototypeRead read{
          *definition.prototype, raw.call.creation, {}};
      q.Require(HasClassPrototypeKey(read, constants->View(), constants->names,
                                     constants->texts),
                Error::kName);
      include(*definition.prototype);
    }
    scope.installations.push_back(definition.definition);
  }
  auto read_prototypes = FindClassPrototypeReads(graph, q.left);
  const auto* prototypes = std::get_if<ClassPrototypeReads>(&read_prototypes);
  q.Require(
      prototypes,
      ClassSelfResource(read_prototypes).value_or(Error::kCorrespondence));
  q.Admit(prototypes->allocated_bytes);
  q.Step(prototypes->work);
  for (const auto& read : prototypes->reads) {
    q.Step();
    if (read.creation != raw.call.creation) continue;
    const auto& op = graph.ops[read.read.value];
    if (order.Before(graph, raw.call.call, op.block, op.order)) continue;
    q.Require(order.Before(graph, raw.store, op.block, op.order),
              Error::kOrder);
    if (!constants) {
      auto resolved = ResolveTreeConstants(
          input.file, input.header, input.index, raw.creator, graph, q.left);
      q.Require(std::get_if<TreeConstants>(&resolved),
                ClassSelfResource(resolved).value_or(Error::kName));
      constants.emplace(std::get<TreeConstants>(std::move(resolved)));
      q.Admit(constants->allocated_bytes);
      q.Step(constants->work);
    }
    q.Require(HasClassPrototypeKey(read, constants->View(), constants->names,
                                   constants->texts),
              Error::kName);
    q.Require(op.result.has_value(), Error::kCorrespondence);
    for (auto u = graph.values[op.result->value].first_use; u;
         u = graph.uses[u->value].next) {
      q.Step();
      const auto* consumer = std::get_if<OpId>(&graph.uses[u->value].owner);
      q.Require(consumer && corridor[consumer->value], Error::kOrder);
    }
    include(read.read);
  }
  // Pinned RuntimeDefineGetterSetterByValue installs descriptors without
  // invoking their functions. Constant keys + fresh home + this must-reach
  // corridor prove methods installed before the first initializer observation.
  // This is not generic permission to move a definition past a user effect.
  auto current = raw.call.call;
  std::size_t seen = 0;
  while (current != raw.store) {
    q.Step();
    q.Require(current.value < corridor.size() && corridor[current.value],
              Error::kOrder);
    corridor[current.value] = false;
    ++seen;
    const auto* prior = std::get_if<OpId>(&order.before[current.value]);
    q.Require(prior && order.after[prior->value] == current, Error::kOrder);
    current = *prior;
  }
  q.Require(seen == required, Error::kOrder);
  // The internal slot is initialized before the initializer; the public
  // binding is not. Preserve original use identities for every publication.
  q.Require(graph.uses.size() <= UINT32_MAX - 4, Error::kResourceLimit);
  q.Reserve(scope.ports, 4 + graph.uses.size());
  const auto port = [&](ClassSelfPortKind kind, OpId op,
                        std::optional<UseId> use = {},
                        std::optional<ValueId> value = {}) {
    q.Step();
    q.Require(op.value < graph.ops.size(), Error::kCorrespondence);
    scope.ports.push_back({kind, op, graph.ops[op.value].origin, use, value});
  };
  port(ClassSelfPortKind::kClassAvailable, raw.call.creation, {},
       raw.call.receiver);
  const auto& store = graph.ops[raw.store.value];
  q.Require(store.use_count == 1, Error::kStorage);
  port(ClassSelfPortKind::kInternalCommit, raw.store, store.first_use,
       raw.call.receiver);
  port(ClassSelfPortKind::kInitializerBefore, raw.call.call);
  port(ClassSelfPortKind::kInitializerCommit, raw.call.call);
  std::vector<bool> values;
  q.Reserve(values, graph.values.size());
  values.resize(graph.values.size());
  values[raw.call.receiver.value] = true;
  const auto cell = FindCell(*input.creator.values.cells,
                             AllocatedCell{raw.allocation, raw.slot});
  q.Step(std::bit_width(input.creator.values.cells->cells.size()) + 1);
  q.Require(cell.has_value(), Error::kStorage);
  const auto& local = input.creator.values.cells->cells[cell->value];
  for (std::uint32_t a = 0; a < local.count; ++a) {
    q.Step();
    const auto& access = input.creator.values.cells->accesses[local.first + a];
    if (access.kind != CellAccessKind::kLoad) continue;
    const auto& op = graph.ops[access.operation.value];
    q.Require(
        op.result && order.Before(graph, raw.call.call, op.block, op.order),
        Error::kOrder);
    values[op.result->value] = true;
  }
  for (std::uint32_t u = 0; u < graph.uses.size(); ++u) {
    q.Step();
    const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive || use.role != OperandRole::kStoredValue)
      continue;
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (!value || !values[value->value]) continue;
    const auto* op = std::get_if<OpId>(&use.owner);
    q.Require(op, Error::kCorrespondence);
    if (*op == raw.store) continue;
    const auto& write = graph.ops[op->value];
    q.failure.location = OperationLocation(graph, *op, raw.creator);
    q.failure.use = UseId{u};
    q.Require(order.Before(graph, raw.call.call, write.block, write.order),
              Error::kOrder);
    port(ClassSelfPortKind::kPublicCommit, *op, UseId{u}, *value);
  }
  // A creator need not publish the class itself (it may export a method's
  // result). Preserve each use of the retained class, rather than inventing a
  // class-valued publication from that different result. Existing public-store
  // certificates keep their original port inventory.
  bool published = false;
  for (const auto& p : scope.ports) {
    q.Step();
    published |= p.kind == ClassSelfPortKind::kPublicCommit;
  }
  if (!published) for (std::uint32_t u = 0; u < graph.uses.size(); ++u) {
    q.Step();
    const auto& use = graph.uses[u];
    if (use.life != ValueLife::kLive) continue;
    const auto* value = std::get_if<ValueId>(&use.operand);
    if (!value || !values[value->value]) continue;
    const auto* owner = std::get_if<OpId>(&use.owner);
    q.Require(owner, Error::kCorrespondence);
    const auto& op = graph.ops[owner->value];
    // Installation and the initializer's own receiver are already certified
    // before/at StaticInitialize. Only successful continuation uses belong to
    // CompletedClass; a use on neither side of the call remains unproved.
    if (*owner == raw.call.call ||
        order.Before(graph, *owner, graph.ops[raw.call.call.value].block,
                     graph.ops[raw.call.call.value].order)) continue;
    q.failure.location = OperationLocation(graph, *owner, raw.creator);
    q.failure.use = UseId{u};
    q.Require(order.Before(graph, raw.call.call, op.block, op.order),
              Error::kOrder);
    port(ClassSelfPortKind::kCompletedClass, *owner, UseId{u}, *value);
  }
  for (const auto& cleanup : raw.cleanup) {
    q.Step();
    scope.cleanup.push_back(cleanup);
  }
  scope.initializer_exception = raw.initializer_exception;
  return scope;
}
}  // namespace
const ClassInnerReference* ClassSelfScopeProof::FindReference(
    const ClassSelfFamilyStamp& owner, analysis::OpId read) const {
  const auto key = std::tuple{owner.family, owner.member, read};
  const auto found = std::lower_bound(
      evidence_.references.begin(), evidence_.references.end(), key,
      [](const auto& ref, auto at) {
        return std::tuple{ref.owner.family, ref.owner.member, ref.read} < at;
      });
  if (found == evidence_.references.end() || found->owner != owner ||
      found->read != read)
    return nullptr;
  return &*found;
}
std::variant<ClassSelfScopeProof, ClassSelfInitializationFailure>
ClassSelfScopeProof::Build(const ClassSelfInitializationInput& input,
                           const ClassSelfInitialization& raw,
                           analysis::SemanticBudget budget) {
  Check q(input, budget);
  try {
    ClassSelfScopeProof result;
    result.evidence_ = Derive(input, raw, q);
    result.cost_ = q.Cost();
    return result;
  } catch (const ClassSelfInitializationFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
std::variant<ClassSelfInitializationCost, ClassSelfInitializationFailure>
VerifyClassSelfScope(const ClassSelfInitializationInput& input,
                     const ClassSelfInitialization& raw,
                     const ClassSelfScope& proposed,
                     analysis::SemanticBudget budget) {
  Check q(input, budget);
  try {
    auto fresh = Derive(input, raw, q);
    q.Step(fresh.ranges.size() + fresh.owners.size() +
           fresh.requirements.size() + fresh.references.size() +
           fresh.ports.size() + fresh.cleanup.size() +
           fresh.installations.size());
    q.Require(fresh.binding == proposed.binding, Error::kStorage);
    q.Require(fresh.ranges == proposed.ranges &&
                  fresh.owners == proposed.owners &&
                  fresh.requirements == proposed.requirements,
              Error::kScope);
    q.Require(fresh.references == proposed.references, Error::kGuard);
    q.Require(fresh.ports == proposed.ports, Error::kOrder);
    q.Require(fresh.installations == proposed.installations, Error::kOrder);
    q.Require(fresh.cleanup == proposed.cleanup &&
                  fresh.initializer_exception == proposed.initializer_exception,
              Error::kCatchState);
    return q.Cost();
  } catch (const ClassSelfInitializationFailure& f) {
    return f;
  } catch (const std::bad_alloc&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  } catch (const std::length_error&) {
    q.failure.kind = Error::kResourceLimit;
    return q.failure;
  }
}
}  // namespace abcd::source
