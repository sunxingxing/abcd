#include "family_emission.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = FamilyEmitError;
void Require(bool ok, Error error) { if (!ok) throw FamilyEmitFailure{error}; }
class EmissionBudget {
 public:
  explicit EmissionBudget(analysis::SemanticBudget budget) : left_(budget) {}
  void Step(std::uint64_t count = 1) { Require(count <= left_.work, Error::kWorkLimit); left_.work -= count; }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= left_.bytes / width, Error::kResourceLimit); left_.bytes -= count * width;
  }
  analysis::SemanticBudget Left() const { return left_; }
 private:
  analysis::SemanticBudget left_;
};
void CheckPositions(const PreparedFamily& family, const PrintedFamilyDraft& printed, EmissionBudget& budget) {
  const auto& members = family.Family().members;
  Require(printed.members.size() == members.size() && printed.functions.size() == members.size(), Error::kPositions);
  for (std::size_t i = 0; i < members.size(); ++i) {
    budget.Step();
    const auto& member = members[i]; const auto& tree = member.tree;
    const auto& positions = printed.members[i]; const auto owner = printed.functions[i];
    Require(owner.begin < owner.end && owner.end <= printed.bytes && member.sources &&
        positions.identity == tree.identity && positions.method == tree.method &&
        positions.value_revision == tree.value_revision && positions.cfg_revision == tree.cfg_revision &&
        positions.bytes == owner.end - owner.begin &&
        positions.parameters.size() == tree.parameters.size() &&
        positions.expressions.size() == tree.expressions.size() &&
        positions.properties.size() == tree.properties.size() &&
        positions.statements.size() == tree.statements.size() &&
        positions.anchors.size() == tree.anchors.size(), Error::kPositions);
    Require(tree.modules.empty() ? !positions.module_binding :
        positions.module_binding && member.record && positions.module_binding->record == *member.record,
        Error::kPositions);
    for (const auto& source : member.sources->sites) {
      budget.Step();
      // IDs come from the sealed instruction-source pass, not caller tables.
      std::visit([&](auto target) {
        using T = decltype(target);
        if constexpr (std::is_same_v<T, ElidedInstruction>) {
          Require(std::holds_alternative<ElidedOrigin>(tree.origins[source.origin.value]), Error::kPositions);
        } else if constexpr (std::is_same_v<T, AnchorId>) {
          const auto byte = positions.anchors[target.value];
          Require(owner.begin <= byte && byte <= owner.end, Error::kPositions);
        } else {
          GeneratedRange range;
          if constexpr (std::is_same_v<T, ExprId>) range = positions.expressions[target.value];
          else if constexpr (std::is_same_v<T, StmtId>) range = positions.statements[target.value];
          else if constexpr (std::is_same_v<T, PropertyId>) range = positions.properties[target.value];
          else { static_assert(std::is_same_v<T, ParameterId>); range = positions.parameters[target.value]; }
          Require(owner.begin <= range.begin && range.begin < range.end && range.end <= owner.end, Error::kPositions);
        }
      }, source.target);
    }
  }
}
}  // namespace
std::variant<FamilyEmission, FamilyEmitFailure> FamilyEmission::Write(
    PreparedFamily family, FamilyDestination context, std::ostream& output,
    GeneratedOffset destination, EmitBudget limit, analysis::SemanticBudget scratch) {
  try {
    EmissionBudget budget(scratch);
    budget.Admit(1, sizeof(FamilyEmission));
    const auto count = family.Family().members.size();
    Require(family.Receipts().size() == count, Error::kReceipt);
    budget.Admit(count, sizeof(MethodRouteReceipt)); budget.Step(count);
    std::vector<MethodRouteReceipt> receipts(family.Receipts().begin(), family.Receipts().end());
    budget.Admit(receipts.capacity() - count, sizeof(MethodRouteReceipt));
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step(); const auto& receipt = receipts[i]; const auto& tree = family.Family().members[i].tree;
      Require(!ValidateRouteReceipt(receipt) && receipt.route == SourceRoute::kValueTree &&
          receipt.stage == SourceStage::kTreeVerified && receipt.coverage == Coverage::kIncomplete &&
          receipt.identity == tree.identity && receipt.method == tree.method &&
          receipt.work_revision == tree.value_revision.value, Error::kReceipt);
    }
    limit.bytes = std::min(limit.bytes, std::numeric_limits<std::size_t>::max() - destination.byte);
    auto result = family.PrintDraft(context, output, limit, budget.Left());
    if (const auto* failure = std::get_if<FamilyPrepareFailure>(&result)) return FamilyEmitFailure{*failure};
    if (const auto* failure = std::get_if<TreePrintError>(&result)) return FamilyEmitFailure{*failure};
    auto positions = std::get<PrintedFamilyDraft>(std::move(result));
    budget.Admit(positions.allocated_bytes, 1); budget.Step(positions.work);
    CheckPositions(family, positions, budget);
    for (std::size_t i = 0; i < count; ++i) {
      budget.Step();
      receipts[i].stage = SourceStage::kPrinted; receipts[i].coverage = Coverage::kComplete;
      Require(!ValidateRouteReceipt(receipts[i]), Error::kReceipt);
      if (positions.members[i].fragment == FunctionFragment::kConditionalExpression)
        positions.members[i].fragment = FunctionFragment::kFunctionExpression;
    }
    const auto charge = scratch.bytes - budget.Left().bytes;
    Require(family.AllocatedBytes() <= std::numeric_limits<std::size_t>::max() - charge, Error::kResourceLimit);
    const auto bytes = family.AllocatedBytes() + charge;
    const GeneratedRange range{destination.byte, destination.byte + positions.bytes};
    return FamilyEmission(std::move(family), std::move(positions), std::move(receipts), range, bytes);
  } catch (const FamilyEmitFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyEmitFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return FamilyEmitFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
