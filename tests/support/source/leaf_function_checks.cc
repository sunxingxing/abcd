#include "source/leaf_function_checks.h"

#include <array>
#include <sstream>

#include "analysis/ssa_fixture.h"
#include "leaf_sources.h"

namespace abcd::test {
void CheckLeafFunctions(const corpus::AbcFile& file, const abc::Header& header,
                        abc::MetadataIndex& index,
                        const abc::MethodLookup& lookup,
                        abc::CodeInfo large_code, bool emit) {
  auto& target = index.methods[1];
  const auto saved_code = target.code;
  const auto creation = Body({0x33, 0, 0, 0, 1}).instructions[0].decoded;
  const source::BaseFunction parent{
      std::get<analysis::FunctionCreation>(
          analysis::LiftFunctionCreation(creation)),
      std::get<abc::MethodAttributes>(
          abc::DecodeMethodAttributes(target.access_flags, header.profile)),
      std::get<abc::FrameLayout>(
          abc::BuildFrameLayout(0, 4, abc::DecodeCallFlags({}))),
      "Factory"};
  const source::MethodConstants constants{
      file, header, index, index.methods[0].offset, {}};
  // Both regional slots alias one method. Cached source bytes may be shared;
  // the two evaluated function objects must compare unequal.
  const auto distinct =
      Body({0x33, 0, 0, 0, 1, 0x61, 3, 0x33, 0, 1, 0, 1, 0x28, 0, 3, 0x64});
  const auto prepared =
      source::LeafSources::Build(distinct, constants, lookup, {});
  const auto& sources = std::get<source::LeafSources>(prepared);
  Check(sources.Facts().size() == 2);
  Check(sources.Source(0).data() == sources.Source(1).data());
  const auto render = [&](const abc::MethodBody& body,
                          source::LocalLexicalBudget budget = {}) {
    std::ostringstream output;
    const auto result = source::WriteBaseFunction(parent, body, output, budget,
                                                  &constants, &lookup);
    if (!std::holds_alternative<std::size_t>(result))
      Check(output.str().empty());
    return std::pair(result, output.str());
  };
  const auto [different_result, different_source] = render(distinct);
  Check(std::holds_alternative<std::size_t>(different_result));
  const auto preserved =
      Body({0x09, 1, 0x60, 3, 0x3d, 0, 0x33, 0, 0, 0, 1, 0x3c, 0, 0x64});
  const auto [preserved_result, preserved_source] = render(preserved);
  Check(std::holds_alternative<std::size_t>(preserved_result));
  const auto reverse =
      Body({0x33, 0, 2, 0, 1, 0x61, 3, 0x33, 0, 0, 0, 1, 0x60, 3, 0x64});
  const auto reverse_build =
      source::LeafSources::Build(reverse, constants, lookup, {});
  const auto& reverse_sources = std::get<source::LeafSources>(reverse_build);
  Check(reverse_sources.Source(0).find("function Other(") !=
        std::string_view::npos);
  Check(reverse_sources.Source(1).find("function Recovered(") !=
        std::string_view::npos);
  const auto [reverse_result, reverse_source] = render(reverse);
  Check(std::holds_alternative<std::size_t>(reverse_result));
  if (emit) {
    std::cout << "export const different = " << different_source << ";\n";
    std::cout << "export const preserved = " << preserved_source << ";\n";
    std::cout << "export const reverse = " << reverse_source << ";\n";
  }
  source::LocalLexicalBudget budget;
  budget.output.bytes = different_source.size();
  Check(std::holds_alternative<std::size_t>(render(distinct, budget).first));
  --budget.output.bytes;
  Check(!std::holds_alternative<std::size_t>(render(distinct, budget).first));
  std::size_t low = 0, high = source::LocalLexicalBudget{}.bytes;
  while (low < high) {
    budget = {};
    budget.bytes = low + (high - low) / 2;
    if (std::holds_alternative<std::size_t>(render(distinct, budget).first))
      high = budget.bytes;
    else
      low = budget.bytes + 1;
  }
  budget = {};
  budget.bytes = low;
  Check(std::holds_alternative<std::size_t>(render(distinct, budget).first));
  --budget.bytes;
  Check(!std::holds_alternative<std::size_t>(render(distinct, budget).first));

  // Direct facts are a proof boundary, not a permission for arbitrary opcodes.
  const auto graph =
      std::get<analysis::NormalFlow>(analysis::BuildNormalFlow(distinct));
  const auto env = std::get<analysis::EnvironmentFlow>(
      analysis::AnalyzeEnvironments(distinct, graph, 4));
  const auto plan = [&](std::span<const analysis::IndependentCreation> facts) {
    analysis::IdfQuota work{source::LocalLexicalBudget{}.work};
    return analysis::LinearLexicals::Build(distinct, env, 4, work, {}, facts);
  };
  const std::array<analysis::IndependentCreation, 2> valid{{{{0}}, {{7}}}};
  Check(std::holds_alternative<analysis::LinearLexicals>(plan(valid)));
  const std::array<analysis::IndependentCreation, 2> duplicate{{{{0}}, {{0}}}};
  const std::array<analysis::IndependentCreation, 2> reversed{{{{7}}, {{0}}}};
  const std::array<analysis::IndependentCreation, 2> non_creation{
      {{{0}}, {{5}}}};
  const std::array<analysis::IndependentCreation, 3> extra{
      {{{0}}, {{7}}, {{999}}}};
  using Facts = std::span<const analysis::IndependentCreation>;
  for (const auto facts : {Facts(valid).first(1), Facts(duplicate),
                           Facts(reversed), Facts(non_creation)})
    Check(std::holds_alternative<analysis::LinearLexicalFailure>(plan(facts)));
  Check(std::holds_alternative<analysis::LinearLexicalFailure>(plan(extra)));

  target.code = abc::CodeInfo{{700}, {704}, 0, 4, 3, 0};
  Check(std::get<source::BodyEmitFailure>(render(distinct).first).kind ==
        source::BodyEmitError::kCreation);
  target.code = abc::CodeInfo{{720}, {724}, 0, 4, 6, 0};
  Check(std::get<source::BodyEmitFailure>(render(distinct).first).kind ==
        source::BodyEmitError::kCreation);
  target.code = large_code;
  budget = {};
  budget.work = 0;
  const auto exhausted =
      std::get<source::BodyEmitFailure>(render(preserved, budget).first);
  Check(exhausted.kind == source::BodyEmitError::kWorkLimit);
  Check(exhausted.offset.value == 6);
  for (const auto count : {200u, 201u, 4096u, 65536u}) {
    abc::MethodBody many;
    many.instructions.reserve(count + 1);
    for (std::uint32_t i = 0; i < count; ++i) {
      auto site = distinct.instructions.front();
      site.offset.value = i * site.decoded.size;
      many.instructions.push_back(site);
    }
    auto end = Body({0x64}).instructions.front();
    end.offset.value = count * creation.size;
    many.instructions.push_back(end);
    Check(std::holds_alternative<std::size_t>(render(many).first));
  }
  target.code = saved_code;
}
}  // namespace abcd::test
