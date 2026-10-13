#include "prepared_family.h"

#include <new>
#include <stdexcept>
#include <algorithm>
#include "abcd/source/module_enclosure.h"

namespace abcd::source {
struct PreparedFamily::Storage {
  FunctionContext context;
  ConstructedFamily family;
  std::vector<MethodRouteReceipt> receipts;
  std::size_t allocated_bytes;
  std::uint64_t work;
};
PreparedFamily::PreparedFamily(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
PreparedFamily::PreparedFamily(PreparedFamily&&) noexcept = default;
PreparedFamily& PreparedFamily::operator=(PreparedFamily&&) noexcept = default;
PreparedFamily::~PreparedFamily() = default;
const ConstructedFamily& PreparedFamily::Family() const { return storage_->family; }
const RouteInputIdentity& PreparedFamily::Identity() const { return storage_->context.Identity(); }
const analysis::ClassMethodSite* PreparedFamily::ClassCreation() const { return storage_->context.ClassSite(); }
const ClassDefinedMember* PreparedFamily::DefinedMemberCreation() const {
  const auto* context = storage_->context.ClassContext();
  return context ? context->DefinedMember() : nullptr;
}
const analysis::FunctionCreationSite* PreparedFamily::FunctionCreation() const { return storage_->context.CreationSite(); }
std::u16string_view PreparedFamily::Name() const { return storage_->context.Name(); }
std::span<const MethodRouteReceipt> PreparedFamily::Receipts() const { return storage_->receipts; }
std::size_t PreparedFamily::AllocatedBytes() const { return storage_->allocated_bytes + preparation_bytes_; }
std::uint64_t PreparedFamily::Work() const { return storage_->work + preparation_work_; }
namespace {
using Error = FamilyPrepareError;
void Require(bool ok, Error error) { if (!ok) throw FamilyPrepareFailure{error}; }
void CheckPlacement(const FunctionContext& root, FamilyDestination destination) {
  const auto expected = root.ClassContext() ? FunctionFragment::kClassMember : FunctionFragment::kFunctionExpression;
  Require(destination.fragment == expected, Error::kPlacement);
}
template<class T, class E> T Take(std::variant<T, E> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  throw FamilyPrepareFailure{std::get<E>(std::move(result))};
}
class Budget {
 public:
  explicit Budget(analysis::SemanticBudget budget) : left_(budget) {}
  void Account(std::size_t bytes, std::uint64_t work) {
    Require(bytes <= left_.bytes, Error::kResourceLimit);
    Require(work <= left_.work, Error::kWorkLimit);
    left_.bytes -= bytes; left_.work -= work;
  }
  analysis::SemanticBudget Left() const { return left_; }
 private:
  analysis::SemanticBudget left_;
};
ParentModuleInput ModuleInputAt(const FileFunction& creator, const FunctionContext& owner) {
  const auto pc = owner.ConstructorSite() ? owner.ConstructorSite()->pc :
      owner.ClassSite() ? owner.ClassSite()->pc : owner.CreationSite()->pc;
  const auto& instructions = creator.input.Input().instructions;
  const auto found = std::lower_bound(instructions.begin(), instructions.end(), pc,
      [](const auto& instruction, auto site) { return instruction.pc < site; });
  Require(found != instructions.end() && found->pc == pc, Error::kModules);
  return {creator.input, {static_cast<std::uint32_t>(found - instructions.begin())}};
}
}  // namespace
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::PrepareCreated(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    analysis::FunctionSiteId site, RouteInputIdentity identity,
    FamilyDestination destination, analysis::SemanticBudget allowance) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    Require(allowance.bytes >= sizeof(Storage), Error::kResourceLimit);
    auto resolution_budget = allowance; resolution_budget.bytes -= sizeof(Storage);
    auto context = Take<CreationTreeContext, CreationContextFailure>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, site, identity, resolution_budget));
    return Prepare(file, header, index, lookup, roles, sites,
        FunctionContext{std::move(context)}, destination, allowance);
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::PrepareClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& functions,
    const analysis::ClassMethodSites& classes, analysis::ClassSiteId site,
    RouteInputIdentity identity, FamilyDestination destination, analysis::SemanticBudget allowance,
    RecoveryMode recovery, const ParentModuleInput* modules, const FileFunction* creator) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    Require(allowance.bytes >= sizeof(Storage), Error::kResourceLimit);
    auto resolution_budget = allowance; resolution_budget.bytes -= sizeof(Storage);
    auto context = Take<ClassTreeContext, ClassContextFailure>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, classes, site, identity, resolution_budget));
    return Prepare(file, header, index, lookup, roles, functions,
        FunctionContext{std::move(context)}, destination, allowance, recovery, modules, creator);
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::PrepareDefinedMember(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, const analysis::MethodRoleIndex& roles,
    const analysis::FunctionCreationSites& functions, const FileFunction& creator,
    analysis::OpId definition, abc::ClassMethodKind kind, RouteInputIdentity identity,
    FamilyDestination destination, analysis::SemanticBudget allowance, RecoveryMode recovery) {
  try {
    Require(allowance.bytes >= sizeof(Storage), Error::kResourceLimit);
    auto resolution = allowance; resolution.bytes -= sizeof(Storage);
    auto context = Take<ClassTreeContext, ClassContextFailure>(ClassTreeContext::ResolveDefinedMember(
        file, header, index, lookup, roles, functions, creator, definition, kind, identity, resolution));
    FunctionContext owner{std::move(context)};
    const auto modules = ModuleInputAt(creator, owner);
    return Prepare(file, header, index, lookup, roles, functions,
        std::move(owner), destination, allowance, recovery, &modules, &creator);
  } catch (const FamilyPrepareFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
    catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::PrepareConstructor(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& functions,
    const FileFunction& creator, analysis::OpId creation, RouteInputIdentity identity,
    FamilyDestination destination, analysis::SemanticBudget allowance, RecoveryMode recovery) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    Require(allowance.bytes >= sizeof(Storage), Error::kResourceLimit);
    auto resolution = allowance; resolution.bytes -= sizeof(Storage);
    auto context = Take<ClassTreeContext, ClassContextFailure>(ClassTreeContext::ResolveConstructor(
        file, header, index, lookup, roles, creator, creation, identity, resolution));
    FunctionContext owner{std::move(context)};
    const auto modules = ModuleInputAt(creator, owner);
    return Prepare(file, header, index, lookup, roles, functions,
        std::move(owner), destination, allowance, recovery, &modules, &creator);
  } catch (const FamilyPrepareFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
    catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<FamilyPreparation, FamilyPrepareFailure> FamilyPreparation::Read(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    FunctionContext root, FamilyDestination destination, analysis::SemanticBudget allowance,
    RecoveryMode recovery, const ParentModuleInput* root_modules, const FileFunction* creator) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    CheckPlacement(root, destination);
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    Budget budget(allowance);
    budget.Account(sizeof(FamilyPreparation), 0);
    budget.Account(root.AllocatedBytes(), root.Work());
    const RecordModuleContext* modules = nullptr;
    ModuleOwnerId module_owner{};
    if (destination.globals && destination.globals->module_enclosure) {
      const auto& enclosure = *destination.globals->module_enclosure;
      Require(root.Position().value < index.methods.size(), Error::kModules);
      const auto record = index.methods[root.Position().value].owner_offset;
      Require(enclosure.Identity() == root.Identity() && enclosure.Context().record() == record &&
          enclosure.Context().ForOwner(file, record), Error::kModules);
      modules = &enclosure.Context(); module_owner = enclosure.Owner();
    }
    auto read = ReadFunctionFamily(file, header, index, lookup, roles, sites, root.Position(), root.Identity(),
        budget.Left(), modules, &root, module_owner, FamilyClassMode::kReject, root_modules, destination.first_owner, creator);
    if (auto* failed = std::get_if<FamilyFileFailure>(&read)) {
      if (auto* phase = std::get_if<PhasedFileFailure>(&failed->cause))
        if (auto* input = std::get_if<PhasedInputFailure>(phase)) {
          input->allocated_bytes += allowance.bytes - budget.Left().bytes;
          input->work += allowance.work - budget.Left().work;
        }
    }
    if (auto* failed = std::get_if<FamilyFileFailure>(&read)) {
      if (auto* normal = std::get_if<FunctionFileFailure>(&failed->cause))
        if (auto* input = std::get_if<FunctionInputFailure>(normal))
          if (auto* proof = std::get_if<analysis::SemanticProofFailure>(&input->cause)) {
            proof->allocated_bytes += allowance.bytes - budget.Left().bytes;
            proof->work_units += allowance.work - budget.Left().work;
          }
    }
    auto input = Take<FileFamily, FamilyFileFailure>(std::move(read));
    budget.Account(input.allocated_bytes, input.work);
    auto prepared = Take<FamilyTreeInput, FamilyTreeFailure>(FamilyTreeInput::Prepare(
        file, header, index, std::move(input), root, budget.Left(), modules, module_owner, &lookup, recovery, creator));
    budget.Account(prepared.AllocatedBytes(), prepared.Work());
    return FamilyPreparation(std::move(root), std::move(prepared), recovery,
        allowance.bytes - budget.Left().bytes, allowance.work - budget.Left().work);
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::Finish(
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, FamilyPreparation preparation, FamilyDestination destination,
    analysis::SemanticBudget allowance, const FileFunction* creator) {
  try {
    auto& root = preparation.root_;
    const auto recovery = preparation.recovery_;
    const auto& input = preparation.input_.Input();
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    CheckPlacement(root, destination);
    Require(input.first_owner == destination.first_owner, Error::kPlacement);
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    Budget budget(allowance); budget.Account(sizeof(Storage), 0);
    const RecordModuleContext* modules = nullptr;
    ModuleOwnerId module_owner{};
    if (destination.globals && destination.globals->module_enclosure) {
      const auto& enclosure = *destination.globals->module_enclosure;
      Require(enclosure.Identity() == root.Identity(), Error::kModules);
      modules = &enclosure.Context(); module_owner = enclosure.Owner();
    }
    Require(input.members.size() <= budget.Left().bytes / sizeof(MethodRouteReceipt), Error::kResourceLimit);
    std::vector<MethodRouteReceipt> receipts;
    budget.Account(input.members.size() * sizeof(MethodRouteReceipt), input.members.size());
    receipts.reserve(input.members.size());
    budget.Account((receipts.capacity() - input.members.size()) * sizeof(MethodRouteReceipt), 0);
    for (const auto& member : input.members) {
      MethodRouteReceipt receipt;
      receipt.identity = member.function.Identity();
      receipt.method = member.function.Method(); receipt.record = member.function.Record();
      receipts.push_back(receipt); // Identity only; no stage claim until all proofs close.
    }
    std::optional<ExternalFamilyCapture> external;
    if (destination.captures) {
      Require(creator != nullptr, Error::kPlacement);
      external.emplace(ExternalFamilyCapture{*creator, *destination.captures});
    }
    auto family = Take<ConstructedFamily, FamilyTreeFailure>(std::move(preparation.input_).Build(
        file, header, index, root, budget.Left(), modules, module_owner, &lookup, recovery,
        external ? &*external : nullptr));
    budget.Account(family.allocated_bytes, family.work);
    for (const auto& member : family.members) {
      budget.Account(0, 1);
      Require(member.tree.strictness == Strictness::kStrict, Error::kStrictness);
      // Creation/phase owners already checked the actual coroutine protocol.
      // Publication preserves those callable kinds; the original plain-only
      // allowlist must not reject a complete shared async/generator tree.
      // FamilyTrees already proves the actual derived initialization protocol
      // before building its tree/ABI. Guard presence is not that proof: a
      // same-owner constructor without guards must not face a second gate.
      Require(member.tree.callable == CallableKind::kFunction ||
          member.tree.callable == CallableKind::kAsyncFunction ||
          member.tree.callable == CallableKind::kGenerator ||
          member.tree.callable == CallableKind::kAsyncGenerator ||
          IsArrowCallable(member.tree.callable) ||
          (&member == &family.members[family.root.value] && root.ClassContext() &&
           (IsMethodCallable(member.tree.callable) || member.tree.callable == CallableKind::kBaseConstructor ||
            member.tree.callable == CallableKind::kDerivedConstructor)), Error::kCallable);
      if (member.tree.recovery_issues.empty()) {
        Require(member.abi.has_value(), Error::kAbi);
        Require(member.sources.has_value(), Error::kSources);
      } else {
        Require(recovery == RecoveryMode::kAllowPartial && !member.abi && !member.sources,
            Error::kSources);
      }
    }
    const auto enclosure = Take<FamilyEnclosure, FamilyEnclosureFailure>(
        VerifyFamilyDestination(family, destination.globals, budget.Left()));
    budget.Account(enclosure.allocated_bytes, enclosure.work);
    Require(receipts.size() == family.members.size(), Error::kReceipt);
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      budget.Account(0, 1);
      const auto& member = family.members[i]; auto& receipt = receipts[i];
      Require(receipt.identity == member.tree.identity && receipt.method == member.tree.method, Error::kReceipt);
      Require(member.record == receipt.record, Error::kReceipt);
      receipt.route = SourceRoute::kValueTree;
      // Partial trees retain checked bindings, not whole-body flow/ABI/source
      // proof. FamilyEmission rejects this stage before it can publish bytes.
      receipt.stage = member.tree.recovery_issues.empty() ? SourceStage::kTreeVerified
                                                        : SourceStage::kBindingsFrozen;
      receipt.coverage = Coverage::kIncomplete; receipt.work_revision = member.tree.value_revision.value;
      Require(!ValidateRouteReceipt(receipt), Error::kReceipt);
    }
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    // No external pointers or mutable input/SSA products survive this boundary.
    // The factory's actual passes, not FamilyWriteFacts counters, authorize it.
    return PreparedFamily(std::make_unique<const Storage>(Storage{std::move(root), std::move(family), std::move(receipts),
        allowance.bytes - budget.Left().bytes, allowance.work - budget.Left().work}));
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::Prepare(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    FunctionContext root, FamilyDestination destination, analysis::SemanticBudget allowance,
    RecoveryMode recovery, const ParentModuleInput* root_modules, const FileFunction* creator) {
  try {
    auto input = Take<FamilyPreparation, FamilyPrepareFailure>(FamilyPreparation::Read(file, header, index,
        lookup, roles, sites, std::move(root), destination, allowance, recovery, root_modules, creator));
    const auto bytes = input.AllocatedBytes(); const auto work = input.Work();
    Budget budget(allowance); budget.Account(bytes, work);
    auto result = Take<PreparedFamily, FamilyPrepareFailure>(Finish(file, header, index, lookup,
        std::move(input), destination, budget.Left(), creator));
    // Immediate callers own both phases; split callers account them separately.
    result.preparation_bytes_ = bytes; result.preparation_work_ = work;
    return result;
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<FamilyEnclosure, FamilyPrepareFailure> PreparedFamily::CheckDestination(
    FamilyDestination destination, analysis::SemanticBudget budget) const {
  if (destination.strictness != Strictness::kStrict)
    return FamilyPrepareFailure{Error::kStrictness};
  const auto expected = storage_->context.ClassContext() ? FunctionFragment::kClassMember : FunctionFragment::kFunctionExpression;
  if (destination.fragment != expected) return FamilyPrepareFailure{Error::kPlacement};
  std::uint64_t capture_work = 0;
  if (storage_->family.root_capture) {
    if (!destination.enclosure) return FamilyPrepareFailure{Error::kPlacement};
    const auto checked = VerifyClassCaptureDestination(*storage_->family.root_capture,
        storage_->family.members[storage_->family.root.value].tree, *destination.enclosure, storage_->context, budget);
    if (const auto* error = std::get_if<CaptureTreeError>(&checked))
      return FamilyPrepareFailure{*error == CaptureTreeError::kResourceLimit ? Error::kResourceLimit :
          *error == CaptureTreeError::kWorkLimit ? Error::kWorkLimit : Error::kPlacement};
    capture_work = std::get<std::uint64_t>(checked); budget.work -= capture_work;
  }
  auto result = VerifyFamilyDestination(storage_->family, destination.globals, budget);
  if (const auto* failure = std::get_if<FamilyEnclosureFailure>(&result))
    return FamilyPrepareFailure{*failure};
  auto enclosure = std::get<FamilyEnclosure>(std::move(result));
  enclosure.work += capture_work;
  return enclosure;
}
std::variant<PrintedFamilyDraft, FamilyPrepareFailure, TreePrintFailure> PreparedFamily::PrintDraft(
    FamilyDestination destination, std::ostream& output,
    EmitBudget output_budget, analysis::SemanticBudget scratch, std::size_t indent,
    const ClassNames* prepared) const {
  auto checked = CheckDestination(destination, scratch);
  if (const auto* failure = std::get_if<FamilyPrepareFailure>(&checked)) return *failure;
  const auto& enclosure = std::get<FamilyEnclosure>(checked);
  scratch.bytes -= enclosure.allocated_bytes; scratch.work -= enclosure.work;
  const auto* member = storage_->context.ClassContext();
  if (!member && indent) return TreePrintError::kUnsupportedContext;
  auto printed = member ? PrintClassFamilyDraft(storage_->family, *member, output, output_budget, scratch, destination.globals, indent, prepared)
                        : PrintFamilyDraft(storage_->family, output, output_budget, scratch, destination.globals);
  if (const auto* failure = std::get_if<TreePrintFailure>(&printed)) return *failure;
  auto result = std::get<PrintedFamilyDraft>(std::move(printed));
  result.allocated_bytes += enclosure.allocated_bytes; result.work += enclosure.work;
  return result;
}
}  // namespace abcd::source
