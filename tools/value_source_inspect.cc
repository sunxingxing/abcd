#include "value_source_inspect.h"

#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>
#include <type_traits>

#include "abcd/source/tree_printer.h"
#include "abcd/source/class_method.h"
#include "abcd/source/class_method_selection.h"
#include "abcd/source/record_source.h"
#include "../src/source/function_file.h"
#include "../src/source/module_aliases.h"

namespace {
using namespace abcd;
using namespace abcd::source;
enum class InspectionError : std::uint8_t {
  kLookup, kRoles, kSites, kMissingSite, kAmbiguousSite, kPrepare, kPrint,
  kChanged, kEmission, kFunctionSites
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
  } else if constexpr (std::is_same_v<T, analysis::FunctionSiteError>) {
    switch (error) {
      case analysis::FunctionSiteError::kResourceLimit: return Class::kMemoryLimit;
      case analysis::FunctionSiteError::kWorkLimit: return Class::kWorkLimit;
      case analysis::FunctionSiteError::kMalformedCreation: return Class::kInvalidInput;
    }
  } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
    if (error == abc::MetadataError::kResourceLimit) return Class::kMemoryLimit;
    return error == abc::MetadataError::kUnsupportedProfile ? Class::kUnsupported : Class::kInvalidInput;
  } else if constexpr (std::is_same_v<T, abc::MethodAttributeError>) {
    switch (error) {
      case abc::MethodAttributeError::kUnsupportedProfile: return Class::kUnsupported;
      case abc::MethodAttributeError::kUnknownKind: return Class::kInvalidInput;
    }
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
    case FunctionPrepareError::kTree:
    case FunctionPrepareError::kAbi: return RouteFailureClass::kUnproved;
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
void Positions(std::ostream& output, const FunctionTree& tree, const PrintedFunction& printed) {
  // Stream tables independently: do not expand each node's transitive PC set,
  // or hold an unbounded JSON DOM duplicating the complete source tree.
  output << "{\"unit\":\"utf8_byte\",\"origins\":";
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
  output << '}';
}

int RecordSlice(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const abc::MethodInfo& method,
    const ClassMethodSelection& selection, std::ostream& output) {
  const auto refuse = [&](std::string_view stage) {
    output << nlohmann::json{{"fragment", "record_slice"}, {"source_complete", false},
        {"executable", false}, {"record_emitted", false}, {"refused_at", stage}} << '\n';
    return EXIT_FAILURE;
  };
  const auto owner = std::find_if(index.records.begin(), index.records.end(),
      [&](const abc::RecordInfo& record) { return record.offset == method.owner_offset; });
  if (owner == index.records.end()) return refuse("record_owner");
  const auto packaging = abc::DetectFilePackaging(file, header, index);
  const auto* packed = std::get_if<abc::FilePackaging>(&packaging);
  if (!packed) return refuse("packaging");
  const auto context = ReadRecordModuleContext(file, header, index, *owner, *packed);
  const auto* ready = std::get_if<RecordModuleContext>(&context);
  if (!ready) return refuse("module_context");
  auto built = BuildRecordSource(file, header, index, lookup, *owner, *ready, roles,
      {}, nullptr, RoutePolicy::kLegacyAllowed, &selection);
  const auto* source = std::get_if<PrintedSource>(&built);
  if (!source) {
    nlohmann::json failure{{"fragment", "record_slice"}, {"source_complete", false},
        {"executable", false}, {"record_emitted", false}, {"refused_at", "record_assembly"},
        {"error_variant", built.index()}};
    std::visit([&](const auto& error) {
      using T = std::decay_t<decltype(error)>;
      if constexpr (std::is_same_v<T, RouteRefusal>) {
        failure["failure_class"] = RouteFailureClassName(error.failure.failure_class);
        failure["reason"] = ValueRouteErrorName(error.failure.reason);
      } else if constexpr (requires { error.kind; }) {
        failure["error"] = static_cast<unsigned>(error.kind);
      }
      if constexpr (requires { error.pc.value; }) failure["pc"] = error.pc.value;
    }, built);
    output << failure << '\n';
    return EXIT_FAILURE;
  }
  if (file.CheckUnchanged()) return refuse("file_changed");
  output << "{\"fragment\":\"record_slice\",\"source_complete\":false,\"executable\":false"
            ",\"record_emitted\":true,\"enclosing_route\":\"legacy\",\"source\":"
         << nlohmann::json(source->text) << ",\"function_segments\":[";
  bool first = true;
  for (const auto& segment : source->function_segments) {
    if (!first) output << ',';
    first = false;
    const auto& emission = *segment.function;
    const auto serialized = SerializeRouteReceipt(emission.Receipt());
    const auto* receipt = std::get_if<std::string>(&serialized);
    if (!receipt) return EXIT_FAILURE;
    const auto* site = emission.Function().ClassCreation();
    if (!site) return EXIT_FAILURE;
    const auto& creation = *site;
    output << "{\"begin\":" << segment.begin.byte << ",\"end\":" << segment.end.byte
           << ",\"creation\":" << nlohmann::json{{"creator", creation.creator.value},
                {"pc", creation.pc.value}, {"literal", creation.literal.value},
                {"property", creation.property}}
           << ",\"route_receipt\":" << *receipt << ",\"local_positions\":";
    Positions(output, emission.Function().Tree(), emission.Positions());
    output << '}';
  }
  output << "]}\n";
  // A migrated child does not certify the legacy entry or its other children.
  return EXIT_FAILURE;
}
int InspectCreated(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const abc::MethodInfo& method,
    MethodRouteReceipt receipt, std::ostream& output) {
  auto indexed = analysis::FunctionCreationSites::Build(file, header, index, lookup);
  const auto* sites = std::get_if<analysis::FunctionCreationSites>(&indexed);
  if (!sites) {
    const auto& failure = std::get<analysis::FunctionSiteFailure>(indexed);
    return Refused(output, receipt, InspectionError::kFunctionSites,
        std::visit([](const auto& error) { return IndexClass(error); }, failure.error));
  }
  const auto range = sites->Find(method.offset);
  if (range.count == 0) return Refused(output, receipt, InspectionError::kMissingSite);
  if (range.count != 1) return Refused(output, receipt, InspectionError::kAmbiguousSite);
  auto prepared = VerifiedFunction::PrepareCreated(file, header, index, lookup,
      roles, *sites, range.first, receipt.identity);
  const auto* function = std::get_if<VerifiedFunction>(&prepared);
  if (!function) {
    const auto& failure = std::get<FunctionPrepareFailure>(prepared);
    if (failure.values) receipt = *failure.values;
    return Refused(output, receipt, InspectionError::kPrepare, PreparationClass(failure.kind),
        failure.values ? ValueRouteError::kTreeStage : ValueRouteError::kContextStage,
        static_cast<unsigned>(failure.kind), failure.pc);
  }
  // A verified ordinary/arrow body is not yet an emitted closure expression.
  // Never disguise it as a class member to bypass name/strictness/owner checks.
  return Refused(output, function->Receipt(), InspectionError::kPrint,
      RouteFailureClass::kUnsupported, ValueRouteError::kPrintStage);
}
}  // namespace

int InspectValueSource(const abcd::corpus::AbcFile& file,
    const abcd::abc::Header& header, const abcd::abc::MetadataIndex& index,
    const abcd::abc::MethodInfo& method, abcd::source::RouteInputIdentity identity,
    std::ostream& output, ValueSourceScope scope) {
  MethodRouteReceipt receipt;
  receipt.identity = identity; receipt.method = method.offset; receipt.record = method.owner_offset;
  receipt.route = SourceRoute::kValueTree; receipt.stage = SourceStage::kDecoded;
  const auto looked_up = abc::MethodLookup::Build(index.methods);
  const auto* lookup = std::get_if<abc::MethodLookup>(&looked_up);
  if (!lookup) return Refused(output, receipt, InspectionError::kLookup,
      IndexClass(std::get<abc::MethodLookupError>(looked_up)));
  if (scope == ValueSourceScope::kConditionalValues) {
    const auto position = lookup->Find(method.offset);
    if (!position) return Refused(output, receipt, InspectionError::kLookup);
    nlohmann::json report{{"source_complete", false}, {"executable", false},
        {"conditional", true}, {"method", method.offset.value}};
    // One diagnostic method per invocation. Family callers reuse this record
    // context rather than scanning packaging/record metadata for every child.
    std::optional<RecordModuleContext> modules;
    const auto owner = std::find_if(index.records.begin(), index.records.end(),
        [&](const abc::RecordInfo& record) { return record.offset == method.owner_offset; });
    if (owner != index.records.end()) {
      const auto packaging = abc::DetectFilePackaging(file, header, index);
      if (const auto* packed = std::get_if<abc::FilePackaging>(&packaging)) {
        auto context = ReadRecordModuleContext(file, header, index, *owner, *packed);
        if (auto* ready = std::get_if<RecordModuleContext>(&context)) modules = std::move(*ready);
        else {
          const auto& failure = std::get<RecordModuleFailure>(context).reason;
          report["module_context_failure"] = failure.index();
          std::visit([&](const auto& error) {
            using T = std::decay_t<decltype(error)>;
            if constexpr (std::is_same_v<T, RecordModuleError>)
              report["module_context_error"] = static_cast<unsigned>(error);
            else report["module_context_error"] = static_cast<unsigned>(error.kind);
          }, failure);
        }
      } else report["module_packaging_error"] = static_cast<unsigned>(std::get<abc::AttributeFailure>(packaging).kind);
    }
    report["module_context_ready"] = modules.has_value();
    const auto prepared = ReadFunctionValues(file, header, index, *position, identity,
        InputCaptureMode::kConditional, {}, modules ? &*modules : nullptr);
    if (const auto* function = std::get_if<FileFunction>(&prepared)) {
      report["values_prepared"] = true;
      report["capture_occurrences"] = function->input.Captures().size();
      report["child_obligations"] = function->input.Children().size();
      report["operations"] = function->values.graph.ops.size();
      report["cells"] = function->values.cells ? function->values.cells->cells.size() : 0;
      report["work_revision"] = function->values.graph.revision.value;
      report["guarded_module_pairs"] = function->values.modules.pairs.size();
      report["module_reads_resolved"] = function->module_reads.has_value();
      if (modules) {
        analysis::SemanticBudget budget;
        if (function->allocated_bytes > budget.bytes) {
          report["module_alias_error"] = static_cast<unsigned>(ModuleAliasError::kResourceLimit);
        } else {
          budget.bytes -= function->allocated_bytes;
          const ModuleOwnerId module_owner{static_cast<std::uint32_t>(owner - index.records.begin())};
          const auto bound = BindModuleAliases(file, header, index, function->input,
              function->values, *modules, module_owner, {}, budget);
          const auto* aliases = std::get_if<ModuleAliases>(&bound);
          report["module_aliases_bound"] = aliases != nullptr;
          if (aliases) {
            report["module_evaluations"] = aliases->evaluations.size();
            report["module_aliases"] = nlohmann::json::array();
            for (const auto& alias : aliases->aliases) {
              const auto& name = aliases->names[alias.name.value - aliases->first_name.value];
              report["module_aliases"].push_back({{"symbol", alias.target.symbol.value},
                  {"owner", alias.target.owner.value}, {"access", static_cast<unsigned>(alias.access)},
                  {"name_utf16", std::vector<std::uint16_t>(name.begin(), name.end())}});
            }
          } else {
            const auto& failure = std::get<ModuleAliasFailure>(bound);
            report["module_alias_failure"] = failure.cause.index();
            std::visit([&](const auto& error) {
              using T = std::decay_t<decltype(error)>;
              if constexpr (std::is_same_v<T, ModuleReadFailure>)
                std::visit([&](const auto& reason) { report["module_alias_error"] = static_cast<unsigned>(reason); }, error.reason);
              else report["module_alias_error"] = static_cast<unsigned>(error);
            }, failure.cause);
            if (failure.pc) report["module_alias_pc"] = failure.pc->value;
          }
        }
      }
      if (function->module_reads) {
        report["module_reads"] = nlohmann::json::array();
        for (const auto& binding : function->module_reads->bindings) {
          nlohmann::json read{{"operation", binding.operation.value},
              {"slot", binding.read.slot.value}, {"space", static_cast<unsigned>(binding.read.space)},
              {"target_variant", binding.target.index()}};
          std::visit([&](const auto& target) {
            using T = std::decay_t<decltype(target)>;
            read["local_name_entity"] = target.local.value;
            if constexpr (!std::is_same_v<T, LocalModuleBinding>) {
              read["request"] = target.request.value;
              read["specifier_entity"] = target.specifier.value;
            }
            if constexpr (std::is_same_v<T, ImportedModuleBinding>)
              read["imported_name_entity"] = target.imported.value;
          }, binding.target);
          report["module_reads"].push_back(std::move(read));
        }
      }
    } else {
      report["values_prepared"] = false;
      const auto& failure = std::get<FunctionFileFailure>(prepared);
      report["failure_variant"] = failure.index();
      if (const auto* module = std::get_if<ModuleReadFailure>(&failure)) {
        report["module_read_failure"] = module->reason.index();
        std::visit([&](const auto& error) { report["module_read_error"] = static_cast<unsigned>(error); }, module->reason);
        if (module->pc) report["pc"] = module->pc->value;
      }
      if (const auto* value = std::get_if<FunctionValueFailure>(&failure)) {
        report["reason"] = ValueRouteErrorName(value->cause.reason);
        if (value->cause.pc) report["pc"] = value->cause.pc->value;
      }
      if (const auto* input = std::get_if<FunctionInputFailure>(&failure)) {
        report["input_failure_variant"] = input->cause.index();
        std::visit([&](const auto& error) {
          using T = std::decay_t<decltype(error)>;
          if constexpr (std::is_same_v<T, FunctionInputError>)
            report["input_error"] = static_cast<unsigned>(error);
          else {
            report["input_error"] = static_cast<unsigned>(error.kind);
            report["pc"] = error.pc.value;
          }
        }, input->cause);
      }
    }
    output << report << '\n';
    return EXIT_FAILURE; // Conditional analysis is not executable source.
  }
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
  if (range.count == 0) {
    if (scope == ValueSourceScope::kRecordSlice)
      return Refused(output, receipt, InspectionError::kMissingSite);
    return InspectCreated(file, header, index, *lookup, *roles, method, receipt, output);
  }
  if (range.count != 1) return Refused(output, receipt, InspectionError::kAmbiguousSite);
  if (scope == ValueSourceScope::kRecordSlice)
    return RecordSlice(file, header, index, *lookup, *roles, method,
                       {*sites, range.first, identity}, output);
  auto prepared = VerifiedFunction::PrepareClass(file, header, index, *lookup, *roles, *sites, range.first, identity);
  auto* function = std::get_if<VerifiedFunction>(&prepared);
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
  receipt = function->Receipt();
  const auto attributes = abc::DecodeMethodAttributes(method.access_flags, header.profile);
  const auto* flags = std::get_if<abc::MethodAttributes>(&attributes);
  if (!flags) return Refused(output, receipt, InspectionError::kEmission,
      RouteFailureClass::kInvalidInput, ValueRouteError::kPrintStage);
  const auto* creation_site = function->ClassCreation();
  if (!creation_site) return Refused(output, receipt, InspectionError::kMissingSite);
  const auto site = *creation_site;
  const ClassMethod shape{{site.target, site.kind, site.length}, *flags, function->Frame(),
      site.placement, function->Name(), {}, EffectiveMethodKind::kOrdinary};
  auto printing = WriteClassMethod(shape, std::move(*function), source,
      GeneratedOffset{0}, {kInspectionSourceBytes});
  const auto* emission = std::get_if<FunctionEmission>(&printing);
  if (!emission) {
    if (const auto* shape_error = std::get_if<FunctionEmitError>(&printing))
      return Refused(output, receipt, InspectionError::kEmission,
          RouteFailureClass::kInvariantViolation, ValueRouteError::kPrintStage,
          static_cast<unsigned>(*shape_error));
    const auto error = std::get<TreePrintError>(printing);
    return Refused(output, receipt, InspectionError::kPrint, PrintClass(error),
        ValueRouteError::kPrintStage, static_cast<unsigned>(error));
  }
  if (file.CheckUnchanged()) return Refused(output, receipt, InspectionError::kChanged,
      RouteFailureClass::kInvalidInput, ValueRouteError::kPrintStage);
  receipt = emission->Receipt();
  // The method segment is complete; the containing record still has open
  // obligations. These are different scopes, not competing coverage claims.
  const auto serialized = SerializeRouteReceipt(receipt);
  const auto* text = std::get_if<std::string>(&serialized);
  if (!text) return EXIT_FAILURE;
  output << "{\"route_receipt\":" << *text
         << ",\"source_complete\":false,\"executable\":false,\"method_slice_complete\":true"
         << ",\"fragment\":\"class_member\",\"creation\":";
  const auto& creation = *emission->Function().ClassCreation();
  output << nlohmann::json{{"creator", creation.creator.value}, {"pc", creation.pc.value},
      {"literal", creation.literal.value}, {"property", creation.property},
      {"placement", creation.placement == abc::ClassPlacement::kStatic ? "static" : "prototype"}};
  const auto& abi = emission->Function().Abi();
  output << ",\"abi\":{\"layout\":" << nlohmann::json(ParameterLayoutName(abi.layout)) << ",\"length\":" << abi.length
         << ",\"receiver\":" << nlohmann::json{{"operands", abi.receiver.operands}, {"references", abi.receiver.references}}
         << ",\"callable\":" << nlohmann::json(AbiObservationName(abi.callable))
         << ",\"new_target\":" << nlohmann::json(AbiObservationName(abi.new_target))
         << ",\"arguments\":" << nlohmann::json(AbiObservationName(abi.arguments)) << ",\"parameters\":";
  Array(output, abi.parameters, [](const ParameterAbiUse& use) {
    return nlohmann::json{{"symbol", use.symbol.value}, {"operands", use.reads.operands},
        {"references", use.reads.references}, {"writes", use.writes}};
  });
  output << "},\"destination\":" << nlohmann::json{{"begin", emission->Destination().begin}, {"end", emission->Destination().end}}
         << ",\"source\":" << nlohmann::json(source.view()) << ",\"positions\":";
  Positions(output, emission->Function().Tree(), emission->Positions());
  output << "}\n";
  return EXIT_FAILURE;  // Incomplete record coverage, even after fragment printing.
}
