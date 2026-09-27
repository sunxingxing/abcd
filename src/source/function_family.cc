#include "function_family.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace abcd::source {
namespace {
using Error = FamilyError;
void Require(bool condition, Error error) { if (!condition) throw error; }
enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
class Quota {
 public:
  explicit Quota(analysis::SemanticBudget budget) : left_(budget) {}
  void Step(std::uint64_t count = 1) {
    Require(count <= left_.work, Error::kWorkLimit); left_.work -= count;
  }
  template<class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Require(count <= left_.bytes / sizeof(T), Error::kResourceLimit);
    values.reserve(count);
    Require(values.capacity() <= left_.bytes / sizeof(T), Error::kResourceLimit);
    left_.bytes -= values.capacity() * sizeof(T);
  }
  analysis::SemanticBudget Remaining() const { return left_; }
 private:
  analysis::SemanticBudget left_;
};
}  // namespace

std::variant<ConstructedFamily, FamilyError> BuildFunctionFamily(
    std::vector<FamilyMember> members, FunctionOwnerId root, analysis::SemanticBudget budget) {
  try {
    Quota quota(budget);
    Require(!members.empty() && members.size() <= std::numeric_limits<std::uint32_t>::max(), Error::kRoot);
    std::vector<FamilyId> owners, path;
    std::vector<Visit> visits;
    ConstructedFamily result{std::move(members), {0}};
    const auto count = result.members.size();
    quota.Reserve(owners, count); quota.Reserve(path, count); quota.Reserve(visits, count);
    quota.Reserve(result.parents, count); quota.Reserve(result.order, count);
    quota.Step(count); visits.resize(count, Visit::kUnseen); result.parents.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      quota.Step(); owners.push_back({static_cast<std::uint32_t>(i)});
    }
    const auto owner = [&](FamilyId id) { return result.members[id.value].tree.owner; };
    std::sort(owners.begin(), owners.end(), [&](FamilyId a, FamilyId b) {
      quota.Step(); return owner(a) < owner(b);
    });
    for (std::size_t i = 1; i < count; ++i) {
      quota.Step(); Require(owner(owners[i - 1]) != owner(owners[i]), Error::kOwner);
    }
    const auto find = [&](FunctionOwnerId wanted, Error error) {
      const auto found = std::lower_bound(owners.begin(), owners.end(), wanted,
          [&](FamilyId id, FunctionOwnerId value) { quota.Step(); return owner(id) < value; });
      Require(found != owners.end() && owner(*found) == wanted, error);
      return *found;
    };
    result.root = find(root, Error::kRoot);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& member = result.members[i];
      Require(member.tree.identity == result.members[result.root.value].tree.identity, Error::kOwner);
      for (std::size_t e = 0; e < member.tree.expressions.size(); ++e) {
        quota.Step();
        const auto* expression = std::get_if<FunctionExpr>(&member.tree.expressions[e].expression);
        if (!expression) continue;
        Require(e <= std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
        const auto child = find(expression->child, Error::kMissingChild);
        Require(!result.parents[child.value], Error::kMultipleParents);
        result.parents[child.value] = FamilyParent{{static_cast<std::uint32_t>(i)},
            ExprId{static_cast<std::uint32_t>(e)}};
      }
    }
    for (std::size_t i = 0; i < count; ++i) {
      quota.Step();
      const auto& member = result.members[i];
      const auto& parent = result.parents[i];
      if (i == result.root.value) {
        Require(!parent && !member.incoming, Error::kRoot);
        continue;
      }
      Require(parent.has_value(), Error::kMissingChild);
      Require(member.incoming.has_value(), Error::kCapture);
      const auto& outer = result.members[parent->function.value];
      const auto creation = member.incoming->creation;
      Require(creation.value < outer.flow.expressions.size() &&
          outer.flow.expressions[creation.value] == parent->expression, Error::kCapture);
      const auto checked = VerifyCaptureTree(*member.incoming, {outer.tree, outer.flow},
          {member.tree, member.flow}, quota.Remaining());
      if (const auto* error = std::get_if<CaptureTreeError>(&checked))
        return *error == CaptureTreeError::kWorkLimit ? Error::kWorkLimit :
            *error == CaptureTreeError::kResourceLimit ? Error::kResourceLimit : Error::kCapture;
      quota.Step(std::get<std::uint64_t>(checked));
    }
    // Parent pointers form a functional graph. Each node enters/exits once;
    // active reentry is a syntax ownership cycle, not runtime recursion through
    // mutable cells. No recursive C++ stack or repeated ancestor walk.
    for (std::size_t i = 0; i < count; ++i) {
      quota.Step();
      std::optional<FamilyId> cursor = FamilyId{static_cast<std::uint32_t>(i)};
      while (cursor && visits[cursor->value] != Visit::kDone) {
        quota.Step(); Require(visits[cursor->value] != Visit::kActive, Error::kCycle);
        visits[cursor->value] = Visit::kActive; path.push_back(*cursor);
        const auto& parent = result.parents[cursor->value];
        cursor = parent ? std::optional{parent->function} : std::nullopt;
      }
      while (!path.empty()) {
        quota.Step(); const auto id = path.back(); path.pop_back();
        visits[id.value] = Visit::kDone; result.order.push_back(id);
      }
    }
    result.allocated_bytes = result.parents.capacity() * sizeof(std::optional<FamilyParent>) +
        result.order.capacity() * sizeof(FamilyId);
    result.work = budget.work - quota.Remaining().work;
    return result;
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
