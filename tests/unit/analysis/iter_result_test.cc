#include "abcd/analysis/iter_result.h"

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

#include "abcd/analysis/accumulator_flow.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/register_value_flow.h"
#include "abcd/source/acyclic_body.h"
#include "abcd/source/linear_body.h"
#include "analysis/ssa_fixture.h"

int main(int argc, char** argv) {
  using namespace abcd;
  const auto body = test::Body({0xb2, 0, 1, 0x64});
  const auto& decoded = body.instructions.front().decoded;
  const auto creation = std::get<analysis::IterResult>(
      analysis::LiftIterResult(decoded, 2));
  test::Check(creation.value.value == 0 && creation.done.value == 1);
  const auto effect = std::get<analysis::RegisterEffect>(
      analysis::LiftRegisterEffect(decoded, 2));
  test::Check(effect.read_count == 2 && effect.write &&
              std::get<abc::RegisterId>(effect.reads[0]).value == 0 &&
              std::get<abc::RegisterId>(effect.reads[1]).value == 1 &&
              std::holds_alternative<analysis::Accumulator>(*effect.write));
  const auto graph = std::get<analysis::NormalFlow>(
      analysis::BuildNormalFlow(body));
  test::Check(std::holds_alternative<analysis::AccumulatorFlow>(
      analysis::AnalyzeAcyclicAccumulator(body, graph, 2)));
  analysis::IdfQuota work{UINT64_MAX};
  const auto ssa = std::get<analysis::RegisterSsa>(
      analysis::RegisterSsa::Build(body, graph, 2, work));
  const auto values = std::get<analysis::RegisterValueFlow>(
      analysis::RegisterValueFlow::Build(body, ssa, 2, {}, work));
  test::Check(values.At(*ssa.Write({0})) ==
              analysis::ValueKinds::Language());
  const std::array<std::string_view, 2> registers{"value", "done"};
  const source::ScalarNames names{"acc", registers};
  std::ostringstream output, ordered;
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteLinearBody(body, names, output)));
  test::Check(output.str() ==
              "acc = {value: value, done: done === true};\nreturn acc;\n");
  test::Check(std::holds_alternative<std::size_t>(
      source::WriteAcyclicBody(body, names, ordered)));
  test::Check(ordered.str() == output.str());
  if (argc == 2 && std::string_view(argv[1]) == "--emit")
    std::cout << "export function result(value, done) {let acc;\n"
              << output.str() << "}\n";
  std::ostringstream limited;
  const auto short_result = source::WriteLinearBody(
      body, names, limited, {output.str().size() - 1});
  test::Check(std::get<source::BodyEmitFailure>(short_result).emit_error ==
              source::EmitError::kOutputLimit && limited.str().empty());
  auto malformed = decoded;
  malformed.operand_count = 1;
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(malformed, 2)) ==
              analysis::IterResultError::kMalformedInstruction);
  malformed = decoded;
  malformed.operands[1].kind = abc::OperandKind::kImmediate;
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(malformed, 2)) ==
              analysis::IterResultError::kMalformedInstruction);
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(decoded, 1)) ==
              analysis::IterResultError::kRegisterRange);
  test::Check(std::get<analysis::IterResultError>(
                  analysis::LiftIterResult(body.instructions[1].decoded, 2)) ==
              analysis::IterResultError::kUnsupportedOpcode);
  char path[] = "/tmp/abcd-iter-result-XXXXXX";
  const auto fd = mkstemp(path);
  test::Check(fd >= 0 && close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  test::Check(unlink(path) == 0);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.profile = abc::ProfileId::kDynamic1206;
  abc::MetadataIndex index;
  const source::MethodConstants historical{file, header, index, {0}, {}};
  std::ostringstream refused;
  test::Check(!std::holds_alternative<std::size_t>(
      source::WriteLinearBody(body, names, refused, {}, &historical)));
  test::Check(refused.str().empty());
}
