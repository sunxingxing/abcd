#include "abcd/analysis/environment_renewal.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace abcd::analysis {
namespace {
using Error = EnvironmentRenewalError;
void Need(bool yes, Error error, abc::CodeOffset pc = {}) {
  if (!yes) throw EnvironmentRenewalFailure{error, pc};
}
class Quota {
 public:
  explicit Quota(SemanticBudget b) : initial_(b), left_(b) {}
  void Step(std::uint64_t n = 1) {
    Need(n <= left_.work, Error::kWorkLimit);
    left_.work -= n;
  }
  void Admit(std::size_t n, std::size_t w = 1) {
    Need(n <= left_.bytes / w, Error::kResourceLimit);
    left_.bytes -= n * w;
  }
  template <class T>
  void Reserve(std::vector<T>& v, std::size_t n) {
    Admit(n, sizeof(T));
    v.reserve(n);
    Admit(v.capacity() - n, sizeof(T));
  }
  template <class T>
  void Size(std::vector<T>& v, std::size_t n) {
    Step(n);
    Reserve(v, n);
    v.resize(n);
  }
  SemanticBudget Left() const { return left_; }
  std::uint64_t Work() const { return initial_.work - left_.work; }

 private:
  SemanticBudget initial_, left_;
};
template <class T>
Error Cause(const T& failure, Error fallback) {
  if constexpr (std::is_enum_v<T>) {
    if constexpr (requires { T::kResourceLimit; })
      if (failure == T::kResourceLimit) return Error::kResourceLimit;
    if constexpr (requires { T::kWorkLimit; })
      if (failure == T::kWorkLimit) return Error::kWorkLimit;
  } else if constexpr (requires { failure.kind; })
    return Cause(failure.kind, fallback);
  return fallback;
}
template <class... T>
Error Cause(const std::variant<T...>& failure, Error fallback) {
  return std::visit([&](const auto& item) { return Cause(item, fallback); },
                    failure);
}
struct Union {
  std::vector<std::uint32_t> parent;
  Quota& q;
  Union(std::size_t n, Quota& b) : q(b) {
    q.Size(parent, n);
    std::iota(parent.begin(), parent.end(), 0);
  }
  std::uint32_t Find(std::uint32_t n) {
    Need(n < parent.size(), Error::kInput);
    auto root = n;
    while (root != parent[root]) {
      q.Step();
      root = parent[root];
    }
    while (n != root) {
      q.Step();
      const auto next = parent[n];
      parent[n] = root;
      n = next;
    }
    return root;
  }
  void Join(std::uint32_t a, std::uint32_t b) {
    a = Find(a);
    b = Find(b);
    if (a != b) parent[std::max(a, b)] = std::min(a, b);
  }
};
bool Captures(const SemanticOperation& op) {
  return std::holds_alternative<FunctionCreation>(op) ||
         std::holds_alternative<ClassCreation>(op) ||
         std::holds_alternative<BufferedObjectCreation>(op) ||
         std::holds_alternative<BufferedArrayCreation>(op);
}
struct Builder {
  const ExceptionFlow& f;
  const ExceptionSsa& values;
  Quota q;
  EnvironmentRenewal out;
  NormalFlow graph;
  std::vector<std::array<std::uint8_t, kMaxNormalSuccessors>> original_edges;
  std::vector<std::optional<std::uint32_t>> depth;
  std::vector<ExceptionNodeId> commits, befores;
  std::vector<std::optional<EnvironmentId>> physical;
  std::vector<std::optional<EnvironmentId>> token_family;
  std::vector<std::uint32_t> order;
  std::vector<std::optional<std::uint32_t>> transfer;
  std::vector<std::optional<LexicalBinding>> bindings;
  std::optional<DominatorTree> dom;

  Builder(const ExceptionFlow& flow, const ExceptionSsa& s,
          EnvironmentRenewalResources resources, SemanticBudget b)
      : f(flow), values(s), q(b) {
    out.resources = resources;
  }
  abc::CodeOffset Pc(std::uint32_t n) const {
    return f.instructions[f.nodes[n].instruction.value].pc;
  }
  const SemanticOperation& Op(std::uint32_t n) const {
    return f.instructions[f.nodes[n].instruction.value].operation;
  }
  bool Commit(std::uint32_t n) const {
    return f.nodes[n].phase == ExceptionPhase::kCommit;
  }
  std::uint32_t AfterDepth(std::uint32_t n) {
    auto d = *depth[n];
    if (Commit(n)) {
      if (std::holds_alternative<PushEnvironment>(Op(n))) {
        Need(d != UINT32_MAX, Error::kResourceLimit);
        ++d;
      }
      if (std::holds_alternative<PopEnvironment>(Op(n))) {
        Need(d != 0, Error::kParent, Pc(n));
        --d;
      }
    }
    return d;
  }
  void Graph() {
    Need(out.resources == EnvironmentRenewalResources::kObservable ||
             out.resources == EnvironmentRenewalResources::kExcluded,
         Error::kInput);
    Need(!f.nodes.empty() && f.nodes.size() < UINT32_MAX, Error::kInput);
    q.Size(graph.blocks, f.nodes.size());
    q.Size(depth, f.nodes.size());
    q.Size(original_edges, f.nodes.size());
    q.Size(commits, f.instructions.size());
    q.Size(befores, f.instructions.size());
    q.Size(physical, f.nodes.size());
    q.Size(out.coverage, f.instructions.size());
    q.Reserve(out.excluded, f.nodes.size());
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n) {
      q.Step();
      const auto& node = f.nodes[n];
      Need(node.instruction.value < f.instructions.size() &&
               node.successor_count <= node.successors.size(),
           Error::kInput);
      auto& b = graph.blocks[n];
      b.begin = b.end = Pc(n);
      b.first_instruction = n;
      b.instruction_count = 1;
      if (Commit(n)) commits[node.instruction.value] = {n};
      if (node.phase == ExceptionPhase::kBefore)
        befores[node.instruction.value] = {n};
      for (std::uint8_t e = 0; e < node.successor_count; ++e) {
        q.Step();
        const auto edge = node.successors[e];
        Need(edge.target.value < f.nodes.size(), Error::kInput);
        if (edge.kind == ExceptionArcKind::kException &&
            f.instructions[node.instruction.value].effects.exception_cause ==
                ExceptionCause::kResourceExhaustion &&
            std::holds_alternative<PushEnvironment>(
                f.instructions[node.instruction.value].operation) &&
            out.resources == EnvironmentRenewalResources::kExcluded) {
          out.excluded.push_back({{n}, edge.target});
          continue;
        }
        original_edges[n][b.successor_count] = e;
        b.successors[b.successor_count++] = {BlockId{edge.target.value},
                                             FlowEdgeKind::kFallthrough};
      }
    }
    std::vector<std::uint32_t> pending;
    q.Reserve(pending, f.nodes.size());
    depth[0] = 0;
    pending.push_back(0);
    for (std::size_t at = 0; at < pending.size(); ++at) {
      const auto n = pending[at];
      q.Step();
      const auto after = AfterDepth(n);
      for (std::uint8_t e = 0; e < graph.blocks[n].successor_count; ++e) {
        q.Step();
        const auto target = graph.blocks[n].successors[e].target.value;
        Need(!depth[target] || *depth[target] == after, Error::kException,
             Pc(target));
        if (!depth[target]) {
          depth[target] = after;
          pending.push_back(target);
        }
      }
    }
    auto d = BuildNormalDominators(graph, {}, {q.Left().bytes, q.Left().work});
    const auto* tree = std::get_if<DominatorTree>(&d);
    Need(tree != nullptr, Cause(d, Error::kTopology));
    q.Step(tree->work.steps);
    q.Admit(tree->work.allocated_bytes);
    dom = std::move(*tree);
    q.Size(order, f.nodes.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      q.Step();
      return dom->nodes[a].begin < dom->nodes[b].begin;
    });
  }
  RegisterSsa Tokens() {
    std::vector<RegisterSite> sites;
    q.Size(sites, f.nodes.size());
    std::uint32_t maximum = 0;
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n) {
      q.Step();
      sites[n].pc = Pc(n);
      if (!depth[n]) continue;
      auto& effect = sites[n].effect;
      effect.reads[effect.read_count++] = abc::RegisterId{*depth[n]};
      maximum = std::max(maximum, *depth[n]);
      if (Commit(n) && std::holds_alternative<PushEnvironment>(Op(n))) {
        maximum = std::max(maximum, *depth[n] + 1);
        effect.reads[effect.read_count++] = abc::RegisterId{*depth[n] + 1};
        effect.write = abc::RegisterId{*depth[n] + 1};
      }
    }
    IdfQuota work{q.Left().work};
    auto s = RegisterSsa::BuildSites(sites, graph, std::uint64_t(maximum) + 1,
                                     work, {q.Left().bytes});
    q.Step(work.Consumed());
    auto* result = std::get_if<RegisterSsa>(&s);
    Need(result != nullptr, Cause(s, Error::kActivation));
    q.Admit(result->AllocatedBytes());
    return std::move(*result);
  }
  void Correspondence(const RegisterSsa& s) {
    q.Size(out.tokens, s.Values().size());
    q.Size(out.current, f.nodes.size());
    q.Reserve(out.inputs, s.AllInputs().size());
    q.Reserve(out.definitions, f.instructions.size());
    q.Reserve(out.allocations, f.instructions.size());
    Union families(s.Values().size(), q);
    for (const auto& input : s.AllInputs()) {
      q.Step();
      const auto* edge = std::get_if<SsaEdge>(&input.edge);
      Need(edge != nullptr, Error::kActivation);
      families.Join(input.phi.value, input.value.value);
      Need(edge->source.value < original_edges.size() &&
               edge->successor <
                   graph.blocks[edge->source.value].successor_count,
           Error::kActivation);
      out.inputs.push_back(
          {{input.phi.value},
           {input.value.value},
           {edge->source,
            original_edges[edge->source.value][edge->successor]}});
    }
    for (std::uint32_t v = 0; v < s.Values().size(); ++v) {
      q.Step();
      const auto& value = s.Values()[v];
      const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
      Need(reg != nullptr, Error::kInput);
      auto& t = out.tokens[v];
      t.depth = reg->value;
      if (const auto* site = std::get_if<InstructionValue>(&value.origin)) {
        t.kind = EnvironmentTokenKind::kAllocation;
        t.site = site->instruction.value;
        const auto parent = s.Reads(site->instruction);
        Need(!parent.empty(), Error::kParent);
        t.parent = EnvironmentTokenId{parent[0].value.value};
        Need(Commit(t.site) &&
                 std::holds_alternative<PushEnvironment>(Op(t.site)),
             Error::kActivation);
      } else if (const auto* phi = std::get_if<PhiValue>(&value.origin)) {
        t.kind = EnvironmentTokenKind::kPhi;
        t.site = phi->block.value;
      } else {
        t.kind = EnvironmentTokenKind::kEntry;
        t.site = reg->value;
      }
    }
    // Physical parents precede children. Family identity is a separate index;
    // neither a debug scope literal nor lexical-slot count merges definitions.
    std::vector<std::uint32_t> pushes;
    q.Reserve(pushes, f.instructions.size());
    for (auto n : order)
      if (depth[n] && Commit(n) &&
          std::holds_alternative<PushEnvironment>(Op(n)))
        pushes.push_back(n);
    std::sort(pushes.begin(), pushes.end(), [&](auto a, auto b) {
      q.Step();
      return std::pair{*depth[a], dom->nodes[a].begin} <
             std::pair{*depth[b], dom->nodes[b].begin};
    });
    std::vector<std::optional<EnvironmentId>> roots;
    q.Size(roots, s.Values().size());
    q.Size(token_family, s.Values().size());
    q.Reserve(out.families, pushes.size());
    for (auto n : pushes) {
      q.Step();
      const auto write = s.Write({n});
      Need(write.has_value(), Error::kActivation);
      const EnvironmentId id{
          static_cast<std::uint32_t>(out.definitions.size())};
      physical[n] = id;
      out.allocations.push_back({n, id});
      const auto root = families.Find(write->value);
      if (!roots[root]) roots[root] = id;
      out.families.push_back(*roots[root]);
      const auto read = s.Reads({n});
      Need(!read.empty(), Error::kActivation);
      EnvironmentRef parent = CapturedDepth{0};
      if (*depth[n]) {
        const auto p = roots[families.Find(read[0].value.value)];
        Need(p.has_value(), Error::kParent, Pc(n));
        parent = *p;
      }
      out.definitions.push_back({parent, std::get<PushEnvironment>(Op(n))});
      out.coverage[f.nodes[n].instruction.value] =
          EnvironmentRenewalRole::kAllocation;
    }
    for (std::uint32_t v = 0; v < s.Values().size(); ++v) {
      q.Step();
      token_family[v] = roots[families.Find(v)];
      if (token_family[v]) Need(out.tokens[v].depth != 0, Error::kActivation);
    }
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      if (depth[n]) {
        q.Step();
        const auto reads = s.Reads({n});
        Need(!reads.empty(), Error::kActivation);
        out.current[n] = EnvironmentTokenId{reads[0].value.value};
        Need(*depth[n] == 0 || token_family[reads[0].value.value].has_value(),
             Error::kActivation, Pc(n));
        if (Commit(n) && std::holds_alternative<PopEnvironment>(Op(n)))
          out.coverage[f.nodes[n].instruction.value] =
              EnvironmentRenewalRole::kDeparture;
      }
    // Nearest dominating departure at each depth. One dominator-order sweep,
    // not a backwards walk per allocation or a per-cell traversal of the CFG.
    std::vector<std::vector<std::uint32_t>> departures;
    std::uint32_t maximum = 0;
    for (const auto& d : depth)
      if (d) maximum = std::max(maximum, *d);
    q.Size(departures, std::size_t(maximum) + 1);
    std::vector<std::uint32_t> pop_counts;
    q.Size(pop_counts, departures.size());
    for (auto n : order)
      if (depth[n] && Commit(n) &&
          std::holds_alternative<PopEnvironment>(Op(n)))
        ++pop_counts[*depth[n]];
    for (std::size_t i = 0; i < departures.size(); ++i)
      q.Reserve(departures[i], pop_counts[i]);
    std::vector<std::uint32_t> family_sites;
    q.Size(family_sites, out.definitions.size());
    for (const auto family : out.families) ++family_sites[family.value];
    q.Reserve(out.events, pushes.size());
    for (auto n : order)
      if (depth[n]) {
        q.Step();
        const auto d = *depth[n];
        if (Commit(n) && std::holds_alternative<PopEnvironment>(Op(n)))
          departures[d].push_back(n);
        if (!physical[n]) continue;
        const auto read = s.Reads({n});
        Need(read.size() == 2, Error::kActivation);
        const auto old = read[1].value.value, fresh = s.Write({n})->value;
        if (!token_family[old] || token_family[old] != token_family[fresh] ||
            family_sites[token_family[fresh]->value] < 2)
          continue;
        auto& stack = departures[d + 1];
        while (!stack.empty() && !dom->Dominates({stack.back()}, {n})) {
          q.Step();
          stack.pop_back();
        }
        Need(!stack.empty(), Error::kTopology, Pc(n));
        const auto pop = stack.back();
        Need(out.current[pop]->value == old, Error::kActivation, Pc(n));
        const auto family = *token_family[fresh];
        Need(out.definitions[family.value].allocation.slots ==
                 out.definitions[physical[n]->value].allocation.slots,
             Error::kSlotMap, Pc(n));
        out.events.push_back(
            {{pop}, {n}, {old}, {fresh}, family, *physical[n]});
      }
    Need(!out.events.empty(), Error::kSourceScope);
  }
  std::vector<std::optional<SsaValueId>> ScalarRoots() {
    // Directed copy/phi equations: an unrelated mixed phi must not equate
    // its distinct inputs. The three-point lattice changes at most twice.
    const auto count = values.Values().size();
    std::vector<std::uint32_t> heads, next, targets, pending;
    enum class Identity : std::uint8_t { kUnknown, kValue, kMixed };
    std::vector<std::uint8_t> derived;
    std::vector<Identity> kind;
    std::vector<std::optional<SsaValueId>> roots;
    q.Size(heads, count);
    std::fill(heads.begin(), heads.end(), UINT32_MAX);
    q.Size(derived, count);
    q.Size(kind, count);
    q.Size(roots, count);
    const auto capacity = values.AllInputs().size() + values.AllReads().size();
    q.Reserve(next, capacity);
    q.Reserve(targets, capacity);
    q.Reserve(pending, 2 * count);
    const auto edge = [&](std::uint32_t source, std::uint32_t target) {
      q.Step();
      Need(next.size() < next.capacity(), Error::kResourceLimit);
      next.push_back(heads[source]);
      targets.push_back(target);
      heads[source] = static_cast<std::uint32_t>(next.size() - 1);
      derived[target] = 1;
    };
    for (const auto& input : values.AllInputs())
      edge(input.value.value, input.phi.value);
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      if (f.nodes[n].phase == ExceptionPhase::kBefore) {
        q.Step();
        const auto* assignment = std::get_if<ScalarAssignment>(&Op(n));
        if (!assignment ||
            !std::holds_alternative<ScalarValue>(assignment->expression))
          continue;
        const auto read = values.Reads({n});
        const auto write = values.Write(commits[f.nodes[n].instruction.value]);
        if (read.size() == 1 && write) edge(read[0].value.value, write->value);
      }
    for (std::uint32_t v = 0; v < count; ++v)
      if (!derived[v] &&
          !std::holds_alternative<PhiValue>(values.Values()[v].origin)) {
        q.Step();
        kind[v] = Identity::kValue;
        roots[v] = SsaValueId{v};
        pending.push_back(v);
      }
    for (std::size_t at = 0; at < pending.size(); ++at) {
      const auto value = pending[at];
      for (auto e = heads[value]; e != UINT32_MAX; e = next[e]) {
        q.Step();
        const auto target = targets[e];
        if (kind[target] == Identity::kMixed) continue;
        if (kind[target] == Identity::kUnknown) {
          kind[target] = kind[value];
          roots[target] = roots[value];
          pending.push_back(target);
        } else if (kind[value] == Identity::kMixed ||
                   roots[target] != roots[value]) {
          kind[target] = Identity::kMixed;
          pending.push_back(target);
        }
      }
    }
    for (std::uint32_t v = 0; v < count; ++v)
      if (kind[v] != Identity::kValue) roots[v].reset();
    return roots;
  }
  EnvironmentRef Ref(std::uint32_t n) const {
    if (*depth[n] == 0) return CapturedDepth{0};
    return *token_family[out.current[n]->value];
  }
  void Bindings() {
    q.Size(bindings, f.nodes.size());
    auto index = EnvironmentIndex::Build(out.definitions, {q.Left().bytes});
    auto* env = std::get_if<EnvironmentIndex>(&index);
    Need(env != nullptr, Error::kParent);
    q.Admit(env->AllocatedBytes());
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      if (depth[n] && f.nodes[n].phase == ExceptionPhase::kBefore) {
        q.Step();
        const LexicalAddress* address = nullptr;
        if (const auto* l = std::get_if<LexicalLoad>(&Op(n)))
          address = &l->address;
        if (const auto* s = std::get_if<LexicalStore>(&Op(n)))
          address = &s->address;
        if (!address) continue;
        const auto result = env->Resolve(Ref(n), *address);
        const auto* b = std::get_if<LexicalBinding>(&result);
        Need(b != nullptr, Error::kSlotMap, Pc(n));
        bindings[n] = *b;
      }
  }
  void Transfers() {
    const auto roots = ScalarRoots();
    q.Size(transfer, f.instructions.size());
    std::vector<std::optional<std::uint32_t>> by_token;
    q.Size(by_token, out.tokens.size());
    for (std::uint32_t i = 0; i < out.events.size(); ++i)
      by_token[out.events[i].new_token.value] = i;
    q.Reserve(out.transfers, f.instructions.size());
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      if (depth[n] && bindings[n] &&
          std::holds_alternative<LexicalStore>(Op(n))) {
        q.Step();
        const auto event = by_token[out.current[n]->value];
        if (!event) continue;
        const auto reads = values.Reads({n});
        Need(reads.size() == 1, Error::kCopyValue, Pc(n));
        const auto root = roots[reads[0].value.value];
        if (!root) continue;
        const auto* source =
            std::get_if<InstructionValue>(&values.Values()[root->value].origin);
        if (!source) continue;
        const auto commit = source->instruction.value;
        if (!Commit(commit) || !std::holds_alternative<LexicalLoad>(Op(commit)))
          continue;
        const auto site = f.nodes[commit].instruction;
        const auto before = befores[site.value].value;
        Need(f.nodes[before].phase == ExceptionPhase::kBefore &&
                 f.nodes[before].instruction == site && bindings[before],
             Error::kCopyValue, Pc(n));
        const auto& e = out.events[*event];
        if (out.current[before] != e.old_token) continue;
        Need(dom->Dominates({before}, {e.pop.value}) &&
                 dom->Dominates({e.allocation.value}, {n}),
             Error::kActivation, Pc(n));
        const auto* old =
            std::get_if<EnvironmentId>(&bindings[before]->environment);
        const auto* fresh =
            std::get_if<EnvironmentId>(&bindings[n]->environment);
        Need(old && fresh && *old == e.old_environment &&
                 *fresh == e.old_environment,
             Error::kSlotMap, Pc(n));
        Need(bindings[before]->slot.value == bindings[n]->slot.value,
             Error::kSlotMap, Pc(n));
        const auto ti = static_cast<std::uint32_t>(out.transfers.size());
        Need(!transfer[site.value] && !transfer[f.nodes[n].instruction.value],
             Error::kCoverage, Pc(n));
        transfer[site.value] = transfer[f.nodes[n].instruction.value] = ti;
        out.coverage[site.value] = EnvironmentRenewalRole::kTransferRead;
        out.coverage[f.nodes[n].instruction.value] =
            EnvironmentRenewalRole::kTransferStore;
        out.transfers.push_back({*event,
                                 bindings[before]->slot.value,
                                 bindings[n]->slot.value,
                                 {before},
                                 {n},
                                 reads[0].value,
                                 e.old_token,
                                 e.new_token});
      }
    std::vector<std::pair<std::uint32_t, std::uint16_t>> slots;
    q.Reserve(slots, out.transfers.size());
    for (const auto& t : out.transfers) slots.emplace_back(t.event, t.new_slot);
    std::sort(slots.begin(), slots.end(), [&](auto a, auto b) {
      q.Step();
      return a < b;
    });
    std::size_t cursor = 0;
    for (std::uint32_t e = 0; e < out.events.size(); ++e) {
      const auto count =
          out.definitions[out.events[e].new_environment.value].allocation.slots;
      for (std::uint32_t slot = 0; slot < count; ++slot) {
        q.Step();
        Need(
            cursor < slots.size() &&
                slots[cursor] == std::pair{e, static_cast<std::uint16_t>(slot)},
            Error::kSlotMap, Pc(out.events[e].allocation.value));
        ++cursor;
      }
    }
    Need(cursor == slots.size(), Error::kCoverage);
    // Full copy corridor, including block splits and exceptional connectivity.
    // A write/callback between snapshot, departure and restore cannot disappear
    // into native binding activation. Each visited node is charged once.
    std::vector<std::uint32_t> offsets, pred, position, pending;
    q.Size(offsets, f.nodes.size() + 1);
    std::size_t edges = 0;
    for (const auto& b : graph.blocks)
      for (std::uint8_t e = 0; e < b.successor_count; ++e) {
        ++offsets[b.successors[e].target.value + 1];
        ++edges;
      }
    for (std::size_t n = 0; n < f.nodes.size(); ++n)
      offsets[n + 1] += offsets[n];
    q.Size(pred, edges);
    q.Size(position, f.nodes.size());
    std::copy(offsets.begin(), offsets.end() - 1, position.begin());
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      for (std::uint8_t e = 0; e < graph.blocks[n].successor_count; ++e)
        pred[position[graph.blocks[n].successors[e].target.value]++] = n;
    std::vector<std::optional<std::uint32_t>> visited;
    q.Size(visited, f.nodes.size());
    std::vector<std::optional<ExceptionNodeId>> first;
    q.Size(first, out.events.size());
    q.Reserve(pending, edges + out.transfers.size());
    for (const auto& t : out.transfers)
      if (!first[t.event] || dom->nodes[t.read.value].begin <
                                 dom->nodes[first[t.event]->value].begin)
        first[t.event] = t.read;
    for (const auto& t : out.transfers) {
      pending.clear();
      pending.push_back(t.store.value);
      for (std::size_t at = 0; at < pending.size(); ++at) {
        q.Step();
        const auto n = pending[at];
        if (visited[n]) {
          Need(*visited[n] == t.event, Error::kInterference, Pc(n));
          continue;
        }
        visited[n] = t.event;
        Need(dom->Dominates({first[t.event]->value}, {n}), Error::kActivation,
             Pc(n));
        if (n == first[t.event]->value) continue;
        if (f.nodes[n].phase == ExceptionPhase::kBefore) {
          const auto role = out.coverage[f.nodes[n].instruction.value];
          const auto& effects =
              f.instructions[f.nodes[n].instruction.value].effects;
          const bool protocol =
              role == EnvironmentRenewalRole::kTransferRead ||
              role == EnvironmentRenewalRole::kTransferStore ||
              role == EnvironmentRenewalRole::kAllocation ||
              role == EnvironmentRenewalRole::kDeparture;
          Need(protocol ||
                   (effects.normal == LanguageEffect::kNone &&
                    effects.before_throw == LanguageEffect::kNone &&
                    effects.exceptional == ExceptionalCompletion::kImpossible),
               Error::kInterference, Pc(n));
        }
        for (auto p = offsets[n]; p < offsets[n + 1]; ++p) {
          q.Step();
          Need(pending.size() < pending.capacity(), Error::kResourceLimit);
          pending.push_back(pred[p]);
        }
      }
    }
  }
  void Activation() {
    // A sparse cell-state SSA resets each renewed generation to "not yet
    // initialized". Transport stores do not initialize the source binding.
    // This checks cycles by edge definitions, not same-SCC dominance guesses.
    using Key = std::pair<std::uint32_t, std::uint16_t>;
    std::vector<Key> cells;
    q.Reserve(cells, out.transfers.size());
    for (const auto& t : out.transfers)
      cells.emplace_back(out.events[t.event].old_environment.value, t.old_slot);
    std::sort(cells.begin(), cells.end(), [&](auto a, auto b) {
      q.Step();
      return a < b;
    });
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    const auto cell_id =
        [&](const LexicalBinding& b) -> std::optional<std::uint32_t> {
      const auto* e = std::get_if<EnvironmentId>(&b.environment);
      if (!e) return {};
      q.Step(1 + std::bit_width(cells.size()));
      const auto at = std::lower_bound(cells.begin(), cells.end(),
                                       Key{e->value, b.slot.value});
      if (at == cells.end() || *at != Key{e->value, b.slot.value}) return {};
      return static_cast<std::uint32_t>(at - cells.begin());
    };
    struct Event {
      std::uint32_t node, cell;
      bool write, reset;
    };
    std::vector<Event> events;
    // First count actual incidences; no uncharged vector growth or N*slots
    // state matrix. Replaying this census is linear in its charged output K.
    std::size_t count = 0;
    const auto visit = [&](auto emit) {
      for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
        if (depth[n]) {
          q.Step();
          if (physical[n] && out.families[physical[n]->value] != *physical[n]) {
            const auto family = out.families[physical[n]->value].value;
            const auto lo =
                std::lower_bound(cells.begin(), cells.end(), Key{family, 0});
            for (auto it = lo; it != cells.end() && it->first == family; ++it) {
              q.Step();
              emit(Event{n, static_cast<std::uint32_t>(it - cells.begin()),
                         true, true});
            }
          } else if (physical[n]) {
            const auto family = physical[n]->value;
            const auto lo =
                std::lower_bound(cells.begin(), cells.end(), Key{family, 0});
            for (auto it = lo; it != cells.end() && it->first == family; ++it) {
              q.Step();
              emit(Event{n, static_cast<std::uint32_t>(it - cells.begin()),
                         true, true});
            }
          }
          if (bindings[n] && !transfer[f.nodes[n].instruction.value])
            if (const auto cell = cell_id(*bindings[n])) {
              if (std::holds_alternative<LexicalStore>(Op(n)))
                emit(Event{commits[f.nodes[n].instruction.value].value, *cell,
                           true, false});
              else
                emit(Event{n, *cell, false, false});
            }
          if (f.nodes[n].phase == ExceptionPhase::kBefore && Captures(Op(n))) {
            auto ref = Ref(n);
            while (const auto* env = std::get_if<EnvironmentId>(&ref)) {
              q.Step();
              const auto family = out.families[env->value].value;
              const auto lo =
                  std::lower_bound(cells.begin(), cells.end(), Key{family, 0});
              for (auto it = lo; it != cells.end() && it->first == family;
                   ++it) {
                q.Step();
                emit(Event{n, static_cast<std::uint32_t>(it - cells.begin()),
                           false, false});
              }
              ref = out.definitions[env->value].parent;
            }
          }
        }
    };
    visit([&](auto) {
      Need(count < UINT32_MAX, Error::kResourceLimit);
      ++count;
    });
    q.Reserve(events, count);
    visit([&](auto e) { events.push_back(e); });
    std::sort(events.begin(), events.end(), [&](auto a, auto b) {
      q.Step();
      return std::tuple{a.node, a.cell, a.write} <
             std::tuple{b.node, b.cell, b.write};
    });
    std::vector<std::uint32_t> first;
    q.Size(first, f.nodes.size() + 1);
    for (const auto& e : events) ++first[e.node + 1];
    for (std::size_t n = 0; n < f.nodes.size(); ++n) {
      q.Step();
      first[n + 1] += first[n] + 1;
    }
    Need(first.back() < UINT32_MAX, Error::kResourceLimit);
    std::vector<std::optional<std::uint32_t>> initial;
    q.Size(initial, cells.size());
    for (auto n : order)
      if (depth[n] && bindings[n] && !transfer[f.nodes[n].instruction.value] &&
          std::holds_alternative<LexicalStore>(Op(n))) {
        if (const auto cell = cell_id(*bindings[n]); cell && !initial[*cell])
          initial[*cell] = commits[f.nodes[n].instruction.value].value;
      }
    std::vector<RegisterSite> sites;
    q.Size(sites, first.back());
    NormalFlow expanded;
    q.Size(expanded.blocks, sites.size());
    std::vector<std::uint8_t> reset, declaration_read;
    q.Size(reset, sites.size());
    q.Size(declaration_read, sites.size());
    std::vector<std::uint32_t> source;
    q.Size(source, sites.size());
    std::size_t cursor = 0;
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n) {
      q.Step();
      for (auto at = first[n]; at < first[n + 1]; ++at) {
        q.Step();
        sites[at].pc = Pc(n);
        source[at] = n;
        auto& b = expanded.blocks[at];
        b.begin = b.end = Pc(n);
        b.first_instruction = at;
        b.instruction_count = 1;
        if (at + 1 < first[n + 1]) {
          b.successor_count = 1;
          b.successors[0] = {{at + 1}, FlowEdgeKind::kFallthrough};
        } else
          for (std::uint8_t e = 0; e < graph.blocks[n].successor_count; ++e)
            b.successors[b.successor_count++] = {
                {first[graph.blocks[n].successors[e].target.value]},
                FlowEdgeKind::kFallthrough};
      }
      auto at = first[n] + 1;
      while (cursor < events.size() && events[cursor].node == n) {
        const auto e = events[cursor++];
        auto& effect = sites[at].effect;
        if (e.write) {
          effect.write = abc::RegisterId{e.cell};
          if (!e.reset && initial[e.cell] == n) {
            // A source declaration must execute exactly once in a generation:
            // its incoming state must be a reset, never an earlier store.
            effect.reads[0] = abc::RegisterId{e.cell};
            effect.read_count = 1;
            declaration_read[at] = 1;
          }
        } else {
          effect.reads[0] = abc::RegisterId{e.cell};
          effect.read_count = 1;
        }
        reset[at] = e.reset;
        ++at;
      }
    }
    IdfQuota work{q.Left().work};
    auto built =
        RegisterSsa::BuildSites(sites, expanded, cells.size(), work,
                                {q.Left().bytes}, SsaPlacement::kComplete);
    q.Step(work.Consumed());
    const auto* s = std::get_if<RegisterSsa>(&built);
    Need(s != nullptr, Cause(built, Error::kActivation));
    q.Admit(s->AllocatedBytes());
    enum class Initialization : std::uint8_t {
      kUnreachable = 0,
      kUninitialized = 1,
      kInitialized = 2,
      kBoth = 3
    };
    std::vector<Initialization> state;
    q.Size(state, s->Values().size());
    std::vector<std::uint32_t> heads, next, targets, pending;
    q.Size(heads, state.size());
    std::fill(heads.begin(), heads.end(), UINT32_MAX);
    q.Reserve(next, s->AllInputs().size());
    q.Reserve(targets, s->AllInputs().size());
    q.Reserve(pending, 2 * state.size());
    for (const auto& input : s->AllInputs()) {
      q.Step();
      next.push_back(heads[input.value.value]);
      targets.push_back(input.phi.value);
      heads[input.value.value] = static_cast<std::uint32_t>(next.size() - 1);
    }
    for (std::uint32_t v = 0; v < state.size(); ++v) {
      q.Step();
      const auto& origin = s->Values()[v].origin;
      if (std::holds_alternative<MethodEntry>(origin))
        state[v] = Initialization::kUninitialized;
      else if (const auto* value = std::get_if<InstructionValue>(&origin))
        state[v] = reset[value->instruction.value]
                       ? Initialization::kUninitialized
                       : Initialization::kInitialized;
      if (state[v] != Initialization::kUnreachable) pending.push_back(v);
    }
    for (std::size_t at = 0; at < pending.size(); ++at)
      for (auto edge = heads[pending[at]]; edge != UINT32_MAX;
           edge = next[edge]) {
        q.Step();
        const auto target = targets[edge];
        const auto joined = static_cast<Initialization>(
            static_cast<std::uint8_t>(state[target]) |
            static_cast<std::uint8_t>(state[pending[at]]));
        if (joined != state[target]) {
          state[target] = joined;
          pending.push_back(target);
        }
      }
    for (const auto& read : s->AllReads()) {
      q.Step();
      const auto expected = declaration_read[read.instruction.value]
                                ? Initialization::kUninitialized
                                : Initialization::kInitialized;
      Need(state[read.value.value] == expected, Error::kSourceScope,
           Pc(source[read.instruction.value]));
    }
    // One actual initialization Commit must dominate each cell's observable
    // body occurrences. The activation SSA above additionally proves that this
    // store executes exactly once after EVERY renewal, including
    // continue/backedges.
    for (const auto& e : events)
      if (!e.reset) {
        q.Step();
        Need(initial[e.cell].has_value(), Error::kInitialization, Pc(e.node));
        Need(dom->Dominates({*initial[e.cell]}, {e.node}), Error::kSourceScope,
             Pc(e.node));
      }
    q.Reserve(out.resets, cells.size());
    for (std::size_t c = 0; c < cells.size(); ++c) {
      Need(initial[c].has_value(), Error::kInitialization);
      out.resets.push_back({{cells[c].first}, cells[c].second, {*initial[c]}});
    }
    q.Size(out.initializers, f.instructions.size());
    for (std::uint32_t r = 0; r < out.resets.size(); ++r) {
      const auto i = f.nodes[out.resets[r].initializer.value].instruction.value;
      Need(!out.initializers[i], Error::kInitialization);
      out.initializers[i] = r;
    }
    // Transfer values are private binding transport. No other use may consume
    // them, even when a register name is reused in a later iteration.
    const auto roots = ScalarRoots();
    std::vector<std::uint8_t> copied;
    q.Size(copied, values.Values().size());
    for (const auto& t : out.transfers) {
      const auto value =
          values.Write(commits[f.nodes[t.read.value].instruction.value]);
      Need(value.has_value(), Error::kCopyValue);
      copied[value->value] = 1;
    }
    for (const auto& read : values.AllReads())
      if (roots[read.value.value] && copied[roots[read.value.value]->value]) {
        q.Step();
        const auto n = read.instruction.value;
        const auto* a = std::get_if<ScalarAssignment>(&Op(n));
        Need((a && std::holds_alternative<ScalarValue>(a->expression)) ||
                 out.coverage[f.nodes[n].instruction.value] ==
                     EnvironmentRenewalRole::kTransferStore,
             Error::kCoverage, Pc(n));
      }
  }
  EnvironmentRenewal Run() {
    Graph();
    auto tokens = Tokens();
    Correspondence(tokens);
    Bindings();
    Transfers();
    // Moving private allocation to body binding activation changes resource
    // completion timing. Only the source owner's existing exclusion permits
    // that projection, even when an exceptional completion leaves the graph.
    Need(out.resources == EnvironmentRenewalResources::kExcluded,
         Error::kException);
    // Activation and closed-use checks are completed below, separately from
    // topology. No certificate can be used merely because phi families agree.
    Activation();
    out.work = q.Work();
    const auto bytes = [](const auto& v) {
      return v.capacity() *
             sizeof(typename std::decay_t<decltype(v)>::value_type);
    };
    out.allocated_bytes =
        bytes(out.tokens) + bytes(out.inputs) + bytes(out.current) +
        bytes(out.definitions) + bytes(out.allocations) + bytes(out.families) +
        bytes(out.events) + bytes(out.transfers) + bytes(out.resets) +
        bytes(out.initializers) + bytes(out.excluded) + bytes(out.coverage);
    return std::move(out);
  }
};
}  // namespace
std::variant<EnvironmentRenewal, EnvironmentRenewalFailure>
ProveEnvironmentRenewal(const ExceptionFlow& f, const ExceptionSsa& s,
                        EnvironmentRenewalResources r, SemanticBudget budget) {
  try {
    return Builder(f, s, r, budget).Run();
  } catch (const EnvironmentRenewalFailure& e) {
    return e;
  } catch (const std::bad_alloc&) {
    return EnvironmentRenewalFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return EnvironmentRenewalFailure{Error::kResourceLimit};
  }
}
}  // namespace abcd::analysis

namespace abcd::analysis {
namespace {
bool Same(const EnvironmentRenewal& a, const EnvironmentRenewal& b) {
  if (a.resources != b.resources || a.tokens != b.tokens ||
      a.current != b.current || a.excluded != b.excluded ||
      a.coverage != b.coverage || a.families != b.families ||
      a.initializers != b.initializers || a.inputs.size() != b.inputs.size() ||
      a.definitions.size() != b.definitions.size() ||
      a.allocations.size() != b.allocations.size() ||
      a.events.size() != b.events.size() ||
      a.transfers.size() != b.transfers.size() ||
      a.resets.size() != b.resets.size())
    return false;
  for (std::size_t i = 0; i < a.inputs.size(); ++i) {
    const auto& x = a.inputs[i];
    const auto& y = b.inputs[i];
    if (x.phi != y.phi || x.value != y.value ||
        x.edge.source != y.edge.source || x.edge.successor != y.edge.successor)
      return false;
  }
  for (std::size_t i = 0; i < a.definitions.size(); ++i) {
    const auto& x = a.definitions[i];
    const auto& y = b.definitions[i];
    if (x.parent != y.parent || x.allocation.slots != y.allocation.slots ||
        x.allocation.scope != y.allocation.scope)
      return false;
  }
  for (std::size_t i = 0; i < a.allocations.size(); ++i) {
    const auto& x = a.allocations[i];
    const auto& y = b.allocations[i];
    if (x.instruction != y.instruction || x.environment != y.environment)
      return false;
  }
  for (std::size_t i = 0; i < a.events.size(); ++i) {
    const auto& x = a.events[i];
    const auto& y = b.events[i];
    if (x.pop != y.pop || x.allocation != y.allocation ||
        x.old_token != y.old_token || x.new_token != y.new_token ||
        x.old_environment != y.old_environment ||
        x.new_environment != y.new_environment)
      return false;
  }
  for (std::size_t i = 0; i < a.transfers.size(); ++i) {
    const auto& x = a.transfers[i];
    const auto& y = b.transfers[i];
    if (x.event != y.event || x.old_slot != y.old_slot ||
        x.new_slot != y.new_slot || x.read != y.read || x.store != y.store ||
        x.saved != y.saved || x.old_token != y.old_token ||
        x.new_token != y.new_token)
      return false;
  }
  for (std::size_t i = 0; i < a.resets.size(); ++i) {
    const auto& x = a.resets[i];
    const auto& y = b.resets[i];
    if (x.family != y.family || x.slot != y.slot ||
        x.initializer != y.initializer)
      return false;
  }
  return true;
}
}  // namespace
std::variant<std::uint64_t, EnvironmentRenewalFailure> VerifyEnvironmentRenewal(
    const ExceptionFlow& f, const ExceptionSsa& s,
    const EnvironmentRenewal& proof, SemanticBudget budget) {
  auto expected = ProveEnvironmentRenewal(f, s, proof.resources, budget);
  if (const auto* error = std::get_if<EnvironmentRenewalFailure>(&expected))
    return *error;
  const auto& result = std::get<EnvironmentRenewal>(expected);
  if (!Same(proof, result)) {
    auto reason = Error::kCoverage;
    if (proof.tokens != result.tokens || proof.current != result.current)
      reason = Error::kActivation;
    else if (proof.inputs.size() == result.inputs.size()) {
      for (std::size_t i = 0; i < proof.inputs.size(); ++i) {
        const auto& a = proof.inputs[i];
        const auto& b = result.inputs[i];
        if (a.phi != b.phi || a.value != b.value ||
            a.edge.source != b.edge.source ||
            a.edge.successor != b.edge.successor) {
          reason = Error::kActivation;
          break;
        }
      }
    }
    if (proof.definitions.size() == result.definitions.size())
      for (std::size_t i = 0; i < proof.definitions.size(); ++i)
        if (proof.definitions[i].parent != result.definitions[i].parent)
          reason = Error::kParent;
    if (proof.transfers.size() == result.transfers.size())
      for (std::size_t i = 0; i < proof.transfers.size(); ++i) {
        const auto& a = proof.transfers[i];
        const auto& b = result.transfers[i];
        if (a.old_slot != b.old_slot || a.new_slot != b.new_slot)
          reason = Error::kSlotMap;
        else if (a.saved != b.saved)
          reason = Error::kCopyValue;
        else if (a.old_token != b.old_token || a.new_token != b.new_token ||
                 a.read != b.read || a.store != b.store)
          reason = Error::kActivation;
      }
    if (proof.events.size() == result.events.size())
      for (std::size_t i = 0; i < proof.events.size(); ++i)
        if (proof.events[i].old_token != result.events[i].old_token ||
            proof.events[i].new_token != result.events[i].new_token ||
            proof.events[i].pop != result.events[i].pop ||
            proof.events[i].allocation != result.events[i].allocation)
          reason = Error::kActivation;
    if (proof.resets.size() == result.resets.size())
      for (std::size_t i = 0; i < proof.resets.size(); ++i)
        if (proof.resets[i].initializer != result.resets[i].initializer)
          reason = Error::kInitialization;
    return EnvironmentRenewalFailure{reason};
  }
  const auto comparisons = proof.tokens.size() + proof.inputs.size() +
                           proof.current.size() + proof.definitions.size() +
                           proof.allocations.size() + proof.events.size() +
                           proof.transfers.size() + proof.resets.size() +
                           proof.excluded.size() + proof.coverage.size();
  if (comparisons > budget.work - result.work)
    return EnvironmentRenewalFailure{Error::kWorkLimit};
  return result.work + comparisons;
}
std::variant<std::uint64_t, EnvironmentRenewalFailure>
VerifyEnvironmentRenewalEvidence(const EnvironmentRenewalEvidence& evidence,
                                 SemanticBudget budget) {
  if (!evidence.flow || !evidence.ssa)
    return EnvironmentRenewalFailure{Error::kInput};
  return VerifyEnvironmentRenewal(*evidence.flow, *evidence.ssa, evidence.proof,
                                  budget);
}
std::variant<EnvironmentFlow, EnvironmentRenewalFailure>
ProjectRenewedEnvironments(const ExceptionFlow& f, const ExceptionSsa& s,
                           const EnvironmentRenewal& proof,
                           SemanticBudget budget) {
  try {
    Quota q(budget);
    const auto checked = VerifyEnvironmentRenewal(f, s, proof, q.Left());
    if (const auto* error = std::get_if<EnvironmentRenewalFailure>(&checked))
      return *error;
    q.Step(std::get<std::uint64_t>(checked));
    auto index = EnvironmentIndex::Build(proof.definitions, {q.Left().bytes});
    auto* env = std::get_if<EnvironmentIndex>(&index);
    Need(env != nullptr, Error::kParent);
    q.Admit(env->AllocatedBytes());
    EnvironmentFlow result{std::move(*env)};
    q.Size(result.before, f.nodes.size());
    q.Reserve(result.allocations, proof.allocations.size());
    q.Reserve(result.bindings, f.instructions.size());
    Union components(proof.tokens.size(), q);
    for (const auto& input : proof.inputs)
      components.Join(input.phi.value, input.value.value);
    std::vector<std::optional<EnvironmentId>> families;
    q.Size(families, proof.tokens.size());
    std::vector<std::optional<EnvironmentId>> physical;
    q.Size(physical, f.nodes.size());
    for (const auto& a : proof.allocations) {
      physical[a.instruction] = a.environment;
      result.allocations.push_back(a);
    }
    for (std::uint32_t t = 0; t < proof.tokens.size(); ++t)
      if (proof.tokens[t].kind == EnvironmentTokenKind::kAllocation) {
        q.Step();
        Need(physical[proof.tokens[t].site].has_value(), Error::kActivation);
        families[components.Find(t)] =
            proof.families[physical[proof.tokens[t].site]->value];
      }
    for (std::uint32_t n = 0; n < f.nodes.size(); ++n)
      if (proof.current[n]) {
        q.Step();
        const auto token = proof.current[n]->value;
        const auto family = families[components.Find(token)];
        result.before[n] =
            family ? EnvironmentRef{*family} : EnvironmentRef{CapturedDepth{0}};
        if (f.nodes[n].phase != ExceptionPhase::kBefore) continue;
        const LexicalAddress* address = nullptr;
        const auto& op = f.instructions[f.nodes[n].instruction.value].operation;
        if (const auto* load = std::get_if<LexicalLoad>(&op))
          address = &load->address;
        if (const auto* store = std::get_if<LexicalStore>(&op))
          address = &store->address;
        if (!address) continue;
        const auto bound =
            result.environments.Resolve(*result.before[n], *address);
        const auto* binding = std::get_if<LexicalBinding>(&bound);
        Need(binding != nullptr, Error::kSlotMap);
        result.bindings.push_back({n, *binding});
      }
    result.allocated_bytes =
        result.environments.AllocatedBytes() +
        result.before.capacity() * sizeof(decltype(result.before)::value_type) +
        result.allocations.capacity() * sizeof(EnvironmentAllocation) +
        result.bindings.capacity() * sizeof(BoundLexicalSite);
    result.instruction_visits = q.Work();
    return result;
  } catch (const EnvironmentRenewalFailure& e) {
    return e;
  } catch (const std::bad_alloc&) {
    return EnvironmentRenewalFailure{Error::kResourceLimit};
  } catch (const std::length_error&) {
    return EnvironmentRenewalFailure{Error::kResourceLimit};
  }
}
std::variant<std::uint64_t, EnvironmentRenewalFailure>
VerifyRenewedEnvironmentFlow(const EnvironmentRenewalEvidence& evidence,
                             const EnvironmentFlow& supplied,
                             SemanticBudget budget) {
  if (!evidence.flow || !evidence.ssa)
    return EnvironmentRenewalFailure{Error::kInput};
  auto built = ProjectRenewedEnvironments(*evidence.flow, *evidence.ssa,
                                          evidence.proof, budget);
  if (const auto* failure = std::get_if<EnvironmentRenewalFailure>(&built))
    return *failure;
  const auto& expected = std::get<EnvironmentFlow>(built);
  if (supplied.before != expected.before ||
      supplied.bindings.size() != expected.bindings.size() ||
      supplied.allocations.size() != expected.allocations.size() ||
      supplied.environments.Size() != expected.environments.Size())
    return EnvironmentRenewalFailure{Error::kCoverage};
  for (std::size_t i = 0; i < supplied.bindings.size(); ++i) {
    const auto& a = supplied.bindings[i];
    const auto& b = expected.bindings[i];
    if (a.instruction != b.instruction ||
        a.binding.environment != b.binding.environment ||
        a.binding.slot.value != b.binding.slot.value)
      return EnvironmentRenewalFailure{Error::kSlotMap};
  }
  for (std::size_t i = 0; i < supplied.allocations.size(); ++i) {
    const auto& a = supplied.allocations[i];
    const auto& b = expected.allocations[i];
    if (a.instruction != b.instruction || a.environment != b.environment)
      return EnvironmentRenewalFailure{Error::kActivation};
  }
  for (std::uint32_t i = 0; i < supplied.environments.Size(); ++i) {
    const auto* a = supplied.environments.Find({i});
    const auto* b = expected.environments.Find({i});
    if (!a || !b || a->parent != b->parent ||
        a->allocation.slots != b->allocation.slots ||
        a->allocation.scope != b->allocation.scope)
      return EnvironmentRenewalFailure{Error::kParent};
  }
  const auto work = expected.instruction_visits + supplied.before.size() +
                    supplied.bindings.size() + supplied.allocations.size() +
                    supplied.environments.Size();
  if (work > budget.work) return EnvironmentRenewalFailure{Error::kWorkLimit};
  return work;
}
}  // namespace abcd::analysis
