#include "default_body.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "../analysis/value_flow.h"
#include "recovery_scratch.h"
#include "tree_checks.h"

namespace abcd::source {
namespace {
using namespace analysis;
using Error = DefaultBodyError;
void Require(bool condition, Error error = Error::kShape) { if (!condition) throw error; }
template<class Proof> class Projection {
 public:
  Projection(const ValueProgram& original, const Proof& proof, SemanticBudget budget)
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
      if constexpr (std::is_same_v<Proof, ParameterExpressionProof>) {
        const auto& op = original_.ops[placement.operation.value];
        if (std::holds_alternative<ValueJump>(op.operation)) {
          Require(original_.blocks[op.block.value].edge_count == 1);
          paths_[op.block.value] = original_.blocks[op.block.value].first_edge;
        }
      }
    }
    for (const auto& parameter : proof_.Defaults()) {
      Step();
      Require(parameter.header.value < paths_.size() && parameter.arm.value < paths_.size());
      Require(!paths_[parameter.header.value] && !paths_[parameter.arm.value]);
      if constexpr ((std::is_same_v<Proof, DefaultValueProof> ||
                     std::is_same_v<Proof, ParameterExpressionProof>)) {
        if (parameter.merged) {
          Require(parameter.merged->value < aliases_.size() && parameter.entry.value < aliases_.size() && !aliases_[parameter.merged->value]);
          aliases_[parameter.merged->value] = parameter.entry;
        }
      }
      paths_[parameter.header.value] = parameter.missing;
      if constexpr (!std::is_same_v<Proof, ParameterExpressionProof>)
        paths_[parameter.arm.value] = parameter.merge;
    }
    ValueProgram body;
    body.arguments = original_.arguments;
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
      Require(op.origin.value < body.origins.size(), Error::kOrigin);
      // The sealed proof can reclaim an elided, unused literal for the native
      // signature. It stays removed from execution; its PC now belongs there.
      if (op.life == ValueLife::kRemoved) {
        Require(body.origins[op.origin.value].disposition == OriginDisposition::kElidedPure, Error::kOrigin);
        body.origins[op.origin.value].disposition = OriginDisposition::kRetained;
      }
      op.life = ValueLife::kRemoved;
      if (op.result) body.values[op.result->value].life = ValueLife::kRemoved;
      {
        // This is the sealed RESIDUAL program under initialized formals, not
        // a claim that original effects/control are erasable. Migrated instructions
        // become tombstones; Signature().Operations() still owns their PCs.
        // Environment rebuilding must see no body read/store at these sites.
        op.operation = ValueNop{};
        // Dormant result/use IDs remain for bidirectional use-chain auditing;
        // their values/occurrences are marked removed below, never evaluated.
        op.normal = LanguageEffect::kNone; op.before_throw = LanguageEffect::kNone;
        op.exceptional = ExceptionalCompletion::kImpossible;
        op.exception_cause = ExceptionCause::kUnclassified;
      }
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
    {
      // Surviving-site lookup for ORIGINAL capture/module rebinding, not an
      // input-correspondence certificate for initialized source parameters.
      // Removed signature sites have no body operation; old value maps are
      // deliberately not copied into this new parameter interpretation.
      Clone(body.input.sites, original_.input.sites);
      for (auto& site : body.input.sites) {
        Step();
        if (!site) continue;
        Require(site->value < body.ops.size(), Error::kOrigin);
        if (removed_[site->value]) site.reset();
      }
    }
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
  const Proof& proof_;
  SemanticBudget initial_;
  SemanticBudget budget_;
  std::vector<std::uint8_t> removed_;
  std::vector<std::optional<ValueId>> aliases_;
  std::vector<std::optional<EdgeId>> paths_;
  std::vector<std::optional<EdgeId>> edges_;
  std::vector<std::optional<UseId>> last_;
};
struct ProjectedDefault {
  ValueProgram graph;
  std::vector<DefaultInitializer> initializers;
  std::vector<SignatureString> strings;
  std::size_t bytes;
  std::uint64_t work;
};
template<class Proof> ProjectedDefault Project(const ValueProgram& original,
    const Proof& proof, SemanticBudget budget) {
  const auto initial_work = budget.work;
  Require(proof.Work() <= budget.work, Error::kWorkLimit);
  const auto count = proof.Defaults().size();
  budget.work -= proof.Work();
  Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
  Require(count <= budget.bytes / sizeof(DefaultInitializer), Error::kResourceLimit);
  std::vector<DefaultInitializer> initializers;
  initializers.reserve(count);
  Require(initializers.capacity() <= budget.bytes / sizeof(DefaultInitializer), Error::kResourceLimit);
  const auto initializer_bytes = initializers.capacity() * sizeof(DefaultInitializer);
  budget.bytes -= initializer_bytes;
  for (const auto& candidate : proof.Defaults()) {
    if constexpr ((std::is_same_v<Proof, DefaultValueProof> ||
                   std::is_same_v<Proof, ParameterExpressionProof>))
      initializers.push_back({candidate.parameter, candidate.fallback, candidate.initializer, candidate.text});
    else initializers.push_back({candidate.parameter,candidate.fallback,candidate.fallback_store,candidate.text});
  }
  std::vector<SignatureString> strings;
  std::size_t string_count = 0;
  Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
  for (const auto& candidate : initializers) string_count += candidate.text.has_value();
  Require(string_count <= budget.bytes / sizeof(SignatureString), Error::kResourceLimit);
  strings.reserve(string_count);
  Require(strings.capacity() <= budget.bytes / sizeof(SignatureString), Error::kResourceLimit);
  const auto string_bytes = strings.capacity() * sizeof(SignatureString); budget.bytes -= string_bytes;
  Require(count <= budget.work, Error::kWorkLimit); budget.work -= count;
  for (const auto& candidate : initializers)
    if (candidate.text) strings.push_back({candidate.initializer,*candidate.text});
  if constexpr (std::is_same_v<Proof, ParameterExpressionProof>) {
    std::size_t count = 0;
    for (const auto& e : proof.Operations()) {
      if (std::holds_alternative<ValueString>(
              original.ops[e.operation.value].operation) ||
          std::holds_alternative<ValueNamedRead>(
              original.ops[e.operation.value].operation) ||
          std::holds_alternative<ValueGlobalRead>(
              original.ops[e.operation.value].operation))
        ++count;
    }
    Require(count <= budget.bytes / sizeof(SignatureString),
            Error::kResourceLimit);
    budget.bytes -= count * sizeof(SignatureString);
    strings.reserve(count);
    Require(
        strings.capacity() - count <= budget.bytes / sizeof(SignatureString),
        Error::kResourceLimit);
    budget.bytes -= (strings.capacity() - count) * sizeof(SignatureString);
    for (const auto& e : proof.Operations()) {
      Require(budget.work != 0, Error::kWorkLimit);
      --budget.work;
      const auto& op = original.ops[e.operation.value];
      if (const auto* text = std::get_if<ValueString>(&op.operation))
        strings.push_back({e.operation, text->string});
      else if (const auto* name = std::get_if<ValueNamedRead>(&op.operation))
        strings.push_back(
            {e.operation, name->name, SignatureConstantKind::kName});
      else if (const auto* name = std::get_if<ValueGlobalRead>(&op.operation))
        strings.push_back(
            {e.operation, name->name, SignatureConstantKind::kName});
    }
  }
  const auto preparation_work = initial_work - budget.work;
  Projection<Proof> projection(original, proof, budget);
  auto graph = projection.Build();
  std::size_t bytes = sizeof(DefaultBody) + proof.AllocatedBytes();
  const auto arena = [&](const auto& values) {
    using T = typename std::decay_t<decltype(values)>::value_type;
    Require(values.capacity() <= (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(T), Error::kResourceLimit);
    bytes += values.capacity() * sizeof(T);
  };
  arena(graph.blocks); arena(graph.edges); arena(graph.ops); arena(graph.values);
  arena(graph.uses); arena(graph.origins); arena(graph.exceptional_exits);
  arena(graph.input.sites); arena(graph.input.values);
  Require(bytes <= budget.bytes, Error::kResourceLimit);
  const auto retained_string_bytes =
      strings.capacity() * sizeof(SignatureString);
  return {std::move(graph), std::move(initializers), std::move(strings),
          bytes + initializer_bytes + retained_string_bytes,
          preparation_work + projection.Work()};
}
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
    auto projected = Project(original, proof, budget);
    return DefaultBody{std::move(projected.graph), std::move(proof), std::move(projected.initializers), std::move(projected.strings),
        projected.bytes, projected.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<DefaultBody, DefaultBodyError> DefaultBody::BuildExpressions(
    const analysis::ValueProgram& original, const ParameterPlan& parameters,
    const DefaultCandidates& candidates, std::uint32_t length,
    analysis::SemanticBudget budget) {
  try {
    auto checked = ParameterExpressionProof::Build(original, parameters,
                                                   candidates, length, budget);
    if (const auto* e = std::get_if<ParameterExpressionFailure>(&checked))
      return e->kind == ParameterExpressionError::kResourceLimit
                 ? Error::kResourceLimit
             : e->kind == ParameterExpressionError::kWorkLimit
                 ? Error::kWorkLimit
                 : Error::kProof;
    auto proof = std::get<ParameterExpressionProof>(std::move(checked));
    auto projected = Project(original, proof, budget);
    std::size_t bytes = sizeof(ValueProgram);
    std::uint64_t copy_work = 0;
    const auto charge = [&](const auto& v) {
      using T = typename std::decay_t<decltype(v)>::value_type;
      Require(v.size() <=
                  (budget.bytes - std::min(budget.bytes, bytes)) / sizeof(T),
              Error::kResourceLimit);
      bytes += v.size() * sizeof(T);
      Require(v.size() <= budget.work - std::min(copy_work, budget.work),
              Error::kWorkLimit);
      copy_work += v.size();
    };
    charge(original.blocks);
    charge(original.edges);
    charge(original.ops);
    charge(original.values);
    charge(original.uses);
    charge(original.origins);
    charge(original.exceptional_exits);
    charge(original.input.sites);
    charge(original.input.values);
    Require(projected.bytes <= budget.bytes &&
                bytes <= budget.bytes - projected.bytes,
            Error::kResourceLimit);
    DefaultBody body{
        std::move(projected.graph),        std::move(proof),
        std::move(projected.initializers), std::move(projected.strings),
        projected.bytes + bytes,           projected.work};
    Require(body.work_ <= budget.work && copy_work <= budget.work - body.work_,
            Error::kWorkLimit);
    body.work_ += copy_work;
    TreeCheckBudget q(
        {budget.bytes - std::min(body.allocated_bytes_, budget.bytes),
         budget.work - std::min(body.work_, budget.work)});
    body.original_ = std::allocate_shared<const analysis::ValueProgram>(
        RecoveryAllocator<analysis::ValueProgram>{q}, original);
    body.formal_count_ = parameters.formals.size();
    body.length_ = length;
    const auto& cert = body.Expressions()->Evidence();
    std::vector<std::optional<OpId>> guards;
    q.Array(guards, original.values.size());
    std::size_t count = 0;
    for (const auto& e : cert.events) {
      q.Step();
      const auto& op = original.ops[e.operation.value];
      if (std::holds_alternative<ValueLexicalLoad>(op.operation)) ++count;
      if (std::holds_alternative<ValueGuard>(op.operation)) {
        const auto* value =
            std::get_if<ValueId>(&original.uses[op.first_use.value].operand);
        Require(value);
        guards[value->value] = e.operation;
      }
    }
    if (count) {
      auto env = BuildValueEnvironments(original, q.Remaining());
      if (const auto* e = std::get_if<ValueEnvironmentError>(&env))
        throw *e == ValueEnvironmentError::kResourceLimit
            ? Error::kResourceLimit
        : *e == ValueEnvironmentError::kWorkLimit ? Error::kWorkLimit
                                                  : Error::kProof;
      const auto& environments = std::get<ValueEnvironments>(env);
      q.Admit(environments.allocated_bytes, 1);
      q.Step(environments.work);
      auto planned = BuildCellPlan(original, environments, q.Remaining());
      if (const auto* e = std::get_if<CellError>(&planned))
        throw *e == CellError::kResourceLimit ? Error::kResourceLimit
        : *e == CellError::kWorkLimit         ? Error::kWorkLimit
                                              : Error::kProof;
      const auto& cells = std::get<CellPlan>(planned);
      q.Admit(cells.allocated_bytes, 1);
      q.Step(cells.work);
      q.Admit(count, sizeof(SignatureCapture));
      body.captures_.reserve(count);
      q.Admit(body.captures_.capacity() - count, sizeof(SignatureCapture));
      q.Admit(count, sizeof(std::uint32_t));
      body.capture_sites_.reserve(count);
      q.Admit(body.capture_sites_.capacity() - count, sizeof(std::uint32_t));
      for (const auto& e : cert.events) {
        q.Step();
        const auto& op = original.ops[e.operation.value];
        const auto* load = std::get_if<ValueLexicalLoad>(&op.operation);
        if (!load) continue;
        Require(cells.operations[e.operation.value].has_value());
        const auto* incoming = std::get_if<IncomingCell>(
            &cells.cells[cells.operations[e.operation.value]->value].storage);
        Require(incoming);
        body.capture_sites_.push_back(body.captures_.size());
        body.captures_.push_back(
            {e.parameter,
             e.operation,
             guards[op.result->value],
             {original.origins[op.origin.value].site.value},
             load->address,
             *incoming});
      }
      std::sort(body.capture_sites_.begin(), body.capture_sites_.end(),
                [&](auto a, auto b) {
                  q.Step();
                  return body.captures_[a].site < body.captures_[b].site;
                });
    }
    q.Admit(1, sizeof(ParameterExpressionInput) +
                   sizeof(std::shared_ptr<ParameterExpressionInput>));
    auto input = std::allocate_shared<ParameterExpressionInput>(
        RecoveryAllocator<ParameterExpressionInput>{q});
    input->graph = body.original_;
    input->certificate.revision = cert.revision;
    input->certificate.cfg_revision = cert.cfg_revision;
    input->certificate.formals = cert.formals;
    input->certificate.length = cert.length;
    const auto clone = [&](auto& to, const auto& from) {
      q.Array(to, from.size());
      std::copy(from.begin(), from.end(), to.begin());
    };
    clone(input->certificate.defaults, cert.defaults);
    clone(input->certificate.events, cert.events);
    clone(input->certificate.exceptions, cert.exceptions);
    body.expression_input_ = std::move(input);
    body.allocated_bytes_ += q.Bytes();
    body.work_ += q.Work();
    return body;
  } catch (TreeStructureError e) {
    return e == TreeStructureError::kWorkLimit       ? Error::kWorkLimit
           : e == TreeStructureError::kResourceLimit ? Error::kResourceLimit
                                                     : Error::kProof;
  } catch (Error e) {
    return e;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  }
}

std::variant<DefaultBody, DefaultBodyError> DefaultBody::BuildCaptured(
    const analysis::ValueProgram& original, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ParameterPlan& parameters,
    const DefaultCandidates& candidates, std::uint32_t length, analysis::SemanticBudget budget) {
  try {
    auto checked = DefaultValueProof::BuildCaptured(original, environments, cells, parameters, candidates, length, budget);
    auto* proof = std::get_if<DefaultValueProof>(&checked);
    if (!proof) {
      const auto error = std::get<DefaultProofError>(checked);
      if (error == DefaultProofError::kResourceLimit) return Error::kResourceLimit;
      if (error == DefaultProofError::kWorkLimit) return Error::kWorkLimit;
      return error == DefaultProofError::kRevision ? Error::kRevision : Error::kProof;
    }
    auto projected = Project(original, *proof, budget);
    return DefaultBody{std::move(projected.graph), std::move(*proof), std::move(projected.initializers), std::move(projected.strings),
        projected.bytes, projected.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}

std::variant<DefaultBody, DefaultBodyError> DefaultBody::BuildCell(
    const analysis::ValueProgram& original, const analysis::ValueEnvironments& environments,
    const CellPlan& cells, const ParameterPlan& parameters, const CellDefaults& candidates,
    std::uint32_t length, analysis::SemanticBudget budget) {
  try {
    auto checked = CellDefaultProof::Build(original, environments, cells, parameters, candidates, length, budget);
    auto* proof = std::get_if<CellDefaultProof>(&checked);
    if (!proof) {
      const auto error = std::get<CellDefaultProofError>(checked);
      if (error == CellDefaultProofError::kResourceLimit) return Error::kResourceLimit;
      if (error == CellDefaultProofError::kWorkLimit) return Error::kWorkLimit;
      return error == CellDefaultProofError::kRevision ? Error::kRevision : Error::kProof;
    }
    auto projected = Project(original, *proof, budget);
    return DefaultBody{std::move(projected.graph), std::move(*proof), std::move(projected.initializers), std::move(projected.strings),
        projected.bytes, projected.work};
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
