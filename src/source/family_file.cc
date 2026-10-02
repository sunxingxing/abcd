#include "family_file.h"
#include "family_cells.h"

#include <bit>
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/corpus/abc_file.h"
#include "phased_capture_evaluation.h"
#include "module_aliases.h"

namespace abcd::source {
namespace {
using Error = FamilyFileError;
struct FamilyCursor { FamilyId member; abc::MethodPosition method; std::size_t next = 0; };
class FamilyReader {
 public:
  FamilyReader(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      RouteInputIdentity identity, analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner)
      : file_(file), header_(header), index_(index), lookup_(lookup), roles_(roles), sites_(sites),
        identity_(identity), budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules), module_owner_(module_owner) {}

  FileFamily Read(abc::MethodPosition root, const FunctionContext* owner, FamilyClassMode classes,
      const ParentModuleInput* root_modules, FunctionOwnerId first_owner, const FileFunction* root_creator) {
    class_mode_ = classes;
    result_.first_owner = first_owner;
    Require(root.value < index_.methods.size(), Error::kMethod);
    method_ = index_.methods[root.value].offset;
    Admit(1, sizeof(FamilyReader));
    Grow(active_, index_.methods.size()); Step(index_.methods.size());
    active_.resize(index_.methods.size());
    auto function = ReadMethod(root, owner, InputCaptureMode::kConditional, root_modules);
    if (!function.Phase() && modules_ && classes == FamilyClassMode::kRetainRoot) {
      auto qualified = ModuleCapturePrefix::Build(file_,header_,index_,lookup_,index_.methods[root.value],
          *modules_,function.Normal().input,Remaining());
      if (const auto* error=std::get_if<ModulePrefixError>(&qualified)) {
        if (*error==ModulePrefixError::kResourceLimit) Fail(Error::kResourceLimit);
        if (*error==ModulePrefixError::kWorkLimit) Fail(Error::kWorkLimit);
        Fail(*error==ModulePrefixError::kChanged ? abc::MetadataError::kFileChanged : abc::MetadataError::kRead);
      }
      function.Normal().module_capture_prefix=std::get<std::optional<ModuleCapturePrefix>>(std::move(qualified));
      if (function.Normal().module_capture_prefix) {
        const auto bytes=function.Normal().module_capture_prefix->AllocatedBytes();
        Admit(bytes,1); Step(function.Normal().module_capture_prefix->Work()); function.Normal().allocated_bytes += bytes;
      }
    }
    if (root_modules) result_.root_modules.emplace(*root_modules);
    Add(std::move(function), {}, {}, root);
    while (!stack_.empty()) {
      Step();
      const auto cursor = stack_.back();
      const auto& parent = result_.members[cursor.member.value].function;
      method_ = parent.Method(); parent_.reset(); pc_.reset();
      if (cursor.next == parent.Children().size()) {
        active_[cursor.method.value] = 0; stack_.pop_back(); continue;
      }
      const auto site = parent.Children()[cursor.next];
      ++stack_.back().next;
      Require(site.value < parent.Body().instructions.size(), Error::kCreation);
      parent_ = parent.Method(); pc_ = parent.Body().instructions[site.value].offset;
      Require(site.value < parent.Instructions().size(), Error::kCreation);
      pc_ = parent.Instructions()[site.value].origin.value_or(*pc_);
      const auto& operation = parent.Instructions()[site.value].operation;
      auto& member = result_.members[cursor.member.value];
      if (member.class_projection) {
        const auto& graph = parent.Graph();
        Require(site.value < graph.input.sites.size() && graph.input.sites[site.value], Error::kCreation);
        const auto current = *graph.input.sites[site.value];
        Require(current.value < graph.ops.size(), Error::kCreation);
        const auto& op = graph.ops[current.value];
        Require(op.life == analysis::ValueLife::kLive, Error::kCreation);
        auto creation = std::holds_alternative<analysis::ClassCreation>(operation) ?
            std::optional<analysis::OpId>{current} : std::nullopt;
        if (!creation && !member.class_projection->owners.empty()) creation = member.class_projection->owners[current.value];
        if (creation) {
          Require(creation->value < graph.ops.size() &&
              std::holds_alternative<analysis::ValueClassCreation>(graph.ops[creation->value].operation), Error::kCreation);
          Require(std::holds_alternative<analysis::ClassCreation>(operation) ||
              (std::holds_alternative<analysis::FunctionCreation>(operation) &&
               std::get<analysis::FunctionCreation>(operation).kind == analysis::CreationKind::kMethod), Error::kCreation);
          AppendRoom(member.classes); member.classes.push_back({site, current, *creation});
          continue;
        }
      }
      if (const auto* object = std::get_if<analysis::BufferedObjectCreation>(&operation)) {
        // Capture barriers are not all function edges. The shared literal
        // reader refuses methods/accessors and validates every key/value.
        // Keep the original barrier intact and retain its discharge evidence.
        const MethodConstants constants(file_, header_, index_, *parent_, {bytes_});
        auto parsed = ReadObjectBuffer(*object, *pc_, constants, {bytes_, work_});
        if (const auto* error = std::get_if<BodyEmitFailure>(&parsed)) Fail(*error);
        auto buffer = std::get<ObjectBuffer>(std::move(parsed));
        Admit(buffer.allocated_bytes, 1); Step(buffer.work);
        auto& objects = result_.members[cursor.member.value].objects;
        AppendRoom(objects);
        objects.push_back({site, *object, std::move(buffer)});
        continue;
      }
      if (const auto* array = std::get_if<analysis::BufferedArrayCreation>(&operation)) {
        auto parsed = ReadBufferedArray(file_, header_, index_, *parent_, *array, bytes_, {bytes_});
        if (!std::holds_alternative<BufferedArray>(parsed))
          std::visit([&](const auto& error) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, BufferedArray>) Fail(error);
          }, parsed);
        auto buffer = std::get<BufferedArray>(std::move(parsed));
        Admit(buffer.retained_bytes, 1); Step(buffer.elements.size());
        auto& arrays = result_.members[cursor.member.value].arrays;
        AppendRoom(arrays); arrays.push_back({site, *array, std::move(buffer)});
        continue;
      }
      constexpr std::size_t kMethodLookups = 2;
      Step(std::bit_width(index_.regions.size()) + kMethodLookups * std::bit_width(index_.methods.size()) + 1);
      std::optional<analysis::CreationBinding> normal_binding;
      PhasedCreation* phase_creation = nullptr;
      if (parent.Phase()) {
        auto& creations = result_.members[cursor.member.value].phase_creations;
        Require(creations.has_value(), Error::kCreation);
        Step(std::bit_width(creations->sites.size()) + 1);
        auto found = std::lower_bound(creations->sites.begin(), creations->sites.end(), site,
            [](const auto& creation, auto sought) { return creation.instruction < sought; });
        Require(found != creations->sites.end() && found->instruction == site, Error::kCreation);
        phase_creation = &*found;
      } else {
        auto resolved = analysis::BindCreation(file_, header_, index_, lookup_, parent.Method(),
            parent.Body(), parent.Normal().input.Proof().environments, site.value);
        Require(std::holds_alternative<analysis::CreationBinding>(resolved), Error::kCreation);
        normal_binding.emplace(std::get<analysis::CreationBinding>(std::move(resolved)));
      }
      const auto* binding = phase_creation ? &phase_creation->binding : &*normal_binding;
      const auto target = binding->target.position;
      Require(target.value < index_.methods.size(), Error::kMethod);
      method_ = index_.methods[target.value].offset;
      Require(!active_[target.value], Error::kRecursiveCreation);
      Step(std::bit_width(sites_.Size()) + 1);
      const auto occurrence = sites_.FindOccurrence(*method_, *parent_, *pc_);
      Require(occurrence.has_value(), Error::kOccurrence);
      const auto resolve = [&]() -> CreationTreeContext {
        if (phase_creation) return std::move(phase_creation->context);
        auto result = CreationTreeContext::Resolve(file_, header_, index_, lookup_, roles_, sites_, *occurrence, identity_, Remaining());
        if (const auto* error = std::get_if<CreationContextFailure>(&result)) Fail(*error);
        auto context = std::get<CreationTreeContext>(std::move(result));
        Admit(context.AllocatedBytes(), 1); Step(context.Work()); return context;
      };
      FunctionContext context{resolve()};
      Require(index_.methods[target.value].code.has_value(), Error::kMethod);
      const auto module_input = !parent.Phase()
          ? std::optional<ParentModuleInput>{{parent.Normal().input, site}} : std::nullopt;
      auto child = ReadMethod(target, &context, InputCaptureMode::kConditional,
          module_input ? &*module_input : nullptr);
      const auto child_work = child.Capture();
      auto captured = parent.Phase() ? BindPhasedCaptureCells(index_, lookup_, parent.Phase()->Phases(),
          parent.Body(), parent.Phase()->Locals(), *phase_creation, child_work, context, Remaining()) :
          BindCaptureCells(index_, parent.NormalCapture(), child_work, site, *binding, Remaining());
      if (const auto* error = std::get_if<CaptureCellError>(&captured)) Fail(*error);
      auto cells = std::get<CaptureCells>(std::move(captured));
      Admit(cells.allocated_bytes, 1); Step(cells.work);
      Require(result_.members.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      // No parent reference may survive Add: member-vector growth moves its
      // FileFunction envelope. All relationship identity is stored as IDs.
      FamilyCreation incoming{cursor.member, site, *binding, std::move(cells)};
      if (phase_creation) incoming.phase_operation = phase_creation->operation;
      Add(std::move(child), std::move(context), std::move(incoming), target);
    }
    BindEvaluations(owner, root_creator);
    if (file_.CheckUnchanged()) Fail(FunctionFileFailure{FunctionFileError::kChanged});
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    return std::move(result_);
  }
 private:
  void BindRoot(const FunctionContext* owner, const FileFunction* creator) {
    method_ = result_.members.front().function.Method(); parent_.reset(); pc_.reset();
    // Borrow the actual creator only for this synchronous binding. Root evidence
    // owns its stamps/cell map; a class root never becomes an ordinary family edge.
    if (!creator || !owner || !owner->ClassContext())
      Fail(CaptureEvaluationFailure{CaptureEvaluationError::kOuterCapture});
    parent_ = creator->input.Method();
    if (const auto* site = owner->CreationSite()) pc_ = site->pc;
    else if (const auto* site = owner->ConstructorSite()) pc_ = site->pc;
    else if (const auto* site = owner->ClassSite()) pc_ = site->pc;
    const auto child = result_.members.front().function.Capture();
    auto bound = BindClassRootCaptureCells(file_, header_, index_, lookup_, *creator, child, *owner, Remaining());
    if (const auto* error = std::get_if<CaptureCellError>(&bound)) Fail(*error);
    auto cells = std::get<CaptureCells>(std::move(bound));
    pc_ = cells.input.pc;
    Admit(cells.allocated_bytes, 1); Step(cells.work);
    for (const auto& target : cells.targets) {
      Step();
      if (!std::holds_alternative<ParentCell>(target.target))
        Fail(CaptureEvaluationFailure{CaptureEvaluationError::kOuterCapture});
    }
    result_.root_capture.emplace(std::move(cells));
  }
  void BindEvaluations(const FunctionContext* owner, const FileFunction* creator) {
    // Discovery must finish before committing evaluation bindings: a child's
    // own direct accesses are not its descendants' complete capture demand.
    // Each occurrence is bound once here; no repeated subtree discovery.
    auto planned = PlanFamilyCells(result_, Remaining());
    if (const auto* error = std::get_if<CaptureCellError>(&planned)) Fail(*error);
    const auto cost = std::get<FamilyCellCost>(planned); Admit(cost.allocated_bytes, 1); Step(cost.work);
    // Descendant forwarding must settle before binding the external root. An
    // absent creator keeps the original closed-root refusal, even for count0.
    if (const auto* cells = result_.members.front().function.Cells()) {
      for (const auto& cell : cells->cells) {
        Step();
        if (std::holds_alternative<IncomingCell>(cell.storage)) {
          BindRoot(owner, creator);
          break;
        }
      }
    }
    for (std::size_t i = 1; i < result_.members.size(); ++i) {
      Step();
      auto& child = result_.members[i];
      Require(child.incoming && child.context, Error::kOccurrence);
      auto& edge = *child.incoming;
      Require(edge.parent.value < i, Error::kOccurrence);
      auto& parent = result_.members[edge.parent.value];
      method_ = child.function.Method(); parent_ = parent.function.Method();
      pc_ = child.context->CreationSite() ? child.context->CreationSite()->pc : edge.creation.offset;
      const auto work = child.function.Capture();
      const auto* phase = parent.function.Phase();
      Require((phase != nullptr) == edge.phase_operation.has_value(), Error::kCreation);
      const auto owner = result_.Owner({static_cast<std::uint32_t>(i)});
      auto rebound = phase ? BindPhasedCaptureCells(index_, lookup_, phase->Phases(), phase->Body(),
          phase->Locals(), {*edge.phase_operation, edge.site, edge.creation}, work, *child.context,
          Remaining(), parent.function.Cells()) :
          BindCaptureCells(index_, parent.function.NormalCapture(), work, edge.site, edge.creation, Remaining());
      if (const auto* error = std::get_if<CaptureCellError>(&rebound)) Fail(*error);
      edge.cells = std::get<CaptureCells>(std::move(rebound));
      Admit(edge.cells.allocated_bytes, 1); Step(edge.cells.work);
      auto evaluated = phase ? BindPhasedCaptureEvaluation(index_, lookup_, phase->Phases(), phase->Body(),
          phase->Locals(), {*edge.phase_operation, edge.site, edge.creation}, work, *child.context,
          edge.cells, result_.Owner(edge.parent), owner, Remaining(), parent.function.Cells()) :
          BindCaptureEvaluation(index_, parent.function.NormalCapture(),
              work, edge.site, edge.creation, edge.cells, result_.Owner(edge.parent), owner, Remaining());
      if (const auto* error = std::get_if<CaptureEvaluationFailure>(&evaluated)) Fail(*error);
      auto evaluation = std::get<CapturedFunctionBinding>(std::move(evaluated));
      Admit(evaluation.allocated_bytes, 1); Step(evaluation.work);
      Require(parent.children.empty() || parent.children.back().creation < evaluation.binding.creation, Error::kCreation);
      parent.children.push_back(std::move(evaluation.binding));
    }
  }
  template <class T> [[noreturn]] void Fail(T cause) const {
    throw FamilyFileFailure{std::move(cause), method_, parent_, pc_};
  }
  void Require(bool condition, Error error) const { if (!condition) Fail(error); }
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit); work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit); bytes_ -= count * width;
  }
  template <class T> void Grow(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  template <class T> void AppendRoom(std::vector<T>& values) {
    if (values.size() < values.capacity()) return;
    constexpr std::size_t kGrowth = 2;
    Require(values.size() <= (std::numeric_limits<std::uint32_t>::max() - 1) / kGrowth, Error::kResourceLimit);
    Grow(values, values.empty() ? 1 : values.size() * kGrowth);
  }
  analysis::SemanticBudget Remaining() const { return {bytes_, work_}; }
  FamilyFunction ReadMethod(abc::MethodPosition position, const FunctionContext* owner,
      InputCaptureMode mode = InputCaptureMode::kClosed, const ParentModuleInput* modules_input = nullptr) {
    Require(position.value < index_.methods.size() && index_.methods[position.value].code, Error::kMethod);
    const bool cleanup_entry = !owner && modules_ && class_mode_ == FamilyClassMode::kRetainRoot &&
        index_.methods[position.value].code->try_count;
    if ((index_.methods[position.value].code->try_count && !cleanup_entry) ||
        (owner && IsGeneratorCallable(owner->Callable()))) {
      Require(owner != nullptr, Error::kOccurrence);
      Require(owner->Position() == position && owner->Target() == index_.methods[position.value].offset &&
          owner->Identity() == identity_, Error::kOccurrence);
      std::optional<PhasedModuleBinding> modules;
      if (modules_) modules.emplace(PhasedModuleBinding{file_, header_, index_, *modules_, module_owner_});
      auto read = FilePhasedFunction::Read(file_, header_, index_, *owner, Remaining(), modules ? &*modules : nullptr, mode);
      if (const auto* error = std::get_if<PhasedFileFailure>(&read)) Fail(*error);
      auto function = std::get<FilePhasedFunction>(std::move(read));
      Admit(function.AllocatedBytes(), 1); Step(function.Work()); return function;
    }
    auto read = ReadFunctionValues(file_, header_, index_, position, identity_,
        InputCaptureMode::kConditional, Remaining(), modules_, owner, modules_input,
        cleanup_entry ? FunctionBodyMode::kCleanupEntry : FunctionBodyMode::kOriginal);
    if (const auto* error = std::get_if<FunctionFileFailure>(&read)) Fail(*error);
    auto function = std::get<FileFunction>(std::move(read));
    Admit(function.allocated_bytes, 1);
    return function;
  }
  void Add(FamilyFunction function, std::optional<FunctionContext> context,
      std::optional<FamilyCreation> incoming, abc::MethodPosition method) {
    Require(result_.members.size() <= std::numeric_limits<std::uint32_t>::max() - result_.first_owner.value,
        Error::kResourceLimit);
    AppendRoom(result_.members); AppendRoom(stack_);
    const FamilyId id{static_cast<std::uint32_t>(result_.members.size())};
    FileFamilyMember member{std::move(function), std::move(context), std::move(incoming)};
    Grow(member.children, member.function.Children().size());
    if (id == FileFamily::kRoot && class_mode_ == FamilyClassMode::kRetainRoot) {
      Require(!member.function.Phase(), Error::kUnsupportedFlow);
      const auto& values = member.function.Normal().values;
      auto projected = ProjectClassMembers(values.graph, values.initializers.calls, Remaining(), values.definitions.sites);
      if (const auto* error = std::get_if<ClassInitializerError>(&projected)) Fail(*error);
      member.class_projection.emplace(std::get<ClassProjection>(std::move(projected)));
      Admit(member.class_projection->allocated_bytes, 1); Step(member.class_projection->work);
    }
    if (const auto* phase = member.function.Phase()) {
      auto created = BindPhasedCreations(file_, header_, index_, lookup_, roles_, sites_, phase->Phases(), phase->Body(), Remaining());
      if (const auto* error = std::get_if<PhasedCreationFailure>(&created)) Fail(*error);
      member.phase_creations.emplace(std::get<PhasedCreations>(std::move(created)));
      Admit(member.phase_creations->allocated_bytes, 1); Step(member.phase_creations->work);
    }
    result_.members.push_back(std::move(member));
    stack_.push_back({id, method}); active_[method.value] = 1;
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const analysis::MethodRoleIndex& roles_;
  const analysis::FunctionCreationSites& sites_;
  RouteInputIdentity identity_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  const RecordModuleContext* modules_;
  ModuleOwnerId module_owner_;
  FileFamily result_;
  FamilyClassMode class_mode_ = FamilyClassMode::kReject;
  std::vector<FamilyCursor> stack_;
  std::vector<std::uint8_t> active_;
  std::optional<abc::EntityOffset> method_;
  std::optional<abc::EntityOffset> parent_;
  std::optional<abc::CodeOffset> pc_;
};
}  // namespace
std::variant<FileFamily, FamilyFileFailure> ReadFunctionFamily(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    abc::MethodPosition root, RouteInputIdentity identity,
    analysis::SemanticBudget budget, const RecordModuleContext* modules, const FunctionContext* owner, ModuleOwnerId module_owner, FamilyClassMode classes,
    const ParentModuleInput* root_modules, FunctionOwnerId first_owner, const FileFunction* root_creator) {
  try { return FamilyReader(file, header, index, lookup, roles, sites, identity, budget, modules, module_owner).Read(root, owner, classes, root_modules, first_owner, root_creator);
  } catch (const FamilyFileFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}};
  } catch (const std::length_error&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}}; }
}
}  // namespace abcd::source
