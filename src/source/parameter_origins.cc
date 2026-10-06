#include "parameter_origins.h"
#include "abcd/analysis/exception_flow.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
namespace abcd::source {
namespace {
using namespace analysis;
using Error = ParameterNameError;
enum class OriginKind : std::uint8_t { kBottom, kParameter, kOther };
struct Origin {
  OriginKind kind = OriginKind::kBottom;
  std::uint32_t parameter = 0;
  auto operator<=>(const Origin&) const = default;
};
struct Dependency { SsaValueId source, target; };
Origin Join(Origin left, Origin right) {
  if (left.kind == OriginKind::kBottom) return right;
  if (right.kind == OriginKind::kBottom || left == right) return left;
  return {OriginKind::kOther};
}
class Storage {
 public:
  explicit Storage(std::size_t limit) : bytes(limit) {}
  void Admit(std::size_t n, std::size_t width) {
    if (n > bytes / width) throw Error::kResourceLimit;
    bytes -= n * width;
  }
  template<class T> void Reserve(std::vector<T>& values, std::size_t n) {
    Admit(n, sizeof(T)); values.reserve(n);
    Admit(values.capacity() - n, sizeof(T));
  }
  template<class T> void Size(std::vector<T>& values, std::size_t n) {
    Reserve(values, n); values.resize(n);
  }
  std::size_t bytes;
};
struct ParameterFlow {
  std::vector<std::optional<SemanticInstruction>> instructions;
  std::vector<ExceptionNode> nodes;
  std::vector<std::uint8_t> unknown_before;
  std::size_t allocated_bytes = 0;
};
ParameterFlow BuildParameterFlow(const abc::FrameLayout& frame, const abc::MethodBody& body,
    abc::ProfileId profile, std::size_t bytes) {
  Storage storage(bytes);
  auto normal = BuildNormalFlow(body, {storage.bytes});
  const auto* graph = std::get_if<NormalFlow>(&normal);
  if (!graph) {
    if (std::get<FlowFailure>(normal).kind == FlowError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kUnproven;
  }
  storage.Admit(graph->blocks.capacity(), sizeof(FlowBlock));
  auto selected = BuildExceptionDispatch(body, profile, {storage.bytes});
  const auto* dispatch = std::get_if<ExceptionDispatch>(&selected);
  if (!dispatch) {
    if (std::get<DispatchFailure>(selected).kind == DispatchError::kResourceLimit) throw Error::kResourceLimit;
    throw Error::kUnproven;
  }
  storage.Admit(dispatch->spans.capacity(), sizeof(DispatchSpan));
  constexpr std::size_t kInstructionPhases = 2;  // Original Before/Commit timing.
  const auto count = body.instructions.size();
  constexpr auto kNodeLimit = std::numeric_limits<std::uint32_t>::max();
  if (body.handlers.size() > kNodeLimit || count > (kNodeLimit - body.handlers.size()) / kInstructionPhases)
    throw Error::kResourceLimit;
  const auto base = count * kInstructionPhases;
  ParameterFlow result;
  storage.Size(result.instructions, count);
  storage.Size(result.nodes, base + body.handlers.size());
  storage.Size(result.unknown_before, result.nodes.size());
  std::vector<std::uint8_t> opaque;
  storage.Size(opaque, count);
  const auto before = [](std::uint32_t instruction) {
    return ExceptionNodeId{static_cast<std::uint32_t>(instruction * kInstructionPhases)};
  };
  const auto commit = [](std::uint32_t instruction) {
    return ExceptionNodeId{static_cast<std::uint32_t>(instruction * kInstructionPhases + 1)};
  };
  for (std::size_t i = 0; i < count; ++i) {
    auto lifted = LiftSemanticInstruction(body.instructions[i], profile, frame.registers, &frame);
    if (const auto* known = std::get_if<SemanticInstruction>(&lifted)) result.instructions[i] = *known;
    else {
      const auto kind = std::get<SemanticFailure>(lifted).kind;
      if (kind == SemanticError::kResourceLimit) throw Error::kResourceLimit;
      if (kind != SemanticError::kUnsupportedInstruction) throw Error::kUnproven;
      opaque[i] = 1;
    }
    if (*abc::GetControlTransfer(body.instructions[i].decoded.opcode) == abc::ControlTransfer::kSuspend)
      opaque[i] = 1;  // Raw activation state has no normalization owner here.
    result.nodes[before(i).value] = {InstructionId{static_cast<std::uint32_t>(i)}, ExceptionPhase::kBefore};
    result.nodes[commit(i).value] = {InstructionId{static_cast<std::uint32_t>(i)}, ExceptionPhase::kCommit};
  }
  for (std::size_t h = 0; h < body.handlers.size(); ++h) {
    const auto pc = body.handlers[h].offset;
    const auto found = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
        [](const abc::BodyInstruction& instruction, abc::CodeOffset target) { return instruction.offset < target; });
    if (found == body.instructions.end() || found->offset != pc) throw Error::kUnproven;
    const auto instruction = static_cast<std::uint32_t>(found - body.instructions.begin());
    auto& node = result.nodes[base + h];
    node = {InstructionId{instruction}, ExceptionPhase::kCatch};
    node.successor_count = 1; node.successors[0] = {before(instruction), ExceptionArcKind::kNormal};
  }
  for (const auto& block : graph->blocks) {
    for (std::uint32_t i = block.first_instruction; i < block.first_instruction + block.instruction_count; ++i) {
      const auto completion = result.instructions[i] ? result.instructions[i]->effects.exceptional :
          ExceptionalCompletion::kMayThrow;
      auto& entry = result.nodes[before(i).value];
      if (completion != ExceptionalCompletion::kAlwaysThrow)
        entry.successors[entry.successor_count++] = {commit(i), ExceptionArcKind::kNormal};
      if (completion != ExceptionalCompletion::kImpossible) {
        if (const auto target = dispatch->Find(body.instructions[i].offset))
          entry.successors[entry.successor_count++] = {{static_cast<std::uint32_t>(base + target->handler.value)},
              ExceptionArcKind::kException};
      }
      auto& exit = result.nodes[commit(i).value];
      if (i + 1 < block.first_instruction + block.instruction_count) {
        exit.successor_count = 1; exit.successors[0] = {before(i + 1), ExceptionArcKind::kNormal};
      } else {
        exit.successor_count = block.successor_count;
        for (std::size_t e = 0; e < block.successor_count; ++e)
          exit.successors[e] = {before(graph->blocks[block.successors[e].target.value].first_instruction),
              block.successors[e].kind == FlowEdgeKind::kFallthrough ?
                  ExceptionArcKind::kFallthrough : ExceptionArcKind::kTaken};
      }
    }
  }
  // Unknown semantics invalidate entry-origin facts only after they execute,
  // along all original control/dispatch paths. Earlier observations survive
  // unless a real backedge/handler path can return with unknown frame state.
  // This is not a fabricated NoOperation or a whitelist of source producers.
  std::vector<ExceptionNodeId> pending;
  storage.Reserve(pending, result.nodes.size());
  std::vector<std::uint8_t> reachable;
  storage.Size(reachable, result.nodes.size());
  reachable[0] = 1; pending.push_back({0});
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto& node = result.nodes[pending[head].value];
    for (std::size_t e = 0; e < node.successor_count; ++e) {
      const auto target = node.successors[e].target;
      if (!reachable[target.value]) { reachable[target.value] = 1; pending.push_back(target); }
    }
  }
  pending.clear();
  const auto mark = [&](ExceptionNodeId id) {
    if (!result.unknown_before[id.value]) {
      result.unknown_before[id.value] = 1; pending.push_back(id);
    }
  };
  for (std::size_t i = 0; i < count; ++i) if (opaque[i] && reachable[before(i).value]) {
    const auto& node = result.nodes[before(i).value];
    for (std::size_t e = 0; e < node.successor_count; ++e) mark(node.successors[e].target);
  }
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto& node = result.nodes[pending[head].value];
    for (std::size_t e = 0; e < node.successor_count; ++e) mark(node.successors[e].target);
  }
  result.allocated_bytes = bytes - storage.bytes;
  return result;
}
}
std::variant<ObservedParameters, Error> ObserveParameters(
    const abc::FrameLayout& frame, const abc::MethodBody& body, abc::ProfileId profile,
    std::span<const ParameterObservation> observations, std::size_t bytes) {
  try {
    Storage storage(bytes);
    auto projected = BuildParameterFlow(frame, body, profile, storage.bytes);
    const auto* flow = &projected;
    storage.Admit(flow->allocated_bytes, 1);
    constexpr auto kSiteLimit = std::numeric_limits<std::uint32_t>::max();
    if (observations.size() > kSiteLimit || flow->nodes.size() > kSiteLimit - observations.size())
      return Error::kResourceLimit;
    std::vector<std::size_t> order;
    storage.Size(order, observations.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
      if (observations[i].instruction.value >= flow->instructions.size() ||
          observations[i].reg.value >= frame.registers) return Error::kUnproven;
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
      return observations[a].instruction < observations[b].instruction;
    });
    std::vector<std::size_t> starts;
    storage.Size(starts, flow->instructions.size() + 1);
    for (const auto& item : observations) ++starts[std::size_t(item.instruction.value) + 1];
    for (std::size_t i = 1; i < starts.size(); ++i) starts[i] += starts[i - 1];
    std::vector<RegisterSite> sites;
    storage.Reserve(sites, flow->nodes.size() + observations.size());
    NormalFlow graph;
    storage.Size(graph.blocks, flow->nodes.size());
    std::vector<std::optional<InstructionId>> queries, before;
    storage.Size(queries, observations.size());
    storage.Size(before, flow->instructions.size());
    std::vector<ExceptionNodeId> owners;
    storage.Reserve(owners, sites.capacity());
    for (std::size_t i = 0; i < flow->nodes.size(); ++i) {
      const auto& node = flow->nodes[i];
      const auto& original = flow->instructions[node.instruction.value];
      const auto pc = body.instructions[node.instruction.value].offset;
      auto& block = graph.blocks[i];
      block.begin = pc; block.end = pc;
      block.first_instruction = static_cast<std::uint32_t>(sites.size());
      if (node.phase == ExceptionPhase::kBefore) {
        for (auto p = starts[node.instruction.value]; p < starts[node.instruction.value + 1]; ++p) {
          const auto q = order[p];
          if (queries[q]) return Error::kUnproven;
          queries[q] = InstructionId{static_cast<std::uint32_t>(sites.size())};
          RegisterEffect effect;
          effect.read_count = 1; effect.reads[0] = observations[q].reg;
          sites.push_back({pc, effect}); owners.push_back({static_cast<std::uint32_t>(i)});
        }
        before[node.instruction.value] = InstructionId{static_cast<std::uint32_t>(sites.size())};
      }
      RegisterEffect effect;
      switch (node.phase) {
        case ExceptionPhase::kBefore:
          if (original) effect = original->effects.normal_registers; effect.write.reset(); break;
        case ExceptionPhase::kCommit: if (original) effect.write = original->effects.normal_registers.write; break;
        case ExceptionPhase::kCatch: effect.write = Accumulator{}; break;
      }
      sites.push_back({pc, effect}); owners.push_back({static_cast<std::uint32_t>(i)});
      block.instruction_count = static_cast<std::uint32_t>(sites.size()) - block.first_instruction;
      block.successor_count = node.successor_count;
      for (std::size_t e = 0; e < node.successor_count; ++e)
        block.successors[e] = {{node.successors[e].target.value},
            node.successors[e].kind == ExceptionArcKind::kFallthrough ?
                FlowEdgeKind::kFallthrough : FlowEdgeKind::kTaken};
    }
    // Queries add no definition or language effect. The shared pruned SSA
    // supplies precisely the joins demanded by actual reads and observations.
    IdfQuota work(std::numeric_limits<std::uint64_t>::max());
    auto built = RegisterSsa::BuildSites(sites, graph, frame.registers, work, {storage.bytes});
    const auto* ssa = std::get_if<RegisterSsa>(&built);
    if (!ssa) {
      if (const auto* failure = std::get_if<RegisterUseFailure>(&built))
        if (failure->kind == RegisterUseError::kResourceLimit) return Error::kResourceLimit;
      if (const auto* failure = std::get_if<IdfError>(&built))
        if (*failure == IdfError::kResourceLimit) return Error::kResourceLimit;
      if (const auto* failure = std::get_if<DominatorError>(&built))
        if (*failure == DominatorError::kResourceLimit) return Error::kResourceLimit;
      if (const auto* failure = std::get_if<SsaError>(&built))
        if (*failure == SsaError::kResourceLimit) return Error::kResourceLimit;
      return Error::kUnproven;
    }
    storage.Admit(ssa->work.peak_bytes, 1);
    const auto values = ssa->Values();
    std::vector<Origin> origins;
    storage.Size(origins, values.size());
    std::vector<Dependency> edges;
    if (ssa->AllInputs().size() > std::numeric_limits<std::size_t>::max() - values.size())
      return Error::kResourceLimit;
    storage.Reserve(edges, values.size() + ssa->AllInputs().size());
    for (std::size_t i = 0; i < values.size(); ++i) {
      const auto& value = values[i];
      const SsaValueId target{static_cast<std::uint32_t>(i)};
      if (std::holds_alternative<MethodEntry>(value.origin)) {
        const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
        origins[i] = reg && reg->value >= frame.first_parameter.value &&
            reg->value - frame.first_parameter.value < frame.parameters ?
                Origin{OriginKind::kParameter, reg->value - frame.first_parameter.value} :
                Origin{OriginKind::kOther};
        continue;
      }
      if (std::holds_alternative<PhiValue>(value.origin)) continue;
      const auto& definition = std::get<InstructionValue>(value.origin);
      const auto& node = flow->nodes[owners[definition.instruction.value].value];
      const auto& original = flow->instructions[node.instruction.value];
      const auto* copy = original ? std::get_if<ScalarAssignment>(&original->operation) : nullptr;
      const auto* source = copy ? std::get_if<ScalarValue>(&copy->expression) : nullptr;
      if (node.phase == ExceptionPhase::kCommit && source &&
          (std::holds_alternative<Accumulator>(*source) || std::holds_alternative<abc::RegisterId>(*source))) {
        if (!before[node.instruction.value]) return Error::kUnproven;
        const auto reads = ssa->Reads(*before[node.instruction.value]);
        if (reads.size() != 1) return Error::kUnproven;
        edges.push_back({reads.front().value, target});
      } else origins[i] = {OriginKind::kOther};
    }
    for (const auto& input : ssa->AllInputs()) edges.push_back({input.value, input.phi});
    std::vector<std::size_t> offsets, cursor;
    storage.Size(offsets, values.size() + 1);
    for (const auto& edge : edges) ++offsets[std::size_t(edge.source.value) + 1];
    for (std::size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];
    storage.Size(cursor, offsets.size()); cursor = offsets;
    std::vector<SsaValueId> dependents;
    storage.Size(dependents, edges.size());
    for (const auto& edge : edges) dependents[cursor[edge.source.value]++] = edge.target;
    std::vector<SsaValueId> pending;
    constexpr std::size_t kOriginChanges = 2;  // Height above lattice bottom.
    if (values.size() > std::numeric_limits<std::size_t>::max() / kOriginChanges) return Error::kResourceLimit;
    storage.Reserve(pending, values.size() * kOriginChanges);
    for (std::size_t i = 0; i < origins.size(); ++i)
      if (origins[i].kind != OriginKind::kBottom) pending.push_back({static_cast<std::uint32_t>(i)});
    // Agreement lattice: bottom -> one entry parameter -> other. Every value
    // changes at most twice, so copy/phi cycles need no per-formal graph walk.
    for (std::size_t head = 0; head < pending.size(); ++head) {
      const auto source = pending[head].value;
      for (auto e = offsets[source]; e < offsets[source + 1]; ++e) {
        const auto target = dependents[e].value;
        const auto joined = Join(origins[target], origins[source]);
        if (joined != origins[target]) {
          origins[target] = joined; pending.push_back({static_cast<std::uint32_t>(target)});
        }
      }
    }
    ObservedParameters result;
    storage.Size(result, queries.size());
    for (std::size_t i = 0; i < queries.size(); ++i) {
      if (!queries[i]) continue;
      const auto reads = ssa->Reads(*queries[i]);
      if (reads.empty()) continue;  // Unreachable observation has no evidence.
      if (reads.size() != 1) return Error::kUnproven;
      if (flow->unknown_before[owners[queries[i]->value].value]) continue;
      const auto origin = origins[reads.front().value.value];
      if (origin.kind == OriginKind::kParameter) result[i] = origin.parameter;
    }
    // Iterative Kosaraju over the same sparse copy/phi dependencies. Copies
    // within one SCC cannot be their own earlier named-storage ancestor.
    std::vector<std::uint8_t> seen;
    storage.Size(seen, values.size());
    std::vector<SsaValueId> finished;
    storage.Reserve(finished, values.size());
    cursor = offsets;
    pending.clear();
    for (std::size_t root = 0; root < values.size(); ++root) if (!seen[root]) {
      seen[root] = 1; pending.push_back({static_cast<std::uint32_t>(root)});
      while (!pending.empty()) {
        const auto value = pending.back().value;
        if (cursor[value] == offsets[value + 1]) {
          finished.push_back(pending.back()); pending.pop_back(); continue;
        }
        const auto next = dependents[cursor[value]++];
        if (!seen[next.value]) { seen[next.value] = 1; pending.push_back(next); }
      }
    }
    std::vector<std::size_t> reverse_offsets;
    storage.Size(reverse_offsets, values.size() + 1);
    for (const auto& edge : edges) ++reverse_offsets[std::size_t(edge.target.value) + 1];
    for (std::size_t i = 1; i < reverse_offsets.size(); ++i) reverse_offsets[i] += reverse_offsets[i - 1];
    cursor = reverse_offsets;
    std::vector<SsaValueId> predecessors;
    storage.Size(predecessors, edges.size());
    for (const auto& edge : edges) predecessors[cursor[edge.target.value]++] = edge.source;
    std::vector<std::optional<SsaValueId>> components;
    storage.Size(components, values.size());
    for (auto it = finished.rbegin(); it != finished.rend(); ++it) if (!components[it->value]) {
      components[it->value] = *it; pending.push_back(*it);
      while (!pending.empty()) {
        const auto value = pending.back().value; pending.pop_back();
        for (auto e = reverse_offsets[value]; e < reverse_offsets[value + 1]; ++e) {
          const auto next = predecessors[e];
          if (!components[next.value]) { components[next.value] = *it; pending.push_back(next); }
        }
      }
    }
    // A later debug local may still contain a copied formal before its own
    // initializer overwrites that register. Prefer the named storage upstream
    // in the real transparent def-use graph, never PC order or copy distance.
    // Independent named copies and conflicting names within an SCC remain
    // ambiguous. Original parameter agreement already rejects ungrounded cycles.
    std::vector<std::uint8_t> inherited;
    storage.Size(inherited, values.size());
    pending.clear();
    const auto mark = [&](SsaValueId id) {
      if (!inherited[id.value]) { inherited[id.value] = 1; pending.push_back(id); }
    };
    std::fill(seen.begin(), seen.end(), 0);
    for (std::size_t i = 0; i < result.size(); ++i) if (result[i]) {
      const auto value = ssa->Reads(*queries[i]).front().value.value;
      seen[components[value]->value] = 1;
    }
    for (const auto& edge : edges)
      if (components[edge.source.value] != components[edge.target.value] &&
          seen[components[edge.source.value]->value]) mark(edge.target);
    for (std::size_t head = 0; head < pending.size(); ++head) {
      const auto value = pending[head].value;
      for (auto e = offsets[value]; e < offsets[value + 1]; ++e) mark(dependents[e]);
    }
    for (std::size_t i = 0; i < result.size(); ++i) if (result[i])
      if (inherited[ssa->Reads(*queries[i]).front().value.value]) result[i].reset();
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
