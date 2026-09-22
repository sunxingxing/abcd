#include "abcd/source/local_lexical_body.h"

#include <sstream>

#include "analysis/ssa_fixture.h"

using namespace abcd;
using namespace abcd::test;
namespace abcd::test {
void VerifyLocalLexicalScaling();
}
namespace {
class FailingBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};
}  // namespace
int main(int argc, char** argv) {
  const auto body =
      Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x3c, 0, 0xfe, 9, 0xff, 0xff, 0x64});
  const std::array<std::string_view, 1> registers{"r0"};
  std::ostringstream output;
  const auto result =
      source::WriteLocalLexicalBody(body, {"acc", registers}, output);
  Check(std::holds_alternative<std::size_t>(result));
  Check(output.str() ==
        "let $lex0_0;\nacc = r0;\n$lex0_0 = acc;\n"
        "acc = $lex0_0;\nreturn acc;\n");
  const std::array<std::string_view, 2> two{"r0", "r1"};
  const auto emit = [&](const abc::MethodBody& input) {
    std::ostringstream text;
    Check(std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(input, {"acc", two}, text)));
    return text.str();
  };
  const auto parent = emit(Body(
      {0x09, 1, 0x60, 0, 0x3d, 0, 0x09, 1, 0x60, 1, 0x3d, 0, 0x3c, 1, 0x64}));
  const auto child = emit(Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x09, 1, 0x60, 1,
                                0x3d, 0, 0x3c, 0, 0x69, 0x64}));
  const auto after_pop = emit(Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x09, 1, 0x60, 1,
                                    0x3d, 0, 0x69, 0x3c, 0, 0x64}));
  const auto deprecated =
      emit(Body({0x09, 1, 0x70, 0xfc, 0x20, 0, 0, 0x3c, 0, 0x64}));
  const auto wide =
      emit(Body({0xfd, 2, 0xff, 0xff, 0x60, 0, 0xfd, 0x0d, 0, 0, 0xfe, 0xff,
                 0xfd, 0x0c, 0, 0, 0xfe, 0xff, 0x64}));
  const auto effect =
      emit(Body({0x09, 1, 0x60, 1, 0x3d, 0, 0x60, 0, 0x29, 0, 0x3c, 0, 0x64}));
  const auto thrown = emit(Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x3c, 0, 0xfe, 0}));
  const auto diagnostic =
      emit(Body({0x70, 0x61, 1, 0xfe, 6, 0, 1, 0x60, 0, 0x64}));
  const auto omitted = emit(Body({0x09, 1, 0xfc, 0, 0x61, 1, 0x70, 0x61, 1,
                                  0x60, 0, 0x61, 1, 0x60, 1, 0x64}));
  if (argc == 2 && std::string_view(argv[1]) == "--emit") {
    const auto roundtrip = output.str();
    const std::array<std::pair<std::string_view, std::string_view>, 10>
        functions{{{"roundtrip", roundtrip},
                   {"parent", parent},
                   {"child", child},
                   {"afterPop", after_pop},
                   {"deprecated", deprecated},
                   {"wide", wide},
                   {"effect", effect},
                   {"thrown", thrown},
                   {"diagnostic", diagnostic},
                   {"omitted", omitted}}};
    for (const auto& [name, text] : functions)
      std::cout << "export function " << name << "(r0,r1){let acc;\n"
                << text << "}\n";
    return 0;
  }
  for (const auto code :
       {std::initializer_list<std::uint8_t>{0x09, 1, 0x3c, 0, 0x64},
        {0x3c, 0, 0x64},
        {0x09, 1, 0x64},
        {0x09, 1, 0x61, 0, 0, 0x60, 0, 0x64},
        {0, 0x70, 0x64},
        {0x70, 0xfe, 9, 0, 0, 0x65},
        {0x09, 1, 0x61, 0, 0xfe, 6, 0, 1, 0x65},
        {0x69, 0x65},
        {0x09, 1, 0x69, 0x69, 0x65},
        {0xb6, 1, 0, 0, 0x65},
        {0xfd, 3, 1, 0, 0, 0, 0x65},
        {0x09, 1, 0x33, 0, 0, 0, 0, 0x65},
        {0x09, 1, 0x3d, 0, 0x65},
        {0x09, 1, 0x60, 0, 0x29, 0, 0x3d, 0, 0x65},
        {0x09, 1, 0x60, 0, 0x4f, 2, 0x65},
        {0x09, 1, 0x65, 0},
        {0x09, 1, 0x60, 0}}) {
    std::ostringstream rejected;
    Check(!std::holds_alternative<std::size_t>(
        source::WriteLocalLexicalBody(Body(code), {"acc", two}, rejected)));
    Check(rejected.str().empty());
  }
  for (const std::string_view name : {"$lex0_0", "$lex", "bad name"}) {
    std::ostringstream rejected;
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLocalLexicalBody(body, {name, registers}, rejected))
              .emit_error == source::EmitError::kInvalidBinding);
    Check(rejected.str().empty());
  }
  source::LocalLexicalBudget budget;
  budget.output.bytes = output.str().size() - 1;
  std::ostringstream limited;
  Check(std::get<source::BodyEmitFailure>(
            source::WriteLocalLexicalBody(body, {"acc", registers}, limited,
                                          budget))
            .emit_error == source::EmitError::kOutputLimit);
  Check(limited.str().empty());
  ++budget.output.bytes;
  Check(std::get<std::size_t>(source::WriteLocalLexicalBody(
            body, {"acc", registers}, limited, budget)) == output.str().size());
  Check(limited.str() == output.str());
  for (const bool exceptions : {false, true}) {
    FailingBuffer buffer;
    std::ostream stream(&buffer);
    if (exceptions) stream.exceptions(std::ios::badbit | std::ios::failbit);
    Check(std::get<source::BodyEmitFailure>(
              source::WriteLocalLexicalBody(body, {"acc", registers}, stream))
              .emit_error == source::EmitError::kWrite);
  }
  const auto graph =
      std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(body));
  const auto environments = std::get<analysis::EnvironmentFlow>(
      analysis::AnalyzeEnvironments(body, graph, 1));
  analysis::IdfQuota quota{UINT64_MAX};
  const auto plan = std::get<analysis::LinearLexicals>(
      analysis::LinearLexicals::Build(body, environments, 1, quota));
  for (std::uint64_t work = 0; work < plan.work.units; ++work) {
    analysis::IdfQuota short_quota{work};
    Check(
        std::get<analysis::LinearLexicalFailure>(
            analysis::LinearLexicals::Build(body, environments, 1, short_quota))
            .kind == analysis::LinearLexicalError::kWorkLimit);
    Check(short_quota.Remaining() == 0);
  }
  analysis::IdfQuota exact{plan.work.units};
  Check(std::get<analysis::LinearLexicals>(
            analysis::LinearLexicals::Build(body, environments, 1, exact,
                                            {plan.work.peak_bytes}))
            .Cells()
            .size() == 1);
  Check(exact.Remaining() == 0);
  Check(std::get<analysis::LinearLexicalFailure>(
            analysis::LinearLexicals::Build(body, environments, 1, quota,
                                            {plan.work.peak_bytes - 1}))
            .kind == analysis::LinearLexicalError::kResourceLimit);
  auto owned = [] {
    const auto input = Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x3c, 0, 0x64});
    const auto graph =
        std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(input));
    const auto flow = std::get<analysis::EnvironmentFlow>(
        analysis::AnalyzeEnvironments(input, graph, 1));
    analysis::IdfQuota work{UINT64_MAX};
    return std::get<analysis::LinearLexicals>(
        analysis::LinearLexicals::Build(input, flow, 1, work));
  }();
  auto moved = std::move(owned);
  Check(owned.Cells().empty() && owned.Statements().empty() &&
        owned.AllocatedBytes() == 0);
  owned = std::move(moved);
  Check(moved.Cells().empty() && owned.Cells().size() == 1);
  Check(std::get<analysis::LocalStore>(owned.Statements()[2]).cell.value == 0);
  VerifyLocalLexicalScaling();
}
