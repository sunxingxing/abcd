#include "abcd/abc/debug_scopes.h"

#include <unistd.h>

#include <cstdlib>

#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
void Register(std::vector<std::uint8_t>& program, std::uint32_t reg) {
  constexpr std::uint32_t kPositiveSlebLimit = 64;
  constexpr std::uint8_t kContinuation = 0x80;
  constexpr std::uint32_t kPayloadMask = 0x7f;
  constexpr unsigned kPayloadBits = 7;
  while (reg >= kPositiveSlebLimit) {
    program.push_back(static_cast<std::uint8_t>(reg & kPayloadMask) |
                      kContinuation);
    reg >>= kPayloadBits;
  }
  program.push_back(static_cast<std::uint8_t>(reg));
}
abcd::abc::DebugScopeResult Read(std::vector<std::uint8_t> program,
                                 std::vector<std::uint8_t> pool = {},
                                 abcd::abc::DebugScopeBudget budget = {},
                                 std::uint32_t code_bytes = 100,
                                 std::uint32_t line_start = 10) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(abc::kDynamicHeaderSize);
  bytes.insert(bytes.end(), {3, 'x', 0});  // String entity at 60.
  abc::DebugInfo debug;
  debug.line_start = line_start;
  debug.constant_pool = {static_cast<std::uint32_t>(bytes.size())};
  debug.constant_pool_bytes = pool.size();
  bytes.insert(bytes.end(), pool.begin(), pool.end());
  debug.line_program = {static_cast<std::uint32_t>(bytes.size())};
  bytes.insert(bytes.end(), program.begin(), program.end());
  char path[] = "/tmp/abcd-debug-scopes-XXXXXX";
  const auto fd = mkstemp(path);
  Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) ==
        static_cast<ssize_t>(bytes.size()));
  close(fd);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  auto result = abc::ReadDebugScopes(file, header, debug, code_bytes, budget);
  // Unlink changes ctime; retain the input until immutable-file checks finish.
  unlink(path);
  return result;
}
}  // namespace
int main() {
  using namespace abcd::abc;
  // set file/source, extended local(-1), pc+3, line-2, column9,
  // special opcode pc+1 line+0, end local(-1), end sequence.
  const auto result = Read({9, 10, 4, 0x7f, 1, 2, 11, 31, 5, 0x7f, 0},
                           {60, 60, 60, 60, 60, 3, 0x7e, 9});
  const auto& scopes = std::get<DebugScopes>(result);
  Check(scopes.locals.size() == 1);
  const auto& local = scopes.locals.front();
  Check(local.reg == -1 && local.start == 0 && local.end == 4);
  Check(local.name.value == 60 && local.type.value == 60 &&
        local.signature->value == 60);
  Check(scopes.terminal_pc == 4 && scopes.positions.back().line == 8);
  Check(scopes.positions.back().column == 9 &&
        scopes.positions.back().file->value == 60);
  Check(scopes.positions.back().source_code->value == 60);
  Check(std::get<DebugScopes>(Read({3, 0, 0}, {60, 60})).locals[0].end ==
        std::nullopt);
  Check(std::get<DebugScopes>(Read({3, 0, 5, 0, 0}, {60, 60})).locals[0].end ==
        0);
  Check(std::get<DebugScopes>(Read({7, 8, 0})).terminal_pc == 0);
  const auto final_source = std::get<DebugScopes>(Read({9, 10, 0}, {60, 60}));
  Check(final_source.terminal_file->value == 60);
  Check(final_source.terminal_source_code->value == 60);
  Check(final_source.positions.size() == 1 && !final_source.positions[0].file);
  const auto missing_line =
      std::get<DebugScopes>(Read({2, 16, 0}, {11}, {}, 100, 0xffffffffU));
  Check(!missing_line.positions.front().line);
  Check(missing_line.positions.back().line == 10);
  const auto error = [](DebugScopeResult value, DebugScopeError expected) {
    Check(std::get<DebugScopeError>(std::get<DebugScopeFailure>(value).error) ==
          expected);
  };
  error(Read({6, 0}), DebugScopeError::kRestartUnsupported);
  error(Read({5, 0, 0}), DebugScopeError::kLocalState);
  error(Read({3, 0, 3, 0, 0}, {60, 60, 60, 60}), DebugScopeError::kLocalState);
  error(Read({1, 0}, {101}), DebugScopeError::kPcRange);
  error(Read({2, 0}, {0x70}), DebugScopeError::kLineRange);
  Check(std::holds_alternative<DebugScopeFailure>(Read({3, 0, 0}, {60})));
  Check(std::holds_alternative<DebugScopeFailure>(Read({3, 0, 0}, {0, 60})));
  Check(std::holds_alternative<DebugScopeFailure>(Read({3, 0x80}, {60, 60})));
  Check(
      std::holds_alternative<DebugScopeFailure>(Read({})));  // No END_SEQUENCE.
  error(Read({0}, {}, {0, 100}), DebugScopeError::kResourceLimit);
  Check(std::holds_alternative<DebugScopes>(
      Read({0}, {}, {sizeof(DebugPosition), 100})));
  error(Read({0}, {}, {sizeof(DebugPosition) - 1, 100}),
        DebugScopeError::kResourceLimit);
  error(Read({7, 0}, {}, {1024, 1}), DebugScopeError::kResourceLimit);
  for (const std::size_t count : {200U, 201U, 8192U}) {
    std::vector<std::uint8_t> program, pool;
    for (std::size_t i = 0; i < count; ++i) {
      program.insert(program.end(), {3, 0, 5, 0});
      pool.insert(pool.end(), {60, 60});
    }
    program.push_back(0);
    Check(std::get<DebugScopes>(Read(program, pool)).locals.size() == count);
    program.clear();
    for (std::size_t i = 0; i < count; ++i) {
      program.push_back(3);
      Register(program, i);
    }
    for (std::size_t i = count; i > 0; --i) {
      program.push_back(5);
      Register(program, i - 1);
    }
    program.push_back(0);
    const auto many = std::get<DebugScopes>(Read(program, pool));
    Check(many.locals.size() == count &&
          many.locals.back().reg == static_cast<std::int32_t>(count - 1));
    for (const auto& entry : many.locals) Check(entry.end == 0);
  }
}
