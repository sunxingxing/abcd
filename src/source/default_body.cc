#include "default_body.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "../analysis/value_flow.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = DefaultBodyError;
void Require(bool condition, Error error = Error::kShape) { if (!condition) throw error; }
class Projection {
 public:
  Projection(const ValueProgram& original, const DefaultValueProof& proof, SemanticBudget budget)
      : original_(original), proof_(proof), initial_(budget), budget_(budget) {}
  ValueProgram Build() {
    Require(original_.revision == proof_.Revision() && original_.cfg_revision == proof_.ControlRevision(), Error::kRevision);
    Require(original_.revision.value < std::numeric_limits<std::uint64_t>::max() &&
        original_.cfg_revision.value < std::numeric_limits<std::uint64_t>::max(), Error::kRevision);
    Admit(1, sizeof(DefaultBody)); Admit(proof_.AllocatedBytes(), 1);
    Flow(original_);
    Array(removed_, original_.ops.size());
    Array(aliases_, original_.values.size());
    Array(paths_, original_.blocks.size());
    Array(edges_, original_.edges.size());
    for (const auto placement : proof_.Operations()) {
      Step(); Require(placement.operation.value < removed_.size() && !removed_[placement.operation.value]);
      removed_[placement.operation.value] = true;
    }
    for (const auto& parameter : proof_.Defaults()) {
      Step();
      Require(parameter.merged.value < aliases_.size() && parameter.entry.value < aliases_.size() &&
          parameter.header.value < paths_.size() && parameter.arm.value < paths_.size());
      Require(!aliases_[parameter.merged.value] && !paths_[parameter.header.value] && !paths_[parameter.arm.value]);
      aliases_[parameter.merged.value] = parameter.entry;
      paths_[parameter.header.value] = parameter.missing;
      paths_[parameter.arm.value] = parameter.merge;
    }
    ValueProgram body;
    Clone(body.blocks, original_.blocks); Clone(body.ops, original_.ops);
    Clone(body.values, original_.values); Clone(body.uses, original_.uses);
    Clone(body.origins, original_.origins);
    Reserve(body.edges, original_.edges.size());
    Reserve(body.exceptional_exits, original_.exceptional_exits.size());
    // Keep operation/value/block IDs stable. Only edge IDs compact; remap live
    // phi incoming keys below. Empty prologue blocks remain a harmless linear
    // path, so no unreachable block is hidden from dominance verification.
    for (std::size_t i = 0; i < body.blocks.size(); ++i) {
      Step(); const auto& block = original_.blocks[i];
      auto& output = body.blocks[i];
      output.first_edge = EdgeId{static_cast<std::uint32_t>(body.edges.size())};
      output.edge_count = 0;
      for (std::uint32_t j = 0; j < block.edge_count; ++j) {
        Step(); const EdgeId old{block.first_edge.value + j};
        if (paths_[i] && *paths_[i] != old) continue;
        auto edge = original_.edges[old.value];
        edges_[old.value] = EdgeId{static_cast<std::uint32_t>(body.edges.size())};
        edge.successor = output.edge_count++;
        if (paths_[i]) edge.kind = FlowEdgeKind::kFallthrough;
        body.edges.push_back(edge);
      }
    }
    for (std::size_t i = 0; i < body.ops.size(); ++i) {
      Step(); if (!removed_[i]) continue;
      auto& op = body.ops[i];
      Require(op.life == ValueLife::kLive && op.origin.value < body.origins.size(), Error::kOrigin);
      op.life = ValueLife::kRemoved;
      if (op.result) body.values[op.result->value].life = ValueLife::kRemoved;
      // Origins are not relabeled ElidedPure: the signature still owns these
      // PCs, including branch and refined numeric-conversion operations.
    }
    for (std::size_t i = 0; i < body.values.size(); ++i) {
      Step(); body.values[i].first_use.reset();
      if (aliases_[i]) body.values[i].life = ValueLife::kRemoved;
    }
    Array(last_, body.values.size());
    for (std::size_t i = 0; i < body.uses.size(); ++i) {
      Step(); auto& use = body.uses[i];
      use.previous.reset(); use.next.reset();
      if (const auto* owner = std::get_if<OpId>(&use.owner)) {
        if (removed_[owner->value]) use.life = ValueLife::kRemoved;
      } else if (aliases_[std::get<ValueId>(use.owner).value]) use.life = ValueLife::kRemoved;
      if (use.life != ValueLife::kLive) continue;
      if (use.incoming) {
        if (auto* edge = std::get_if<EdgeId>(&*use.incoming)) {
          Require(edge->value < edges_.size() && edges_[edge->value]);
          *edge = *edges_[edge->value];
        }
      }
      auto* value = std::get_if<ValueId>(&use.operand);
      if (!value) continue;
      if (const auto alias = aliases_[value->value]) *value = *alias;
      Require(body.values[value->value].life == ValueLife::kLive);
      const UseId id{static_cast<std::uint32_t>(i)};
      use.previous = last_[value->value];
      if (use.previous) body.uses[use.previous->value].next = id;
      else body.values[value->value].first_use = id;
      last_[value->value] = id;
    }
    for (const auto& exit : original_.exceptional_exits) {
      Step(); Require(exit.op.value < removed_.size());
      if (!removed_[exit.op.value]) body.exceptional_exits.push_back(exit);
    }
    body.revision = {original_.revision.value + 1};
    body.cfg_revision = {original_.cfg_revision.value + 1};
    // Reconstruct use chains rather than adjusting doubly linked occurrences
    // piecemeal. Verify availability after rewriting all retained incoming keys.
    Flow(body);
    return body;
  }
  std::uint64_t Work() const { return initial_.work - budget_.work; }
 private:
  void Step(std::uint64_t count = 1) { Require(count <= budget_.work, Error::kWorkLimit); budget_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= budget_.bytes / width, Error::kResourceLimit); budget_.bytes -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& items, std::size_t count) {
    Require(count <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
    Admit(count, sizeof(T)); items.reserve(count);
    Admit(items.capacity() - count, sizeof(T));
  }
  template <class T> void Array(std::vector<T>& items, std::size_t count) {
    Reserve(items, count); Step(count); items.resize(count);
  }
  template <class T> void Clone(std::vector<T>& to, const std::vector<T>& from) {
    Reserve(to, from.size()); Step(from.size()); to.assign(from.begin(), from.end());
  }
  void Flow(const ValueProgram& graph) {
    const auto checked = VerifyValueFlow(graph, budget_);
    if (const auto* proof = std::get_if<ValueFlowProof>(&checked)) { Step(proof->work); return; }
    switch (std::get<ValueAvailabilityError>(checked)) {
      case ValueAvailabilityError::kResourceLimit: throw Error::kResourceLimit;
      case ValueAvailabilityError::kWorkLimit: throw Error::kWorkLimit;
      case ValueAvailabilityError::kInvalidGraph:
      case ValueAvailabilityError::kInvalidTerminator:
      case ValueAvailabilityError::kInvalidPhi:
      case ValueAvailabilityError::kUnavailableValue:
      case ValueAvailabilityError::kUnreachableBlock:
      case ValueAvailabilityError::kResumeFlow: throw Error::kFlow;
    }
    throw Error::kFlow;
  }
  const ValueProgram& original_;
  const DefaultValueProof& proof_;
  SemanticBudget initial_;
  SemanticBudget budget_;
  std::vector<std::uint8_t> removed_;
  std::vector<std::optional<ValueId>> aliases_;
  std::vector<std::optional<EdgeId>> paths_;
  std::vector<std::optional<EdgeId>> edges_;
  std::vector<std::optional<UseId>> last_;
};
}  // namespace

std::variant<DefaultBody, DefaultBodyError> DefaultBody::Build(
    const analysis::ValueProgram& original, const ParameterPlan& parameters,
    const DefaultCandidates& candidates, std::uint32_t length,
    analysis::SemanticBudget budget) {
  try {
    // Bind the proof to this exact graph inside the owner factory. A caller
    // cannot supply a sealed proof from another graph with coincident revisions.
    auto checked = DefaultValueProof::Build(original, parameters, candidates, length, budget);
    auto* verified = std::get_if<DefaultValueProof>(&checked);
    if (!verified) {
      const auto error = std::get<DefaultProofError>(checked);
      if (error == DefaultProofError::kResourceLimit) return Error::kResourceLimit;
      if (error == DefaultProofError::kWorkLimit) return Error::kWorkLimit;
      return error == DefaultProofError::kRevision ? Error::kRevision : Error::kProof;
    }
    auto proof = std::move(*verified);
    Require(proof.Work() <= budget.work, Error::kWorkLimit);
    const auto proof_work = proof.Work();
    budget.work -= proof_work;
    Projection projection(original, proof, budget);
    auto graph = projection.Build();
    std::size_t bytes = sizeof(DefaultBody) + proof.AllocatedBytes();
    const auto arena = [&](const auto& values) {
      using T = typename std::decay_t<decltype(values)>::value_type;
      Require(values.capacity() <= (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(T), Error::kResourceLimit);
      bytes += values.capacity() * sizeof(T);
    };
    arena(graph.blocks); arena(graph.edges); arena(graph.ops); arena(graph.values);
    arena(graph.uses); arena(graph.origins); arena(graph.exceptional_exits);
    Require(bytes <= budget.bytes, Error::kResourceLimit);
    return DefaultBody{std::move(graph), std::move(proof), bytes, proof_work + projection.Work()};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
