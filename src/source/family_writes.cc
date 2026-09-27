#include "family_writes.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = FamilyWriteError;
enum class Barrier : std::uint8_t { kMissing, kFunction, kPrimitiveObject, kConsumed };
class WriteCheck {
 public:
  WriteCheck(const abc::MetadataIndex& index, const FileFamily& inputs,
      const ConstructedFamily& trees, analysis::SemanticBudget budget)
      : index_(index), inputs_(inputs), trees_(trees), initial_(budget), left_(budget) {}
  FamilyWrites Run() {
    Admit(1, sizeof(WriteCheck));
    const auto count = inputs_.members.size();
    Require(count && count <= std::numeric_limits<std::uint32_t>::max() && trees_.members.size() == count &&
        trees_.root == FileFamily::kRoot && trees_.parents.size() == count, Error::kOwner);
    Array(offsets_, count);
    std::size_t sites = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = {static_cast<std::uint32_t>(i)}; Step();
      const auto& input = inputs_.members[i].function.input;
      const auto& values = inputs_.members[i].function.values;
      const auto& tree = trees_.members[i].tree;
      const auto& flow = trees_.members[i].flow;
      Require(input.Mode() == InputCaptureMode::kConditional && values.identity == input.Identity() &&
          values.capture_mode == input.Mode() && values.record == input.Record() &&
          tree.identity == input.Identity() && values.method == input.Method() && tree.method == input.Method() &&
          tree.owner.value == i && tree.value_revision == values.graph.revision && tree.cfg_revision == values.graph.cfg_revision &&
          flow.value_revision == values.graph.revision && flow.cfg_revision == values.graph.cfg_revision, Error::kOwner);
      Require(input.Proof().closure_writes == analysis::ClosureWrites::kLanguage, Error::kDomain);
      if (values.cells) Require(values.cells->revision == values.graph.revision &&
          values.cells->cfg_revision == values.graph.cfg_revision &&
          values.cells->operations.size() == values.graph.ops.size(), Error::kOwner);
      offsets_[i] = sites;
      Require(input.Input().instructions.size() <= std::numeric_limits<std::size_t>::max() - sites, Error::kResourceLimit);
      sites += input.Input().instructions.size();
    }
    Require(inputs_.members[0].function.input.Captures().empty() && !inputs_.members[0].incoming &&
        !trees_.members[0].incoming && !trees_.parents[0], Error::kCapture);
    Array(barriers_, sites); Array(signature_, sites);
    for (std::size_t i = 0; i < count; ++i) {
      member_ = {static_cast<std::uint32_t>(i)}; pc_.reset();
      const auto& source = inputs_.members[i];
      const auto& target = trees_.members[i];
      const auto& instructions = source.function.input.Input().instructions;
      if (i != 0) {
        Require(source.incoming && target.incoming && trees_.parents[i] &&
            source.incoming->parent.value < i && trees_.parents[i]->function == source.incoming->parent, Error::kCapture);
        const auto& edge = *source.incoming;
        const auto p = edge.parent.value;
        const auto& parent = inputs_.members[p].function;
        Mark(p, edge.site, Barrier::kFunction); pc_ = edge.creation.offset;
        const auto checked = VerifyCaptureCells(index_, {parent.input, parent.values},
            {source.function.input, source.function.values}, edge.site, edge.creation, *target.incoming, left_);
        if (const auto* error = std::get_if<CaptureCellError>(&checked))
          Fail(*error == CaptureCellError::kResourceLimit ? Error::kResourceLimit :
              *error == CaptureCellError::kWorkLimit ? Error::kWorkLimit : Error::kCapture);
        Step(std::get<std::uint64_t>(checked));
        const auto captured = VerifyCaptureTree(*target.incoming,
            {trees_.members[p].tree, trees_.members[p].flow}, {target.tree, target.flow}, left_);
        if (const auto* error = std::get_if<CaptureTreeError>(&captured))
          Fail(*error == CaptureTreeError::kResourceLimit ? Error::kResourceLimit :
              *error == CaptureTreeError::kWorkLimit ? Error::kWorkLimit : Error::kCapture);
        Step(std::get<std::uint64_t>(captured)); ++facts_.creations;
      }
      for (const auto& object : source.objects) {
        Step(); Mark(i, object.site, Barrier::kPrimitiveObject);
        const auto* actual = std::get_if<analysis::BufferedObjectCreation>(&instructions[object.site.value].operation);
        Require(actual && actual->literal == object.creation.literal, Error::kCoverage);
        // The retained ReadObjectBuffer result already rejects callable values;
        // it is consumed unchanged, not permission to erase this allocation.
        ++facts_.primitive_objects;
      }
      for (const auto& placement : target.tree.signature) {
        Step(); Require(placement.origin.value < target.tree.origins.size(), Error::kStore);
        const auto* origin = std::get_if<OriginalSite>(&target.tree.origins[placement.origin.value]);
        Require(origin && origin->site.value < instructions.size(), Error::kStore);
        Require(!signature_[offsets_[i] + origin->site.value], Error::kStore);
        signature_[offsets_[i] + origin->site.value] = 1;
      }
      Stores(i);
    }
    // All descendants are accounted for before any barrier is discharged.
    for (std::size_t i = 0; i < count; ++i) {
      member_ = {static_cast<std::uint32_t>(i)}; pc_.reset();
      for (const auto site : inputs_.members[i].function.input.Children()) {
        Step(); Require(site.value < inputs_.members[i].function.input.Input().instructions.size(), Error::kCoverage);
        pc_ = inputs_.members[i].function.input.Input().instructions[site.value].pc;
        auto& barrier = barriers_[offsets_[i] + site.value];
        Require(barrier == Barrier::kFunction || barrier == Barrier::kPrimitiveObject, Error::kCoverage);
        barrier = Barrier::kConsumed;
      }
    }
    for (const auto barrier : barriers_) {
      Step(); Require(barrier == Barrier::kMissing || barrier == Barrier::kConsumed, Error::kCoverage);
    }
    return {facts_, initial_.bytes - left_.bytes, initial_.work - left_.work};
  }
 private:
  void Stores(std::size_t owner) {
    const auto& function = inputs_.members[owner].function;
    const auto& input = function.input.Input();
    const auto& graph = function.values.graph;
    const auto& target = trees_.members[owner];
    const auto accesses = function.input.CaptureAccesses();
    Require(graph.input.sites.size() == input.instructions.size(), Error::kStore);
    std::size_t access = 0, retained = 0;
    for (std::size_t i = 0; i < input.instructions.size(); ++i) {
      Step(); pc_ = input.instructions[i].pc;
      const bool captured = access < accesses.size() && accesses[access].instruction.value == i;
      if (captured) ++access;
      if (!std::holds_alternative<analysis::LexicalStore>(input.instructions[i].operation)) continue;
      ++facts_.stores;
      if (captured) {
        Require(accesses[access - 1].kind == analysis::LocalAccessKind::kStore, Error::kStore);
        ++facts_.captured_stores;
      }
      const auto reads = input.ssa.Reads({static_cast<std::uint32_t>(i)});
      Require(!reads.empty(), Error::kDomain);
      for (const auto& read : reads) {
        Step(); const auto domain = function.input.Proof().values.At(read.value);
        Require(domain && domain->IsLanguage(), Error::kDomain);
      }
      const auto operation = graph.input.sites[i];
      if (!operation) {
        Require(!captured && signature_[offsets_[owner] + i], Error::kStore);
        ++facts_.signature_stores; continue;
      }
      Require(!signature_[offsets_[owner] + i] && operation->value < graph.ops.size(), Error::kStore);
      const auto& op = graph.ops[operation->value];
      Require(op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueLexicalStore>(op.operation) &&
          op.origin.value < graph.origins.size() && graph.origins[op.origin.value].site.value == i &&
          graph.origins[op.origin.value].pc == *pc_ &&
          graph.origins[op.origin.value].original == input.instructions[i].origin &&
          graph.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained, Error::kStore);
      Require(function.values.cells && operation->value < function.values.cells->operations.size() &&
          function.values.cells->operations[operation->value] && operation->value < target.flow.expressions.size() &&
          target.flow.expressions[operation->value], Error::kStore);
      const auto cell = *function.values.cells->operations[operation->value];
      Require(cell.value < target.flow.cells.size() && target.flow.cells[cell.value], Error::kStore);
      const auto expression = *target.flow.expressions[operation->value];
      Require(expression.value < target.tree.expressions.size(), Error::kStore);
      const auto* assignment = std::get_if<AssignmentExpr>(&target.tree.expressions[expression.value].expression);
      Require(assignment != nullptr, Error::kStore);
      const auto* symbol = std::get_if<SymbolTarget>(&assignment->target);
      Require(symbol && symbol->symbol == *target.flow.cells[cell.value], Error::kStore);
      ++retained;
    }
    Require(access == accesses.size(), Error::kCoverage);
    std::size_t stores = 0;
    for (const auto& operation : graph.ops) {
      Step(); stores += operation.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueLexicalStore>(operation.operation);
    }
    Require(stores == retained, Error::kStore);
  }
  void Mark(std::size_t owner, analysis::InstructionId site, Barrier proof) {
    Require(site.value < inputs_.members[owner].function.input.Input().instructions.size(), Error::kCoverage);
    auto& slot = barriers_[offsets_[owner] + site.value];
    Require(slot == Barrier::kMissing, Error::kCoverage); slot = proof;
  }
  void Step(std::uint64_t count = 1) { Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  template<class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T)); Step(count); values.resize(count); Admit(values.capacity() - count, sizeof(T));
  }
  [[noreturn]] void Fail(Error error) const { throw FamilyWriteFailure{error, member_, pc_}; }
  void Require(bool ok, Error error) const { if (!ok) Fail(error); }
  const abc::MetadataIndex& index_;
  const FileFamily& inputs_;
  const ConstructedFamily& trees_;
  analysis::SemanticBudget initial_, left_;
  FamilyId member_{0};
  std::optional<abc::CodeOffset> pc_;
  FamilyWriteFacts facts_;
  std::vector<std::size_t> offsets_;
  std::vector<Barrier> barriers_;
  std::vector<std::uint8_t> signature_;
};
}  // namespace
std::variant<FamilyWrites, FamilyWriteFailure> VerifyFamilyWrites(
    const abc::MetadataIndex& index, const FileFamily& inputs,
    const ConstructedFamily& trees, analysis::SemanticBudget budget) {
  try { return WriteCheck(index, inputs, trees, budget).Run();
  } catch (const FamilyWriteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}}; }
}
}  // namespace abcd::source
