#include "value_source_inspect.h"

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>
#include <type_traits>

#include "abcd/source/tree_printer.h"

namespace {
using namespace abcd;
using namespace abcd::source;
enum class InspectionError : std::uint8_t {
  kLookup, kRoles, kSites, kMissingSite, kAmbiguousSite, kPrepare, kPrint,
  kChanged
};

template <class T> RouteFailureClass IndexClass(const T& error) {
  using Class = RouteFailureClass;
  if constexpr (std::is_same_v<T, abc::BodyFailure>) {
    if (error.kind == abc::BodyError::kResourceLimit) return Class::kMemoryLimit;
    return error.kind == abc::BodyError::kUnsupportedProfile ? Class::kUnsupported : Class::kInvalidInput;
  } else if constexpr (std::is_same_v<T, abc::LiteralFailure>) {
    switch (error.kind) {
      case abc::LiteralError::kResourceLimit: return Class::kMemoryLimit;
      case abc::LiteralError::kUnsupportedProfile:
      case abc::LiteralError::kUnsupportedTag:
      case abc::LiteralError::kPackedArray: return Class::kUnsupported;
      case abc::LiteralError::kFileChanged:
      case abc::LiteralError::kRead:
      case abc::LiteralError::kTruncated:
      case abc::LiteralError::kReference:
      case abc::LiteralError::kOddCount:
      case abc::LiteralError::kUnknownTag: return Class::kInvalidInput;
    }
  } else if constexpr (std::is_same_v<T, analysis::MethodRoleError>) {
    switch (error) {
      case analysis::MethodRoleError::kResourceLimit: return Class::kMemoryLimit;
      case analysis::MethodRoleError::kLegacyLiteral:
      case analysis::MethodRoleError::kNestedLiteral: return Class::kUnsupported;
    }
  } else if constexpr (std::is_same_v<T, analysis::ClassSiteError>) {
    switch (error) {
      case analysis::ClassSiteError::kResourceLimit: return Class::kMemoryLimit;
      case analysis::ClassSiteError::kWorkLimit: return Class::kWorkLimit;
      case analysis::ClassSiteError::kMalformedCreation:
      case analysis::ClassSiteError::kTargetMissing: return Class::kInvalidInput;
    }
  } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
    if (error == abc::MetadataError::kResourceLimit) return Class::kMemoryLimit;
    return error == abc::MetadataError::kUnsupportedProfile ? Class::kUnsupported : Class::kInvalidInput;
  } else if constexpr (std::is_same_v<T, abc::MethodLookupError> ||
                       std::is_same_v<T, abc::ClassBufferError>) {
    return error == T::kResourceLimit ? Class::kMemoryLimit : Class::kInvalidInput;
  } else {
    static_assert(std::is_same_v<T, abc::MethodTargetError>);
    return Class::kInvalidInput;
  }
  return Class::kInvariantViolation;
}

RouteFailureClass PreparationClass(FunctionPrepareError error) {
  switch (error) {
    case FunctionPrepareError::kResourceLimit: return RouteFailureClass::kMemoryLimit;
    case FunctionPrepareError::kWorkLimit: return RouteFailureClass::kWorkLimit;
    case FunctionPrepareError::kBody:
    case FunctionPrepareError::kFileChanged: return RouteFailureClass::kInvalidInput;
    case FunctionPrepareError::kContext:
    case FunctionPrepareError::kValues:
    case FunctionPrepareError::kControl:
    case FunctionPrepareError::kEvaluation:
    case FunctionPrepareError::kConstants:
    case FunctionPrepareError::kParameters:
    case FunctionPrepareError::kTree: return RouteFailureClass::kUnproved;
  }
  return RouteFailureClass::kInvariantViolation;
}
RouteFailureClass PrintClass(TreePrintError error) {
  switch (error) {
    case TreePrintError::kResourceLimit: return RouteFailureClass::kMemoryLimit;
    case TreePrintError::kWorkLimit: return RouteFailureClass::kWorkLimit;
    case TreePrintError::kOutputLimit:
    case TreePrintError::kWrite: return RouteFailureClass::kOutputFailure;
    case TreePrintError::kUnsupportedContext:
    case TreePrintError::kInvalidLiteral: return RouteFailureClass::kInvariantViolation;
  }
  return RouteFailureClass::kInvariantViolation;
}
int Refused(std::ostream& output, MethodRouteReceipt receipt, InspectionError error,
    RouteFailureClass kind = RouteFailureClass::kUnproved,
    ValueRouteError reason = ValueRouteError::kContextStage,
    std::optional<unsigned> detail = {}, std::optional<abc::CodeOffset> pc = {}) {
  if (!receipt.failure) receipt.failure = RouteFailure{kind, reason, pc};
  receipt.coverage = Coverage::kUnsupported;
  const auto serialized = SerializeRouteReceipt(receipt);
  nlohmann::json report{{"source_complete", false}, {"executable", false},
                        {"inspection_error", static_cast<unsigned>(error)}};
  if (detail) report["detail"] = *detail;
  if (const auto* text = std::get_if<std::string>(&serialized))
    report["route_receipt"] = nlohmann::json::parse(*text);
  else report["receipt_error"] = static_cast<unsigned>(std::get<RouteReceiptError>(serialized));
  output << report << '\n';
  return EXIT_FAILURE;
}
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
void Positions(std::ostream& output, const FunctionTree& tree, const PrintedFunction& printed) {
  // Stream tables independently: do not expand each node's transitive PC set,
  // or hold an unbounded JSON DOM duplicating the complete source tree.
  output << "{\"unit\":\"utf8_byte\",\"origins\":";
  Array(output, tree.origins, Origin);
  output << ",\"origin_parts\":";
  Array(output, tree.origin_parts, [](auto id) { return id.value; });
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
      constexpr std::string_view kind = std::is_same_v<T, ExprId> ? "expression" :
          std::is_same_v<T, StmtId> ? "statement" : "anchor";
      return nlohmann::json{{"operation", operation.operation.value}, {"kind", kind}, {"node", node.value}};
    }, operation.node);
  });
  output << '}';
}
}  // namespace

int InspectValueSource(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodInfo& method, abcd::source::RouteInputIdentity identity,
    std::ostream& output) {
  MethodRouteReceipt receipt;
  receipt.identity = identity; receipt.method = method.offset; receipt.record = method.owner_offset;
  receipt.route = SourceRoute::kValueTree; receipt.stage = SourceStage::kDecoded;
  const auto looked_up = abc::MethodLookup::Build(index.methods);
  const auto* lookup = std::get_if<abc::MethodLookup>(&looked_up);
  if (!lookup) return Refused(output, receipt, InspectionError::kLookup,
      IndexClass(std::get<abc::MethodLookupError>(looked_up)));
  const auto indexed_roles = analysis::MethodRoleIndex::Build(file, header, index, *lookup);
  const auto* roles = std::get_if<analysis::MethodRoleIndex>(&indexed_roles);
  if (!roles) return Refused(output, receipt, InspectionError::kRoles,
      std::visit([](const auto& error) { return IndexClass(error); },
          std::get<analysis::MethodRoleFailure>(indexed_roles).error));
  const auto indexed_sites = analysis::ClassMethodSites::Build(file, header, index, *lookup);
  const auto* sites = std::get_if<analysis::ClassMethodSites>(&indexed_sites);
  if (!sites) return Refused(output, receipt, InspectionError::kSites,
      std::visit([](const auto& error) { return IndexClass(error); },
          std::get<analysis::ClassSiteFailure>(indexed_sites).error));
  const auto range = sites->Find(method.offset);
  if (range.count == 0) return Refused(output, receipt, InspectionError::kMissingSite);
  if (range.count != 1) return Refused(output, receipt, InspectionError::kAmbiguousSite);
  auto prepared = VerifiedFunction::PrepareClass(file, header, index, *lookup, *roles, *sites, range.first, identity);
  const auto* function = std::get_if<VerifiedFunction>(&prepared);
  if (!function) {
    const auto& failure = std::get<FunctionPrepareFailure>(prepared);
    if (failure.values) receipt = *failure.values;
    return Refused(output, receipt, InspectionError::kPrepare, PreparationClass(failure.kind),
        failure.values ? ValueRouteError::kTreeStage : ValueRouteError::kContextStage,
        static_cast<unsigned>(failure.kind), failure.pc);
  }
  // This inspection buffer is bounded independently from printer scratch.
  constexpr std::size_t kInspectionSourceBytes = 4 * 1024 * 1024;
  std::ostringstream source;
  const auto printing = PrintFunctionTree(*function, source, {kInspectionSourceBytes});
  receipt = function->Receipt();
  const auto* printed = std::get_if<PrintedFunction>(&printing);
  if (!printed) {
    const auto error = std::get<TreePrintError>(printing);
    return Refused(output, receipt, InspectionError::kPrint, PrintClass(error),
        ValueRouteError::kPrintStage, static_cast<unsigned>(error));
  }
  if (file.CheckUnchanged()) return Refused(output, receipt, InspectionError::kChanged,
      RouteFailureClass::kInvalidInput, ValueRouteError::kPrintStage);
  receipt.stage = SourceStage::kPrinted;
  // A checked method fragment is not a complete record and has no standalone
  // execution context. Keep the public inspection incomplete until integrated.
  const auto serialized = SerializeRouteReceipt(receipt);
  const auto* text = std::get_if<std::string>(&serialized);
  if (!text) return EXIT_FAILURE;
  output << "{\"route_receipt\":" << *text
         << ",\"source_complete\":false,\"executable\":false,\"method_slice_complete\":true"
         << ",\"fragment\":\"class_member\",\"creation\":";
  const auto& creation = function->Creation();
  output << nlohmann::json{{"creator", creation.creator.value}, {"pc", creation.pc.value},
      {"literal", creation.literal.value}, {"property", creation.property},
      {"placement", creation.placement == abc::ClassPlacement::kStatic ? "static" : "prototype"}};
  output << ",\"source\":" << nlohmann::json(source.view()) << ",\"positions\":";
  Positions(output, function->Tree(), *printed);
  output << "}\n";
  return EXIT_FAILURE;  // Incomplete record coverage, even after fragment printing.
}
