#include "exception_forwarding.h"
#include "for_in_quota.h"
#include "../analysis/value_phi.h"
#include <new>
#include <stdexcept>

namespace abcd::source {
using namespace analysis;
std::variant<ExceptionForwarding, ForInFailure> AnalyzeExceptionForwarding(
    const ValueProgram& graph, SemanticBudget budget) {
  detail::ForInQuota quota{budget};
  try {
    auto indexed = AnalyzeValueIdentities(graph, quota.Remaining());
    if (const auto* error = std::get_if<ValueSimplifyError>(&indexed))
      quota.Fail(*error == ValueSimplifyError::kWorkLimit ? ForInError::kWorkLimit :
          *error == ValueSimplifyError::kResourceLimit ? ForInError::kResourceLimit : ForInError::kIdentity);
    auto identities = std::get<ValueIdentities>(std::move(indexed));
    quota.memory.Retain(identities.allocated_bytes); quota.Step(identities.work);
    const auto count = graph.blocks.size();
    ExceptionForwarding result;
    quota.Reserve(result.blocks, count); result.blocks.resize(count);
    std::pmr::vector<std::optional<ValueExceptionalExit>> exits(graph.ops.size(), {}, &quota.memory);
    std::pmr::vector<std::uint32_t> pending(count, 0, &quota.memory), offsets(count + 1, 0, &quota.memory);
    std::pmr::vector<WorkBlockId> predecessors(graph.edges.size(), {}, &quota.memory), queue(&quota.memory);
    std::pmr::vector<std::uint8_t> possible(count, 1, &quota.memory), terminal(count, 0, &quota.memory);
    queue.reserve(count);
    for (const auto& exit : graph.exceptional_exits) {
      quota.Step(); quota.Require(exit.op.value < exits.size() && !exits[exit.op.value]);
      exits[exit.op.value] = exit;
    }
    for (const auto& edge : graph.edges) {
      quota.Step(); quota.Require(edge.source.value < count && edge.target.value < count);
      if (edge.kind == FlowEdgeKind::kException) continue;
      ++pending[edge.source.value]; ++offsets[edge.target.value + 1];
    }
    for (std::size_t b = 1; b < offsets.size(); ++b) { quota.Step(); offsets[b] += offsets[b - 1]; }
    auto cursor = std::pmr::vector<std::uint32_t>{offsets, &quota.memory};
    for (const auto& edge : graph.edges) {
      quota.Step(); if (edge.kind != FlowEdgeKind::kException) predecessors[cursor[edge.target.value]++] = edge.source;
    }
    for (std::uint32_t b = 0; b < count; ++b) {
      quota.Step(); const auto& block = graph.blocks[b];
      quota.Require(block.first_op.value <= graph.ops.size() && block.op_count <= graph.ops.size() - block.first_op.value);
      for (std::uint32_t n = 0; n < block.op_count; ++n) {
        quota.Step(); const OpId id{block.first_op.value + n}; const auto& op = graph.ops[id.value];
        if (op.life == ValueLife::kRemoved) continue;
        if (std::holds_alternative<ValueThrow>(op.operation)) {
          quota.Require(op.use_count == 1 && op.first_use.value < graph.uses.size());
          const auto& use = graph.uses[op.first_use.value];
          const auto* value = std::get_if<ValueId>(&use.operand);
          if (value && exits[id.value] && use.role == OperandRole::kThrownValue &&
              op.exceptional == ExceptionalCompletion::kAlwaysThrow &&
              op.normal == LanguageEffect::kNone && op.before_throw == LanguageEffect::kNone) {
            result.blocks[b] = ForwardedException{identities.canonical[value->value], exits[id.value]->handler};
            terminal[b] = 1;
          } else possible[b] = 0;
        } else if (!detail::Inert(op) || terminal[b]) possible[b] = 0;
      }
      if (!possible[b]) result.blocks[b].reset();
      if (terminal[b] || pending[b] == 0) queue.push_back({b});
    }
    // Reverse Kahn: a cycle never acquires a terminating summary. No repeated
    // handler walks; each ordinary arc participates once, including joins.
    for (std::size_t at = 0; at < queue.size(); ++at) {
      quota.Step(); const auto b = queue[at].value;
      for (auto p = offsets[b]; p < offsets[b + 1]; ++p) {
        quota.Step(); const auto pred = predecessors[p].value;
        if (terminal[pred]) continue;
        quota.Require(pending[pred] != 0);
        if (!result.blocks[b]) possible[pred] = 0;
        else if (result.blocks[pred] && result.blocks[pred] != result.blocks[b]) possible[pred] = 0;
        else result.blocks[pred] = result.blocks[b];
        if (--pending[pred] == 0) {
          if (!possible[pred]) result.blocks[pred].reset();
          queue.push_back({pred});
        }
      }
    }
    for (std::size_t b = 0; b < count; ++b) {
      quota.Step(); if (pending[b] && !terminal[b]) result.blocks[b].reset();
    }
    result.allocated_bytes = quota.memory.Peak(); result.work = quota.initial - quota.remaining;
    return result;
  } catch (ForInFailure failure) { return failure;
  } catch (const std::bad_alloc&) { quota.witness.kind = ForInError::kResourceLimit; return quota.witness;
  } catch (const std::length_error&) { quota.witness.kind = ForInError::kResourceLimit; return quota.witness; }
}
std::variant<ProtocolForwarding, ForInFailure> AnalyzeProtocolForwarding(
    const ValueProgram& g,const ExceptionPlan& plan,std::span<const std::uint8_t> owned,
    std::span<const std::uint8_t> close,SemanticBudget budget,std::span<const std::uint8_t> retained) {
  detail::ForInQuota q(budget);
  try {
    q.Require(owned.size()==plan.regions.size() && close.size()==g.blocks.size());
    q.Require(retained.empty() || retained.size()==plan.regions.size());
    auto analyzed=AnalyzeExceptionForwarding(g,q.Remaining());
    if(const auto* f=std::get_if<ForInFailure>(&analyzed))return *f;
    const auto& summary=std::get<ExceptionForwarding>(analyzed);
    q.memory.Retain(summary.allocated_bytes);q.Step(summary.work);
    auto identity=AnalyzeValueIdentities(g,q.Remaining());
    if(const auto* e=std::get_if<ValueSimplifyError>(&identity))q.Fail(*e==ValueSimplifyError::kWorkLimit?ForInError::kWorkLimit:*e==ValueSimplifyError::kResourceLimit?ForInError::kResourceLimit:ForInError::kIdentity);
    const auto& ids=std::get<ValueIdentities>(identity);q.memory.Retain(ids.allocated_bytes);q.Step(ids.work);
    ProtocolForwarding out;
    const auto size=[&](auto& v,std::size_t n){q.Reserve(v,n);q.Step(n);v.resize(n);};
    size(out.regions,plan.regions.size());size(out.blocks,g.blocks.size());size(out.operations,g.ops.size());size(out.exits,g.exceptional_exits.size());
    std::pmr::vector<std::optional<TryId>> handlers(g.blocks.size(),{},&q.memory);
    std::pmr::vector<std::uint8_t> terminal(g.blocks.size(),0,&q.memory);
    if(!retained.empty())for(std::uint32_t r=0;r<retained.size();++r){q.Step();if(retained[r]){q.Require(owned[r] && plan.regions[r].binding);terminal[plan.regions[r].binding->block.value]=1;}}
    std::pmr::vector<WorkBlockId> queue(&q.memory);queue.reserve(g.blocks.size());
    for(std::uint32_t r=0;r<plan.regions.size();++r) {
      q.Step();if(owned[r] || plan.layout.regions[r].inactive)continue;
      const auto& region=plan.regions[r];q.witness.block=region.handler;
      q.Require(region.binding,ForInError::kExceptionCorrespondence);
      const auto handler=region.binding->block;
      q.Require(handler.value<summary.blocks.size() && summary.blocks[handler.value],ForInError::kExceptionCorrespondence);
      const auto f=*summary.blocks[handler.value];
      q.Require(region.binding->value.value<ids.canonical.size() && ids.canonical[region.binding->value.value]==f.payload,ForInError::kExceptionCorrespondence);
      out.regions[r]=f;
      if(!handlers[handler.value]){handlers[handler.value]=TryId{r};out.blocks[handler.value]=TryId{r};queue.push_back(handler);}
      else q.Require(out.regions[handlers[handler.value]->value]==out.regions[r],ForInError::kExceptionCorrespondence);
    }
    // Disjointness includes the protected enclosure, not just handler blocks.
    // Prefix counts over verified instruction identities avoid a region x op scan.
    std::pmr::vector<std::uint32_t> overlap(plan.layout.scopes.size()+1,0,&q.memory);
    for(std::size_t b=0;b<close.size();++b){q.Step();if(close[b]){const auto i=g.phases[b].instruction.value;q.Require(i+1<overlap.size());++overlap[i+1];}}
    for(std::size_t i=1;i<overlap.size();++i){q.Step();overlap[i]+=overlap[i-1];}
    for(std::size_t r=0;r<out.regions.size();++r){q.Step();if(!out.regions[r])continue;
      for(const auto range:{plan.layout.regions[r].body,plan.layout.regions[r].handler}){q.Require(range.begin.value<=range.end.value && range.end.value<overlap.size());q.Require(overlap[range.begin.value]==overlap[range.end.value],ForInError::kCatchState);}}
    // Walk a shared handler only once. The summary already proved universal
    // termination and inertness; this census proves disjointness and entries.
    for(std::size_t i=0;i<queue.size();++i) {
      q.Step();const auto b=queue[i];q.witness.block=b;const auto r=*out.blocks[b.value];
      q.Require(!close[b.value] && summary.blocks[b.value]==out.regions[r.value],ForInError::kCatchState);
      const auto& block=g.blocks[b.value];
      for(std::uint32_t o=0;o<block.op_count;++o){q.Step();out.operations[block.first_op.value+o]=r;}
      for(std::uint32_t e=0;e<block.edge_count;++e){q.Step();const auto& edge=g.edges[block.first_edge.value+e];
        // The terminal throw's dispatch edge belongs to its enclosing route,
        // not to the normal forwarding path summarized above.
        if(edge.kind==FlowEdgeKind::kException)continue;
        if(out.blocks[edge.target.value])q.Require(out.regions[out.blocks[edge.target.value]->value]==out.regions[r.value],ForInError::kExceptionCorrespondence);
        else {out.blocks[edge.target.value]=r;queue.push_back(edge.target);}
      }
    }
    // Enclosing transparent handlers may forward again. Resolve the finite
    // route forest by reverse Kahn; a cycle never authorizes erasure.
    std::pmr::vector<std::uint32_t> pending(plan.regions.size(),0,&q.memory),offset(plan.regions.size()+1,0,&q.memory),pred(plan.regions.size(),0,&q.memory),ready(&q.memory);
    ready.reserve(plan.regions.size());
    for(std::uint32_t r=0;r<out.regions.size();++r){q.Step();if(!out.regions[r])continue;
      if(const auto dest=out.regions[r]->destination){q.Require(dest->entry.value<handlers.size() && (handlers[dest->entry.value] || terminal[dest->entry.value]),ForInError::kCatchState);
        if(terminal[dest->entry.value])ready.push_back(r);
        else {const auto parent=*handlers[dest->entry.value];pending[r]=1;++offset[parent.value+1];}}
      else ready.push_back(r);
    }
    for(std::size_t r=1;r<offset.size();++r){q.Step();offset[r]+=offset[r-1];}
    auto cursor=std::pmr::vector<std::uint32_t>(offset,&q.memory);
    for(std::uint32_t r=0;r<out.regions.size();++r){q.Step();if(out.regions[r] && out.regions[r]->destination && !terminal[out.regions[r]->destination->entry.value])pred[cursor[handlers[out.regions[r]->destination->entry.value]->value]++]=r;}
    for(std::size_t i=0;i<ready.size();++i)for(auto p=offset[ready[i]];p<offset[ready[i]+1];++p){q.Step();pending[pred[p]]=0;ready.push_back(pred[p]);}
    for(std::uint32_t r=0;r<out.regions.size();++r){q.Step();q.Require(!pending[r],ForInError::kExceptionCorrespondence);}
    for(std::uint32_t x=0;x<g.exceptional_exits.size();++x){q.Step();const auto& exit=g.exceptional_exits[x];if(!exit.handler)continue;
      const auto h=exit.handler->entry;q.Require(h.value<handlers.size());if(handlers[h.value])out.exits[x]=handlers[h.value];
    }
    std::pmr::vector<std::optional<std::uint32_t>> exit_by_op(g.ops.size(),{},&q.memory);
    std::pmr::vector<std::uint8_t> incoming(g.exceptional_exits.size(),0,&q.memory);
    for(std::uint32_t e=0;e<g.exceptional_exits.size();++e){q.Step();const auto op=g.exceptional_exits[e].op;q.Require(op.value<exit_by_op.size() && !exit_by_op[op.value]);exit_by_op[op.value]=e;}
    for(const auto& edge:g.edges){q.Step();if(!out.blocks[edge.target.value])continue;
      if(edge.kind==FlowEdgeKind::kException){q.Require(handlers[edge.target.value],ForInError::kExceptionCorrespondence);const auto& phase=g.phases[edge.source.value];q.Require(phase.operation && exit_by_op[phase.operation->value],ForInError::kExceptionCorrespondence);
        const auto e=*exit_by_op[phase.operation->value];q.Require(out.exits[e] && !incoming[e] && g.exceptional_exits[e].handler && g.exceptional_exits[e].handler->entry==edge.target,ForInError::kExceptionCorrespondence);incoming[e]=1;}
      else q.Require(out.blocks[edge.source.value] && out.regions[out.blocks[edge.source.value]->value]==out.regions[out.blocks[edge.target.value]->value],ForInError::kExceptionCorrespondence);
    }
    for(std::size_t e=0;e<out.exits.size();++e){q.Step();q.Require(!out.exits[e] || incoming[e],ForInError::kExceptionCorrespondence);}
    out.allocated_bytes=q.memory.Peak();out.work=q.initial-q.remaining;return out;
  }catch(ForInFailure f){return f;}catch(const std::bad_alloc&){return ForInFailure{ForInError::kResourceLimit};}catch(const std::length_error&){return ForInFailure{ForInError::kResourceLimit};}
}
}  // namespace abcd::source
