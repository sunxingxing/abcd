#include <cstdlib>
#include <sstream>
#include <vector>

#include "record_literal.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
std::vector<abcd::source::RecordLiteralNode> Chain(std::size_t count) {
  using namespace abcd::source;
  std::vector<RecordLiteralNode> nodes(count);
  for (std::size_t i = 0; i < count; ++i) {
    nodes[i].kind = RecordLiteralKind::kObject;
    if (i + 1 < count)
      nodes[i].fields.push_back({u"next", RecordNodeRef{i + 1}});
  }
  return nodes;
}
}  // namespace

int main() {
  using namespace abcd::source;
  for (const std::size_t count : {200, 201, 256}) {
    const auto nodes = Chain(count);
    std::ostringstream output;
    const auto result = WriteRecordLiteral(nodes, 0, output, {});
    Check(std::holds_alternative<std::size_t>(result));
    Check(std::get<std::size_t>(result) == output.str().size());
  }
  const auto deep = Chain(257);
  std::ostringstream rejected_depth;
  Check(std::get<RecordLiteralError>(WriteRecordLiteral(
            deep, 0, rejected_depth, {})) ==
        RecordLiteralError::kDepthLimit);
  auto reverse = Chain(2);
  reverse[1].fields.push_back({u"back", RecordNodeRef{0}});
  std::ostringstream rejected_cycle;
  Check(std::get<RecordLiteralError>(WriteRecordLiteral(
            reverse, 0, rejected_cycle, {})) ==
        RecordLiteralError::kInvalidGraph);
  std::ostringstream rejected_size;
  Check(std::get<abcd::source::EmitError>(WriteRecordLiteral(
            Chain(1), 0, rejected_size, {1})) ==
        abcd::source::EmitError::kOutputLimit);
}
