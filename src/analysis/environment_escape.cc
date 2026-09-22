#include "abcd/analysis/environment_escape.h"

#include <algorithm>
#include <stdexcept>

#include "environment_escape_builder.h"

namespace abcd::analysis {
EnvironmentEscape::Builder::AliasId EnvironmentEscape::Builder::Find(
    AliasId id) {
  Charge();
  while (parents[id.value] != id) {
    Charge();
    ++work.find_steps;
    parents[id.value] = parents[parents[id.value].value];
    id = parents[id.value];
  }
  return id;
}
void EnvironmentEscape::Builder::Unite(AliasId a, AliasId b) {
  Charge();
  a = Find(a);
  b = Find(b);
  if (a == b) return;
  if (ranks[a.value] < ranks[b.value]) std::swap(a, b);
  parents[b.value] = a;
  exposed[a.value] |= exposed[b.value];
  if (ranks[a.value] == ranks[b.value]) ++ranks[a.value];
  ++work.unions;
}
void EnvironmentEscape::Builder::Propagate() {
  const auto count = parents.size();
  Charge(count);
  std::pmr::vector<std::size_t> begin(count + 1, 0, &memory);
  std::pmr::vector<std::size_t> cursor(count, 0, &memory);
  for (auto& edge : edges) {
    Charge();
    edge.owner = Find(edge.owner);
    edge.value = Find(edge.value);
    ++begin[edge.owner.value + 1];
  }
  for (std::size_t i = 1; i < begin.size(); ++i) {
    Charge();
    begin[i] += begin[i - 1];
    cursor[i - 1] = begin[i - 1];
  }
  std::pmr::vector<AliasId> targets(edges.size(), AliasId{0}, &memory);
  for (const auto edge : edges) {
    Charge();
    targets[cursor[edge.owner.value]++] = edge.value;
  }
  std::pmr::vector<AliasId> pending{&memory};
  for (std::size_t i = 0; i < count; ++i) {
    Charge();
    if (parents[i].value == i && exposed[i]) pending.push_back({i});
  }
  // Containment is directional: storing an external value into a local cell
  // does not itself expose the cell's owner. Each escaped class/edge runs once.
  for (std::size_t head = 0; head < pending.size(); ++head) {
    Charge();
    const auto source = pending[head].value;
    for (auto i = begin[source]; i < begin[source + 1]; ++i) {
      Charge();
      ++work.edge_visits;
      const auto target = targets[i];
      if (!exposed[target.value]) {
        exposed[target.value] = 1;
        pending.push_back(target);
      }
    }
  }
}
std::variant<EnvironmentEscape, EscapeError> EnvironmentEscape::Build(
    const abc::MethodBody& body, const RegisterSsa& ssa,
    const EnvironmentFlow& flow, std::uint64_t frame_size, IdfQuota& quota,
    IdfBudget budget) {
  try {
    const auto consumed = quota.Consumed();
    Builder builder{body, ssa, flow, frame_size, quota, budget};
    builder.Extract();
    builder.Propagate();
    EnvironmentEscape result;
    const auto count = flow.environments.Size();
    builder.Charge(count);
    if (count > builder.memory.Available() / sizeof(std::uint8_t))
      throw std::bad_alloc{};
    if (count) result.escaped_ = std::make_unique<std::uint8_t[]>(count);
    for (std::size_t i = 0; i < count; ++i)
      result.escaped_[i] =
          builder.exposed[builder
                              .Find(builder.Environment(
                                  EnvironmentId{static_cast<std::uint32_t>(i)}))
                              .value];
    result.count_ = count;
    builder.memory.Observe(result.AllocatedBytes());
    result.work = builder.work;
    result.work.cells = builder.cells.size();
    result.work.units = quota.Consumed() - consumed;
    result.work.peak_bytes = builder.memory.Peak();
    return result;
  } catch (EscapeError error) {
    return error;
  } catch (const detail::IdfWorkLimit&) {
    return EscapeError::kWorkLimit;
  } catch (const std::bad_alloc&) {
    return EscapeError::kResourceLimit;
  } catch (const std::length_error&) {
    return EscapeError::kResourceLimit;
  }
}
}  // namespace abcd::analysis
