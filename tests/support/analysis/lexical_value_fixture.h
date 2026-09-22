#ifndef ABCD_TEST_SUPPORT_LEXICAL_VALUE_FIXTURE_H_
#define ABCD_TEST_SUPPORT_LEXICAL_VALUE_FIXTURE_H_

#include "abcd/analysis/environment_escape.h"
#include "abcd/analysis/register_value_flow.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
struct LexicalValueFixture {
  using RegisterSsa = analysis::RegisterSsa;
  using EnvironmentFlow = analysis::EnvironmentFlow;
  using EnvironmentEscape = analysis::EnvironmentEscape;
  explicit LexicalValueFixture(abc::MethodBody input, std::uint64_t frame = 4)
      : body(std::move(input)),
        graph(std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(body))),
        frame_size(frame),
        ssa(std::get<RegisterSsa>(
            RegisterSsa::Build(body, graph, frame_size, quota))),
        environments(std::get<EnvironmentFlow>(
            analysis::AnalyzeEnvironments(body, graph, frame_size))),
        escape(std::get<EnvironmentEscape>(EnvironmentEscape::Build(
            body, ssa, environments, frame_size, quota))) {}
  analysis::RegisterValueFlow Values(
      std::span<const analysis::EntryValueFact> entries = {}) {
    return std::get<analysis::RegisterValueFlow>(
        analysis::RegisterValueFlow::BuildWithLexicals(
            body, ssa, frame_size, entries, {environments, escape}, quota));
  }
  abc::MethodBody body;
  analysis::NormalFlow graph;
  std::uint64_t frame_size;
  analysis::IdfQuota quota{UINT64_MAX};
  RegisterSsa ssa;
  EnvironmentFlow environments;
  EnvironmentEscape escape;
};
void VerifyLexicalValueOracle();
void VerifyLexicalValueScaling();
}  // namespace abcd::test
#endif  // ABCD_TEST_SUPPORT_LEXICAL_VALUE_FIXTURE_H_
