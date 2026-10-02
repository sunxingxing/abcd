#include "cell_names.h"
#include "record_tree.h"

#include <algorithm>
#include <numeric>
#include <limits>
#include <sstream>
#include <type_traits>
#include "function_file.h"
#include "class_tree.h"
#include "family_route_failure.h"
#include "family_print.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "module_default_export.h"
#include "module_binding_name.h"
#include "abcd/source/module_enclosure.h"
#include "tree_constants.h"
#include "tree_globals.h"
#include "tree_semantics.h"
#include "abcd/source/scalar_expression.h"
#include "abcd/source/string_literal.h"
#include "abcd/source/tree_printer.h"

namespace abcd::source {
static RecordSourceResult BuildRecordTreeAttempt(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    const RecordModuleContext& context, abc::MethodPosition entry,
    RouteInputIdentity identity, RecordProgramBudget budget, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites* functions,
    const analysis::ClassMethodSites* classes, bool metadata_names, bool& proposed_names) {
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
    // below are metadata-derived (including anonymous binding synthesis),
    // never names obtained from a paired source file.
    retain(budget.export_bytes);
    auto exports = BuildModuleExports(file, header, *module, {budget.export_bytes, budget.strings});
    const auto* slots = std::get_if<ModuleExports>(&exports);
    if (!slots) return fail(Error::kExport);
    auto named = ResolveModuleBindingNames(file, header, *module, {budget.bytes});
    const auto* local_names = std::get_if<ModuleBindingNames>(&named);
    if (!local_names) return std::visit([&](auto error) -> RecordSourceResult {
      using T = decltype(error);
      if (error == T::kResourceLimit) return fail(Error::kResourceLimit);
      if constexpr (std::is_same_v<T, abc::MetadataError>) {
        if (error == T::kFileChanged) return fail(Error::kFileChanged);
        if (error == T::kRead) return fail(Error::kRead);
      } else if (error == T::kWorkLimit) {
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      }
      return fail(Error::kExport);
    }, std::get<ModuleBindingNameFailure>(named));
    retain(local_names->allocated_bytes);
    if (local_names->locals.size() != slots->slot_count()) return fail(Error::kExport);
    std::optional<FileFunction> standalone;
    std::optional<FileFamily> family;
    FileFunction* function = nullptr;
    if (functions) {
      auto read = ReadFunctionFamily(file, header, index, lookup, roles, *functions, entry, identity,
          {budget.bytes}, &context, nullptr, owner, FamilyClassMode::kRetainRoot);
      if (const auto* error = std::get_if<FamilyFileFailure>(&read))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
      family.emplace(std::get<FileFamily>(std::move(read)));
      retain(family->allocated_bytes);
      if (family->members.empty() || family->members.front().function.Phase()) return fail(Error::kBody);
      function = &family->members.front().function.Normal();
    } else {
      auto loaded = ReadFunctionValues(file, header, index, entry, identity,
          InputCaptureMode::kClosed, {budget.bytes}, &context);
      function = std::get_if<FileFunction>(&loaded);
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
      standalone.emplace(std::move(*function)); function = &*standalone;
      retain(function->allocated_bytes);
    }
    const auto& graph = function->values.graph;
    if (function->frame.parameters) return fail(Error::kFrame);
    std::vector<EnvironmentCleanupSite> cleanup_sites;
    if (function->cleanup) {
      const auto& cleanup = *function->cleanup;
      const auto count = cleanup.Proof().Pops().size() + cleanup.Proof().Handlers().size();
      if (count > budget.bytes / sizeof(EnvironmentCleanupSite)) return fail(Error::kResourceLimit);
      retain(count*sizeof(EnvironmentCleanupSite)); cleanup_sites.reserve(count);
      retain((cleanup_sites.capacity()-count)*sizeof(EnvironmentCleanupSite));
      for (const auto& pop : cleanup.Proof().Pops())
        cleanup_sites.push_back({cleanup.Original().instructions[pop.instruction.value].offset,
            CleanupInstructionKind::kPop,cleanup.Original().instructions[pop.allocation.value].offset});
      for (const auto& handler : cleanup.Proof().Handlers())
        cleanup_sites.push_back({cleanup.Original().instructions[handler.rethrow.value].offset,
            CleanupInstructionKind::kRethrow,{}});
      std::sort(cleanup_sites.begin(),cleanup_sites.end(),[](const auto& a,const auto& b) { return a.pc < b.pc; });
    }
    auto controlled = BuildControlPlan(graph, {budget.bytes});
    auto* control = std::get_if<ControlPlan>(&controlled);
    if (!control) return fail(Error::kBody);
    retain(control->allocated_bytes);
    auto resolved = ResolveTreeConstants(file, header, index, index.methods[entry.value].offset, graph, {budget.bytes});
    auto* constants = std::get_if<TreeConstants>(&resolved);
    if (!constants) return fail(Error::kValue);
    retain(constants->allocated_bytes);
    auto projected = ProjectClassMembers(graph, function->values.initializers.calls, {budget.bytes}, function->values.definitions.sites);
    const auto* projection = std::get_if<ClassProjection>(&projected);
    if (!projection) {
      const auto error = std::get<ClassInitializerError>(projected);
      if (error == ClassInitializerError::kResourceLimit) return fail(Error::kResourceLimit);
      if (error == ClassInitializerError::kWorkLimit)
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      return fail(Error::kInitializer);
    }
    retain(projection->allocated_bytes);
    for (const auto read : projection->prototypes) {
      const auto* name = std::get_if<NameId>(&constants->operations[read.value]);
      if (!name || name->value >= constants->names.size() || constants->names[name->value] != u"prototype")
        return fail(Error::kInitializer);
    }
    retain(graph.ops.size() * sizeof(const ClassInitializerCall*));
    std::vector<const ClassInitializerCall*> initialization(graph.ops.size());
    retain((initialization.capacity() - initialization.size()) * sizeof(const ClassInitializerCall*));
    for (const auto& call : function->values.initializers.calls) initialization[call.creation.value] = &call;
    GlobalScope class_scope{};
    class_scope.module_enclosure = enclosure;
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = index.methods[entry.value].offset; syntax.owner = {family ? 0U : 1U};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    if (metadata_names) {
      auto named = ResolveCellNames(file, header, index, index.methods[entry.value].offset,
          graph, constants->names, syntax.locals, {budget.bytes});
      if (const auto* storage = std::get_if<CellNameStorage>(&named)) retain(storage->bytes);
      else if (const auto* error = std::get_if<abc::MetadataError>(&named)) {
        if (*error == abc::MetadataError::kFileChanged) return fail(Error::kFileChanged);
        if (*error == abc::MetadataError::kResourceLimit) return fail(Error::kResourceLimit);
        return fail(Error::kRead);
      } else {
        const auto constant_error = std::get<TreeConstantError>(named);
        if (constant_error == TreeConstantError::kResourceLimit) return fail(Error::kResourceLimit);
        if (constant_error == TreeConstantError::kWorkLimit)
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
        return fail(Error::kValue);
      }
      proposed_names = !syntax.locals.empty();
    }
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
    for (std::size_t slot = 0; slot < local_names->locals.size(); ++slot) {
      const auto& name = local_names->locals[slot];
      retain(4 * (name.size() + 1) * sizeof(char16_t));
      names.push_back(name);
      const auto symbol = static_cast<std::uint32_t>(names.size() - 1);
      syntax.modules.push_back({{owner, {symbol}},
          {static_cast<std::uint32_t>(constants->names.size())}, ModuleAccess::kMutable,
          local_names->evidence[slot], true});
      constants->names.push_back(name);
    }
    for (std::size_t row = 0; row < module->local_exports.size(); ++row) {
      const auto& item = module->local_exports[row];
      auto exported = abc::ReadStringEntity(file, header, item.exported, budget.strings);
      auto* alias = std::get_if<std::u16string>(&exported);
      if (!alias) return fail(Error::kRead);
      retain(alias->capacity() * sizeof(char16_t) + sizeof(std::u16string) + sizeof(std::size_t));
      aliases.push_back(std::move(*alias)); alias_slots.push_back(external + local_names->rows[row]);
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
    retain(slots->slot_count());
    std::vector<std::uint8_t> entry_initialized(slots->slot_count());
    retain(entry_initialized.capacity() - entry_initialized.size());
    std::size_t stores = 0, placed = 0;
    for (const auto& op : graph.ops)
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueModuleWrite>(op.operation)) ++stores;
    // Only the root control sequence owns native record declarations. Branch
    // joins may precede them. Arm/loop writes of a binding initialized on
    // entry remain assignments, never new declarations or hoisted stores.
    // Traverse source order, not physical SSA block numbering, for first-store
    // identity. Existing tree-flow checks revalidate root declaration placement.
    for (std::optional<ControlId> at = control->entry; at; at = control->nodes[at->value].next) {
      const auto& node = control->nodes[at->value];
      if (!std::holds_alternative<std::monostate>(node.synthetic)) continue;
      const auto& block = graph.blocks[node.block.value];
      for (std::size_t i = block.first_op.value; i < block.first_op.value + block.op_count; ++i) {
        const auto& op = graph.ops[i];
        if (op.life != analysis::ValueLife::kLive) continue;
        if (const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation)) {
          if (store->slot.value >= initialized.size()) return fail(Error::kExport);
          constants->operations[i] = ModuleStoreConstant{store->slot,
              {owner, {static_cast<std::uint32_t>(external + store->slot.value)}}, !initialized[store->slot.value]};
          if (!initialized[store->slot.value] && node.block.value == 0)
            entry_initialized[store->slot.value] = 1;
          initialized[store->slot.value] = true; ++placed;
        }
      }
    }
    // Reuse the root's native module binding only after its completed entry
    // declaration. This avoids a per-write dominance traversal and retains
    // conservative refusal for conditional first initialization / future lets.
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation);
      if (!store || std::holds_alternative<ModuleStoreConstant>(constants->operations[i])) continue;
      if (store->slot.value >= initialized.size()) return fail(Error::kExport);
      if (!entry_initialized[store->slot.value]) return fail(Error::kInitializer);
      constants->operations[i] = ModuleStoreConstant{store->slot,
          {owner, {static_cast<std::uint32_t>(external + store->slot.value)}}, false};
      ++placed;
    }
    if (placed != stores) return fail(Error::kInitializer);
    if (std::find(initialized.begin(), initialized.end(), false) != initialized.end())
      return fail(Error::kInitializer);
    const auto children = family ? std::span<const FunctionBinding>(family->members.front().children) : std::span<const FunctionBinding>{};
    const auto* cells = family ? family->members.front().function.Cells() : nullptr;
    const auto demands = cells ? std::span<const CellDemand>(cells->demands) : std::span<const CellDemand>{};
    // Class bodies are prepared against this parent's native cells below, just
    // as function families discharge conditional closure writes after building
    // their parent. This fact is provisional: every class body/store/capture must
    // pass preparation and final verification before any record text is emitted.
    const auto closure_writes = function->values.classes.classes.empty()
        ? analysis::ClosureWrites::kUnknown : analysis::ClosureWrites::kLanguage;
    auto evaluated = BuildEvaluationPlan(graph, {budget.bytes}, IncomingCells::kReject, children, bindings->evaluations, demands,
        function->values.initializers.calls, closure_writes, function->values.definitions.sites);
    auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!evaluation) return fail(Error::kValue);
    retain(evaluation->allocated_bytes);
    // First-store declarations and slot/name correspondence above establish
    // the enclosing native record bindings before preparing class descendants.
    class_scope.module_local_names = std::span<const std::u16string>(names).subspan(external);
    class_scope.module_local_evidence = local_names->evidence;
    // File-level occurrence indexes are borrowed across records. Never rescan
    // the whole ABC for every class or use a detached class as a body fallback.
    retain(function->values.definitions.sites.size() * sizeof(ClassDefinitionSite));
    auto definitions = function->values.definitions.sites;
    retain((definitions.capacity() - definitions.size()) * sizeof(ClassDefinitionSite));
    std::sort(definitions.begin(), definitions.end(), [](const auto& a, const auto& b) {
      return a.creation != b.creation ? a.creation < b.creation : a.definition < b.definition;
    });
    const auto class_failure = [&](const ClassTreeFailure& failure, analysis::OpId creation) -> RecordSourceResult {
      const auto classify = [&](auto&& self, const auto& error) -> RecordSourceResult {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_same_v<T, FamilyPrepareFailure>)
          return RouteRefusal{FamilyRouteFailure(error)};
        else if constexpr (std::is_same_v<T, ClassContextFailure> || std::is_same_v<T, CreationContextFailure>)
          return std::visit([&](const auto& nested) { return self(self, nested); }, error.error);
        else if constexpr (std::is_same_v<T, ClassInitializerReadFailure> || std::is_same_v<T, SyntaxResolutionFailure> || std::is_same_v<T, FunctionFileFailure>)
          return std::visit([&](const auto& nested) { return self(self, nested); }, error);
        else if constexpr (std::is_same_v<T, ModuleAliasFailure>)
          return std::visit([&](const auto& nested) { return self(self, nested); }, error.cause);
        else if constexpr (std::is_same_v<T, ModuleReadFailure>)
          return std::visit([&](const auto& nested) { return self(self, nested); }, error.reason);
        else if constexpr (std::is_same_v<T, FunctionValueFailure>) return RouteRefusal{error.cause};
        else if constexpr (requires { error.kind; }) return self(self, error.kind);
        else {
          if constexpr (requires { T::kResourceLimit; })
            if (error == T::kResourceLimit) return fail(Error::kResourceLimit);
          if constexpr (requires { T::kWorkLimit; })
            if (error == T::kWorkLimit)
              return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
          if constexpr (requires { T::kFileChanged; })
            if (error == T::kFileChanged) return fail(Error::kFileChanged);
          if constexpr (requires { T::kRead; })
            if (error == T::kRead) return fail(Error::kRead);
          return RecordProgramFailure{Error::kClass, graph.origins[graph.ops[creation.value].origin.value].pc};
        }
      };
      return std::visit([&](const auto& error) { return classify(classify, error); }, failure);
    };
    struct PendingClass {
      analysis::OpId operation;
      std::shared_ptr<ClassTree> tree;
      std::span<const ClassDefinitionSite> definitions;
    };
    retain(function->values.classes.classes.size() * sizeof(PendingClass));
    std::vector<PendingClass> pending_classes;
    pending_classes.reserve(function->values.classes.classes.size());
    retain((pending_classes.capacity() - function->values.classes.classes.size()) * sizeof(PendingClass));
    for (const auto& occurrence : function->values.classes.classes) {
      if (!functions || !classes) return fail(Error::kClass);
      const auto first = std::lower_bound(definitions.begin(), definitions.end(), occurrence.operation,
          [](const auto& site, auto id) { return site.creation < id; });
      const auto last = std::upper_bound(first, definitions.end(), occurrence.operation,
          [](auto id, const auto& site) { return id < site.creation; });
      auto layout = BuildClassLayout(file, header, index, lookup, roles, *classes,
          *function, occurrence.operation, identity, {budget.bytes},
          initialization[occurrence.operation.value], std::span(first, last));
      if (const auto* failure = std::get_if<ClassTreeFailure>(&layout))
        return class_failure(*failure, occurrence.operation);
      auto definition = std::get<ClassTree>(std::move(layout));
      retain(definition.allocated_bytes);
      auto retained = std::make_shared<ClassTree>(std::move(definition));
      constants->operations[occurrence.operation.value] = retained;
      pending_classes.push_back({occurrence.operation, std::move(retained), std::span(first, last)});
    }
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
          symbol.evidence != (local ? local_names->evidence[slot - external] : NameEvidence::kMetadata) ||
          symbol.name.value >= result->tree.names.size() ||
          result->tree.names[symbol.name.value] != names[slot]) return fail(Error::kModule);
    }
    std::vector<std::string> lexical_names;
    std::vector<std::string_view> lexical_views;
    retain(result->tree.symbols.size() * (sizeof(std::string) + sizeof(std::string_view)));
    lexical_names.reserve(result->tree.symbols.size()); lexical_views.reserve(result->tree.symbols.size());
    retain((lexical_names.capacity() - result->tree.symbols.size()) * sizeof(std::string));
    retain((lexical_views.capacity() - result->tree.symbols.size()) * sizeof(std::string_view));
    for (const auto& symbol : result->tree.symbols) {
      if (symbol.kind == BindingKind::kModule) continue;
      const auto& name = result->tree.names[symbol.name.value];
      // All admitted module/global identifiers are ASCII; non-ASCII locals
      // cannot shadow them. Generated wrappers introduce no named class scope.
      if (std::any_of(name.begin(), name.end(), [](char16_t c) { return c > u'\x7f'; })) continue;
      retain(name.size() + 1);
      lexical_names.emplace_back(name.begin(), name.end());
      retain(lexical_names.back().capacity() - name.size());
    }
    for (const auto& name : lexical_names) lexical_views.push_back(name);
    std::sort(lexical_views.begin(), lexical_views.end());
    class_scope.record_bindings = lexical_views;
    // Parent symbols/scopes now exist. Resolve bodies in this actual native
    // enclosure, keeping the ClassExpr pointer stable. This is single-owner
    // construction: a failed preparation discards the entire unprinted tree.
    // Reserve the complete record-family namespace, not just root owner zero.
    // Every later class/initializer consumes its actual number of occurrences.
    std::uint64_t next_class_owner = result->tree.owner.value +
        static_cast<std::uint64_t>(family ? family->members.size() : 1);
    std::optional<TreeFlow> class_parent_flow;
    if (!pending_classes.empty()) {
      auto checked = VerifyTreeFlow(graph, *control, *evaluation, result->tree, {budget.bytes});
      if (!std::holds_alternative<TreeFlow>(checked)) return fail(Error::kValue);
      class_parent_flow.emplace(std::get<TreeFlow>(std::move(checked)));
      retain(class_parent_flow->allocated_bytes);
    }
    const auto class_parent = class_parent_flow ? std::optional<CaptureTree>{{result->tree, *class_parent_flow}} : std::nullopt;
    for (auto& pending : pending_classes) {
      if (next_class_owner > std::numeric_limits<std::uint32_t>::max()) return fail(Error::kResourceLimit);
      auto built = BuildClassTree(file, header, index, lookup, roles, *functions, *classes,
          *function, pending.operation, identity, &class_scope, {budget.bytes},
          initialization[pending.operation.value], pending.definitions, RecoveryMode::kAllowPartial,
          {static_cast<std::uint32_t>(next_class_owner)}, class_parent ? &*class_parent : nullptr);
      if (const auto* failure = std::get_if<ClassTreeFailure>(&built))
        return class_failure(*failure, pending.operation);
      auto definition = std::get<ClassTree>(std::move(built));
      if (definition.name != pending.tree->name || definition.callable != pending.tree->callable ||
          definition.member_sites != pending.tree->member_sites || !definition.BodiesPrepared())
        return fail(Error::kClass);
      retain(definition.allocated_bytes);
      next_class_owner += definition.constructor->Family().members.size();
      for (const auto& member : definition.members) next_class_owner += member.Family().members.size();
      for (const auto& site : definition.definitions)
        for (const auto& member : site.bodies) if (member) next_class_owner += member->Family().members.size();
      next_class_owner += definition.initializer.has_value();
      *pending.tree = std::move(definition);
    }
    if (!pending_classes.empty()) {
      // The provisional parent check cannot certify bodies. Recheck actual
      // final constants/constructor kinds before family assembly or printing.
      auto checked = VerifyTreeSemantics(graph, *control, *evaluation, result->tree, view, {budget.bytes});
      if (!std::holds_alternative<TreeSemantics>(checked)) return fail(Error::kValue);
      retain(std::get<TreeSemantics>(checked).allocated_bytes);
    }
    // The emitted ESM shares this complete import/local enclosure with its
    // classes and nested functions. Check free names against it as well;
    // missing realm/intrinsic bindings still fail their existing checks.
    const auto globals = VerifyTreeGlobals(result->tree, &class_scope, {budget.bytes});
    if (!std::holds_alternative<TreeGlobals>(globals)) return fail(Error::kValue);
    std::optional<ConstructedFamily> module_family;
    if (!children.empty()) {
      auto checked_flow = VerifyTreeFlow(graph, *control, *evaluation, result->tree, {budget.bytes});
      auto* flow = std::get_if<TreeFlow>(&checked_flow);
      if (!flow) return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{
          FamilyTreeFailure{FileFamily::kRoot, std::get<TreeFlowError>(checked_flow)}})};
      retain(flow->allocated_bytes);
      FamilyMember entry_tree{std::move(result->tree), std::move(*flow), {}, {}, {}, record.offset};
      auto assembled = BuildRecordFamilyTrees(file, header, index, std::move(*family), std::move(entry_tree),
          {budget.bytes}, &context, owner, &lookup);
      if (const auto* error = std::get_if<FamilyTreeFailure>(&assembled))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
      module_family.emplace(std::get<ConstructedFamily>(std::move(assembled)));
      retain(module_family->allocated_bytes);
      auto destination = VerifyFamilyDestination(*module_family, &class_scope, {budget.bytes}, ModuleRole::kInitializer);
      if (const auto* error = std::get_if<FamilyEnclosureFailure>(&destination))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
    }
    std::optional<ModuleDefaultExport> default_export;
    std::optional<std::size_t> default_alias;
    // Metadata owns alias selection; the syntax check never dispatches on a
    // printed name. Keep live/read/shared bindings and incomplete trees intact.
    for (std::size_t i = 0; i < aliases.size(); ++i)
      if (aliases[i] == u"default") default_alias = i;
    if (default_alias) {
      const auto slot = alias_slots[*default_alias];
      std::size_t users = 0;
      for (const auto target : alias_slots) users += target == slot;
      if (slot >= external && local_names->evidence[slot - external] == NameEvidence::kSynthesized && users == 1) {
        const auto& root = module_family ? module_family->members[module_family->root.value].tree : result->tree;
        const ModuleSymbol target{owner, {static_cast<std::uint32_t>(slot)}};
        std::optional<SymbolId> symbol;
        for (const auto& alias : root.modules) if (alias.target == target) symbol = alias.symbol;
        if (symbol) for (std::size_t i = 0; i < root.statements.size(); ++i) {
          const auto* declaration = std::get_if<DeclarationStmt>(&root.statements[i].statement);
          if (!declaration || declaration->symbol != *symbol) continue;
          const ModuleDefaultExport candidate{{static_cast<std::uint32_t>(i)}, target};
          auto checked = CheckModuleDefaultExport(root, module_family ? &*module_family : nullptr, candidate, {budget.bytes});
          if (const auto* error = std::get_if<TreePrintError>(&checked)) {
            if (*error == TreePrintError::kWorkLimit)
              return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kPrintStage, {}}};
            return fail(Error::kResourceLimit);
          }
          const auto& check = std::get<DefaultExportCheck>(checked);
          retain(check.allocated_bytes);
          if (check.eligible) default_export = candidate;
          break;
        }
      }
    }
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
    bool incomplete = false;
    auto printed = [&]() -> std::variant<std::size_t, TreePrintError> {
      if (!module_family) {
        auto emitted = PrintModuleTree(result->tree, output,
            {budget.output.bytes - *prelude_bytes}, {budget.bytes}, &class_scope, default_export);
        if (const auto* error = std::get_if<TreePrintError>(&emitted)) return *error;
        const auto& positions = std::get<PrintedFunction>(emitted);
        incomplete = positions.incomplete;
        return positions.bytes;
      }
      auto emitted = PrintModuleFamilyDraft(*module_family, output,
          {budget.output.bytes - *prelude_bytes}, {budget.bytes}, &class_scope, default_export);
      if (const auto* error = std::get_if<TreePrintError>(&emitted)) return *error;
      const auto& positions = std::get<PrintedFamilyDraft>(emitted);
      incomplete = positions.members[module_family->root.value].incomplete;
      return positions.bytes;
    }();
    const auto* body = std::get_if<std::size_t>(&printed);
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
    std::size_t remaining = budget.output.bytes - *prelude_bytes - *body;
    const auto append = [&](std::string_view text) {
      if (text.size() > remaining) throw fail(Error::kOutputLimit);
      output << text; remaining -= text.size();
    };
    for (std::size_t i = 0; i < aliases.size(); ++i) {
      if (default_export && i == *default_alias) continue;
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
    source.incomplete = incomplete;
    source.environment_cleanup = std::move(cleanup_sites);
    return source;
  } catch (const RecordProgramFailure& failure) { return failure; }
    catch (const std::bad_alloc&) { return fail(Error::kResourceLimit); }
    catch (const std::length_error&) { return fail(Error::kResourceLimit); }
}
RecordSourceResult BuildRecordTree(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    const RecordModuleContext& context, abc::MethodPosition entry,
    RouteInputIdentity identity, RecordProgramBudget budget, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites* functions,
    const analysis::ClassMethodSites* classes) {
  bool proposed = false;
  auto result = BuildRecordTreeAttempt(file, header, index, record, context, entry, identity,
      budget, lookup, roles, functions, classes, true, proposed);
  if (!proposed || std::holds_alternative<PrintedSource>(result)) return result;
  // Metadata is a naming hint, not authority to capture a descendant's global.
  // A failed name-bearing tree is destroyed before this one bounded retry with
  // generated names. Both attempts use identical semantic/publication gates.
  bool retry = false;
  if (const auto* failure = std::get_if<RecordProgramFailure>(&result))
    retry = failure->kind == RecordProgramError::kValue || failure->kind == RecordProgramError::kClass ||
        failure->kind == RecordProgramError::kFunction;
  if (const auto* failure = std::get_if<RouteRefusal>(&result))
    retry = failure->failure.failure_class == RouteFailureClass::kUnproved ||
        failure->failure.failure_class == RouteFailureClass::kUnsupported;
  if (!retry) return result;
  bool unused = false;
  return BuildRecordTreeAttempt(file, header, index, record, context, entry, identity,
      budget, lookup, roles, functions, classes, false, unused);
}

}
