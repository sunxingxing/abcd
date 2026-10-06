#include "parameter_names.h"

#include <algorithm>
#include <array>

#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
using source::ParameterNameError;
void CheckUnproven(const abc::FrameLayout& frame,
                   const abc::MethodBody& body,
                   const abc::DebugScopes& scopes) {
  test::Check(std::get<ParameterNameError>(
                  source::MapParameterDebugNames(frame, body, scopes,
                                                 abc::ProfileId::kDynamic1206, 1024 * 1024)) ==
              ParameterNameError::kUnproven);
}
}  // namespace
int main() {
  using namespace abcd;
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      2, 2, {false, false, false, false}));
  const auto body = test::Body({0x44, 0x20, 0x44, 0x31, 0x65});
  abc::DebugScopes scopes;
  scopes.locals.push_back({0, {100}, {}, {}, 4, {}});
  scopes.locals.push_back({1, {110}, {}, {}, 4, {}});
  const auto mapped = std::get<source::ParameterOffsets>(
      source::MapParameterDebugNames(frame, body, scopes, abc::ProfileId::kDynamic1206, 1024 * 1024));
  test::Check(mapped.size() == 2 && mapped[0]->value == 100 &&
              mapped[1]->value == 110);
  const auto partial = [&](const abc::MethodBody& code, const abc::DebugScopes& debug,
      std::optional<abc::EntityOffset> first, std::optional<abc::EntityOffset> second) {
    const auto result = std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
        frame, code, debug, abc::ProfileId::kDynamic1206, 1024 * 1024,
        source::ParameterNameDomain::kSourceTree));
    test::Check(result == source::ParameterOffsets{first, second});
  };
  auto incomplete = scopes; incomplete.locals.pop_back();
  CheckUnproven(frame, body, incomplete);
  partial(body, incomplete, abc::EntityOffset{100}, {});
  std::reverse(incomplete.locals.begin(), incomplete.locals.end());
  partial(body, incomplete, abc::EntityOffset{100}, {});
  auto conflict = scopes;
  conflict.locals.push_back({0, {120}, {}, {}, 4, {}});
  conflict.locals.push_back(scopes.locals[0]);
  partial(body, conflict, {}, abc::EntityOffset{110});
  auto invalid_range = scopes; invalid_range.locals[0].start = 1;
  partial(body, invalid_range, {}, abc::EntityOffset{110});
  partial(body, {}, {}, {});
  // Pure block splitting and accumulator transports do not change entry
  // origins. Naming must query the actual debug boundary, not a MOV prefix.
  const auto split = test::Body({0x44, 0x20, 0xd5, 0x60, 3, 0x61, 1, 0x4d, 2, 0x65});
  auto split_scopes = scopes;
  for (auto& local : split_scopes.locals) local.start = 9;
  const auto split_names = std::get<source::ParameterOffsets>(
      source::MapParameterDebugNames(frame, split, split_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024));
  test::Check(split_names == mapped);
  split_scopes.locals.pop_back();
  partial(split, split_scopes, abc::EntityOffset{100}, {});
  const auto direct = test::Body({0x65});
  auto direct_scopes = scopes;
  for (std::size_t i = 0; i < direct_scopes.locals.size(); ++i) {
    direct_scopes.locals[i].reg = static_cast<std::int32_t>(frame.first_parameter.value + i);
    direct_scopes.locals[i].start = 0;
  }
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, direct, direct_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto branch = test::Body({0x44, 0x20, 0x02, 0x4f, 7,
      0x44, 0x31, 0x4d, 5, 0xd5, 0x44, 0x31, 0x65});
  auto branch_scopes = scopes;
  for (auto& local : branch_scopes.locals) local.start = 12;
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, branch, branch_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto mixed = test::Body({0x44, 0x20, 0x02, 0x4f, 7,
      0x44, 0x31, 0x4d, 5, 0xd5, 0x44, 0x21, 0x65});
  CheckUnproven(frame, mixed, branch_scopes);
  partial(mixed, branch_scopes, abc::EntityOffset{100}, {});
  const auto loop = test::Body({0x44, 0x20, 0x44, 0x31, 0x02, 0x4f, 8,
      0x44, 0x31, 0x4d, static_cast<std::uint8_t>(-5), 0xd5, 0xd5, 0x65});
  auto loop_scopes = scopes;
  for (auto& local : loop_scopes.locals) local.start = 13;
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, loop, loop_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto self_loop = test::Body({0x44, 0x20, 0x44, 0x31, 0x02, 0x4f, 8,
      0x44, 0x11, 0x4d, static_cast<std::uint8_t>(-5), 0xd5, 0xd5, 0x65});
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, self_loop, loop_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto mixed_loop = test::Body({0x44, 0x20, 0x44, 0x31, 0x02, 0x4f, 8,
      0x44, 0x21, 0x4d, static_cast<std::uint8_t>(-5), 0xd5, 0xd5, 0x65});
  CheckUnproven(frame, mixed_loop, loop_scopes);
  const auto one_frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      1, 1, {false, false, false, false}));
  auto caught = test::Body({0x44, 0x10, 0x62, 42, 0, 0, 0, 0xfe, 0x00, 0x65});
  caught.tries.push_back({{2}, 7, 0, 1});
  caught.handlers.push_back({{}, {9}, 1});
  abc::DebugScopes caught_scopes;
  caught_scopes.locals.push_back({0, {100}, {}, {}, 9, {}});
  const auto caught_names = std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      one_frame, caught, caught_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024));
  test::Check(caught_names.size() == 1 && caught_names[0]->value == 100);
  auto caught_write = test::Body({0x44, 0x10, 0x62, 42, 0, 0, 0, 0xfe, 0x00, 0x61, 0, 0x65});
  caught_write.tries.push_back({{2}, 7, 0, 1});
  caught_write.handlers.push_back({{}, {9}, 3});
  caught_scopes.locals[0].start = 11;
  CheckUnproven(one_frame, caught_write, caught_scopes);
  auto inside_instruction = scopes;
  inside_instruction.locals[0].start = 1;
  CheckUnproven(frame, body, inside_instruction);
  auto repeated = scopes;
  repeated.locals.push_back(scopes.locals[0]);
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, body, repeated, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  for (const auto count : {200U, 201U, 402U, 804U}) {
    const auto many_frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
        0, count, {false, false, false, false}));
    abc::DebugScopes many_scopes;
    for (std::uint32_t i = 0; i < count; ++i)
      many_scopes.locals.push_back({static_cast<std::int32_t>(i), {100 + i}, {}, {}, 0, {}});
    const auto many_names = std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
        many_frame, direct, many_scopes, abc::ProfileId::kDynamic1206, 4 * 1024 * 1024));
    test::Check(many_names.size() == count);
    for (std::uint32_t i = 0; i < count; ++i) test::Check(many_names[i]->value == 100 + i);
    many_scopes.locals.erase(many_scopes.locals.begin() + count / 2);
    const auto partial_names = std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
        many_frame, direct, many_scopes, abc::ProfileId::kDynamic1206, 4 * 1024 * 1024,
        source::ParameterNameDomain::kSourceTree));
    for (std::uint32_t i = 0; i < count; ++i)
      test::Check(i == count / 2 ? !partial_names[i] : partial_names[i] == abc::EntityOffset{100 + i});
  }
  const auto raw_tail = test::Body({0x44, 0x20, 0x44, 0x31, 0xc3, 0, 0x65});
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, raw_tail, scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  auto after_raw = scopes;
  for (auto& local : after_raw.locals) local.start = 6;
  CheckUnproven(frame, raw_tail, after_raw);
  const auto raw_backedge = test::Body({0x44, 0x20, 0x44, 0x31, 0xc3, 0, 0x4d,
      static_cast<std::uint8_t>(-2), 0x65});
  CheckUnproven(frame, raw_backedge, scopes);
  const auto dead_raw = test::Body({0x44, 0x20, 0x44, 0x31, 0x4d, 4, 0xc3, 0, 0x65});
  auto after_dead = scopes;
  for (auto& local : after_dead.locals) local.start = 8;
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      frame, dead_raw, after_dead, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto chain_frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      3, 2, {false, false, false, false}));
  const auto chain = test::Body({0x44, 0x30, 0x44, 0x41, 0x44, 0x02, 0x65});
  auto chain_scopes = scopes;
  for (auto& local : chain_scopes.locals) local.start = 6;
  chain_scopes.locals.push_back({2, {120}, {}, {}, 6, {}});
  test::Check(std::get<source::ParameterOffsets>(source::MapParameterDebugNames(
      chain_frame, chain, chain_scopes, abc::ProfileId::kDynamic1206, 1024 * 1024)) == mapped);
  const auto independent = test::Body({0x44, 0x30, 0x44, 0x41, 0x44, 0x32, 0x65});
  CheckUnproven(chain_frame, independent, chain_scopes);
  const auto wide_frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      20, 1, {false, false, false, false}));
  const auto wide_body = test::Body({0x45, 0, 20, 0x65});
  abc::DebugScopes wide_scope;
  wide_scope.locals.push_back({0, {130}, {}, {}, 3, {}});
  const auto wide = std::get<source::ParameterOffsets>(
      source::MapParameterDebugNames(wide_frame, wide_body, wide_scope,
                                     abc::ProfileId::kDynamic1206, 1024 * 1024));
  test::Check(wide.size() == 1 && wide[0]->value == 130);
  auto wrong_scope = scopes;
  wrong_scope.locals[1].start = 2;
  CheckUnproven(frame, body, wrong_scope);
  auto duplicate = scopes;
  duplicate.locals.push_back({0, {120}, {}, {}, 4, {}});
  CheckUnproven(frame, body, duplicate);
  auto incoming = body;
  incoming.instructions.back().branch_target = abc::CodeOffset{0};
  CheckUnproven(frame, incoming, scopes);
  const auto overwritten = test::Body(
      {0x44, 0x20, 0x44, 0x31, 0x44, 0x30, 0x65});
  auto later = scopes;
  for (auto& local : later.locals) local.start = 6;
  CheckUnproven(frame, overwritten, later);
  const auto abi_overwrite = test::Body(
      {0x44, 0x02, 0x44, 0x20, 0x44, 0x31, 0x65});
  CheckUnproven(frame, abi_overwrite, later);
  test::Check(std::get<ParameterNameError>(
                  source::MapParameterDebugNames(frame, body, scopes, abc::ProfileId::kDynamic1206, 0)) ==
              ParameterNameError::kResourceLimit);
  for (const auto count : {200U, 201U, 402U}) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(count * 2 + 1);
    for (std::uint32_t i = 0; i < count; ++i)
      bytes.insert(bytes.end(), {0x44, 0x10});
    bytes.push_back(0x65);
    const auto many = test::Body(bytes);
    const auto one = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
        1, 1, {false, false, false, false}));
    abc::DebugScopes named;
    named.locals.push_back({0, {100}, {}, {}, 2 * count, {}});
    const auto offsets = std::get<source::ParameterOffsets>(
        source::MapParameterDebugNames(one, many, named, abc::ProfileId::kDynamic1206, 1024 * 1024));
    test::Check(offsets.size() == 1 && offsets[0]->value == 100);
  }
}
