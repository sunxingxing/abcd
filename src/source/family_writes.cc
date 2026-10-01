#include "family_writes.h"
#include "lexical_stores.h"
#include "class_tree.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = FamilyWriteError;
enum class Barrier : std::uint8_t { kMissing, kFunction, kPrimitiveObject, kPrimitiveArray, kClass, kConsumed };
using Inputs = std::variant<const FileFamily*, std::span<const FamilyWriteInput>>;
class WriteCheck {
 public:
  WriteCheck(const abc::MetadataIndex& index, Inputs inputs,
      const ConstructedFamily& trees, analysis::SemanticBudget budget, const abc::MethodLookup* lookup = nullptr)
      : index_(index), inputs_(inputs), trees_(trees), initial_(budget), left_(budget), lookup_(lookup) {}
  FamilyWrites Run() {
    Admit(1, sizeof(WriteCheck));
    const auto count = std::visit([](auto input) -> std::size_t {
      if constexpr (std::is_same_v<decltype(input), const FileFamily*>) return input->members.size();
      else return input.size();
    }, inputs_);
    Require(count && count <= std::numeric_limits<std::uint32_t>::max() && trees_.members.size() == count &&
        trees_.root == FileFamily::kRoot && trees_.parents.size() == count, Error::kOwner);
    Array(offsets_, count);
    std::size_t sites = 0;
    for (std::size_t i = 0; i < count; ++i) {
      member_ = {static_cast<std::uint32_t>(i)}; Step();
      const auto source = Member(i);
      const auto& graph = Graph(source);
      const auto& tree = trees_.members[i].tree;
      const auto& flow = trees_.members[i].flow;
      std::visit([&](auto input) { Owner(input, tree); }, source.function);
      Require(tree.owner.value == i && tree.value_revision == graph.revision && tree.cfg_revision == graph.cfg_revision &&
          flow.value_revision == graph.revision && flow.cfg_revision == graph.cfg_revision, Error::kOwner);
      offsets_[i] = sites;
      const auto size = Instructions(source).size();
      Require(size <= std::numeric_limits<std::size_t>::max() - sites, Error::kResourceLimit);
      sites += size;
    }
    member_ = FileFamily::kRoot; pc_.reset();
    const auto root = Member(0);
    Require(!root.incoming && !trees_.members[0].incoming && !trees_.parents[0], Error::kCapture);
    std::visit([&](auto input) { ClosedRoot(input); }, root.function);
    Array(barriers_, sites); Array(signature_, sites);
    for (std::size_t i = 0; i < count; ++i) {
      member_ = {static_cast<std::uint32_t>(i)}; pc_.reset();
      const auto source = Member(i);
      const auto& target = trees_.members[i];
      const auto instructions = Instructions(source);
      if (i != 0) {
        Require(source.incoming && target.incoming && trees_.parents[i] &&
            source.incoming->parent.value < i && trees_.parents[i]->function == source.incoming->parent, Error::kCapture);
        const auto& edge = *source.incoming;
        const auto p = edge.parent.value;
        const auto parent = Member(p);
        const auto child = Child(source);
        const auto checked = std::visit([&](const auto& occurrence) {
          return Capture(parent, child, occurrence, *target.incoming, p);
        }, edge.occurrence);
        if (const auto* normal_child = std::get_if<CaptureFunction>(&source.function);
            normal_child && (!normal_child->input.InitializedModuleReads().empty() || normal_child->input.HasInheritedModules())) {
          const auto* normal_parent = std::get_if<CaptureFunction>(&parent.function);
          const auto* normal_edge = std::get_if<NormalWriteEdge>(&edge.occurrence);
          Require(normal_parent && normal_edge, Error::kOwner);
          const auto modules = VerifyInitializedModuleReads(normal_parent->input, normal_edge->site,
              normal_child->input, left_);
          if (const auto* error = std::get_if<FunctionInputError>(&modules))
            Fail(*error == FunctionInputError::kWorkLimit ? Error::kWorkLimit :
                *error == FunctionInputError::kResourceLimit ? Error::kResourceLimit : Error::kOwner);
          Step(std::get<std::uint64_t>(modules));
        }
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
      for (const auto& array : source.arrays) {
        Step(); Mark(i, array.site, Barrier::kPrimitiveArray);
        const auto* actual = std::get_if<analysis::BufferedArrayCreation>(&instructions[array.site.value].operation);
        Require(actual && actual->literal == array.creation.literal, Error::kCoverage);
        // The unchanged file reader excludes callable/unsupported elements;
        // independent tree semantics still checks every array constant.
        ++facts_.primitive_arrays;
      }
      Classes(i, source);
      for (const auto& placement : target.tree.signature) {
        Require(std::holds_alternative<CaptureFunction>(source.function), Error::kStore);
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
      const auto source = Member(i);
      std::visit([&](auto input) { Barriers(input, i); }, source.function);
    }
    for (const auto barrier : barriers_) {
      Step(); Require(barrier == Barrier::kMissing || barrier == Barrier::kConsumed, Error::kCoverage);
    }
    return {facts_, initial_.bytes - left_.bytes, initial_.work - left_.work};
  }
 private:
  void Classes(std::size_t owner, const FamilyWriteInput& source) {
    if (source.classes.empty()) return;
    Require(owner == FileFamily::kRoot.value && std::holds_alternative<CaptureFunction>(source.function), Error::kCoverage);
    const auto& graph = Graph(source);
    const auto& target = trees_.members[owner];
    // Build one operation-owner index from actual composed ClassExpr bodies.
    // No per-method scan over all class definitions and no detached class receipt.
    std::vector<std::optional<analysis::OpId>> owners;
    Array(owners, graph.ops.size());
    for (const auto& obligation : source.classes) {
      Step();
      if (obligation.operation != obligation.creation) continue;
      const auto creation = obligation.creation;
      Require(creation.value < graph.ops.size() && creation.value < target.flow.expressions.size() &&
          target.flow.expressions[creation.value], Error::kCoverage);
      const auto expression = *target.flow.expressions[creation.value];
      Require(expression.value < target.tree.expressions.size(), Error::kCoverage);
      const auto* node = std::get_if<ClassExpr>(&target.tree.expressions[expression.value].expression);
      Require(node && node->definition, Error::kCoverage);
      const auto& definition = *node->definition;
      Require(definition.identity == target.tree.identity && definition.creation.creator == target.tree.method &&
          definition.creation.pc == graph.origins[graph.ops[creation.value].origin.value].pc &&
          std::holds_alternative<analysis::ValueClassCreation>(graph.ops[creation.value].operation), Error::kCoverage);
      Require(!owners[creation.value], Error::kCoverage); owners[creation.value] = creation;
      for (const auto& member : definition.definitions) {
        Require(member.site.creation == creation, Error::kCoverage);
        const auto methods = member.site.Methods();
        for (std::size_t k = 0; k < methods.size(); ++k) {
          Step();
          if (!methods[k].operation) continue;
          const auto operation = *methods[k].operation;
          Require(operation.value < owners.size() && !owners[operation.value] && member.bodies[k], Error::kCoverage);
          owners[operation.value] = creation;
        }
      }
    }
    for (const auto& obligation : source.classes) {
      Step();
      Require(obligation.site.value < graph.input.sites.size() &&
          graph.input.sites[obligation.site.value] == obligation.operation &&
          obligation.operation.value < owners.size() && owners[obligation.operation.value] == obligation.creation,
          Error::kCoverage);
      if (obligation.operation != obligation.creation) {
        const auto* method = std::get_if<analysis::ValueFunctionCreation>(&graph.ops[obligation.operation.value].operation);
        Require(method && method->kind == analysis::CreationKind::kMethod, Error::kCoverage);
      }
      Mark(owner, obligation.site, Barrier::kClass);
    }
  }
  FamilyWriteInput Member(std::size_t i) const {
    if (const auto* file = std::get_if<const FileFamily*>(&inputs_)) {
      const auto& member = (*file)->members[i];
      std::optional<FamilyWriteEdge> edge;
      if (member.incoming) {
        const auto& incoming = *member.incoming;
        if (incoming.phase_operation) {
          Require(member.context.has_value(), Error::kOwner);
          edge.emplace(FamilyWriteEdge{incoming.parent, PhasedWriteEdge{
              {*incoming.phase_operation, incoming.site, incoming.creation}, *member.context}});
        } else edge.emplace(FamilyWriteEdge{incoming.parent, NormalWriteEdge{incoming.site, incoming.creation}});
      }
      if (const auto* phase = member.function.Phase()) {
        Require(lookup_ != nullptr, Error::kOwner);
        return {PhasedFamilyInput{phase->Phases(), phase->Body(), phase->Locals(), *lookup_, phase, member.function.Storage()}, edge, member.objects, {}, member.arrays};
      }
      return {member.function.NormalCapture(), edge, member.objects, member.classes, member.arrays};
    }
    return std::get<std::span<const FamilyWriteInput>>(inputs_)[i];
  }
  static std::span<const analysis::SemanticInstruction> Instructions(const FamilyWriteInput& source) {
    return std::visit([](auto input) -> std::span<const analysis::SemanticInstruction> {
      if constexpr (std::is_same_v<decltype(input), CaptureFunction>) return input.input.Input().instructions;
      else return input.function.TreeInput(input.body).input.flow.instructions;
    }, source.function);
  }
  static const analysis::ValueProgram& Graph(const FamilyWriteInput& source) {
    return std::visit([](auto input) -> const analysis::ValueProgram& {
      if constexpr (std::is_same_v<decltype(input), CaptureFunction>) return input.values.graph;
      else return input.function.Graph();
    }, source.function);
  }
  void Owner(CaptureFunction source, const FunctionTree& tree) {
    const auto& input = source.input; const auto& values = source.values;
    Require(input.Mode() == InputCaptureMode::kConditional && values.identity == input.Identity() &&
        values.capture_mode == input.Mode() && values.record == input.Record() &&
        tree.identity == input.Identity() && values.method == input.Method() && tree.method == input.Method(), Error::kOwner);
    Require(input.Proof().closure_writes == analysis::ClosureWrites::kLanguage, Error::kDomain);
  }
  void Owner(PhasedFamilyInput source, const FunctionTree& tree) {
    Require(tree.identity == source.function.Identity() && tree.method == source.function.Target(), Error::kOwner);
    Require(source.locals.writes == analysis::ClosureWrites::kLanguage, Error::kDomain);
  }
  void ClosedRoot(CaptureFunction source) {
    Require(CaptureChild(source).Input().Captures().empty(), Error::kCapture);
    if (const auto* family = std::get_if<const FileFamily*>(&inputs_); family && (*family)->root_modules) {
      const auto& parent = *(*family)->root_modules;
      const auto check = VerifyInitializedModuleReads(parent.parent, parent.creation, source.input, left_);
      if (const auto* error = std::get_if<FunctionInputError>(&check))
        Fail(*error == FunctionInputError::kWorkLimit ? Error::kWorkLimit : Error::kOwner);
      Step(std::get<std::uint64_t>(check));
    } else Require(source.input.InitializedModuleReads().empty() && !source.input.HasInheritedModules(), Error::kOwner);
  }
  void ClosedRoot(PhasedFamilyInput source) {
    if (source.storage) Require(source.storage->uses.empty(), Error::kCapture);
    for (const auto& binding : source.function.TreeInput(source.body).input.environments.bindings) {
      Step(); Require(!std::holds_alternative<analysis::CapturedDepth>(binding.binding.environment), Error::kCapture);
    }
  }
  CaptureChild Child(const FamilyWriteInput& source) const {
    if (const auto* normal = std::get_if<CaptureFunction>(&source.function)) return *normal;
    const auto& phase = std::get<PhasedFamilyInput>(source.function);
    Require(phase.owner && &phase.owner->Phases() == &phase.function &&
        &phase.owner->Body() == &phase.body && &phase.owner->Locals() == &phase.locals, Error::kOwner);
    return CaptureChild{*phase.owner, phase.storage};
  }
  std::variant<std::uint64_t, CaptureCellError> Capture(const FamilyWriteInput& source, CaptureChild child,
      const NormalWriteEdge& edge, const CaptureCells& cells, std::size_t parent) {
    const auto* normal = std::get_if<CaptureFunction>(&source.function);
    Require(normal != nullptr, Error::kCapture);
    Mark(parent, edge.site, Barrier::kFunction);
    return VerifyCaptureCells(index_, *normal, child, edge.site, edge.creation, cells, left_);
  }
  std::variant<std::uint64_t, CaptureCellError> Capture(const FamilyWriteInput& source, CaptureChild child,
      const PhasedWriteEdge& edge, const CaptureCells& cells, std::size_t parent) {
    const auto* phase = std::get_if<PhasedFamilyInput>(&source.function);
    Require(phase != nullptr, Error::kCapture);
    Mark(parent, edge.creation.instruction, Barrier::kFunction);
    return VerifyPhasedCaptureCells(index_, phase->lookup, phase->function, phase->body,
        phase->locals, edge.creation, child, edge.owner, cells, left_, phase->storage ? &phase->storage->cells : nullptr);
  }
  void Consume(std::size_t owner, analysis::InstructionId site) {
    Step(); const auto instructions = Instructions(Member(owner));
    Require(site.value < instructions.size(), Error::kCoverage); pc_ = instructions[site.value].pc;
    auto& barrier = barriers_[offsets_[owner] + site.value];
    Require(barrier == Barrier::kFunction || barrier == Barrier::kPrimitiveObject ||
        barrier == Barrier::kPrimitiveArray || barrier == Barrier::kClass, Error::kCoverage);
    barrier = Barrier::kConsumed;
  }
  void Barriers(CaptureFunction source, std::size_t owner) {
    for (const auto site : source.input.Children()) Consume(owner, site);
  }
  void Barriers(PhasedFamilyInput source, std::size_t owner) {
    // ExceptionLocals may return early for zero local cells; its normal-plan
    // capture_barriers is NOT a phase census. Inspect all original instructions,
    // including class/buffer creations, even if working IR has removed them.
    const auto instructions = source.function.TreeInput(source.body).input.flow.instructions;
    for (std::size_t i = 0; i < instructions.size(); ++i) {
      Step(); const auto& op = instructions[i].operation;
      if (std::holds_alternative<analysis::FunctionCreation>(op) ||
          std::holds_alternative<analysis::ClassCreation>(op) ||
          std::holds_alternative<analysis::BufferedObjectCreation>(op) ||
          std::holds_alternative<analysis::BufferedArrayCreation>(op))
        Consume(owner, {static_cast<std::uint32_t>(i)});
    }
  }
  void Stores(std::size_t owner) {
    const auto source = Member(owner);
    const auto& target = trees_.members[owner];
    const auto signature = std::span<const std::uint8_t>(signature_).subspan(
        offsets_[owner], Instructions(source).size());
    const auto checked = std::visit([&](auto input) {
      if constexpr (std::is_same_v<decltype(input), CaptureFunction>)
        return VerifyLexicalStores(NormalStoreInput{input.input}, input.values.graph,
            input.values.cells ? &*input.values.cells : nullptr, {target.tree, target.flow}, signature, left_);
      else return VerifyLexicalStores(PhasedStoreInput{input.function, input.body}, input.function.Graph(),
          input.function.Cells(), {target.tree, target.flow}, {}, left_);
    }, source.function);
    if (const auto* error = std::get_if<LexicalStoreFailure>(&checked)) {
      pc_ = error->pc;
      switch (error->kind) {
        case LexicalStoreError::kOwner: Fail(Error::kOwner);
        case LexicalStoreError::kCoverage: Fail(Error::kCoverage);
        case LexicalStoreError::kDomain: Fail(Error::kDomain);
        case LexicalStoreError::kStore: Fail(Error::kStore);
        case LexicalStoreError::kResourceLimit: Fail(Error::kResourceLimit);
        case LexicalStoreError::kWorkLimit: Fail(Error::kWorkLimit);
      }
    }
    const auto& stores = std::get<LexicalStores>(checked);
    Admit(stores.allocated_bytes, 1); Step(stores.work);
    facts_.stores += stores.stores; facts_.captured_stores += stores.captured;
    facts_.signature_stores += stores.signature;
  }
  void Mark(std::size_t owner, analysis::InstructionId site, Barrier proof) {
    pc_.reset(); const auto instructions = Instructions(Member(owner));
    Require(site.value < instructions.size(), Error::kCoverage);
    pc_ = instructions[site.value].pc;
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
  Inputs inputs_;
  const ConstructedFamily& trees_;
  analysis::SemanticBudget initial_, left_;
  const abc::MethodLookup* lookup_;
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
    const ConstructedFamily& trees, analysis::SemanticBudget budget, const abc::MethodLookup* lookup) {
  try { return WriteCheck(index, &inputs, trees, budget, lookup).Run();
  } catch (const FamilyWriteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}}; }
}
std::variant<FamilyWrites, FamilyWriteFailure> VerifyFamilyWrites(
    const abc::MetadataIndex& index, std::span<const FamilyWriteInput> inputs,
    const ConstructedFamily& trees, analysis::SemanticBudget budget) {
  try { return WriteCheck(index, inputs, trees, budget).Run();
  } catch (const FamilyWriteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}};
  } catch (const std::length_error&) { return FamilyWriteFailure{Error::kResourceLimit, {0}, {}}; }
}
}  // namespace abcd::source
