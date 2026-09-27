#include "tree_position_json.h"
#include "../src/source/instruction_sources.h"
#include "../src/source/family_emission.h"

#include <nlohmann/json.hpp>
#include <ostream>
#include <type_traits>

namespace abcd::tools {
using namespace source;
namespace {
nlohmann::json Origin(const TreeOrigin& origin) {
  return std::visit([](const auto& node) -> nlohmann::json {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, OriginalSite> || std::is_same_v<T, ElidedOrigin>) {
      nlohmann::json out{{"kind", std::is_same_v<T, OriginalSite> ? "original" : "elided_pure"},
                         {"site", node.site.value}, {"pc", node.pc.value}};
      out["original_pc"] = node.original ? nlohmann::json(node.original->value) : nlohmann::json(nullptr);
      return out;
    } else if constexpr (std::is_same_v<T, CombinedOrigin>) {
      return {{"kind", "combined"}, {"first_part", node.first_part}, {"part_count", node.part_count}};
    } else if constexpr (std::is_same_v<T, EdgeOrigin>) {
      return {{"kind", "edge"}, {"edge", node.edge.value}};
    } else if constexpr (std::is_same_v<T, LoopOrigin>) {
      return {{"kind", "loop"}, {"header", node.header.value}};
    } else if constexpr (std::is_same_v<T, EntryOrigin>) {
      return {{"kind", "method_entry"}};
    } else {
      static_assert(std::is_same_v<T, BindingOrigin>);
      return {{"kind", "binding"}, {"symbol", node.symbol.value}};
    }
  }, origin);
}
template <class Items, class Encode>
void Array(std::ostream& output, const Items& items, Encode encode) {
  output << '[';
  bool first = true;
  for (const auto& item : items) {
    if (!first) output << ',';
    first = false;
    output << encode(item);
  }
  output << ']';
}
}  // namespace
void WriteTreePositions(std::ostream& output, const FunctionTree& tree, const PrintedFunction& printed,
    const InstructionSources* sources) {
  // Stream tables independently: do not expand each node's transitive PC set,
  // or hold an unbounded JSON DOM duplicating the complete source tree.
  output << "{\"unit\":\"utf8_byte\",\"value_revision\":" << printed.value_revision.value
         << ",\"cfg_revision\":" << printed.cfg_revision.value << ",\"origins\":";
  Array(output, tree.origins, Origin);
  output << ",\"origin_parts\":";
  Array(output, tree.origin_parts, [](auto id) { return id.value; });
  output << ",\"parameters\":[";
  for (std::size_t i = 0; i < tree.parameters.size(); ++i) {
    if (i != 0) output << ',';
    const auto& parameter = tree.parameters[i];
    nlohmann::json entry{{"symbol", parameter.symbol.value},
        {"begin", printed.parameters[i].begin}, {"end", printed.parameters[i].end}};
    entry["initializer"] = parameter.initializer
        ? nlohmann::json(parameter.initializer->value) : nlohmann::json(nullptr);
    output << entry;
  }
  output << ']';
  const auto nodes = [&](std::string_view label, const auto& syntax, const auto& spans) {
    output << ",\"" << label << "\":[";
    for (std::size_t i = 0; i < syntax.size(); ++i) {
      if (i != 0) output << ',';
      output << nlohmann::json{{"origin", syntax[i].origin.value},
          {"begin", spans[i].begin}, {"end", spans[i].end}};
    }
    output << ']';
  };
  nodes("expressions", tree.expressions, printed.expressions);
  nodes("properties", tree.properties, printed.properties);
  nodes("statements", tree.statements, printed.statements);
  output << ",\"anchors\":[";
  for (std::size_t i = 0; i < tree.anchors.size(); ++i) {
    if (i != 0) output << ',';
    const auto& anchor = tree.anchors[i];
    nlohmann::json entry{{"origin", anchor.origin.value}, {"byte", printed.anchors[i]}};
    if (const auto* boundary = std::get_if<BlockBoundary>(&anchor.position)) {
      entry["kind"] = "block_boundary";
      entry["block"] = boundary->block.value; entry["index"] = boundary->index;
    } else entry["kind"] = "function_exit";
    output << entry;
  }
  output << "],\"operations\":";
  Array(output, tree.operations, [](const OpPlacement& operation) {
    return std::visit([&](auto node) {
      using T = decltype(node);
      static_assert(std::is_same_v<T, ExprId> || std::is_same_v<T, StmtId> ||
          std::is_same_v<T, PropertyId> || std::is_same_v<T, AnchorId>);
      constexpr std::string_view kind = std::is_same_v<T, ExprId> ? "expression" :
          std::is_same_v<T, StmtId> ? "statement" : std::is_same_v<T, PropertyId> ? "property" : "anchor";
      return nlohmann::json{{"operation", operation.operation.value}, {"kind", kind}, {"node", node.value}};
    }, operation.node);
  });
  output << ",\"signature\":";
  Array(output, tree.signature, [](const SignaturePlacement& placement) {
    return nlohmann::json{{"operation", placement.operation.value},
        {"parameter", placement.parameter.value}, {"origin", placement.origin.value}};
  });
  if (sources) {
    output << ",\"instruction_sources\":";
    Array(output, sources->sites, [&](const InstructionSource& source) {
      nlohmann::json entry{{"origin", source.origin.value}};
      std::visit([&](auto node) {
        using T = decltype(node);
        if constexpr (std::is_same_v<T, ElidedInstruction>) {
          entry["kind"] = "elided_pure"; entry["range"] = nullptr;
        } else {
          GeneratedRange range;
          if constexpr (std::is_same_v<T, ExprId>) {
            entry["kind"] = "expression"; range = printed.expressions[node.value];
          } else if constexpr (std::is_same_v<T, StmtId>) {
            entry["kind"] = "statement"; range = printed.statements[node.value];
          } else if constexpr (std::is_same_v<T, PropertyId>) {
            entry["kind"] = "property"; range = printed.properties[node.value];
          } else if constexpr (std::is_same_v<T, ParameterId>) {
            entry["kind"] = "parameter"; range = printed.parameters[node.value];
          } else {
            static_assert(std::is_same_v<T, AnchorId>);
            entry["kind"] = "anchor"; range = {printed.anchors[node.value], printed.anchors[node.value]};
          }
          entry["node"] = node.value;
          entry["range"] = {{"begin", range.begin}, {"end", range.end}};
        }
      }, source.target);
      return entry;
    });
  }
  output << '}';
}
bool WriteFamilyMembers(std::ostream& output, const FamilyEmission& emission) {
  const auto& family = emission.Family().Family();
  const auto& positions = emission.Positions();
  output << '[';
  for (std::size_t i = 0; i < family.members.size(); ++i) {
    if (i) output << ',';
    const auto serialized = SerializeRouteReceipt(emission.Receipts()[i]);
    const auto* receipt = std::get_if<std::string>(&serialized);
    if (!receipt) return false;
    output << "{\"owner\":" << family.members[i].tree.owner.value
           << ",\"begin\":" << positions.functions[i].begin
           << ",\"end\":" << positions.functions[i].end
           << ",\"route_receipt\":" << *receipt << ",\"local_positions\":";
    WriteTreePositions(output, family.members[i].tree, positions.members[i],
        &*family.members[i].sources);
    output << '}';
  }
  output << ']';
  return static_cast<bool>(output);
}
}  // namespace abcd::tools
