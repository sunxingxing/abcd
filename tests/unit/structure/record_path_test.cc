#include "abcd/structure/record_path.h"

#include <cstdlib>

int main() {
  using namespace abcd;
  const auto check = [](bool value) {
    if (!value) std::abort();
  };
  abc::RecordInfo record;
  record.offset = {123};
  record.descriptor = "Lentry/ets/pages/Index;";
  const auto path = structure::PlanRecordPath(record);
  check(std::get<structure::RecordPath>(path).stem ==
        "entry/ets/pages/Index~r0000007b");
  record.descriptor = "L/../a\\b/%;";
  check(
      std::get<structure::RecordPath>(structure::PlanRecordPath(record)).stem ==
      "%00/%2e%2e/a%5cb/%25~r0000007b");
  record.offset = {124};
  check(std::get<structure::RecordPath>(structure::PlanRecordPath(record))
            .stem.ends_with("~r0000007c"));
  record.descriptor = "invalid";
  check(std::get<structure::PathError>(structure::PlanRecordPath(record)) ==
        structure::PathError::kDescriptor);
  record.descriptor = "L" + std::string(256, 'a') + ";";
  check(std::get<structure::PathError>(structure::PlanRecordPath(record)) ==
        structure::PathError::kComponentLimit);
}
