#include <sstream>

#include "abcd/source/local_lexical_body.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyLocalLexicalScaling() {
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < count; ++i)
      code.insert(code.end(), {0xfd, 2, 0xff, 0xff, 0x60, 0, 0xfd, 0x0d, 0, 0,
                               0xfe, 0xff, 0xfd, 0x0c, 0, 0, 0xfe, 0xff, 0x69});
    code.push_back(0x64);
    const auto body = Body(code);
    const auto graph =
        std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(body));
    const auto environments = std::get<analysis::EnvironmentFlow>(
        analysis::AnalyzeEnvironments(body, graph, 1));
    analysis::IdfQuota quota{UINT64_MAX};
    const auto plan = std::get<analysis::LinearLexicals>(
        analysis::LinearLexicals::Build(body, environments, 1, quota));
    Check(plan.Cells().size() == count);
    Check(plan.work.instructions == body.instructions.size());
    Check(plan.work.units < 200 * body.instructions.size());
    const std::array<std::string_view, 1> registers{"r0"};
    std::ostringstream source;
    const auto result =
        source::WriteLocalLexicalBody(body, {"acc", registers}, source);
    Check(std::get<std::size_t>(result) == source.str().size());
    Check(source.str().size() < 150 * count);
  }
}
}  // namespace abcd::test
