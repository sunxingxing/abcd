#include "family_file.h"
#include "class_self_initialization_file.h"
#include "family_cells.h"
#include "creator_classes.h"
#include "abcd/analysis/transparent_rethrow.h"

#include <bit>
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <set>

#include "abcd/corpus/abc_file.h"
#include "phased_capture_evaluation.h"
#include "module_aliases.h"

namespace abcd::source {
namespace {
using Error = FamilyFileError;
// Ordered active-method set: storage/work depend on this occurrence family,
// not on every unrelated method in the file. std::set gives logarithmic cycle
// checks; its rebound allocator charges the actual node before allocation.
template<class T> struct ActiveAllocator {
  using value_type=T;std::size_t* bytes;
  explicit ActiveAllocator(std::size_t& b):bytes(&b){}
  template<class U>ActiveAllocator(const ActiveAllocator<U>& a):bytes(a.bytes){}
  T* allocate(std::size_t n){
    if(n>*bytes/sizeof(T))throw FamilyFileFailure{FamilyFileError::kResourceLimit};
    *bytes-=n*sizeof(T);return std::allocator<T>{}.allocate(n);
  }
  void deallocate(T* p,std::size_t n){std::allocator<T>{}.deallocate(p,n);}
  template<class U>bool operator==(const ActiveAllocator<U>& a)const{return bytes==a.bytes;}
};
struct FamilyCursor { FamilyId member; abc::MethodPosition method; std::size_t next = 0; };
class FamilyReader {
 public:
  FamilyReader(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      RouteInputIdentity identity, analysis::SemanticBudget budget, const RecordModuleContext* modules, ModuleOwnerId module_owner)
      : file_(file), header_(header), index_(index), lookup_(lookup), roles_(roles), sites_(sites),
        identity_(identity), budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules), module_owner_(module_owner), active_(std::less<abc::MethodPosition>{},ActiveAllocator<abc::MethodPosition>{bytes_}) {}

  FileFamily Read(abc::MethodPosition root, const FunctionContext* owner, FamilyClassMode classes,
      const ParentModuleInput* root_modules, FunctionOwnerId first_owner, const FileFunction* root_creator, const analysis::ClassMethodSites* class_sites, const CaptureStorage* root_storage, ClassRootBinding root_binding) {
    class_sites_ = class_sites;
    class_mode_ = classes;
    result_.first_owner = first_owner;
    result_.class_roles=&roles_; result_.class_functions=&sites_; result_.class_sites=class_sites;
    Require(root.value < index_.methods.size(), Error::kMethod);
    method_ = index_.methods[root.value].offset;
    root_ = method_;
    Admit(1, sizeof(FamilyReader));
    Step(); // Preserve the family-stage work gate before reading any child input.
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
        Step(2*(std::bit_width(active_.size())+1));active_.erase(cursor.method); stack_.pop_back(); continue;
      }
      const auto site = parent.Children()[cursor.next];
      ++stack_.back().next;
      RequireCreation(site.value < parent.Body().instructions.size(), FamilyCreationCheck::kBodySite, parent, cursor.member, site);
      parent_ = parent.Method(); pc_ = parent.Body().instructions[site.value].offset;
      RequireCreation(site.value < parent.Instructions().size(), FamilyCreationCheck::kSemanticSite, parent, cursor.member, site);
      pc_ = parent.Instructions()[site.value].origin.value_or(*pc_);
      const auto& operation = parent.Instructions()[site.value].operation;
      auto& member = result_.members[cursor.member.value];
      const bool prepared_record_root=cursor.member==FileFamily::kRoot && class_mode_==FamilyClassMode::kRetainRoot;
      // The existing explicit family DFS owns the creator occurrence. Discover
      // its class work only at the actual barrier, preserving first-failure order.
      if (!member.class_projection && !parent.Phase() && class_sites_ &&
          !prepared_record_root && std::holds_alternative<analysis::ClassCreation>(operation)) {
        SelfOrder(member.function.Normal(),cursor.member);
        const auto& values=member.function.Normal().values;
        const auto& self=member.function.Normal().self_order;
        auto projected=ProjectClassMembers(values.graph,values.initializers.calls,Remaining(),
            values.definitions.sites,self ? &*self : nullptr);
        if(const auto* error=std::get_if<ClassInitializerError>(&projected)) Fail(*error);
        member.class_projection.emplace(std::get<ClassProjection>(std::move(projected)));
        Admit(member.class_projection->allocated_bytes,1);Step(member.class_projection->work);
      }
      if (member.class_projection) {
        const auto& graph = parent.Graph();
        RequireCreation(site.value < graph.input.sites.size() && graph.input.sites[site.value], FamilyCreationCheck::kProjectionSite, parent, cursor.member, site);
        const auto current = *graph.input.sites[site.value];
        RequireCreation(current.value < graph.ops.size(), FamilyCreationCheck::kProjectionOperation, parent, cursor.member, site);
        const auto& op = graph.ops[current.value];
        RequireCreation(op.life == analysis::ValueLife::kLive, FamilyCreationCheck::kProjectionLive, parent, cursor.member, site);
        auto creation = std::holds_alternative<analysis::ClassCreation>(operation) ?
            std::optional<analysis::OpId>{current} : std::nullopt;
        if (!creation && !member.class_projection->owners.empty()) creation = member.class_projection->owners[current.value];
        if (creation) {
          RequireCreation(creation->value < graph.ops.size() &&
              std::holds_alternative<analysis::ValueClassCreation>(graph.ops[creation->value].operation), FamilyCreationCheck::kProjectionOwner, parent, cursor.member, site, creation);
          RequireCreation(std::holds_alternative<analysis::ClassCreation>(operation) ||
              (std::holds_alternative<analysis::FunctionCreation>(operation) &&
               std::get<analysis::FunctionCreation>(operation).kind == analysis::CreationKind::kMethod), FamilyCreationCheck::kProjectionKind, parent, cursor.member, site, creation);
          AppendRoom(member.classes); member.classes.push_back({site, current, *creation});
          if(!prepared_record_root && root_binding == ClassRootBinding::kRequirements) {
            const CreatorClassInput input{file_,header_,index_,lookup_,roles_,sites_,*class_sites_,parent.Normal(),identity_,{*root_,result_.first_owner,{cursor.member.value}}};
            auto tasks=DiscoverCreatorClasses(input,Remaining());
            const auto reject=[&](const CreatorClassFailure& error) {
              const auto failed=error.operation.value_or(*creation);
              const auto& g=parent.Graph();
              const auto failed_site=failed.value<g.ops.size() && g.ops[failed.value].origin.value<g.origins.size() ?
                  analysis::InstructionId{g.origins[g.ops[failed.value].origin.value].site.value} : site;
              auto failure=CreationFailure(FamilyCreationCheck::kClassPreparationInput,parent,cursor.member,failed_site,failed);
              failure.creation_pc=failure.family_creation->location.pc;
              const auto cause=[&](auto&& self,const auto& e)->void {
                using T=std::decay_t<decltype(e)>;
                if constexpr(std::is_enum_v<T>) {
                  if constexpr(requires{T::kResourceLimit;}) if(e==T::kResourceLimit) Fail(Error::kResourceLimit);
                  if constexpr(requires{T::kWorkLimit;}) if(e==T::kWorkLimit) Fail(Error::kWorkLimit);
                }
                if constexpr(std::is_same_v<T,ClassContextFailure>) std::visit([&](const auto& nested){self(self,nested);},e.error);
                else if constexpr(std::is_assignable_v<decltype(failure.creation_cause)&,T>) failure.creation_cause=e;
              };
              std::visit([&](const auto& e){cause(cause,e);},error.cause);
              throw failure;
            };
            if(const auto* error=std::get_if<CreatorClassFailure>(&tasks)) reject(*error);
            const auto& plan=std::get<CreatorClasses>(tasks);
            Admit(plan.allocated_bytes,1);Step(plan.work);
            auto checked=VerifyCreatorClasses(input,plan,Remaining());
            if(const auto* error=std::get_if<CreatorClassFailure>(&checked)) reject(*error);
            const auto cost=std::get<CreatorClassCost>(checked);Admit(cost.allocated_bytes,1);Step(cost.work);
            // Requirements-only member families cannot recursively schedule
            // more class families. Retain a located representation gap until
            // the inter-class preparation graph owns that additional level.
            throw CreationFailure(FamilyCreationCheck::kClassNestedPreparation,parent,cursor.member,site,creation);
          }
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
        RequireCreation(creations.has_value(), FamilyCreationCheck::kPhasedIndex, parent, cursor.member, site);
        Step(std::bit_width(creations->sites.size()) + 1);
        auto found = std::lower_bound(creations->sites.begin(), creations->sites.end(), site,
            [](const auto& creation, auto sought) { return creation.instruction < sought; });
        RequireCreation(found != creations->sites.end() && found->instruction == site, FamilyCreationCheck::kPhasedSite, parent, cursor.member, site);
        phase_creation = &*found;
      } else {
        auto resolved = analysis::BindCreation(file_, header_, index_, lookup_, parent.Method(),
            parent.Body(), parent.Normal().input.Proof().environments, site.value);
        if (!std::holds_alternative<analysis::CreationBinding>(resolved)) {
          auto failure = CreationFailure(FamilyCreationCheck::kFunctionBinding, parent, cursor.member, site);
          std::visit([&](const auto& cause) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(cause)>, analysis::CreationBinding>)
              failure.creation_cause = cause;
          }, resolved);
          throw failure;
        }
        normal_binding.emplace(std::get<analysis::CreationBinding>(std::move(resolved)));
      }
      const auto* binding = phase_creation ? &phase_creation->binding : &*normal_binding;
      const auto target = binding->target.position;
      Require(target.value < index_.methods.size(), Error::kMethod);
      method_ = index_.methods[target.value].offset;
      Step(2*(std::bit_width(active_.size())+1));Require(!active_.contains(target), Error::kRecursiveCreation);
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
      if (const auto* error = std::get_if<CaptureCellFailure>(&captured)) Fail(*error);
      auto cells = std::get<CaptureCells>(std::move(captured));
      Admit(cells.allocated_bytes, 1); Step(cells.work);
      Require(result_.members.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      // No parent reference may survive Add: member-vector growth moves its
      // FileFunction envelope. All relationship identity is stored as IDs.
      FamilyCreation incoming{cursor.member, site, *binding, std::move(cells)};
      if (phase_creation) incoming.phase_operation = phase_creation->operation;
      Add(std::move(child), std::move(context), std::move(incoming), target);
    }
    BindEvaluations(owner, root_creator, root_storage, root_binding);
    if (file_.CheckUnchanged()) Fail(FunctionFileFailure{FunctionFileError::kChanged});
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    return std::move(result_);
  }
 private:
  void BindRoot(const FunctionContext* owner, const FileFunction* creator, const CaptureStorage* storage) {
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
    auto bound = BindClassRootCaptureCells(file_, header_, index_, lookup_, *creator, child, *owner, Remaining(), storage);
    if (const auto* error = std::get_if<CaptureCellFailure>(&bound)) Fail(*error);
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
  void BindEvaluations(const FunctionContext* owner, const FileFunction* creator, const CaptureStorage* storage, ClassRootBinding root_binding) {
    // Discovery must finish before committing evaluation bindings: a child's
    // own direct accesses are not its descendants' complete capture demand.
    // Each occurrence is bound once here; no repeated subtree discovery.
    auto planned = PlanFamilyCells(result_, Remaining());
    if (const auto* error = std::get_if<CaptureCellFailure>(&planned)) Fail(*error);
    const auto cost = std::get<FamilyCellCost>(planned); Admit(cost.allocated_bytes, 1); Step(cost.work);
    // Descendant forwarding must settle before binding the external root. An
    // absent creator keeps the original closed-root refusal, even for count0.
    if (const auto* cells = result_.members.front().function.Cells()) {
      for (const auto& cell : cells->cells) {
        Step();
        if (std::holds_alternative<IncomingCell>(cell.storage)) {
          if (root_binding == ClassRootBinding::kBind) BindRoot(owner, creator, storage);
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
      RequireCreation((phase != nullptr) == edge.phase_operation.has_value(), FamilyCreationCheck::kCaptureFlow, parent.function, edge.parent, edge.site);
      const auto owner = result_.Owner({static_cast<std::uint32_t>(i)});
      auto rebound = phase ? BindPhasedCaptureCells(index_, lookup_, phase->Phases(), phase->Body(),
          phase->Locals(), {*edge.phase_operation, edge.site, edge.creation}, work, *child.context,
          Remaining(), parent.function.Cells()) :
          BindCaptureCells(index_, parent.function.NormalCapture(), work, edge.site, edge.creation, Remaining());
      if (const auto* error = std::get_if<CaptureCellFailure>(&rebound)) Fail(*error);
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
      RequireCreation(parent.children.empty() || parent.children.back().creation < evaluation.binding.creation, FamilyCreationCheck::kChildOrder, parent.function, edge.parent, edge.site);
      parent.children.push_back(std::move(evaluation.binding));
    }
  }
  // Failure-only O(1) witness: borrow existing origins and incoming occurrence.
  // Do not resolve a new class body or allocate an index while diagnosing it.
  FamilyFileFailure CreationFailure(FamilyCreationCheck check, const FamilyFunction& parent,
      FamilyId member, analysis::InstructionId site, std::optional<analysis::OpId> projected = {}) const {
    auto failure = FamilyFileFailure{Error::kCreation, method_, parent_, pc_, root_};
    FamilyCreationEvidence e{check}; e.root = root_; e.occurrence = FamilyOccurrenceId{member.value};
    e.instruction = site; e.location.method = parent.Method(); e.projected_class = projected;
    const auto& graph = parent.Graph();
    if (site.value < graph.input.sites.size() && graph.input.sites[site.value])
      e.location = OperationLocation(graph, *graph.input.sites[site.value], parent.Method());
    if (site.value < parent.Body().instructions.size()) {
      e.location.working_pc = parent.Body().instructions[site.value].offset;
      e.location.pc = e.location.working_pc;
    }
    if (site.value < parent.Instructions().size()) {
      const auto& instruction = parent.Instructions()[site.value];
      if (instruction.origin) e.location.pc = instruction.origin;
      e.kind = FamilyCreationKind::kOther;
      if (const auto* c = std::get_if<analysis::ClassCreation>(&instruction.operation)) {
        e.kind = FamilyCreationKind::kClass; e.constructor = c->constructor; e.literal = c->literal;
      } else if (const auto* c = std::get_if<analysis::FunctionCreation>(&instruction.operation)) {
        e.kind = c->kind == analysis::CreationKind::kFunction ? FamilyCreationKind::kFunction : FamilyCreationKind::kMethod;
      }
    }
    if (member.value < result_.members.size()) {
      const auto& child = result_.members[member.value];
      if (child.incoming && child.incoming->parent.value < result_.members.size()) {
        const auto& edge = *child.incoming;
        const auto& creator = result_.members[edge.parent.value].function;
        const auto& graph = creator.Graph();
        TreeLocation source; source.method = creator.Method();
        if (edge.site.value < graph.input.sites.size() && graph.input.sites[edge.site.value])
          source = OperationLocation(graph, *graph.input.sites[edge.site.value], creator.Method());
        source.working_pc = edge.creation.offset;
        source.pc = child.context && child.context->CreationSite() ?
            child.context->CreationSite()->pc : edge.creation.offset;
        e.descendant_source = source;
      }
    }
    failure.family_creation = e; return failure;
  }
  void RequireCreation(bool ok, FamilyCreationCheck check, const FamilyFunction& parent,
      FamilyId member, analysis::InstructionId site, std::optional<analysis::OpId> projected = {}) const {
    if (!ok) throw CreationFailure(check, parent, member, site, projected);
  }
  template <class T> [[noreturn]] void Fail(T cause) const {
    throw FamilyFileFailure{std::move(cause), method_, parent_, pc_, root_};
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
  // Transient decode within the remaining byte budget; nothing is retained.
  bool TransparentRegionsOnly(const abc::MethodInfo& method) {
    auto decoded = abc::DecodeMethodBody(file_, header_.profile, *method.code, {bytes_});
    auto* body = std::get_if<abc::MethodBody>(&decoded);
    return body && analysis::EraseTransparentRethrowRegions(*body) && body->tries.empty();
  }
  FamilyFunction ReadMethod(abc::MethodPosition position, const FunctionContext* owner,
      InputCaptureMode mode = InputCaptureMode::kClosed, const ParentModuleInput* modules_input = nullptr) {
    Require(position.value < index_.methods.size() && index_.methods[position.value].code, Error::kMethod);
    // Regions that only rethrow are semantically absent: such a method stays
    // on the normal route, which erases them identically on its own decode.
    const bool regions = index_.methods[position.value].code->try_count &&
        !TransparentRegionsOnly(index_.methods[position.value]);
    const bool cleanup_entry = !owner && modules_ && class_mode_ == FamilyClassMode::kRetainRoot && regions;
    if ((regions && !cleanup_entry) ||
        (owner && IsGeneratorCallable(owner->Callable()))) {
      Require(owner != nullptr, Error::kOccurrence);
      Require(owner->Position() == position && owner->Target() == index_.methods[position.value].offset &&
          owner->Identity() == identity_, Error::kOccurrence);
      std::optional<PhasedModuleBinding> modules;
      if (modules_) modules.emplace(PhasedModuleBinding{file_, header_, index_, *modules_, module_owner_});
      auto read = FilePhasedFunction::Read(file_, header_, index_, *owner, Remaining(), modules ? &*modules : nullptr, mode, modules_input);
      if (auto* error = std::get_if<PhasedFileFailure>(&read)) {
        if (auto* failed = std::get_if<PhasedInputFailure>(error)) {
          failed->allocated_bytes += budget_.bytes - bytes_;
          failed->work += budget_.work - work_;
        }
        Fail(*error);
      }
      auto function = std::get<FilePhasedFunction>(std::move(read));
      Admit(function.AllocatedBytes(), 1); Step(function.Work()); return function;
    }
    auto read = ReadFunctionValues(file_, header_, index_, position, identity_,
        InputCaptureMode::kConditional, Remaining(), modules_, owner, modules_input,
        cleanup_entry ? FunctionBodyMode::kCleanupEntry : FunctionBodyMode::kOriginal);
    if (auto* error = std::get_if<FunctionFileFailure>(&read)) {
      if (auto* input = std::get_if<FunctionInputFailure>(error))
        if (auto* failed = std::get_if<analysis::SemanticProofFailure>(&input->cause)) {
          failed->allocated_bytes += budget_.bytes - bytes_;
          failed->work_units += budget_.work - work_;
        }
      Fail(*error);
    }
    auto function = std::get<FileFunction>(std::move(read));
    Admit(function.allocated_bytes, 1);
    return function;
  }
  void SelfOrder(FileFunction& parent,FamilyId creator) {
    const auto& values = parent.values;
    if (!values.cells || !values.environments || values.initializers.calls.empty()) return;
    auto found = FindClassSelfStores(values.graph,*values.environments,*values.cells,values.initializers.calls,Remaining());
    if (const auto* error = std::get_if<ClassSelfError>(&found)) Fail(*error);
    const auto& candidates = std::get<ClassSelfStores>(found);
    Admit(candidates.allocated_bytes,1); Step(candidates.work);
    if (candidates.stores.empty()) return;
    std::vector<std::optional<std::size_t>> calls;
    Grow(calls,values.graph.ops.size()); Step(values.graph.ops.size()); calls.resize(values.graph.ops.size());
    for (std::size_t i = 0; i < values.initializers.calls.size(); ++i) { Step(1); calls[values.initializers.calls[i].call.value] = i; }
    std::vector<ClassInitializerFunction> bodies;
    std::vector<ClassInitializerWitness> witnesses;
    Grow(bodies,candidates.stores.size()); Grow(witnesses,candidates.stores.size());
    for (const auto& store : candidates.stores) {
      Step(1);
      if (!calls[store.initializer.value]) {
        auto failure = FamilyFileFailure{Error::kCreation, method_, parent_, pc_, root_};
        failure.family_creation = FamilyCreationEvidence{FamilyCreationCheck::kSelfInitializer,
            OperationLocation(values.graph, store.initializer, parent.input.Method())};
        failure.family_creation->root = root_;
        throw failure;
      }
      const auto& call = values.initializers.calls[*calls[store.initializer.value]];
      auto loaded = ReadClassInitializerFunction(file_,header_,index_,lookup_,roles_,sites_,parent,call,Remaining(),modules_);
      if (const auto* error = std::get_if<ClassInitializerReadFailure>(&loaded)) {
        auto failure=FamilyFileFailure{*error,method_,parent_,pc_,root_};
        // The Closed route stays authoritative unless fresh occurrence/storage
        // evidence can construct a separate class-inner draft. That draft is
        // never wrapped as ConstructedFunction: enclosure/recovery must verify
        // before publication, and every uncertified case keeps this refusal.
        if (class_sites_)
          failure.class_self_initialization = ConsumeClassSelfInitialization(
              file_, header_, index_, lookup_, roles_, sites_, *class_sites_,
              parent, call, {*root_, result_.first_owner, {creator.value}},
              Remaining(), modules_);
        if (failure.class_self_initialization &&
            failure.class_self_initialization->recovery) {
          const auto& recovery = *failure.class_self_initialization->recovery;
          if (recovery.kind == ClassSelfInitializationError::kRecoveryGate)
            failure.cause = ClassSelfError::kRecoveryGate;
          else if (recovery.kind ==
                   ClassSelfInitializationError::kResourceLimit)
            failure.cause = ClassSelfError::kResourceLimit;
          else if (recovery.kind == ClassSelfInitializationError::kWorkLimit)
            failure.cause = ClassSelfError::kWorkLimit;
        }
        throw failure;
      }
      auto body = std::get<ClassInitializerFunction>(std::move(loaded));
      Admit(body.function.allocated_bytes,1); Admit(body.context.AllocatedBytes(),1); Step(body.context.Work());
      bodies.push_back(std::move(body)); witnesses.push_back({call,bodies.back()});
    }
    auto built = ClassSelfOrder::Build(parent,witnesses,Remaining());
    if (const auto* error = std::get_if<ClassSelfOrderError>(&built)) Fail(*error);
    parent.self_order.emplace(std::get<ClassSelfOrder>(std::move(built)));
    Admit(parent.self_order->AllocatedBytes(),1); Step(parent.self_order->Work());
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
      SelfOrder(member.function.Normal(),id);
      const auto& values = member.function.Normal().values;
      const auto& order = member.function.Normal().self_order;
      auto projected = ProjectClassMembers(values.graph, values.initializers.calls, Remaining(), values.definitions.sites, order ? &*order : nullptr);
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
    stack_.push_back({id, method});Step(2*(std::bit_width(active_.size())+1));active_.insert(method);
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
  const analysis::ClassMethodSites* class_sites_ = nullptr;
  FileFamily result_;
  FamilyClassMode class_mode_ = FamilyClassMode::kReject;
  std::vector<FamilyCursor> stack_;
  std::set<abc::MethodPosition,std::less<abc::MethodPosition>,ActiveAllocator<abc::MethodPosition>> active_;
  std::optional<abc::EntityOffset> method_;
  std::optional<abc::EntityOffset> root_;
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
    const ParentModuleInput* root_modules, FunctionOwnerId first_owner, const FileFunction* root_creator, const analysis::ClassMethodSites* class_sites, const CaptureStorage* root_storage, ClassRootBinding root_binding) {
  try { return FamilyReader(file, header, index, lookup, roles, sites, identity, budget, modules, module_owner).Read(root, owner, classes, root_modules, first_owner, root_creator, class_sites, root_storage, root_binding);
  } catch (const FamilyFileFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}};
  } catch (const std::length_error&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}}; }
}
}  // namespace abcd::source
