#include "../../../src/source/family_print.h"
#include "../../../src/source/instruction_sources.h"
#include "../../../tools/tree_position_json.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstdlib>
using namespace abcd;
using namespace abcd::source;
void Check(bool valid) { if (!valid) std::abort(); }
int main() {
  // Isolate emission/serialization. Decoded record tests separately validate
  // original instruction correspondence and module publication.
  FunctionTree tree{}; tree.owner={0}; tree.callable=CallableKind::kFunction; tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0}; tree.body_scope={1}; tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.origins={OriginalSite{{0},{0}},OriginalSite{{1},{1}}};
  tree.expressions={{TreeLiteral{UndefinedLiteral{}},{0}}};
  tree.statements={{BlockStmt{{1},0,1},{0}},{ReturnStmt{ExprId{0}},{1}}}; tree.children={{1}};
  InstructionSources sources; sources.input_sites=2;
  sources.occurrences={{{0},{0},ExprId{0}},{{1},{1},StmtId{1}}};
  ConstructedFamily family; family.root={0}; family.members={{tree,{},{}}};
  std::ostringstream source;
  const auto printed=std::get<PrintedFamilyDraft>(PrintModuleFamilyDraft(family,source));
  std::ostringstream mapping;
  tools::WriteTreePositions(mapping,tree,printed.members[0],&sources);
  const auto json=nlohmann::json::parse(mapping.str());
  for (const auto& item:json["instruction_sources"]) {
    Check(item["emission"]=="module_completion");
    Check(item["range"]["begin"]==0 && item["range"]["end"]==0);
  }
  Check(json["instruction_sources"][0]["kind"]=="expression");
  Check(json["instruction_sources"][1]["kind"]=="statement");
  Check(json["expressions"][0]["emission"]=="module_completion");
  Check(json["statements"][1]["emission"]=="module_completion");
  std::ostringstream normal, normal_map;
  const auto ordinary=std::get<PrintedFunction>(PrintFunctionTree(tree,normal));
  tools::WriteTreePositions(normal_map,tree,ordinary,&sources);
  const auto function=nlohmann::json::parse(normal_map.str());
  for (const auto& item:function["instruction_sources"]) {
    Check(item["emission"]=="tail_completion");
    Check(item["range"]["begin"]==item["range"]["end"]);
    Check(item["range"]["end"] < function["statements"][0]["end"]);
  }
  Check(function["expressions"][0]["emission"]=="tail_completion");
  Check(function["statements"][1]["emission"]=="tail_completion");
  tree.callable=CallableKind::kAsyncGenerator;
  std::ostringstream asynchronous, async_map;
  const auto retained=std::get<PrintedFunction>(PrintFunctionTree(tree,asynchronous));
  tools::WriteTreePositions(async_map,tree,retained,&sources);
  const auto awaited=nlohmann::json::parse(async_map.str());
  for (const auto& item:awaited["instruction_sources"]) {
    Check(!item.contains("emission"));
    Check(item["range"]["begin"] < item["range"]["end"]);
  }
}
