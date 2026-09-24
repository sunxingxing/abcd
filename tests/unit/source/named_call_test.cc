#include <unistd.h>

#include <algorithm>
#include <sstream>

#include "abcd/analysis/named_invocation.h"
#include "abcd/source/acyclic_body.h"
#include "abcd/source/linear_body.h"
#include "abcd/source/local_lexical_body.h"
#include "analysis/ssa_fixture.h"

int main(int argc, char** argv) {
  using namespace abcd;
  std::vector<std::uint8_t> bytes(1024);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i)
      bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  };
  word(64, 512);
  word(68, 530);
  const std::uint8_t method[]{13, 'm', 'e', 't', 'h', 'o', 'd', 0};
  std::copy(std::begin(method), std::end(method), bytes.begin() + 512);
  bytes[530] = 3;
  bytes[531] = '0';
  char path[] = "/tmp/abcd-named-call-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0);
  test::Check(write(fd, bytes.data(), bytes.size()) ==
              static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  header.layout = abc::DynamicLayout{};
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({200, 300, {}, {2, 64}, {}, {}});
  const source::MethodConstants constants{file, header, index, {210}, {}};
  const std::array<std::string_view, 4> registers{"object", "a", "b", "c"};
  const auto body = test::Body({0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 0, 0x64});
  std::ostringstream output;
  const auto result =
      source::WriteLinearBody(body, {"acc", registers}, output, {}, &constants);
  test::Check(std::holds_alternative<std::size_t>(result));
  const auto spilled =
      test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 1, 0x2d, 0, 0, 0x64});
  std::ostringstream spilled_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      spilled, {"acc", registers}, spilled_output, {}, &constants)));
  test::Check(spilled_output.str() == output.str());
  const auto returned_receiver = test::Body(
      {0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 1, 0x2d, 0, 0,
       0x60, 0, 0x64});
  const auto returned = analysis::MatchNamedInvocation(
      returned_receiver.instructions, 1, registers.size());
  test::Check(returned && returned->instruction_count == 4);
  std::ostringstream returned_output;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      returned_receiver, {"acc", registers}, returned_output, {}, &constants)));
  const std::string_view terminal = "return acc;\n";
  test::Check(output.str().ends_with(terminal));
  auto expected_returned = output.str();
  expected_returned.resize(expected_returned.size() - terminal.size());
  expected_returned += "acc = object;\nreturn acc;\n";
  test::Check(returned_output.str() == expected_returned);
  std::ostringstream returned_lexical;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLocalLexicalBody(
      returned_receiver, {"acc", registers}, returned_lexical, {},
      &constants)));
  test::Check(returned_lexical.str() == returned_output.str());
  const auto deferred = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0,
      0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x64});
  const auto matched =
      analysis::MatchTailNamedInvocation(deferred, registers.size());
  test::Check(matched && matched->property == 1 && matched->call == 7);
  std::ostringstream deferred_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(deferred, {"acc", registers},
                                    deferred_source, {}, &constants)));
  test::Check(deferred_source.str().find(
      "acc = (object)[\"method\"]((() => {\n") != std::string::npos);
  test::Check(deferred_source.str().find("return b;\n})());\n") !=
              std::string::npos);
  const auto staged = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0,
      0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x61, 1, 0x60, 1, 0x64});
  std::ostringstream staged_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(staged, {"acc", registers},
                                    staged_source, {}, &constants)));
  test::Check(staged_source.str().find(
      "acc = (object)[\"method\"]((() => {\n") != std::string::npos);
  test::Check(staged_source.str().ends_with(
      "a = acc;\nacc = a;\nreturn acc;\n"));
  const auto staged_match =
      analysis::MatchStagedNamedInvocation(staged, registers.size());
  test::Check(staged_match && staged_match->property == 1 &&
              staged_match->call == 7);
  const auto dead_callee = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0,
      0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x61, 2, 0x60, 2, 0x64});
  test::Check(analysis::MatchStagedNamedInvocation(dead_callee,
                                                   registers.size()).has_value());
  std::ostringstream dead_callee_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(dead_callee, {"acc", registers},
                                    dead_callee_source, {}, &constants)));
  test::Check(dead_callee_source.str().ends_with(
      "b = acc;\nacc = b;\nreturn acc;\n"));
  const auto delayed_overwrite = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0,
      0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x61, 2, 0x60, 2,
      0x61, 1, 0x60, 1, 0x64});
  test::Check(analysis::MatchStagedNamedInvocation(delayed_overwrite,
                                                   registers.size()).has_value());
  std::ostringstream delayed_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(delayed_overwrite, {"acc", registers},
                                    delayed_source, {}, &constants)));
  test::Check(delayed_source.str().ends_with(
      "b = acc;\nacc = b;\na = acc;\nacc = a;\nreturn acc;\n"));
  auto old_callee_read = staged;
  old_callee_read.instructions[8].decoded =
      test::Body({0x60, 1}).instructions[0].decoded;
  test::Check(!analysis::MatchStagedNamedInvocation(old_callee_read,
                                                   registers.size()));
  auto read_write_callee = staged;
  read_write_callee.instructions[8].decoded =
      test::Body({0x44, 0x11}).instructions[0].decoded;
  test::Check(!analysis::MatchStagedNamedInvocation(read_write_callee,
                                                   registers.size()));
  auto overwritten_receiver = staged;
  overwritten_receiver.instructions[3].decoded =
      test::Body({0x61, 0}).instructions[0].decoded;
  test::Check(!analysis::MatchStagedNamedInvocation(overwritten_receiver,
                                                   registers.size()));
  auto branched_staged = staged;
  branched_staged.instructions[4].branch_target = abc::CodeOffset{0};
  test::Check(!analysis::MatchStagedNamedInvocation(branched_staged,
                                                   registers.size()));
  auto two_calls = staged;
  two_calls.instructions[8].decoded =
      test::Body({0x2d, 0, 0}).instructions[0].decoded;
  test::Check(!analysis::MatchStagedNamedInvocation(two_calls,
                                                   registers.size()));
  for (const std::size_t count : {200U, 201U, 402U}) {
    std::vector<std::uint8_t> code{
        0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0};
    code.insert(code.end(), count, 0xd5);
    code.insert(code.end(),
                {0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x61, 1,
                 0x60, 1, 0x64});
    const auto many = test::Body(code);
    test::Check(analysis::MatchStagedNamedInvocation(many, registers.size())
                    .has_value());
    std::ostringstream many_source;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(many, {"acc", registers}, many_source,
                                      {}, &constants)));
    test::Check(many_source.str() == staged_source.str());
  }
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function staged(object,b,c){let acc,a;\n"
              << staged_source.str() << "}\n";
  const auto deferred_discard = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3, 0x29, 0,
      0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x00, 0x65});
  test::Check(analysis::MatchTailNamedInvocation(deferred_discard,
                                                 registers.size())
                  .has_value());
  std::ostringstream discard_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(deferred_discard, {"acc", registers},
                                    discard_source, {}, &constants)));
  test::Check(discard_source.str().ends_with(
      "})());\nacc = (void 0);\nreturn;\n"));
  const std::array<std::string_view, 5> three_registers{
      "object", "callee", "a", "b", "c"};
  const auto three_arguments = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1,
      0x62, 42, 0, 0, 0, 0x61, 2, 0x60, 1,
      0x30, 0, 0, 2, 3, 4, 0x00, 0x64});
  const auto three_match = analysis::MatchTailNamedInvocation(
      three_arguments, three_registers.size());
  test::Check(three_match && three_match->invoke.Count() == 3);
  std::ostringstream three_source;
  const auto three_result = source::WriteLocalLexicalBody(
      three_arguments, {"acc", three_registers}, three_source, {}, &constants);
  test::Check(std::holds_alternative<std::size_t>(three_result));
  test::Check(three_source.str().find(
      "return a;\n})(), b, c);\n") != std::string::npos);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function threeDeferred(object,a,b,c){let acc,callee;\n"
              << three_source.str() << "}\n";
  const std::array<std::string_view, 4> copied_registers{
      "object", "callee", "value", "argument"};
  const auto copied_argument = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 1,
      0x60, 2, 0x61, 3, 0x60, 1,
      0x2e, 0, 0, 3, 0x61, 1, 0x60, 1, 0x64});
  std::ostringstream copied_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(copied_argument,
                                    {"acc", copied_registers}, copied_source,
                                    {}, &constants)));
  test::Check(copied_source.str().find(
      "(object)[\"method\"](argument)") != std::string::npos);
  const auto reject_copied = [&](abc::MethodBody candidate) {
    test::Check(!analysis::MatchCopiedArgumentInvocation(
        candidate.instructions, 6, copied_registers.size()));
    std::ostringstream rejected;
    test::Check(!std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(candidate,
                                      {"acc", copied_registers}, rejected,
                                      {}, &constants)));
    test::Check(rejected.str().empty());
  };
  auto clobbered_receiver = copied_argument;
  clobbered_receiver.instructions[4].decoded.operands[0].raw_bits = 0;
  reject_copied(clobbered_receiver);
  auto clobbered_callee = copied_argument;
  clobbered_callee.instructions[4].decoded.operands[0].raw_bits = 1;
  reject_copied(clobbered_callee);
  auto read_callee = copied_argument;
  read_callee.instructions[3].decoded.operands[0].raw_bits = 1;
  reject_copied(read_callee);
  auto wrong_capture = copied_argument;
  wrong_capture.instructions[7].decoded.operands[0].raw_bits = 2;
  reject_copied(wrong_capture);
  auto wrong_receiver = copied_argument;
  wrong_receiver.instructions[6].decoded.operands[1].raw_bits = 2;
  reject_copied(wrong_receiver);
  auto impure_argument = copied_argument;
  impure_argument.instructions[3].decoded =
      test::Body({0x29, 0}).instructions[0].decoded;
  reject_copied(impure_argument);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function copiedArgument(object,value){"
                 "let acc,callee,argument;\n"
              << copied_source.str() << "}\n";
  const std::array<std::string_view, 6> range_registers{
      "object", "a", "b", "c", "d", "callee"};
  const auto direct_range = test::Body(
      {0x60, 0, 0x42, 0, 0, 0, 0x31, 0, 4, 0, 0x64});
  std::ostringstream direct_range_source;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      direct_range, {"acc", range_registers}, direct_range_source, {},
      &constants)));
  test::Check(direct_range_source.str().find(
      "(object)[\"method\"](a, b, c, d)") != std::string::npos);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function rangeDirect(object,a,b,c,d){let acc;\n"
              << direct_range_source.str() << "}\n";
  const auto range_body = test::Body({
      0x60, 0, 0x42, 0, 0, 0, 0x61, 5,
      0x62, 42, 0, 0, 0, 0x61, 1, 0x60, 5,
      0x31, 0, 4, 0, 0x00, 0x64});
  const auto range_match = analysis::MatchTailNamedInvocation(
      range_body, range_registers.size());
  test::Check(range_match.has_value());
  std::ostringstream range_source;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLocalLexicalBody(range_body, {"acc", range_registers},
                                    range_source, {}, &constants)));
  test::Check(range_source.str().find(
      "return a;\n})(), b, c, d);\n") != std::string::npos);
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function rangeDeferred(object,a,b,c,d){"
                 "let acc,callee;\n"
              << range_source.str() << "}\n";
  for (const std::uint8_t count : {std::uint8_t{200}, std::uint8_t{201}}) {
    auto encoded = test::Body({0x31, 0, count, 0});
    const auto lifted = analysis::LiftReceiverCall(
        encoded.instructions[0].decoded, std::uint32_t{count} + 1);
    test::Check(std::holds_alternative<analysis::ReceiverCall>(lifted));
  }
  for (const auto& unsafe_tail : {
           test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3,
                       0x61, 0, 0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x64}),
           test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3,
                       0x60, 1, 0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x64}),
           test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x61, 2,
                       0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x64}),
           test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3,
                       0x60, 2, 0x60, 1, 0x2e, 0, 2, 2, 0x64}),
           test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3,
                       0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x61, 2, 0x64}),
       })
    test::Check(!analysis::MatchTailNamedInvocation(unsafe_tail,
                                                    registers.size()));
  std::ostringstream limited_tail;
  const auto short_tail = source::WriteLocalLexicalBody(
      deferred, {"acc", registers}, limited_tail,
      {.output = {deferred_source.str().size() - 1}}, &constants);
  test::Check(std::get<source::BodyEmitFailure>(short_tail).emit_error ==
              source::EmitError::kOutputLimit);
  test::Check(limited_tail.str().empty());
  for (const std::size_t count : {200U, 201U, 402U}) {
    std::vector<std::uint8_t> encoded{
        0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60, 3};
    encoded.insert(encoded.end(), count, 0xd5);
    encoded.insert(encoded.end(),
                   {0x60, 2, 0x60, 1, 0x2e, 0, 0, 2, 0x64});
    const auto many = test::Body(encoded);
    test::Check(analysis::MatchTailNamedInvocation(many, registers.size())
                    .has_value());
    std::ostringstream many_source;
    test::Check(std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(many, {"acc", registers}, many_source,
                                      {}, &constants)));
    test::Check(many_source.str() ==
                "acc = object;\nacc = (object)[\"method\"]((() => {\n"
                "acc = c;\nacc = b;\nreturn b;\n})());\nreturn acc;\n");
  }
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function returnedReceiver(object){let acc;\n"
              << returned_output.str() << "}\n";
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function deferred(object,b,c){let acc,a;\n"
              << deferred_source.str() << "}\n";
  for (const auto* candidate : {&body, &spilled}) {
    std::ostringstream lexical;
    test::Check(
        std::holds_alternative<std::size_t>(source::WriteLocalLexicalBody(
            *candidate, {"acc", registers}, lexical, {}, &constants)));
    test::Check(lexical.str() == output.str());
  }
  for (const auto& unsafe : {test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 0, 0x60,
                                         0, 0x2d, 0, 0, 0x64}),
                             test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60,
                                         1, 0x2e, 0, 0, 1, 0x64}),
                             test::Body({0x60, 0, 0x42, 0, 0, 0, 0x61, 1, 0x60,
                                         1, 0x2d, 0, 0, 0x60, 1, 0x64})}) {
    test::Check(!analysis::MatchNamedInvocation(unsafe.instructions, 1, 4));
    std::ostringstream rejected;
    test::Check(!std::holds_alternative<std::size_t>(source::WriteLinearBody(
        unsafe, {"acc", registers}, rejected, {}, &constants)));
    test::Check(rejected.str().empty());
    test::Check(
        !std::holds_alternative<std::size_t>(source::WriteLocalLexicalBody(
            unsafe, {"acc", registers}, rejected, {}, &constants)));
    test::Check(rejected.str().empty());
  }
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  const std::array<std::vector<std::uint8_t>, 4> calls{
      {{0x2d, 0xff, 0},
       {0x2e, 0xff, 0, 1},
       {0x2f, 0xff, 0, 2, 1},
       {0x30, 0xff, 0, 1, 1, 3}}};
  const std::array<std::vector<std::uint32_t>, 4> expected{
      {{}, {1}, {2, 1}, {1, 1, 3}}};
  for (std::size_t arity = 0; arity < calls.size(); ++arity) {
    const auto decoded = test::Body(calls[arity]).instructions[0].decoded;
    const auto lifted = analysis::LiftReceiverCall(decoded, registers.size());
    const auto& call = std::get<analysis::ReceiverCall>(lifted);
    test::Check(call.receiver.value == 0 && call.Count() == arity);
    for (std::size_t i = 0; i < arity; ++i)
      test::Check(call.At(i)->value == expected[arity][i]);
    for (std::size_t i = 0; i < decoded.operand_count; ++i) {
      auto malformed = decoded;
      malformed.operands[i].raw_bits = 256;
      test::Check(std::get<analysis::CallError>(
                      analysis::LiftReceiverCall(malformed, 300)) ==
                  analysis::CallError::kMalformedInstruction);
      malformed = decoded;
      malformed.operands[i].kind = abc::OperandKind::kString;
      test::Check(std::get<analysis::CallError>(
                      analysis::LiftReceiverCall(malformed, 4)) ==
                  analysis::CallError::kMalformedInstruction);
    }
    auto malformed = decoded;
    --malformed.operand_count;
    test::Check(std::get<analysis::CallError>(
                    analysis::LiftReceiverCall(malformed, 4)) ==
                analysis::CallError::kMalformedInstruction);
    test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                    decoded, 0)) == analysis::CallError::kRegisterRange);
    for (const bool store : {false, true}) {
      std::vector<std::uint8_t> code{0x60, 0};
      if (store) code.insert(code.end(), {0x61, 0});
      code.insert(code.end(), {0x42, 0, 0, 0});
      code.insert(code.end(), calls[arity].begin(), calls[arity].end());
      code.push_back(0x64);
      const auto candidate = test::Body(code);
      std::ostringstream rendered, ordered;
      test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
          candidate, {"acc", registers}, rendered, {}, &constants)));
      test::Check(std::holds_alternative<std::size_t>(source::WriteAcyclicBody(
          candidate, {"acc", registers}, ordered, {}, &constants)));
      test::Check(rendered.str() == ordered.str());
      std::ostringstream exact, short_output;
      test::Check(std::get<std::size_t>(source::WriteLinearBody(
                      candidate, {"acc", registers}, exact,
                      {rendered.str().size()}, &constants)) ==
                  rendered.str().size());
      test::Check(std::get<source::BodyEmitFailure>(
                      source::WriteLinearBody(
                          candidate, {"acc", registers}, short_output,
                          {rendered.str().size() - 1}, &constants))
                      .emit_error == source::EmitError::kOutputLimit);
      test::Check(short_output.str().empty());
      if (emit)
        std::cout << "export function " << (store ? "store" : "method") << arity
                  << "(object,a,b,c){let acc;\n"
                  << rendered.str() << "}\n";
    }
  }
  const auto last = test::Body({0x2e, 0, 255, 255}).instructions[0].decoded;
  test::Check(std::holds_alternative<analysis::ReceiverCall>(
      analysis::LiftReceiverCall(last, 256)));
  test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                  last, 255)) == analysis::CallError::kRegisterRange);
  for (const auto& bad :
       {test::Body({0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 1, 0x64}),
        test::Body({0x60, 0, 0x42, 0, 0, 0, 0xd5, 0x2d, 0, 0, 0x64}),
        test::Body({0x61, 0, 0x42, 0, 0, 0, 0x2d, 0, 0, 0x64}),
        test::Body({0x60, 0, 0x42, 0, 1, 0, 0x2d, 0, 0, 0x64})}) {
    std::ostringstream rejected;
    test::Check(
        std::holds_alternative<source::BodyEmitFailure>(source::WriteLinearBody(
            bad, {"acc", registers}, rejected, {}, &constants)));
    test::Check(rejected.str().empty());
  }
  const auto middle_entry =
      test::Body({0x60, 0, 0x4f, 8, 0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 0, 0x64});
  std::ostringstream rejected;
  const auto middle = source::WriteAcyclicBody(middle_entry, {"acc", registers},
                                               rejected, {}, &constants);
  test::Check(std::get<analysis::AccumulatorFailure>(middle).offset.value ==
              10);
  test::Check(rejected.str().empty());
  const auto property_entry =
      test::Body({0x60, 0, 0x4f, 4, 0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 0, 0x64});
  test::Check(std::holds_alternative<analysis::AccumulatorFailure>(
      source::WriteAcyclicBody(property_entry, {"acc", registers}, rejected, {},
                               &constants)));
  test::Check(rejected.str().empty());
  const auto loop =
      test::Body({0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 0, 0x51, 0xf7, 0x64});
  std::ostringstream repeated;
  test::Check(std::holds_alternative<std::size_t>(source::WriteBlockLoopBody(
      loop, {"acc", registers}, repeated, {}, &constants)));
  if (emit)
    std::cout << "export function loop(object){let acc;\n"
              << repeated.str() << "}\n";
  auto with_handler = body;
  with_handler.tries.push_back({});
  test::Check(std::get<source::BodyEmitFailure>(
                  source::WriteLinearBody(with_handler, {"acc", registers},
                                          rejected, {}, &constants))
                  .kind == source::BodyEmitError::kExceptionRegions);
  test::Check(rejected.str().empty());
  for (const auto& unsupported : {test::Body({0x73, 0, 0, 0})})
    test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                    unsupported.instructions[0].decoded, 4)) ==
                analysis::CallError::kUnsupportedOpcode);
  const auto empty_range = test::Body({0x31, 0, 0, 0});
  const auto empty_call = std::get<analysis::ReceiverCall>(
      analysis::LiftReceiverCall(empty_range.instructions[0].decoded, 4));
  test::Check(empty_call.Count() == 0 && !empty_call.At(0));
  const auto bad_range = test::Body({0x31, 0, 4, 0});
  test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                  bad_range.instructions[0].decoded, 4)) ==
              analysis::CallError::kRegisterRange);
  for (std::size_t operand = 0; operand < 3; ++operand) {
    auto malformed = bad_range.instructions[0].decoded;
    malformed.operands[operand].kind = abc::OperandKind::kString;
    test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                    malformed, 8)) ==
                analysis::CallError::kMalformedInstruction);
    malformed = bad_range.instructions[0].decoded;
    malformed.operands[operand].raw_bits = 256;
    test::Check(std::get<analysis::CallError>(analysis::LiftReceiverCall(
                    malformed, 300)) ==
                analysis::CallError::kMalformedInstruction);
  }
  const auto wide_name =
      test::Body({0x60, 0, 0x90, 0xff, 0xff, 0, 0, 0x2d, 0, 0, 0x64});
  std::ostringstream wide;
  test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
      wide_name, {"acc", registers}, wide, {}, &constants)));
  test::Check(wide.str() == output.str());
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < count; ++i)
      code.insert(code.end(), {0x60, 0, 0x42, 0, 0, 0, 0x2d, 0, 0});
    code.push_back(0x64);
    std::ostringstream source;
    test::Check(std::holds_alternative<std::size_t>(source::WriteLinearBody(
        test::Body(code), {"acc", registers}, source, {}, &constants)));
    constexpr std::string_view statement =
        "acc = object;\nacc = (object)[\"method\"]();\n";
    test::Check(source.str().size() ==
                count * statement.size() +
                    std::string_view("return acc;\n").size());
  }
  unlink(path);
}
