#include "template_construction.h"

#include <bit>
#include <cmath>
#include <limits>

#include "effect_order.h"
namespace abcd::source {
namespace {
using namespace analysis;
constexpr auto kProof = TreeStructureError::kOwnership;
struct Definition {
  OpId op;
  ValueOperand value;
  std::optional<std::uint32_t> index;
};
bool Same(ValueOperand a, ValueOperand c) {
  if (a.index() != c.index()) return false;
  return std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, SpecialValue>)
          return v == std::get<T>(c);
        else
          return v.value == std::get<T>(c).value;
      },
      a);
}
class Derive {
 public:
  Derive(const ValueProgram& g, std::span<const ValueId> aliases,
         TreeCheckBudget& b)
      : g(g), aliases(aliases), b(b) {}
  ValueOperand Resolve(ValueOperand v) const {
    if (auto* id = std::get_if<ValueId>(&v); id && !aliases.empty())
      *id = aliases[id->value];
    return v;
  }
  ValueOperand Input(OpId id, unsigned i) const {
    const auto& o = g.ops[id.value];
    RequireTree(i < o.use_count, kProof);
    return Resolve(g.uses[o.first_use.value + i].operand);
  }
  std::optional<OpId> Producer(ValueOperand v) const {
    v = Resolve(v);
    if (const auto* id = std::get_if<ValueId>(&v)) {
      RequireTree(id->value < g.values.size(), kProof);
      if (const auto* r = std::get_if<OpResult>(&g.values[id->value].origin))
        return r->op;
    }
    return {};
  }
  ValueOperand Constant(ValueOperand v) const {
    v = Resolve(v);
    if (auto op = Producer(v);
        op && std::holds_alternative<ValueCopy>(g.ops[op->value].operation) &&
        g.ops[op->value].use_count == 1)
      return Input(*op, 0);
    return v;
  }
  std::optional<std::uint32_t> Index(ValueOperand v) const {
    v = Constant(v);
    if (const auto* n = std::get_if<analysis::IntegerLiteral>(&v)) {
      if (n->value < 0) return {};
      return static_cast<std::uint32_t>(n->value);
    }
    if (const auto* n = std::get_if<Float64Bits>(&v)) {
      const auto d = std::bit_cast<double>(n->value);
      if (!std::isfinite(d) || d < 0 || d > UINT32_MAX || std::floor(d) != d)
        return {};
      return static_cast<std::uint32_t>(d);
    }
    return {};
  }
  OpId Array(ValueOperand v) const {
    const auto op = Producer(v);
    RequireTree(op.has_value(), kProof);
    const auto* a =
        std::get_if<ValueArrayAllocation>(&g.ops[op->value].operation);
    RequireTree(a && !a->literal, kProof);
    return *op;
  }
  std::optional<OpId> Text(ValueOperand v, bool undefined) const {
    v = Constant(v);
    if (const auto* s = std::get_if<SpecialValue>(&v);
        s && *s == SpecialValue::kUndefined) {
      RequireTree(undefined, kProof);
      return {};
    }
    const auto op = Producer(v);
    RequireTree(op.has_value(), kProof);
    const auto* text = std::get_if<ValueString>(&g.ops[op->value].operation);
    RequireTree(text && text->kind == PoolTextKind::kString, kProof);
    return op;
  }
  TemplateConstructions Run(SourceContract contract) {
    std::size_t count = 0;
    for (const auto& op : g.ops) {
      b.Step();
      count += IsTemplateObject(op);
    }
    if (!count) return {};
    RequireTree(
        TaggedTemplates(contract),
        kProof);
    RequireTree(aliases.empty() || aliases.size() == g.values.size(), kProof);
    TemplateConstructions p;
    b.Array(p.sites, count);
    b.Array(p.owners, g.ops.size());
    b.Array(p.hidden, g.uses.size());
    std::vector<std::vector<Definition>> definitions;
    b.Array(definitions, g.ops.size());
    // Index every definition once, by the original allocation identity. The
    // sequence is later checked through must-reach edges, never physical PCs.
    std::vector<std::uint32_t> counts;
    b.Array(counts, g.ops.size());
    auto defined = [&](OpId id) -> std::optional<std::pair<OpId, Definition>> {
      const auto& op = g.ops[id.value];
      if (op.life != ValueLife::kLive) return {};
      const auto* fixed = std::get_if<ValueIndexedWrite>(&op.operation);
      const auto* computed = std::get_if<ValueComputedOwnWrite>(&op.operation);
      if (!fixed && !computed) return {};
      RequireTree(op.use_count == (fixed ? 2u : 3u), kProof);
      RequireTree(g.uses[op.first_use.value].role == OperandRole::kBase &&
                      g.uses[op.first_use.value + (fixed ? 1 : 2)].role ==
                          OperandRole::kStoredValue &&
                      (fixed || g.uses[op.first_use.value + 1].role ==
                                    OperandRole::kKey),
                  kProof);
      auto base = Producer(Input(id, 0));
      if (!base || !std::holds_alternative<ValueArrayAllocation>(
                       g.ops[base->value].operation))
        return {};
      if (fixed)
        RequireTree(fixed->kind == IndexedDefinition::kOwnStore, kProof);
      else
        RequireTree(computed->name_setting == PropertyNameSetting::kNone,
                    kProof);
      return std::pair{*base,
                       Definition{id, Input(id, fixed ? 1 : 2),
                                  fixed ? fixed->index : Index(Input(id, 1))}};
    };
    for (std::uint32_t i = 0; i < g.ops.size(); ++i) {
      b.Step();
      if (auto d = defined({i})) ++counts[d->first.value];
    }
    for (std::size_t i = 0; i < counts.size(); ++i) {
      b.Step();
      b.Admit(counts[i], sizeof(Definition));
      definitions[i].reserve(counts[i]);
    }
    for (std::uint32_t i = 0; i < g.ops.size(); ++i) {
      b.Step();
      if (auto d = defined({i}))
        definitions[d->first.value].push_back(d->second);
    }
    std::vector<std::optional<std::uint32_t>> exits;
    b.Array(exits, g.ops.size());
    for (std::uint32_t x = 0; x < g.exceptional_exits.size(); ++x) {
      b.Step();
      const auto& e = g.exceptional_exits[x];
      RequireTree(e.op.value < g.ops.size() && !exits[e.op.value], kProof);
      exits[e.op.value] = x;
    }
    auto flow = BuildEffectOrder(g, b.Remaining());
    if (const auto* e = std::get_if<ValueAvailabilityError>(&flow))
      throw *e == ValueAvailabilityError::kResourceLimit
          ? TreeStructureError::kResourceLimit
      : *e == ValueAvailabilityError::kWorkLimit
          ? TreeStructureError::kWorkLimit
          : kProof;
    auto order = std::get<EffectOrder>(std::move(flow));
    b.Admit(order.allocated_bytes, 1);
    b.Step(order.work);
    std::vector<std::optional<UseId>> first;
    b.Array(first, g.values.size());
    std::vector<std::uint32_t> uses;
    b.Array(uses, g.values.size());
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      b.Step();
      const auto& use = g.uses[u];
      if (use.life != ValueLife::kLive) continue;
      const auto* owner = std::get_if<OpId>(&use.owner);
      if (owner &&
          std::holds_alternative<ValueCopy>(g.ops[owner->value].operation) &&
          g.ops[owner->value].use_count == 1 && g.ops[owner->value].result &&
          Same(Resolve(*g.ops[owner->value].result), Resolve(use.operand)))
        continue;
      if (const auto* value = std::get_if<ValueId>(&use.operand)) {
        const auto resolved = std::get<ValueId>(Resolve(*value));
        ++uses[resolved.value];
        first[resolved.value] = UseId{u};
      }
    }
    std::size_t next = 0;
    for (std::uint32_t i = 0; i < g.ops.size(); ++i) {
      b.Step();
      OpId id{i};
      const auto& op = g.ops[i];
      if (!IsTemplateObject(op)) continue;
      RequireTree(op.use_count == 1 && op.result, kProof);
      const auto result = std::get<ValueId>(Resolve(*op.result));
      RequireTree(uses[result.value] == 1 && first[result.value], kProof);
      const auto argument = *first[result.value];
      const auto* call = std::get_if<OpId>(&g.uses[argument.value].owner);
      RequireTree(call, kProof);
      const auto& invocation = g.ops[call->value];
      const auto* invoke = std::get_if<ValueInvoke>(&invocation.operation);
      RequireTree(invoke && invoke->form == CallArgumentForm::kRegisters &&
                      invoke->arguments >= 1,
                  kProof);
      const auto prefix =
          invoke->receiver == InvokeReceiver::kExplicit ? 2u : 1u;
      RequireTree(argument.value == invocation.first_use.value + prefix,
                  kProof);
      auto& site = p.sites[next++];
      site.operation = id;
      site.call = *call;
      site.argument = argument;
      const auto carrier = Array(Input(id, 0));
      auto& pair = definitions[carrier.value];
      RequireTree(pair.size() == 2, kProof);
      std::sort(pair.begin(), pair.end(), [&](const auto& a, const auto& c) {
        b.Step();
        return a.index < c.index;
      });
      RequireTree(pair[0].index == 0 && pair[1].index == 1, kProof);
      const auto raw = Array(pair[0].value), cooked = Array(pair[1].value);
      auto& r = definitions[raw.value];
      auto& c = definitions[cooked.value];
      RequireTree(
          !r.empty() && r.size() == c.size() && invoke->arguments == r.size(),
          kProof);
      auto indices = [&](auto& d) {
        std::sort(d.begin(), d.end(), [&](const auto& a, const auto& c) {
          b.Step();
          return a.index < c.index;
        });
        for (std::uint32_t n = 0; n < d.size(); ++n) {
          b.Step();
          RequireTree(d[n].index == n, kProof);
        }
      };
      indices(r);
      indices(c);
      b.Array(site.quasis, r.size());
      for (std::size_t n = 0; n < r.size(); ++n) {
        b.Step();
        site.quasis[n] = {*Text(r[n].value, false), Text(c[n].value, true)};
      }
      auto own = [&](OpId event) {
        RequireTree(!p.owners[event.value] || p.owners[event.value] == id,
                    kProof);
        p.owners[event.value] = id;
      };
      for (const auto a : {carrier, raw, cooked}) {
        b.Step();
        own(a);
        for (const auto& d : definitions[a.value]) {
          b.Step();
          own(d.op);
        }
      }
      // Every dynamic own definition must follow the previous index's Commit.
      // Must-reach successors then certify interleaving of the three private
      // arrays as one region, with no calls/getters/control omissions absorbed.
      for (const auto a : {carrier, raw, cooked}) {
        auto previous = a;
        for (const auto& d : definitions[a.value]) {
          b.Step();
          RequireTree(order.Before(g, previous, g.ops[d.op.value].block,
                                   g.ops[d.op.value].order),
                      kProof);
          previous = d.op;
        }
      }
      OpId cursor = raw;
      if (order.Before(g, cooked, g.ops[raw.value].block,
                       g.ops[raw.value].order))
        cursor = cooked;
      if (order.Before(g, carrier, g.ops[cursor.value].block,
                       g.ops[cursor.value].order))
        cursor = carrier;
      std::size_t events = (raw == cooked ? 2u : 3u) + r.size() +
                           (raw == cooked ? 0 : c.size()) + pair.size();
      b.Admit(events, sizeof(OpId));
      site.events.reserve(events);
      while (cursor != id) {
        b.Step();
        RequireTree(site.events.size() < events && p.owners[cursor.value] == id,
                    kProof);
        site.events.push_back(cursor);
        const auto next_event = order.after[cursor.value];
        RequireTree(next_event.has_value(), kProof);
        RequireTree(
            order.before[next_event->value] == EffectPredecessor{cursor},
            kProof);
        cursor = *next_event;
      }
      RequireTree(site.events.size() == events, kProof);
      // Private own definitions on fresh arrays with primitive keys cannot
      // invoke JS. Their remaining allocation failures are excluded by the
      // existing resource premise. Preserve each raw exception dispatch anyway;
      // recovery must never borrow the tag invocation's handler for these
      // ports.
      b.Admit(events + 2, sizeof(TemplateExceptionPort));
      site.exceptions.reserve(events + 2);
      const auto exception = [&](OpId event) {
        b.Step();
        const auto& operation = g.ops[event.value];
        if (operation.exceptional == ExceptionalCompletion::kImpossible) {
          RequireTree(!exits[event.value], kProof);
          return;
        }
        RequireTree(operation.exceptional == ExceptionalCompletion::kMayThrow &&
                        exits[event.value],
                    kProof);
        const auto& exit = g.exceptional_exits[*exits[event.value]];
        RequireTree(exit.origin == operation.origin &&
                        exit.before_throw == operation.before_throw,
                    kProof);
        site.exceptions.push_back(
            {event, exit.origin, exit.before_throw, exit.handler});
      };
      for (const auto event : site.events) exception(event);
      exception(id);
      exception(*call);
    }
    // Immutable constants may be shared. Array identities may only occur as
    // definition bases, the carrier's two values, and the intrinsic's input.
    // Catch phis, user observations and early escapes are real consumers too.
    for (std::uint32_t u = 0; u < g.uses.size(); ++u) {
      b.Step();
      const auto& use = g.uses[u];
      if (use.life != ValueLife::kLive) continue;
      auto producer = Producer(use.operand);
      const auto* owner = std::get_if<OpId>(&use.owner);
      if (producer && p.owners[producer->value] &&
          std::holds_alternative<ValueArrayAllocation>(
              g.ops[producer->value].operation)) {
        const auto root = *p.owners[producer->value];
        RequireTree(
            owner && (p.owners[owner->value] == root || *owner == root ||
                      (std::holds_alternative<ValueCopy>(
                           g.ops[owner->value].operation) &&
                       g.ops[owner->value].result &&
                       Same(Resolve(*g.ops[owner->value].result),
                            Resolve(use.operand)))),
            kProof);
        p.hidden[u] = 1;
      }
      if (owner &&
          (p.owners[owner->value] || IsTemplateObject(g.ops[owner->value])))
        p.hidden[u] = 1;
    }
    // Transport copies have no source storage. The alias verifier separately
    // proves their identity; no private value is default-mapped to a JS symbol.
    for (std::uint32_t i = 0; i < g.ops.size(); ++i) {
      b.Step();
      const auto& op = g.ops[i];
      if (op.life != ValueLife::kLive ||
          !std::holds_alternative<ValueCopy>(op.operation) || !op.result)
        continue;
      auto source = Producer(Resolve(*op.result));
      if (source && p.owners[source->value])
        p.owners[i] = p.owners[source->value];
    }
    return p;
  }

 private:
  const ValueProgram& g;
  std::span<const ValueId> aliases;
  TreeCheckBudget& b;
};
}  // namespace
bool IsTemplateObject(const analysis::ValueOp& op) {
  const auto* unary = std::get_if<analysis::ValueUnary>(&op.operation);
  return op.life == analysis::ValueLife::kLive && unary &&
         unary->op == analysis::UnaryOperator::kTemplateObject;
}
TemplateConstructions BuildTemplateConstructions(
    const analysis::ValueProgram& g, std::span<const analysis::ValueId> aliases,
    SourceContract contract, TreeCheckBudget& b) {
  return Derive(g, aliases, b).Run(contract);
}
bool VerifyTemplateConstructions(const analysis::ValueProgram& g,
                                 std::span<const analysis::ValueId> aliases,
                                 SourceContract contract,
                                 const TemplateConstructions& p,
                                 TreeCheckBudget& b) {
  const auto fresh = BuildTemplateConstructions(g, aliases, contract, b);
  return fresh.sites == p.sites && fresh.owners == p.owners &&
         fresh.hidden == p.hidden;
}
}  // namespace abcd::source
