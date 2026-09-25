#ifndef ABCD_SOURCE_VALUE_STAGE_H_
#define ABCD_SOURCE_VALUE_STAGE_H_

#include "abcd/analysis/value_import.h"
#include "abcd/source/source_route.h"

namespace abcd::source {
struct ValueMethod {
  MethodRouteReceipt receipt;
  std::optional<analysis::ValueProgram> graph;
};
// Owns no file readers. Identity/frame/body must originate from one validated
// method selection. Builds fresh input analysis/proof, never a legacy AST.
// Budget includes simultaneous body payload, input IR/proof, working graph and
// its transactional candidate. Receipt distinguishes import from verification;
// caller separately admits file/index/metadata. No source is emitted here.
[[nodiscard]] ValueMethod BuildValueStage(
    RouteInputIdentity identity, abc::EntityOffset method,
    abc::EntityOffset record, const abc::MethodBody& body,
    const abc::FrameLayout& frame, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
