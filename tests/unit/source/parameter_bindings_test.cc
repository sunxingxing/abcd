#include "parameter_names.h"

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
                                                 1024 * 1024)) ==
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
      source::MapParameterDebugNames(frame, body, scopes, 1024 * 1024));
  test::Check(mapped.size() == 2 && mapped[0].value == 100 &&
              mapped[1].value == 110);
  const auto wide_frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      20, 1, {false, false, false, false}));
  const auto wide_body = test::Body({0x45, 0, 20, 0x65});
  abc::DebugScopes wide_scope;
  wide_scope.locals.push_back({0, {130}, {}, {}, 3, {}});
  const auto wide = std::get<source::ParameterOffsets>(
      source::MapParameterDebugNames(wide_frame, wide_body, wide_scope,
                                     1024 * 1024));
  test::Check(wide.size() == 1 && wide[0].value == 130);
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
                  source::MapParameterDebugNames(frame, body, scopes, 0)) ==
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
        source::MapParameterDebugNames(one, many, named, 1024 * 1024));
    test::Check(offsets.size() == 1 && offsets[0].value == 100);
  }
}
