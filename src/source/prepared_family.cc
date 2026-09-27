#include "prepared_family.h"

#include <new>
#include <stdexcept>

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
const analysis::FunctionCreationSite* PreparedFamily::FunctionCreation() const { return storage_->context.CreationSite(); }
std::u16string_view PreparedFamily::Name() const { return storage_->context.Name(); }
std::span<const MethodRouteReceipt> PreparedFamily::Receipts() const { return storage_->receipts; }
std::size_t PreparedFamily::AllocatedBytes() const { return storage_->allocated_bytes; }
std::uint64_t PreparedFamily::Work() const { return storage_->work; }
namespace {
using Error = FamilyPrepareError;
void Require(bool ok, Error error) { if (!ok) throw FamilyPrepareFailure{error}; }
void CheckPlacement(const FunctionContext& root, FamilyDestination destination) {
  const auto expected = root.ClassSite() ? FunctionFragment::kClassMember : FunctionFragment::kFunctionExpression;
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
    RouteInputIdentity identity, FamilyDestination destination, analysis::SemanticBudget allowance) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    Require(allowance.bytes >= sizeof(Storage), Error::kResourceLimit);
    auto resolution_budget = allowance; resolution_budget.bytes -= sizeof(Storage);
    auto context = Take<ClassTreeContext, ClassContextFailure>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, classes, site, identity, resolution_budget));
    return Prepare(file, header, index, lookup, roles, functions,
        FunctionContext{std::move(context)}, destination, allowance);
  } catch (const FamilyPrepareFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyPrepareFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyPrepareFailure{Error::kResourceLimit}; }
}
std::variant<PreparedFamily, FamilyPrepareFailure> PreparedFamily::Prepare(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    FunctionContext root, FamilyDestination destination, analysis::SemanticBudget allowance) {
  try {
    Require(destination.strictness == Strictness::kStrict, Error::kStrictness);
    CheckPlacement(root, destination);
    Require(!file.CheckUnchanged(), Error::kFileChanged);
    Budget budget(allowance);
    budget.Account(sizeof(Storage), 0);
    budget.Account(root.AllocatedBytes(), root.Work());
    auto input = Take<FileFamily, FamilyFileFailure>(ReadFunctionFamily(
        file, header, index, lookup, roles, sites, root.Position(), root.Identity(), budget.Left()));
    budget.Account(input.allocated_bytes, input.work);
    Require(input.members.size() <= budget.Left().bytes / sizeof(MethodRouteReceipt), Error::kResourceLimit);
    std::vector<MethodRouteReceipt> receipts;
    budget.Account(input.members.size() * sizeof(MethodRouteReceipt), input.members.size());
    receipts.reserve(input.members.size());
    budget.Account((receipts.capacity() - input.members.size()) * sizeof(MethodRouteReceipt), 0);
    for (const auto& member : input.members) {
      MethodRouteReceipt receipt;
      receipt.identity = member.function.input.Identity();
      receipt.method = member.function.input.Method(); receipt.record = member.function.input.Record();
      receipts.push_back(receipt); // Identity only; no stage claim until all proofs close.
    }
    auto family = Take<ConstructedFamily, FamilyTreeFailure>(BuildFileFamilyTrees(
        file, header, index, std::move(input), root, budget.Left()));
    budget.Account(family.allocated_bytes, family.work);
    for (const auto& member : family.members) {
      budget.Account(0, 1);
      Require(member.tree.strictness == Strictness::kStrict, Error::kStrictness);
      Require(member.tree.callable == CallableKind::kFunction ||
          member.tree.callable == CallableKind::kArrow ||
          (&member == &family.members[family.root.value] && root.ClassSite() &&
           member.tree.callable == CallableKind::kMethod), Error::kCallable);
      Require(member.tree.modules.empty(), Error::kModules);
      Require(member.abi.has_value(), Error::kAbi);
      Require(member.sources.has_value(), Error::kSources);
    }
    const auto enclosure = Take<FamilyEnclosure, FamilyEnclosureFailure>(
        VerifyFamilyDestination(family, destination.globals, budget.Left()));
    budget.Account(enclosure.allocated_bytes, enclosure.work);
    Require(receipts.size() == family.members.size(), Error::kReceipt);
    for (std::size_t i = 0; i < family.members.size(); ++i) {
      budget.Account(0, 1);
      const auto& member = family.members[i]; auto& receipt = receipts[i];
      Require(receipt.identity == member.tree.identity && receipt.method == member.tree.method, Error::kReceipt);
      receipt.route = SourceRoute::kValueTree; receipt.stage = SourceStage::kTreeVerified;
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
std::variant<FamilyEnclosure, FamilyPrepareFailure> PreparedFamily::CheckDestination(
    FamilyDestination destination, analysis::SemanticBudget budget) const {
  if (destination.strictness != Strictness::kStrict)
    return FamilyPrepareFailure{Error::kStrictness};
  const auto expected = storage_->context.ClassSite() ? FunctionFragment::kClassMember : FunctionFragment::kFunctionExpression;
  if (destination.fragment != expected) return FamilyPrepareFailure{Error::kPlacement};
  auto result = VerifyFamilyDestination(storage_->family, destination.globals, budget);
  if (const auto* failure = std::get_if<FamilyEnclosureFailure>(&result))
    return FamilyPrepareFailure{*failure};
  return std::get<FamilyEnclosure>(std::move(result));
}
std::variant<PrintedFamilyDraft, FamilyPrepareFailure, TreePrintError> PreparedFamily::PrintDraft(
    FamilyDestination destination, std::ostream& output,
    EmitBudget output_budget, analysis::SemanticBudget scratch) const {
  auto checked = CheckDestination(destination, scratch);
  if (const auto* failure = std::get_if<FamilyPrepareFailure>(&checked)) return *failure;
  const auto& enclosure = std::get<FamilyEnclosure>(checked);
  scratch.bytes -= enclosure.allocated_bytes; scratch.work -= enclosure.work;
  const auto* member = storage_->context.ClassContext();
  auto printed = member ? PrintClassFamilyDraft(storage_->family, *member, output, output_budget, scratch)
                        : PrintFamilyDraft(storage_->family, output, output_budget, scratch);
  if (const auto* failure = std::get_if<TreePrintError>(&printed)) return *failure;
  auto result = std::get<PrintedFamilyDraft>(std::move(printed));
  result.allocated_bytes += enclosure.allocated_bytes; result.work += enclosure.work;
  return result;
}
}  // namespace abcd::source
