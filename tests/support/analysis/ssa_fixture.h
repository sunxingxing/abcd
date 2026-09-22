#ifndef ABCD_TEST_SUPPORT_SSA_FIXTURE_H_
#define ABCD_TEST_SUPPORT_SSA_FIXTURE_H_

#include <cstdlib>
#include <iostream>
#include <source_location>

#include "abcd/analysis/register_ssa.h"

namespace abcd::test {
inline void Check(
    bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    std::cerr << where.file_name() << ':' << where.line() << '\n';
    std::abort();
  }
}
inline abc::MethodBody Body(std::span<const std::uint8_t> bytes) {
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < bytes.size();) {
    const auto decoded =
        std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
            bytes.subspan(pc), abc::ProfileId::kDynamic1206));
    abc::BodyInstruction instruction{{pc}, decoded, {}};
    if (decoded.branch_delta)
      instruction.branch_target = abc::CodeOffset{
          static_cast<std::uint32_t>(pc + *decoded.branch_delta)};
    body.instructions.push_back(instruction);
    pc += decoded.size;
  }
  return body;
}
inline abc::MethodBody Body(std::initializer_list<std::uint8_t> bytes) {
  return Body(std::span(bytes.begin(), bytes.size()));
}
void VerifySsaPaths();
}  // namespace abcd::test
#endif  // ABCD_TEST_SUPPORT_SSA_FIXTURE_H_
