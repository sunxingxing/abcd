#include "../src/source/tail_completion.h"
#include "../src/source/nullish_syntax.h"
#include "tree_position_json.h"
#include "../src/source/instruction_sources.h"
#include "../src/source/family_emission.h"
#include "abcd/source/printed_source.h"

#include <nlohmann/json.hpp>
#include <ostream>
#include <stdexcept>
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
    } else if constexpr (std::is_same_v<T, TryOrigin>) {
      return {{"kind", "try_region"}, {"region", node.region.value}};
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
  const auto nullish_result = source::PlanNullishSyntax(tree);
  if (!std::holds_alternative<source::NullishSyntax>(nullish_result)) {
    // A failed projection cannot supply truthful source-position metadata.
    output.setstate(std::ios::failbit); return;
  }
  const auto& nullish = std::get<source::NullishSyntax>(nullish_result);
  const auto emission = [&](NullishRoot root) -> std::string_view {
    switch (nullish.Form(root)) {
      case NullishForm::kOptionalMember: return "optional_access";
      case NullishForm::kOptionalCall: return "optional_call";
      case NullishForm::kFallback: return "nullish_fallback";
    }
    throw std::logic_error("invalid nullish form");
  };
  const auto projection_metadata=[&](nlohmann::json& item,NullishRoot root) {
    item["emission"]=emission(root);
    std::visit([&](auto id) {
      item["projection"]=id.value;
      if constexpr(std::is_same_v<decltype(id),StmtId>) item["projection_kind"]="statement";
    },root);
  };
  // Stream tables independently: do not expand each node's transitive PC set,
  // or hold an unbounded JSON DOM duplicating the complete source tree.
  output << "{\"unit\":\"utf8_byte\",\"source_contract\":"
         << nlohmann::json(SourceContractName(printed.identity.contract))
         << ",\"value_revision\":" << printed.value_revision.value
         << ",\"cfg_revision\":" << printed.cfg_revision.value << ",\"origins\":";
  Array(output, tree.origins, Origin);
  switch (tree.invocation) {
    case InvocationForm::kDeclared: output << ",\"invocation_form\":\"declared\""; break;
    case InvocationForm::kArrowMethod:
      output << ",\"invocation_form\":\"arrow_method\",\"callable_text_policy\":\"recovered\""; break;
  }
  if (printed.argument_snapshot_binding) {
    const auto& binding = *printed.argument_snapshot_binding;
    std::string_view abi;
    switch (binding.abi) {
      case ArgumentSnapshotAbi::kV7CurrentSlots: abi = "v7_current_slots"; break;
    }
    output << ",\"argument_snapshot_binding\":" << nlohmann::json{
        {"policy", "explicit_host"}, {"name", std::string(binding.name.begin(), binding.name.end())},
        {"abi", abi}, {"contract", SourceContractName(printed.identity.contract)}};
  }
  if (printed.symbol_binding) {
    const auto& name = printed.symbol_binding->name;
    // Trusted binding names have passed the shared ASCII identifier boundary.
    output << ",\"symbol_binding\":" << nlohmann::json{
        {"policy", "explicit_host"}, {"name", std::string(name.begin(), name.end())},
        {"contract", SourceContractName(printed.identity.contract)}};
  }
  if (printed.realm_binding) {
    const auto& binding = *printed.realm_binding;
    std::string_view policy;
    switch (binding.policy) {
      case RealmBindingPolicy::kExplicitHost: policy = "explicit_host"; break;
      case RealmBindingPolicy::kStableGlobalThis: policy = "stable_global_this"; break;
    }
    // Resolver admits ASCII identifiers only; this is a serialization boundary.
    output << ",\"realm_binding\":" << nlohmann::json{
        {"policy", policy}, {"name", std::string(binding.name.begin(), binding.name.end())},
        {"contract", SourceContractName(printed.identity.contract)}};
  }
  if (printed.module_binding) {
    output << ",\"module_binding\":" << nlohmann::json{
        {"record", printed.module_binding->record.value}, {"owner", printed.module_binding->owner.value}};
  }
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
  const auto completion = printed.fragment == FunctionFragment::kStaticFields ||
      printed.fragment == FunctionFragment::kImplicitConstructor ? std::nullopt : ImplicitTailCompletion(tree);
  const auto completion_label = printed.fragment == FunctionFragment::kModuleBody ? "module_completion" : printed.fragment == FunctionFragment::kScriptBody ? "script_completion" : "tail_completion";
  const auto nodes = [&](std::string_view label, const auto& syntax, const auto& spans, const auto& extents) {
    output << ",\"" << label << "\":[";
    for (std::size_t i = 0; i < syntax.size(); ++i) {
      if (i != 0) output << ',';
      nlohmann::json item{{"origin", syntax[i].origin.value},
          {"begin", spans[i].begin}, {"end", spans[i].end}};
      // The printer's own representation decision: an omission boundary is a
      // zero-width coordinate, not a lost span (e.g. a folded branch statement).
      if (i < extents.size() && extents[i] == source::GeneratedExtent::kBoundary) item["extent"] = "boundary";
      if (completion && ((label == "expressions" && completion->value == ExprId{static_cast<std::uint32_t>(i)}) ||
          (label == "statements" && completion->statement == StmtId{static_cast<std::uint32_t>(i)})))
        item["emission"] = completion_label;
      std::optional<NullishRoot> omitted;
      const auto index=static_cast<std::uint32_t>(i);
      if(label=="expressions") omitted=nullish.Omission(ExprId{index});
      else if(label=="properties") omitted=nullish.Omission(PropertyId{index});
      else if(label=="statements") omitted=nullish.Omission(StmtId{index});
      if(omitted) projection_metadata(item,*omitted);
      output << item;
    }
    output << ']';
  };
  nodes("expressions", tree.expressions, printed.expressions, printed.expression_extents);
  nodes("properties", tree.properties, printed.properties, printed.property_extents);
  nodes("statements", tree.statements, printed.statements, printed.statement_extents);
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
    Array(output, sources->occurrences, [&](const InstructionSource& source) {
      nlohmann::json entry{{"site", source.site.value}, {"origin", source.origin.value}};
      std::visit([&](auto node) {
        using T = decltype(node);
        if constexpr (std::is_same_v<T, ElidedInstruction>) {
          entry["kind"] = "elided_pure"; entry["range"] = nullptr;
        } else if constexpr (std::is_same_v<T, UnreachableInstruction>) {
          entry["kind"] = "unreachable"; entry["range"] = nullptr;
        } else if constexpr (std::is_same_v<T, CleanupInstruction>) {
          entry["kind"] = "environment_cleanup"; entry["range"] = nullptr;
          switch (node.kind) {
            case CleanupInstructionKind::kPop: entry["cleanup"] = "pop"; break;
            case CleanupInstructionKind::kRethrow: entry["cleanup"] = "rethrow"; break;
          }
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
          if constexpr (std::is_same_v<T, ExprId> || std::is_same_v<T, StmtId>) {
            const bool omitted = [&] {
              if constexpr (std::is_same_v<T, ExprId>) return completion && completion->value == node;
              else return completion && completion->statement == node;
            }();
            if (omitted) entry["emission"] = completion_label;
          }
          if constexpr(std::is_same_v<T,ExprId> || std::is_same_v<T,StmtId> || std::is_same_v<T,PropertyId>)
            if(const auto root=nullish.Omission(node)) projection_metadata(entry,*root);
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
