#include "abcd/abc/method_body.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {
using namespace abcd::abc;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "method_body_test failed\n";
    std::abort();
  }
}

void AppendUleb(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  constexpr std::uint8_t kPayload = 0x7f;
  constexpr std::uint8_t kContinue = 0x80;
  constexpr unsigned kBits = 7;
  do {
    auto byte = static_cast<std::uint8_t>(value & kPayload);
    value >>= kBits;
    if (value != 0) byte |= kContinue;
    bytes->push_back(byte);
  } while (value != 0);
}

BodyResult Decode(std::span<const std::uint8_t> instructions,
                  std::span<const std::uint8_t> exceptions = {},
                  std::uint32_t tries = 0, std::uint32_t registers = 1,
                  std::uint32_t arguments = 0, BodyBudget budget = {},
                  ProfileId profile = ProfileId::kDynamic1301,
                  bool change_metadata = false) {
  std::vector<std::uint8_t> bytes(kDynamicHeaderSize);
  CodeInfo code;
  code.offset.value = bytes.size();
  code.registers = registers;
  code.arguments = arguments;
  code.instruction_bytes = instructions.size();
  code.try_count = tries;
  AppendUleb(&bytes, registers);
  AppendUleb(&bytes, arguments);
  AppendUleb(&bytes, instructions.size());
  AppendUleb(&bytes, tries);
  code.instruction_offset.value = bytes.size();
  bytes.insert(bytes.end(), instructions.begin(), instructions.end());
  bytes.insert(bytes.end(), exceptions.begin(), exceptions.end());
  char path[] = "/tmp/abcd-body-XXXXXX";
  const int fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  Check(close(fd) == 0);
  auto opened = abcd::corpus::AbcFile::Open(path);
  Check(std::holds_alternative<std::unique_ptr<abcd::corpus::AbcFile>>(opened));
  if (change_metadata) ++code.registers;
  auto result = DecodeMethodBody(
      *std::get<std::unique_ptr<abcd::corpus::AbcFile>>(opened), profile, code,
      budget);
  // Unlink only after parsing: unlink changes ctime of an open descriptor.
  Check(unlink(path) == 0);
  return result;
}

void Fails(const BodyResult& result, BodyError error) {
  Check(std::holds_alternative<BodyFailure>(result));
  Check(std::get<BodyFailure>(result).kind == error);
}

void ValidatesInstructionBoundariesAndRegisters() {
  // Pinned isa.yaml: jmp imm8 is 0x4d; return is 0x64; lda v8 is 0x60.
  const std::uint8_t forward[]{0x4d, 2, 0x64};
  auto result = Decode(forward);
  Check(std::holds_alternative<MethodBody>(result));
  const auto& body = std::get<MethodBody>(result);
  Check(body.instructions.size() == 2);
  Check(body.instructions[0].branch_target == CodeOffset{2});
  Check(body.instructions[1].offset == CodeOffset{2});
  const std::uint8_t backward[]{0x00, 0x4d, 0xff};
  result = Decode(backward);
  Check(std::get<MethodBody>(result).instructions[1].branch_target ==
        CodeOffset{0});
  const std::uint8_t interior[]{0x4d, 1, 0x64};
  Fails(Decode(interior), BodyError::kBranchBoundary);
  const std::uint8_t end[]{0x4d, 3, 0x64};
  Fails(Decode(end), BodyError::kBranchRange);
  const std::uint8_t negative[]{0x4d, 0xff, 0x64};
  Fails(Decode(negative), BodyError::kBranchRange);
  const std::uint8_t bad_register[]{0x60, 1, 0x64};
  Fails(Decode(bad_register), BodyError::kRegisterRange);
  Check(std::holds_alternative<MethodBody>(Decode(bad_register, {}, 0, 1, 1)));
  const std::uint8_t truncated[]{0x62};  // ldai requires a 32-bit immediate.
  Fails(Decode(truncated), BodyError::kInstruction);
  Fails(Decode({}), BodyError::kEmptyBody);
  Fails(Decode(forward, {}, 0, 1, 0, {}, ProfileId::kStatic0107),
        BodyError::kUnsupportedProfile);
  Fails(Decode(forward, {}, 0, 1, 0, {}, ProfileId::kDynamic1301, true),
        BodyError::kMetadataMismatch);
}

void PreservesExceptionOrderAndChecksRanges() {
  const std::uint8_t code[]{0x00, 0x64, 0x00, 0x64};
  // try [0,2), typed catch index 1 at [2,4), then catch-all at [3,4).
  const std::uint8_t exceptions[]{0, 2, 2, 2, 2, 2, 0, 3, 1};
  auto result = Decode(code, exceptions, 1);
  Check(std::holds_alternative<MethodBody>(result));
  const auto& body = std::get<MethodBody>(result);
  Check(body.tries.size() == 1 && body.handlers.size() == 2);
  Check(body.tries[0].first_handler == 0 && body.tries[0].handler_count == 2);
  Check(body.handlers[0].type == CatchTypeIndex{1});
  Check(!body.handlers[1].type.has_value());
  Check(body.handlers[0].offset == CodeOffset{2});
  const std::uint8_t overlapping[]{0, 4, 1, 0, 2, 2, 0, 2, 1, 0, 2, 2};
  Check(std::get<MethodBody>(Decode(code, overlapping, 2)).tries.size() == 2);
  const std::uint8_t oversized[]{0, 5, 1, 0, 2, 2};
  Fails(Decode(code, oversized, 1), BodyError::kExceptionRange);
  const std::uint8_t truncated[]{0x80};
  Fails(Decode(code, truncated, 1), BodyError::kMalformedException);
  const std::uint8_t wide_instruction[]{0x62, 0, 0, 0, 0, 0x64};
  const std::uint8_t bad_handler[]{0, 5, 1, 0, 1, 4};
  Fails(Decode(wide_instruction, bad_handler, 1),
        BodyError::kExceptionBoundary);
  const std::uint8_t bad_end[]{0, 5, 1, 0, 0, 1};
  Fails(Decode(wide_instruction, bad_end, 1), BodyError::kExceptionBoundary);
}

void HandlesGrowingMethodsAndBudgets() {
  for (const auto count : {200U, 201U, 1024U}) {
    std::vector<std::uint8_t> code(count, 0xd5);  // nop
    code.back() = 0x64;
    Check(std::get<MethodBody>(Decode(code)).instructions.size() == count);
    std::vector<std::uint8_t> catches;
    AppendUleb(&catches, 0);
    AppendUleb(&catches, count);
    AppendUleb(&catches, count);
    for (std::uint32_t i = 0; i < count; ++i) {
      AppendUleb(&catches, i + 1);
      AppendUleb(&catches, i);
      AppendUleb(&catches, 1);
    }
    Check(std::get<MethodBody>(Decode(code, catches, 1)).handlers.size() ==
          count);
    Fails(Decode(code, {}, 0, 1, 0, {8}), BodyError::kResourceLimit);
  }
  std::vector<std::uint8_t> dense(1024, 0xd5);  // nop
  dense.back() = 0x64;
  Check(std::get<MethodBody>(Decode(dense, {}, 0, 1, 0, {250000}))
            .instructions.size() == dense.size());
}
}  // namespace

int main() {
  ValidatesInstructionBoundariesAndRegisters();
  PreservesExceptionOrderAndChecksRanges();
  HandlesGrowingMethodsAndBudgets();
}
