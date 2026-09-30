#include "record_tree.h"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <type_traits>
#include "function_file.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "abcd/source/module_enclosure.h"
#include "tree_constants.h"
#include "tree_globals.h"
#include "abcd/source/scalar_expression.h"
#include "abcd/source/string_literal.h"
#include "abcd/source/tree_printer.h"

namespace abcd::source {
RecordSourceResult BuildRecordTree(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    const RecordModuleContext& context, abc::MethodPosition entry,
    RouteInputIdentity identity, RecordProgramBudget budget) {
  using Error = RecordProgramError;
  const auto fail = [](Error kind) { return RecordProgramFailure{kind, {}}; };
  try {
    const auto retain = [&](std::size_t bytes) {
      if (bytes > budget.bytes) throw fail(Error::kResourceLimit);
      budget.bytes -= bytes;
    };
    const auto module = context.ForOwner(file, record.offset);
    if (!module) return fail(Error::kModule);
    const ModuleOwnerId owner{record.offset.value};
    auto enclosed = ModuleEnclosure::Build(file, header, context, identity, owner, {budget.bytes});
    auto* enclosure = std::get_if<ModuleEnclosure>(&enclosed);
    if (!enclosure) {
      const auto error = std::get<ModuleEnclosureError>(enclosed);
      if (error == ModuleEnclosureError::kResourceLimit) return fail(Error::kResourceLimit);
      if (error == ModuleEnclosureError::kFileChanged) return fail(Error::kFileChanged);
      if (error == ModuleEnclosureError::kWorkLimit)
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      return fail(Error::kModule);
    }
    retain(enclosure->AllocatedBytes());
    const auto external = enclosure->Names().size();
    // Reuse the wire slot grouping and duplicate-export validation. Source names
    // below are metadata, never names obtained from a paired source file.
    retain(budget.export_bytes);
    auto exports = BuildModuleExports(file, header, *module, {budget.export_bytes, budget.strings});
    const auto* slots = std::get_if<ModuleExports>(&exports);
    if (!slots) return fail(Error::kExport);
    auto loaded = ReadFunctionValues(file, header, index, entry, identity,
        InputCaptureMode::kClosed, {budget.bytes}, &context);
    auto* function = std::get_if<FileFunction>(&loaded);
    if (!function) {
      const auto& error = std::get<FunctionFileFailure>(loaded);
      if (const auto* values = std::get_if<FunctionValueFailure>(&error))
        return RouteRefusal{values->cause};
      return std::visit([&](const auto& failure) -> RecordSourceResult {
        using T = std::decay_t<decltype(failure)>;
        if constexpr (std::is_same_v<T, FunctionFileError>) {
          if (failure == FunctionFileError::kResourceLimit) return fail(Error::kResourceLimit);
          if (failure == FunctionFileError::kChanged) return fail(Error::kFileChanged);
        } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
          if (failure == abc::MetadataError::kResourceLimit) return fail(Error::kResourceLimit);
          if (failure == abc::MetadataError::kFileChanged) return fail(Error::kFileChanged);
          if (failure == abc::MetadataError::kRead) return fail(Error::kRead);
        }
        return fail(Error::kBody);
      }, error);
    }
    retain(function->allocated_bytes);
    const auto& graph = function->values.graph;
    // First-store lexical declarations require a single unconditional entry
    // region. Control-dependent module initialization needs a separate binding
    // lifetime analysis; never hoist a let to make such input appear supported.
    if (function->frame.parameters || graph.blocks.size() != 1) return fail(Error::kFrame);
    auto resolved = ResolveTreeConstants(file, header, index, index.methods[entry.value].offset, graph, {budget.bytes});
    auto* constants = std::get_if<TreeConstants>(&resolved);
    if (!constants) return fail(Error::kValue);
    retain(constants->allocated_bytes);
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = index.methods[entry.value].offset; syntax.owner = {1};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    std::vector<std::u16string> names;
    std::vector<std::u16string> aliases;
    std::vector<std::size_t> alias_slots;
    // Reserve vector arenas and their possible growth allocation together;
    // payloads are charged separately. Name uniqueness uses sorted indices.
    constexpr std::size_t kEntryStorage = 2 * (3 * sizeof(std::u16string) +
        sizeof(ModuleSyntax) + 2 * sizeof(std::size_t));
    const auto entries = external + module->local_exports.size() + module->indirect_exports.size();
    if (entries > budget.bytes / kEntryStorage) return fail(Error::kResourceLimit);
    retain(entries * kEntryStorage);
    names.reserve(external + slots->slot_count()); aliases.reserve(module->local_exports.size());
    alias_slots.reserve(module->local_exports.size()); syntax.modules.reserve(external + slots->slot_count());
    const auto added_names = external + slots->slot_count();
    if (added_names > (budget.bytes / sizeof(std::u16string)))
      return fail(Error::kResourceLimit);
    const auto name_count = constants->names.size() + added_names;
    if (name_count < constants->names.size()) return fail(Error::kResourceLimit);
    if (name_count > constants->names.capacity()) {
      if (name_count > budget.bytes / sizeof(std::u16string)) return fail(Error::kResourceLimit);
      retain(name_count * sizeof(std::u16string));
      constants->names.reserve(name_count);
      retain((constants->names.capacity() - name_count) * sizeof(std::u16string));
    }
    for (const auto& name : enclosure->Names()) {
      retain(4 * (name.size() + 1) * sizeof(char16_t));
      const auto symbol = static_cast<std::uint32_t>(names.size());
      names.emplace_back(name.begin(), name.end());
      syntax.modules.push_back({{owner, {symbol}},
          {static_cast<std::uint32_t>(constants->names.size())}, ModuleAccess::kReadOnly,
          NameEvidence::kMetadata, false});
      constants->names.push_back(names.back());
    }
    std::u16string previous;
    for (const auto& item : module->local_exports) {
      auto local = abc::ReadStringEntity(file, header, item.local, budget.strings);
      auto exported = abc::ReadStringEntity(file, header, item.exported, budget.strings);
      auto* name = std::get_if<std::u16string>(&local);
      auto* alias = std::get_if<std::u16string>(&exported);
      if (!name || !alias) return fail(Error::kRead);
      if (names.size() == external || *name != previous) {
        retain(4 * name->capacity() * sizeof(char16_t));
        if (!IsSourceIdentifier(std::u16string_view(*name)))
          return fail(Error::kExport);
        names.push_back(*name);
        const auto symbol = static_cast<std::uint32_t>(names.size() - 1);
        syntax.modules.push_back({{{record.offset.value}, {symbol}},
            {static_cast<std::uint32_t>(constants->names.size())}, ModuleAccess::kMutable,
            NameEvidence::kMetadata, true});
        constants->names.push_back(*name);
      }
      previous = std::move(*name);
      retain(alias->capacity() * sizeof(char16_t) + sizeof(std::u16string) + sizeof(std::size_t));
      aliases.push_back(std::move(*alias)); alias_slots.push_back(names.size() - 1);
    }
    if (names.size() != external + slots->slot_count()) return fail(Error::kExport);
    std::vector<std::size_t> order(names.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return names[a] < names[b]; });
    for (std::size_t i = 1; i < order.size(); ++i)
      if (names[order[i - 1]] == names[order[i]]) return fail(Error::kExport);
    // Direct reexports and local aliases share one public export namespace.
    // Star ambiguity is intentionally resolved by native ESM linking instead.
    std::vector<std::u16string> indirect_names;
    indirect_names.reserve(module->indirect_exports.size());
    std::vector<std::u16string_view> export_order;
    export_order.reserve(aliases.size() + module->indirect_exports.size());
    for (const auto& name : aliases) export_order.push_back(name);
    for (const auto& item : module->indirect_exports) {
      auto read = abc::ReadStringEntity(file, header, item.exported, budget.strings);
      auto* name = std::get_if<std::u16string>(&read);
      if (!name) return fail(Error::kRead);
      retain((name->capacity() + 1) * sizeof(char16_t));
      indirect_names.push_back(std::move(*name));
      export_order.push_back(indirect_names.back());
    }
    std::sort(export_order.begin(), export_order.end());
    if (std::adjacent_find(export_order.begin(), export_order.end()) != export_order.end())
      return fail(Error::kExport);
    auto bound = BindModuleAliases(file, header, index, function->input, function->values,
        context, owner, {}, {budget.bytes});
    auto* bindings = std::get_if<ModuleAliases>(&bound);
    if (!bindings) {
      const auto classify = [&](const auto& error) -> RecordSourceResult {
        using T = std::decay_t<decltype(error)>;
        if (error == T::kResourceLimit) return fail(Error::kResourceLimit);
        if constexpr (std::is_same_v<T, abc::MetadataError>) {
          if (error == T::kFileChanged) return fail(Error::kFileChanged);
          if (error == T::kRead) return fail(Error::kRead);
        } else if (error == T::kWorkLimit) {
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
        }
        return fail(Error::kModule);
      };
      return std::visit([&](const auto& error) -> RecordSourceResult {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_same_v<T, ModuleReadFailure>)
          return std::visit(classify, error.reason);
        else return classify(error);
      }, std::get<ModuleAliasFailure>(bound).cause);
    }
    retain(bindings->allocated_bytes);
    // The full destination inventory includes unused imports and local stores.
    // Match each freshly proved read to that same slot/name; never turn an
    // import into a copied value or a local read into an external declaration.
    for (const auto& alias : bindings->aliases) {
      const auto slot = alias.target.symbol.value;
      if (alias.target.owner != owner || slot >= names.size() ||
          alias.name.value >= bindings->names.size() ||
          names[slot] != bindings->names[alias.name.value]) return fail(Error::kModule);
    }
    retain(slots->slot_count());
    std::vector<bool> initialized(slots->slot_count());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      if (const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation)) {
        if (store->slot.value >= initialized.size()) return fail(Error::kExport);
        constants->operations[i] = ModuleStoreConstant{store->slot,
            {owner, {static_cast<std::uint32_t>(external + store->slot.value)}}, !initialized[store->slot.value]};
        initialized[store->slot.value] = true;
      }
    }
    if (std::find(initialized.begin(), initialized.end(), false) != initialized.end())
      return fail(Error::kInitializer);
    auto evaluated = BuildEvaluationPlan(graph, {budget.bytes}, IncomingCells::kReject, {}, bindings->evaluations);
    auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!evaluation) return fail(Error::kValue);
    retain(evaluation->allocated_bytes);
    auto controlled = BuildControlPlan(graph, {budget.bytes});
    auto* control = std::get_if<ControlPlan>(&controlled);
    if (!control) return fail(Error::kBody);
    retain(control->allocated_bytes);
    const auto view = constants->View();
    syntax.names = std::move(constants->names); syntax.texts = std::move(constants->texts);
    auto constructed = BuildFunctionTree(graph, *control, *evaluation, std::move(syntax), view, {budget.bytes});
    auto* result = std::get_if<ConstructedFunction>(&constructed);
    if (!result) return fail(Error::kValue);
    retain(result->allocated_bytes);
    // Recheck the actual emitted tree against the complete metadata inventory.
    // Entry-body locals and imports occupy the same native ESM scope even
    // though the shared function tree models their ownership separately.
    if (result->tree.modules.size() != names.size()) return fail(Error::kModule);
    retain(names.size());
    std::vector<bool> seen(names.size());
    for (const auto& alias : result->tree.modules) {
      const auto slot = alias.target.symbol.value;
      if (alias.target.owner != owner || slot >= names.size() || seen[slot] ||
          alias.symbol.value >= result->tree.symbols.size()) return fail(Error::kModule);
      seen[slot] = true;
      const bool local = slot >= external;
      const auto& symbol = result->tree.symbols[alias.symbol.value];
      if (alias.local != local || alias.access != (local ? ModuleAccess::kMutable : ModuleAccess::kReadOnly) ||
          symbol.kind != (local ? BindingKind::kLocal : BindingKind::kModule) ||
          symbol.evidence != NameEvidence::kMetadata || symbol.name.value >= result->tree.names.size() ||
          result->tree.names[symbol.name.value] != names[slot]) return fail(Error::kModule);
    }
    // No embedding globals have been established by this record adapter yet.
    const auto globals = VerifyTreeGlobals(result->tree, nullptr, {budget.bytes});
    if (!std::holds_alternative<TreeGlobals>(globals)) return fail(Error::kValue);
    // Bound retained stream capacity and the returned string alongside the
    // still-live graph/tree; the printer retains its own scratch allowance.
    budget.output.bytes = std::min(budget.output.bytes, budget.bytes / 6);
    retain(budget.output.bytes * 5);
    std::ostringstream output;
    const auto prelude = enclosure->WriteImports(file, header, output, budget.output,
        {std::min(budget.strings.bytes, budget.bytes)});
    const auto* prelude_bytes = std::get_if<std::size_t>(&prelude);
    if (!prelude_bytes) {
      switch (std::get<ModuleImportFailure>(prelude).kind) {
        case ModuleImportError::kOutputLimit: return fail(Error::kOutputLimit);
        case ModuleImportError::kResourceLimit: return fail(Error::kResourceLimit);
        case ModuleImportError::kWrite: return fail(Error::kWrite);
        case ModuleImportError::kRequestIndex:
        case ModuleImportError::kString: return fail(Error::kModule);
      }
    }
    auto printed = PrintModuleTree(result->tree, output,
        {budget.output.bytes - *prelude_bytes}, {budget.bytes});
    const auto* body = std::get_if<PrintedFunction>(&printed);
    if (!body) {
      switch (std::get<TreePrintError>(printed)) {
        case TreePrintError::kResourceLimit: return fail(Error::kResourceLimit);
        case TreePrintError::kOutputLimit: return fail(Error::kOutputLimit);
        case TreePrintError::kWrite: return fail(Error::kWrite);
        case TreePrintError::kUnsupportedContext:
        case TreePrintError::kInvalidLiteral: return fail(Error::kValue);
        case TreePrintError::kWorkLimit:
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kPrintStage, {}}};
      }
    }
    std::size_t remaining = budget.output.bytes - *prelude_bytes - body->bytes;
    const auto append = [&](std::string_view text) {
      if (text.size() > remaining) throw fail(Error::kOutputLimit);
      output << text; remaining -= text.size();
    };
    for (std::size_t i = 0; i < aliases.size(); ++i) {
      append("export { ");
      for (const auto unit : names[alias_slots[i]]) append(std::string(1, static_cast<char>(unit)));
      append(" as ");
      const auto literal = WriteStringLiteral(aliases[i], output, {remaining});
      if (!std::holds_alternative<std::size_t>(literal)) return fail(Error::kOutputLimit);
      remaining -= std::get<std::size_t>(literal); append(" };\n");
    }
    if (file.CheckUnchanged()) return fail(Error::kFileChanged);
    PrintedSource source;
    source.text = std::move(output).str();
    return source;
  } catch (const RecordProgramFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return fail(Error::kResourceLimit); }
    catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
}
