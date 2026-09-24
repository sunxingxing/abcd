#include "abcd/structure/record_path.h"

#include <array>
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

  std::array<abc::RecordInfo, 2> records;
  records[0].descriptor = "Lcom.example.pasteboard/entry/ets/pages/Home;";
  records[1].descriptor = "Lcom.example.pasteboard/entry/ets/pages/PageA;";
  const auto sources = structure::PlanRecordSourcePaths(records);
  check(std::get<std::vector<structure::RecordPath>>(sources)[0].stem ==
        "com.example.pasteboard/entry/ets/pages/Home");
  check(std::get<std::vector<structure::RecordPath>>(sources)[1].stem ==
        "com.example.pasteboard/entry/ets/pages/PageA");
  records[1].descriptor = records[0].descriptor;
  records[1].offset = {124};
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(records)) ==
        structure::PathError::kCollision);
  records[1].descriptor = "Lcom.example.pasteboard/entry/ets/pages/home;";
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(records)) ==
        structure::PathError::kCollision);
  records[0].descriptor = "L/../a\\b/%;";
  records[1].descriptor = "Lx/a.b;";
  const auto escaped = structure::PlanRecordSourcePaths(records);
  check(std::get<std::vector<structure::RecordPath>>(escaped)[0].stem ==
        "%00/.%2e/a%5cb/%25");
  check(std::get<std::vector<structure::RecordPath>>(escaped)[1].stem == "x/a.b");
  check(std::get<structure::PathError>(structure::PlanRecordSourcePaths(records, 0)) ==
        structure::PathError::kResourceLimit);
}
