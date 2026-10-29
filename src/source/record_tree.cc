#include "concurrent_print.h"
#include "member_key.h"
#include "tree_print_trace.h"
#include "borrowed_print_trees.h"
#include "record_print_sink.h"
#include "abcd/analysis/detail/prepaid_shared.h"
#include "abcd/source/counting_sink.h"
#include "cell_names.h"
#include "deferred_failure.h"
#include "record_tree.h"
#include "family_route_failure.h"
#include "sendable_metadata.h"
#include "abcd/source/module_exports.h"

#include <algorithm>
#include <numeric>
#include <limits>
#include <sstream>
#include <type_traits>
#include "function_file.h"
#include "environment_cleanup.h"
#include "family_tree.h"
#include "function_context.h"
#include "abcd/analysis/transparent_rethrow.h"
#include "class_tree.h"
#include "creator_classes.h"
#include "family_route_failure.h"
#include "family_print.h"
#include "function_tree.h"
#include "module_aliases.h"
#include "module_default_export.h"
#include "module_binding_name.h"
#include "../analysis/value_flow.h"
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
    const analysis::ClassMethodSites* classes, bool metadata_names, bool& proposed_names, RecordPrintDiagnostics* diagnostics) {
  const auto record_limit=budget.bytes;
  using Error = RecordProgramError;
  const auto fail = [](Error kind) { return RecordProgramFailure{kind, {}}; };
  // Record failure with the typed inner cause that produced it (S2).
  const auto fail_with = [&]<class E>(Error kind, const E& cause) {
    auto failure = fail(kind);
    CollectFailureCauses(cause, failure.causes);
    if constexpr (std::is_same_v<E, FunctionInputFailure>) {
      const auto detail = FamilyRouteFailure(FamilyPrepareFailure{FamilyFileFailure{FunctionFileFailure{cause}}});
      failure.internal_value = detail.internal_value;
      failure.non_executable = detail.non_executable;
      failure.method = detail.method; failure.pc = detail.pc; failure.working_pc = detail.working_pc;
    }
    if constexpr(std::is_same_v<E,TreePrintFailure>) { failure.concurrent=cause.concurrent; failure.printing=cause;failure.method=cause.location.method;failure.pc=cause.location.pc;failure.working_pc=cause.location.working_pc; }
    return failure;
  };
  // Record failure carrying the typed error alternative of a stage result.
  const auto fail_from = [&](Error kind, const auto& result) {
    auto failure = fail(kind);
    std::visit([&](const auto& alternative) {
      using T = std::decay_t<decltype(alternative)>;
      if constexpr (std::is_enum_v<T>) AppendFailureCause(failure.causes, alternative);
      else if constexpr (requires { alternative.kind; })
        if constexpr (std::is_enum_v<std::decay_t<decltype(alternative.kind)>>)
          CollectFailureCauses(alternative, failure.causes);
      if constexpr ((std::is_same_v<T, FunctionTreeFailure> || std::is_same_v<T, EvaluationFailure>)) {
        const auto detail = FamilyRouteFailure(alternative);
        failure.cell_parameter_default = detail.cell_parameter_default;
        failure.family_creation = detail.family_creation;
        failure.concurrent = detail.concurrent;
        failure.call_reference = detail.call_reference;
        failure.exception_region = detail.exception_region;
        failure.async = detail.async;
        failure.evaluation = detail.evaluation;
        failure.internal_value = detail.internal_value;
        failure.non_executable = detail.non_executable;
        failure.tree = detail.tree; failure.deferred = detail.deferred;
        failure.method = detail.method.value_or(index.methods[entry.value].offset);
        if (failure.evaluation && !failure.evaluation->location.method)
          failure.evaluation->location.method = failure.method;
        failure.working_pc = detail.working_pc;
        if (detail.pc) failure.pc = *detail.pc;
      }
    }, result);
    return failure;
  };
  try {
    const auto retain = [&](std::size_t bytes) {
      if (bytes > budget.bytes) throw fail(Error::kRecordBudget);
      budget.bytes -= bytes;
    };
    const auto module = context.ForOwner(file, record.offset);
    if (!module) return fail_with(Error::kModule, RecordModuleProofError::kOwner);
    const ModuleOwnerId owner{record.offset.value};
    auto enclosed = ModuleEnclosure::Build(file, header, context, identity, owner, {budget.bytes});
    auto* enclosure = std::get_if<ModuleEnclosure>(&enclosed);
    if (!enclosure) {
      const auto error = std::get<ModuleEnclosureError>(enclosed);
      if (error == ModuleEnclosureError::kResourceLimit) return fail_with(Error::kResourceLimit, error);
      if (error == ModuleEnclosureError::kFileChanged) return fail_with(Error::kFileChanged, error);
      if (error == ModuleEnclosureError::kWorkLimit)
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      return fail_with(Error::kModule, error);
    }
    retain(enclosure->AllocatedBytes());
    const auto external = enclosure->Names().size();
    // Reuse the wire slot grouping and duplicate-export validation. Source names
    // below are metadata-derived (including anonymous binding synthesis),
    // never names obtained from a paired source file.
    retain(budget.export_bytes);
    auto exports = BuildModuleExports(file, header, *module, {budget.export_bytes, budget.strings});
    const auto* slots = std::get_if<ModuleExports>(&exports);
    if (!slots) return fail_from(Error::kExport, exports);
    auto named = ResolveModuleBindingNames(file, header, *module, {budget.bytes});
    const auto* local_names = std::get_if<ModuleBindingNames>(&named);
    if (!local_names) return std::visit([&](auto error) -> RecordSourceResult {
      using T = decltype(error);
      if (error == T::kResourceLimit) return fail_with(Error::kResourceLimit, error);
      if constexpr (std::is_same_v<T, abc::MetadataError>) {
        if (error == T::kFileChanged) return fail_with(Error::kFileChanged, error);
        if (error == T::kRead) return fail_with(Error::kRead, error);
      } else if (error == T::kWorkLimit) {
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      }
      return fail_with(Error::kExport, error);
    }, std::get<ModuleBindingNameFailure>(named));
    retain(local_names->allocated_bytes);
    if (local_names->locals.size() != slots->slot_count()) return fail(Error::kExport);
    // Entries whose exception regions are not all environment-cleanup handlers
    // (real top-level try/catch) are built as a phased family root, like any
    // phased creator. The route is chosen from the handler shapes, not from a
    // refusal; cleanup-only entries keep the record route unchanged.
    std::optional<FunctionContext> entry_context;
    if (functions && index.methods[entry.value].code && index.methods[entry.value].code->try_count) {
      const auto& method = index.methods[entry.value];
      auto decoded = abc::DecodeMethodBody(file, header.profile, *method.code, {budget.bytes});
      auto* body = std::get_if<abc::MethodBody>(&decoded);
      if (!body) return fail_with(Error::kBody, std::get<abc::BodyFailure>(decoded));
      analysis::EraseTransparentRethrowRegions(*body);
      if (!body->tries.empty() && !OnlyEnvironmentCleanupHandlers(*body)) {
        const auto call = abc::ReadCallType(file, header, index, method.annotations, {budget.bytes});
        const auto* type = std::get_if<abc::CallType>(&call);
        if (!type) return fail_with(Error::kRead, std::get<abc::MetadataError>(call));
        const auto framed = abc::BuildFrameLayout(method.code->registers, method.code->arguments,
            abc::DecodeCallFlags(type->value));
        const auto* frame = std::get_if<abc::FrameLayout>(&framed);
        if (!frame) return fail(Error::kFrame);
        entry_context.emplace(EntryTreeContext{identity, entry, method.offset, *frame});
      }
    }
    std::optional<ConstructedFunction> entry_result;
    ConstructedFunction* result = nullptr;
    GlobalScope class_scope{};
    std::optional<ConstructedFamily> module_family;
    // class_scope.record_bindings borrows these until printing completes.
    std::vector<std::string> lexical_names;
    std::vector<std::string_view> lexical_views;
    std::optional<FileFunction> standalone;
    std::optional<FileFamily> family;
    FileFunction* function = nullptr;
    if (functions) {
      auto read = ReadFunctionFamily(file, header, index, lookup, roles, *functions, entry, identity,
          {budget.bytes}, &context, entry_context ? &*entry_context : nullptr, owner, FamilyClassMode::kRetainRoot, nullptr, {}, nullptr, classes);
      if (const auto* error = std::get_if<FamilyFileFailure>(&read))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
      family.emplace(std::get<FileFamily>(std::move(read)));
      retain(family->allocated_bytes);
      if (family->members.empty() || (family->members.front().function.Phase() != nullptr) != entry_context.has_value())
        return fail(Error::kBody);
      if (!entry_context) function = &family->members.front().function.Normal();
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
            if (failure == FunctionFileError::kResourceLimit) return fail_with(Error::kResourceLimit, failure);
            if (failure == FunctionFileError::kChanged) return fail_with(Error::kFileChanged, failure);
          } else if constexpr (std::is_same_v<T, abc::MetadataError>) {
            if (failure == abc::MetadataError::kResourceLimit) return fail_with(Error::kResourceLimit, failure);
            if (failure == abc::MetadataError::kFileChanged) return fail_with(Error::kFileChanged, failure);
            if (failure == abc::MetadataError::kRead) return fail_with(Error::kRead, failure);
          }
          return fail_with(Error::kBody, failure);
        }, error);
      }
      standalone.emplace(std::move(*function)); function = &*standalone;
      retain(function->allocated_bytes);
    }
    std::vector<EnvironmentCleanupSite> cleanup_sites;
    std::vector<std::u16string> names;
    std::vector<std::u16string> aliases;
    std::vector<std::size_t> alias_slots;
    // Reserve vector arenas and their possible growth allocation together;
    // payloads are charged separately. Name uniqueness uses sorted indices.
    constexpr std::size_t kEntryStorage = 2 * (3 * sizeof(std::u16string) +
        sizeof(ModuleSyntax) + 2 * sizeof(std::size_t));
    const auto entries = external + module->local_exports.size() + module->indirect_exports.size();
    if (entries > budget.bytes / kEntryStorage) return fail(Error::kRecordBudget);
    retain(entries * kEntryStorage);
    names.reserve(external + slots->slot_count()); aliases.reserve(module->local_exports.size());
    alias_slots.reserve(module->local_exports.size());
    for (const auto& name : enclosure->Names()) {
      retain(4 * (name.size() + 1) * sizeof(char16_t));
      names.emplace_back(name.begin(), name.end());
    }
    for (std::size_t slot = 0; slot < local_names->locals.size(); ++slot) {
      const auto& name = local_names->locals[slot];
      retain(4 * (name.size() + 1) * sizeof(char16_t));
      names.push_back(name);
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
    if (!entry_context) {
    const auto& graph = function->values.graph;
    if (function->frame.parameters) return fail(Error::kFrame);
    if (function->cleanup) {
      const auto& cleanup = *function->cleanup;
      const auto count = cleanup.Proof().Pops().size() + cleanup.Proof().Handlers().size() + cleanup.Proof().Moves().size();
      if (count > budget.bytes / sizeof(EnvironmentCleanupSite)) return fail(Error::kRecordBudget);
      retain(count*sizeof(EnvironmentCleanupSite)); cleanup_sites.reserve(count);
      retain((cleanup_sites.capacity()-count)*sizeof(EnvironmentCleanupSite));
      for (const auto& pop : cleanup.Proof().Pops())
        cleanup_sites.push_back({cleanup.Original().instructions[pop.instruction.value].offset,
            CleanupInstructionKind::kPop,cleanup.Original().instructions[pop.allocation.value].offset});
      for (const auto& handler : cleanup.Proof().Handlers())
        cleanup_sites.push_back({cleanup.Original().instructions[handler.rethrow.value].offset,
            CleanupInstructionKind::kRethrow,{}});
      for (const auto move : cleanup.Proof().Moves())
        cleanup_sites.push_back({cleanup.Original().instructions[move.value].offset,
            CleanupInstructionKind::kMove,{}});
      std::sort(cleanup_sites.begin(),cleanup_sites.end(),[](const auto& a,const auto& b) { return a.pc < b.pc; });
    }
    auto controlled = BuildControlPlan(graph, {budget.bytes});
    auto* control = std::get_if<ControlPlan>(&controlled);
    if (!control) return fail_from(Error::kBody, controlled);
    retain(control->allocated_bytes);
    auto resolved = ResolveTreeConstants(file, header, index, index.methods[entry.value].offset, graph, {budget.bytes});
    auto* constants = std::get_if<TreeConstants>(&resolved);
    if (!constants) return fail_from(Error::kValue, resolved);
    retain(constants->allocated_bytes);
    auto found_prototypes = FindClassPrototypeReads(graph, {budget.bytes});
    auto* prototype_reads = std::get_if<ClassPrototypeReads>(&found_prototypes);
    if (!prototype_reads) {
      const auto error = std::get<ClassPrototypeError>(found_prototypes);
      if (error == ClassPrototypeError::kResourceLimit) return fail_with(Error::kResourceLimit, error);
      if (error == ClassPrototypeError::kWorkLimit)
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      return fail_with(Error::kValue, error);
    }
    retain(prototype_reads->allocated_bytes);
    std::erase_if(prototype_reads->reads, [&](const auto& site) {
      return !HasClassPrototypeKey(site, constants->View(), constants->names, constants->texts);
    });
    auto projected = ProjectClassMembers(graph, function->values.initializers.calls, {budget.bytes}, function->values.definitions.sites, function->self_order ? &*function->self_order : nullptr, prototype_reads->reads);
    const auto* projection = std::get_if<ClassProjection>(&projected);
    if (!projection) {
      const auto error = std::get<ClassInitializerError>(projected);
      if (error == ClassInitializerError::kResourceLimit) return fail_with(Error::kResourceLimit, error);
      if (error == ClassInitializerError::kWorkLimit)
        return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
      return fail_with(Error::kInitializer, error);
    }
    retain(projection->allocated_bytes);
    for (const auto read : projection->prototypes) {
      const auto site = std::lower_bound(prototype_reads->reads.begin(), prototype_reads->reads.end(), read,
          [](const auto& entry, auto id) { return entry.read < id; });
      if (site != prototype_reads->reads.end() && site->read == read) continue;
      const auto* name = std::get_if<NameId>(&constants->operations[read.value]);
      if (!name || name->value >= constants->names.size() || constants->names[name->value] != u"prototype")
        return fail(Error::kInitializer);
    }
    class_scope.module_enclosure = enclosure;
    FunctionSyntax syntax{};
    syntax.identity = identity; syntax.method = index.methods[entry.value].offset; syntax.owner = {family ? 0U : 1U};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    if (metadata_names) {
      auto named = ResolveCellNames(file, header, index, index.methods[entry.value].offset,
          graph, constants->names, syntax.locals, {budget.bytes});
      if (const auto* storage = std::get_if<CellNameStorage>(&named)) retain(storage->bytes);
      else if (const auto* error = std::get_if<abc::MetadataError>(&named)) {
        if (*error == abc::MetadataError::kFileChanged) return fail_with(Error::kFileChanged, *error);
        if (*error == abc::MetadataError::kResourceLimit) return fail_with(Error::kResourceLimit, *error);
        return fail_with(Error::kRead, *error);
      } else {
        const auto constant_error = std::get<TreeConstantError>(named);
        if (constant_error == TreeConstantError::kResourceLimit) return fail_with(Error::kResourceLimit, constant_error);
        if (constant_error == TreeConstantError::kWorkLimit)
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
        return fail_with(Error::kValue, constant_error);
      }
      proposed_names = !syntax.locals.empty();
    }
    // The entry tree's module symbol table: imports read-only, locals mutable,
    // in the shared metadata order above.
    syntax.modules.reserve(names.size());
    const auto name_count = constants->names.size() + names.size();
    if (name_count < constants->names.size()) return fail(Error::kRecordBudget);
    if (name_count > constants->names.capacity()) {
      if (name_count > budget.bytes / sizeof(std::u16string)) return fail(Error::kRecordBudget);
      retain(name_count * sizeof(std::u16string));
      constants->names.reserve(name_count);
      retain((constants->names.capacity() - name_count) * sizeof(std::u16string));
    }
    for (std::size_t symbol = 0; symbol < names.size(); ++symbol) {
      const bool local = symbol >= external;
      syntax.modules.push_back({{owner, {static_cast<std::uint32_t>(symbol)}},
          {static_cast<std::uint32_t>(constants->names.size())},
          local ? ModuleAccess::kMutable : ModuleAccess::kReadOnly,
          local ? local_names->evidence[symbol - external] : NameEvidence::kMetadata, local});
      constants->names.push_back(names[symbol]);
    }
    auto bound = BindModuleAliases(file, header, index, function->input, function->values,
        context, owner, {}, {budget.bytes});
    auto* bindings = std::get_if<ModuleAliases>(&bound);
    if (!bindings) {
      const auto classify = [&](const auto& error) -> RecordSourceResult {
        using T = std::decay_t<decltype(error)>;
        if (error == T::kResourceLimit) return fail_with(Error::kResourceLimit, error);
        if constexpr (std::is_same_v<T, abc::MetadataError>) {
          if (error == T::kFileChanged) return fail_with(Error::kFileChanged, error);
          if (error == T::kRead) return fail_with(Error::kRead, error);
        } else if (error == T::kWorkLimit) {
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
        }
        return fail_with(Error::kModule, error);
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
          names[slot] != bindings->names[alias.name.value]) return fail_with(Error::kModule, RecordModuleProofError::kAliasBinding);
    }
    if (slots->slot_count() > budget.bytes / sizeof(std::optional<analysis::OpId>))
      return fail(Error::kRecordBudget);
    retain(slots->slot_count() * sizeof(std::optional<analysis::OpId>));
    std::vector<std::optional<analysis::OpId>> initializers(slots->slot_count());
    retain((initializers.capacity() - initializers.size()) * sizeof(initializers.front()));
    std::size_t stores = 0, placed = 0;
    for (const auto& op : graph.ops)
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueModuleWrite>(op.operation)) ++stores;
    // Only the root control sequence owns native record declarations. Branch
    // joins may precede them. Subsequent arm/loop writes remain assignments,
    // never new declarations or hoisted stores.
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
          if (store->slot.value >= initializers.size()) return fail(Error::kExport);
          constants->operations[i] = ModuleStoreConstant{store->slot,
              {owner, {static_cast<std::uint32_t>(external + store->slot.value)}}, !initializers[store->slot.value]};
          if (!initializers[store->slot.value]) initializers[store->slot.value] = analysis::OpId{static_cast<std::uint32_t>(i)};
          ++placed;
        }
      }
    }
    // A native declaration must have completed on every normal path to an
    // assignment. Use one fresh shared dominance proof, then O(1) queries per
    // write; physical entry/same-block placement is not an admission rule.
    // This normal-only route does not authorize catch or conditional first
    // initialization. Root declaration placement is checked by tree flow.
    std::optional<analysis::ValueFlowProof> initialization_flow;
    if (placed != stores) {
      auto checked = analysis::VerifyValueFlow(graph, {budget.bytes});
      if (const auto* error = std::get_if<analysis::ValueAvailabilityError>(&checked)) {
        if (*error == analysis::ValueAvailabilityError::kResourceLimit) return fail_with(Error::kResourceLimit, *error);
        if (*error == analysis::ValueAvailabilityError::kWorkLimit)
          return RouteRefusal{{RouteFailureClass::kWorkLimit, ValueRouteError::kContextStage, {}}};
        return fail_with(Error::kValue, *error);
      }
      initialization_flow.emplace(std::get<analysis::ValueFlowProof>(std::move(checked)));
      retain(initialization_flow->allocated_bytes);
    }
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      const auto& op = graph.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation);
      if (!store || std::holds_alternative<ModuleStoreConstant>(constants->operations[i])) continue;
      if (store->slot.value >= initializers.size()) return fail(Error::kExport);
      const auto initialized = initializers[store->slot.value];
      if (!initialized || !initialization_flow) return fail(Error::kInitializer);
      const auto& declaration = graph.ops[initialized->value];
      if (declaration.block == op.block ? declaration.order >= op.order :
          !initialization_flow->dominators.Dominates({declaration.block.value}, {op.block.value}))
        return fail(Error::kInitializer);
      constants->operations[i] = ModuleStoreConstant{store->slot,
          {owner, {static_cast<std::uint32_t>(external + store->slot.value)}}, false};
      ++placed;
    }
    if (placed != stores) return fail(Error::kInitializer);
    if (std::find(initializers.begin(), initializers.end(), std::nullopt) != initializers.end())
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
    std::optional<ArrayInitializationError> array_error;
    auto evaluated = BuildEvaluationPlan(graph, {budget.bytes}, IncomingCells::kReject, children, bindings->evaluations, demands,
        function->values.initializers.calls, closure_writes, function->values.definitions.sites, {}, {}, function->self_order ? &*function->self_order : nullptr, prototype_reads->reads,
        {identity.contract, constants->View(), constants->names, &array_error});
    auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!evaluation) {
      if (array_error) return fail_with(Error::kValue, *array_error);
      return fail_from(Error::kValue, evaluated);
    }
    retain(evaluation->allocated_bytes);
    // First-store declarations and slot/name correspondence above establish
    // the enclosing native record bindings before preparing class descendants.
    class_scope.module_local_names = std::span<const std::u16string>(names).subspan(external);
    class_scope.module_local_evidence = local_names->evidence;
    // File-level occurrence indexes are borrowed across records. Never rescan
    // the whole ABC for every class or use a detached class as a body fallback.
    const auto class_failure = [&](const ClassTreeFailure& failure, std::optional<analysis::OpId> creation) -> RecordSourceResult {
      const auto classify = [&](auto&& self, const auto& error) -> RecordSourceResult {
        using T = std::decay_t<decltype(error)>;
        if constexpr (std::is_same_v<T, SendableFailure>) {
          if(error.kind==SendableError::kResourceLimit || error.kind==SendableError::kWorkLimit) {
            RouteFailure failure{error.kind==SendableError::kResourceLimit?RouteFailureClass::kMemoryLimit:RouteFailureClass::kWorkLimit,ValueRouteError::kTreeStage,error.pc};
            failure.sendable=error;failure.sendable->record=record.offset;failure.method=error.method;failure.working_pc=error.working_pc;
            AppendFailureCause(failure.causes,error.kind);return RouteRefusal{failure};
          }
          RecordProgramFailure failure{Error::kClass,error.pc};failure.sendable=error;
          failure.sendable->record=record.offset;failure.method=error.method;failure.working_pc=error.working_pc;
          AppendFailureCause(failure.causes,error.kind);
          std::visit([&](auto cause){if constexpr(!std::is_same_v<decltype(cause),std::monostate>)AppendFailureCause(failure.causes,cause);},error.cause);
          return failure;
        } else if constexpr (std::is_same_v<T, FamilyPrepareFailure>)
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
          return RecordProgramFailure{Error::kClass, creation ?
              std::optional<abc::CodeOffset>{graph.origins[graph.ops[creation->value].origin.value].pc} : std::nullopt};
        }
      };
      return std::visit([&](const auto& error) { return classify(classify, error); }, failure);
    };
    std::optional<CreatorClassInput> class_input;
    CreatorClasses creator_classes;
    if (!function->values.classes.classes.empty()) {
      if (!functions || !classes) return fail(Error::kClass);
      class_input.emplace(CreatorClassInput{file,header,index,lookup,roles,*functions,*classes,*function,identity,
          {index.methods[entry.value].offset,family?family->first_owner:syntax.owner,{0}}});
      auto discovered=DiscoverCreatorClasses(*class_input,{budget.bytes});
      if(const auto* failure=std::get_if<CreatorClassFailure>(&discovered))
        return class_failure(failure->cause,failure->operation);
      creator_classes=std::get<CreatorClasses>(std::move(discovered));
      retain(creator_classes.allocated_bytes);
      for(const auto& task:creator_classes.tasks) constants->operations[task.operation.value]=task.tree;
      const auto first=std::uint64_t{syntax.owner.value}+(family? family->members.size():1);
      if(first>UINT32_MAX)return fail(Error::kResourceLimit);
      auto prepared=PrepareCreatorClasses(*class_input,creator_classes,&class_scope,
          {static_cast<std::uint32_t>(first)},RecoveryMode::kAllowPartial,{budget.bytes});
      if(const auto* failure=std::get_if<CreatorClassFailure>(&prepared))
        return class_failure(failure->cause,failure->operation);
      retain(std::get<CreatorClassCost>(prepared).allocated_bytes);
    }
    auto& pending_classes=creator_classes.tasks;
    {
      // Root cells that direct children capture before their first store
      // (deferred TDZ initializations): this entry tree must declare them there.
      std::size_t count = 0;
      if (family) for (const auto& member : family->members)
        if (member.incoming && member.incoming->parent == FileFamily::kRoot) count += member.incoming->cells.deferred.size();
      for (const auto& pending : pending_classes) for (const auto& body : pending.preparation->bodies)
        if (const auto* prepared = std::get_if<FamilyPreparation>(&body))
          if (const auto* captures = prepared->RootCapture()) count += captures->deferred.size();
      if (count) {
        retain(count * sizeof(DeferredCellInitialization));
        std::vector<DeferredCellInitialization> deferred;
        deferred.reserve(count);
        if (family) for (const auto& member : family->members)
          if (member.incoming && member.incoming->parent == FileFamily::kRoot)
            deferred.insert(deferred.end(), member.incoming->cells.deferred.begin(), member.incoming->cells.deferred.end());
        for (const auto& pending : pending_classes) for (const auto& body : pending.preparation->bodies)
          if (const auto* prepared = std::get_if<FamilyPreparation>(&body))
            if (const auto* captures = prepared->RootCapture())
              deferred.insert(deferred.end(), captures->deferred.begin(), captures->deferred.end());
        const auto deferred_failure = [&](DeferredCellError reason, const DeferredCellInitialization* selected) {
          auto failure = fail_with(Error::kFunction, FamilyTreeError::kDeferredCell);
          failure.deferred = DeferredCellEvidence{reason}; failure.deferred->creator = function->input.Method();
          // The merged storage list may contain equal cells for distinct child
          // occurrences. Locate the request by ordinal, never by cell equality.
          std::size_t ordinal = selected ? static_cast<std::size_t>(selected - deferred.data()) : count;
          const auto locate = [&](const CaptureCells& captures, std::span<const CaptureUse> uses) {
            if (failure.deferred->child) return;
            if (ordinal < captures.deferred.size()) {
              failure.deferred = DescribeDeferredFailure(reason, graph, function->values.cells ? &*function->values.cells : nullptr,
                  captures, &captures.deferred[ordinal], uses);
            } else ordinal -= captures.deferred.size();
          };
          if (family) for (const auto& member : family->members)
            if (member.incoming && member.incoming->parent == FileFamily::kRoot)
              locate(member.incoming->cells, member.function.Capture().Input().Captures());
          for (const auto& pending : pending_classes) for (const auto& body : pending.preparation->bodies)
            if (const auto* prepared = std::get_if<FamilyPreparation>(&body))
              if (const auto* captures = prepared->RootCapture()) locate(*captures, prepared->RootUses());
          AppendFailureCause(failure.causes, reason);
          failure.method = failure.deferred->creator;
          return failure;
        };
        if (!evaluation->cells) return deferred_failure(DeferredCellError::kBinding, &deferred.front());
        std::optional<std::size_t> rejected;
        if (!MarkDeferredCells(graph, *evaluation->cells, deferred, {budget.bytes}, &rejected))
          return deferred_failure(DeferredCellError::kPlacement, rejected ? &deferred[*rejected] : nullptr);
        retain(evaluation->cells->deferred.capacity() * sizeof(std::optional<analysis::OpId>));
      }
    }
    const auto view = constants->View();
    syntax.names = std::move(constants->names); syntax.texts = std::move(constants->texts);
    auto constructed = BuildFunctionTree(graph, *control, *evaluation, std::move(syntax), view, {budget.bytes});
    auto* built = std::get_if<ConstructedFunction>(&constructed);
    if (!built) return fail_from(Error::kValue, constructed);
    entry_result.emplace(std::move(*built)); result = &*entry_result;
    retain(result->allocated_bytes);
    // Recheck the actual emitted tree against the complete metadata inventory.
    // Entry-body locals and imports occupy the same native ESM scope even
    // though the shared function tree models their ownership separately.
    if (result->tree.modules.size() != names.size()) return fail_with(Error::kModule, RecordModuleProofError::kReadInventory);
    retain(names.size());
    std::vector<bool> seen(names.size());
    for (const auto& alias : result->tree.modules) {
      const auto slot = alias.target.symbol.value;
      if (alias.target.owner != owner || slot >= names.size() || seen[slot] ||
          alias.symbol.value >= result->tree.symbols.size()) return fail_with(Error::kModule, RecordModuleProofError::kSymbolRange);
      seen[slot] = true;
      const bool local = slot >= external;
      const auto& symbol = result->tree.symbols[alias.symbol.value];
      if (alias.local != local || alias.access != (local ? ModuleAccess::kMutable : ModuleAccess::kReadOnly) ||
          symbol.kind != (local ? BindingKind::kLocal : BindingKind::kModule) ||
          symbol.evidence != (local ? local_names->evidence[slot - external] : NameEvidence::kMetadata) ||
          symbol.name.value >= result->tree.names.size() ||
          result->tree.names[symbol.name.value] != names[slot]) return fail_with(Error::kModule, RecordModuleProofError::kSymbolBinding);
    }
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
    if (class_input) {
      if(next_class_owner>UINT32_MAX)return fail(Error::kResourceLimit);
      auto finished=FinishCreatorClasses(*class_input,creator_classes,&class_scope,
          {static_cast<std::uint32_t>(next_class_owner)},class_parent ? &*class_parent : nullptr,
          RecoveryMode::kAllowPartial,{budget.bytes});
      if(const auto* failure=std::get_if<CreatorClassFailure>(&finished))
        return class_failure(failure->cause,failure->operation);
      retain(std::get<CreatorClassCost>(finished).allocated_bytes);
    }
    if (!pending_classes.empty()) {
      // The provisional parent check cannot certify bodies. Recheck actual
      // final constants/constructor kinds before family assembly or printing.
      auto checked = VerifyTreeSemantics(graph, *control, *evaluation, result->tree, view, {budget.bytes});
      if (!std::holds_alternative<TreeSemantics>(checked)) return fail(Error::kValue);
      retain(std::get<TreeSemantics>(checked).allocated_bytes);
      result->flow=std::move(std::get<TreeSemantics>(checked).flow);
    }
    // The emitted ESM shares this complete import/local enclosure with its
    // classes and nested functions. Check free names against it as well;
    // missing realm/intrinsic bindings still fail their existing checks.
    const auto globals = VerifyTreeGlobals(result->tree, &class_scope, {budget.bytes});
    if (!std::holds_alternative<TreeGlobals>(globals)) return fail(Error::kValue);
    if (!children.empty()) {
      // Final construction/default/class correspondence already owns the
      // original facts. Do not replay verification or retain a second copy.
      if(!result->flow)return fail(Error::kValue);
      auto* flow=&*result->flow;
      FamilyMember entry_tree{std::move(result->tree), std::move(*flow), {}, {}, {}, record.offset};
      family->next_class_owner = pending_classes.empty() ? next_class_owner : pending_classes.back().owners->end;
      auto assembled = BuildRecordFamilyTrees(file, header, index, std::move(*family), std::move(entry_tree),
          {budget.bytes}, &context, owner, &lookup, &class_scope);
      if (const auto* error = std::get_if<FamilyTreeFailure>(&assembled))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
      module_family.emplace(std::get<ConstructedFamily>(std::move(assembled)));
      retain(module_family->allocated_bytes);
      auto destination = VerifyFamilyDestination(*module_family, &class_scope, {budget.bytes}, ModuleRole::kInitializer);
      if (const auto* error = std::get_if<FamilyEnclosureFailure>(&destination))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
    }
    } else {
      // Same module scope as the record route: descendant classes read the
      // module enclosure while the family is built.
      class_scope.module_enclosure = enclosure;
      class_scope.module_local_names = std::span<const std::u16string>(names).subspan(external);
      class_scope.module_local_evidence = local_names->evidence;
      auto built = BuildFileFamilyTrees(file, header, index, std::move(*family), *entry_context,
          {budget.bytes}, &context, owner, &lookup, RecoveryMode::kCompleteOnly, nullptr, &class_scope);
      if (const auto* error = std::get_if<FamilyTreeFailure>(&built))
        return RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{*error})};
      module_family.emplace(std::get<ConstructedFamily>(std::move(built)));
      retain(module_family->allocated_bytes);
      // Same inventory obligation as the record route: every local slot is an
      // entry-owned binding with its metadata spelling, declared exactly once.
      {
        const auto& root = module_family->members[module_family->root.value].tree;
        const auto locals = names.size() - external;
        retain(locals * (sizeof(std::optional<SymbolId>) + 1));
        std::vector<std::optional<SymbolId>> symbols(locals);
        std::vector<std::uint8_t> declared(locals);
        for (const auto& alias : root.modules) {
          const auto slot = alias.target.symbol.value;
          if (alias.target.owner != owner || slot >= names.size() || alias.symbol.value >= root.symbols.size())
            return fail_with(Error::kModule, RecordModuleProofError::kSymbolRange);
          if (slot < external) continue;
          const auto& symbol = root.symbols[alias.symbol.value];
          if (!alias.local || alias.access != ModuleAccess::kMutable || symbol.kind != BindingKind::kLocal ||
              symbol.evidence != local_names->evidence[slot - external] || symbols[slot - external] ||
              symbol.name.value >= root.names.size() || root.names[symbol.name.value] != names[slot])
            return fail_with(Error::kModule, RecordModuleProofError::kSymbolBinding);
          symbols[slot - external] = alias.symbol;
        }
        for (const auto& statement : root.statements) {
          const auto* declaration = std::get_if<DeclarationStmt>(&statement.statement);
          if (!declaration) continue;
          for (std::size_t slot = 0; slot < locals; ++slot)
            if (symbols[slot] == declaration->symbol) {
              if (declared[slot]) return fail(Error::kInitializer);
              declared[slot] = 1;
            }
        }
        if (std::find(declared.begin(), declared.end(), 0) != declared.end()) return fail(Error::kInitializer);
      }
      {
        // Record lexical names for the same destination checks as the record route.
        const auto& root = module_family->members[module_family->root.value].tree;
        retain(root.symbols.size() * (sizeof(std::string) + sizeof(std::string_view)));
        lexical_names.reserve(root.symbols.size()); lexical_views.reserve(root.symbols.size());
        for (const auto& symbol : root.symbols) {
          if (symbol.kind == BindingKind::kModule) continue;
          const auto& name = root.names[symbol.name.value];
          if (std::any_of(name.begin(), name.end(), [](char16_t c) { return c > u'\x7f'; })) continue;
          retain(name.size() + 1);
          lexical_names.emplace_back(name.begin(), name.end());
        }
        for (const auto& name : lexical_names) lexical_views.push_back(name);
        std::sort(lexical_views.begin(), lexical_views.end());
        class_scope.record_bindings = lexical_views;
      }
      const auto globals = VerifyTreeGlobals(module_family->members[module_family->root.value].tree, &class_scope, {budget.bytes});
      if (!std::holds_alternative<TreeGlobals>(globals)) return fail(Error::kValue);
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
          if (const auto* error = std::get_if<TreePrintFailure>(&checked)) {
            if (*error == TreePrintError::kWorkLimit)
              return RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{*error})};
            return fail_with(Error::kResourceLimit, *error);
          }
          const auto& check = std::get<DefaultExportCheck>(checked);
          retain(check.allocated_bytes);
          if (check.eligible) default_export = candidate;
          break;
        }
      }
    }
    // Walk owned syntax once, before printing. Class gaps are diagnostics, not
    // functions to recover or print. Every queue/output growth is admitted
    // before allocation; nested families keep their original occurrence order.
    std::uint64_t omission_work = 0;
    const auto omission_step = [&]() {
      if (omission_work == analysis::kDefaultSemanticWork)
        throw RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{FamilyPrepareError::kWorkLimit})};
      ++omission_work;
    };
    std::vector<OmittedClassMember> omitted_members;
    std::vector<std::shared_ptr<const FailureCauseChain>> omission_causes;
    std::vector<const FunctionTree*> omission_trees;
    const auto grow_omissions = [&](auto& items) {
      if (items.size() < items.capacity()) return;
      const auto old = items.capacity();
      const auto capacity = old ? old * 2 : 1;
      if (capacity < old) throw fail(Error::kRecordBudget);
      using Item = typename std::decay_t<decltype(items)>::value_type;
      // Old and replacement buffers can coexist during vector relocation.
      if (capacity > budget.bytes / sizeof(Item)) throw fail(Error::kRecordBudget);
      retain(capacity * sizeof(Item));
      items.reserve(capacity);
      if (items.capacity() > capacity) retain((items.capacity() - capacity) * sizeof(Item));
    };
    const auto queue_tree = [&](const FunctionTree& tree) {
      omission_step(); grow_omissions(omission_trees); omission_trees.push_back(&tree);
    };
    const auto queue_family = [&](const ConstructedFamily& family) {
      for (const auto& member : family.members) queue_tree(member.tree);
    };
    if (module_family) queue_family(*module_family); else queue_tree(result->tree);
    for (std::size_t cursor = 0; cursor < omission_trees.size(); ++cursor) {
      for (const auto& expression : omission_trees[cursor]->expressions) {
        omission_step();
        const auto* cls = std::get_if<ClassExpr>(&expression.expression);
        if (!cls || !cls->definition) continue;
        const auto& definition = *cls->definition;
        const auto append = [&](const ClassMemberOmission& gap) {
          omission_step(); grow_omissions(omitted_members);
          OmittedClassMember item{definition.creation.target, gap.root, gap.method,
              std::visit([](const auto& proof) -> decltype(OmittedClassMember::cause) {
                if constexpr (std::is_same_v<std::decay_t<decltype(proof)>, PhasedControlFailure>) return proof.input_gap->cause.kind;
                else return proof.kind;
              }, gap.failure),
              gap.Pc(), gap.working_pc, gap.parent, gap.creation_pc, {}, {}};
          if (const auto* proof = std::get_if<analysis::SemanticProofFailure>(&gap.failure))
            item.internal_value = proof->internal_value;
          if (const auto* proof = std::get_if<analysis::ExceptionInputFailure>(&gap.failure)) {
            item.node = proof->node; item.value = proof->value;
          }
          if (const auto* proof = std::get_if<PhasedControlFailure>(&gap.failure)) {
            const auto& region = std::get<ExceptionControlRegionFailure>(proof->cause);
            item.control = region.kind; item.exception_region = region.exception_region;
            if (proof->completion_ports_failure) {
              item.protocol_location=proof->completion_ports_failure->location;
              retain((kMaxFailureCauses + 1) * kMaxFailureDomainBytes);
              analysis::detail::IdfScratch memory{budget.bytes};std::size_t charged=0;
              auto causes = std::allocate_shared<FailureCauseChain>(
                  analysis::detail::PrepaidSharedAllocator<FailureCauseChain>{memory,charged});
              retain(charged);
              CollectFailureCauses(*proof->completion_ports_failure, *causes);
              if(omission_causes.size()>=UINT32_MAX)throw fail(Error::kRecordBudget);
              grow_omissions(omission_causes);
              item.additional_causes=OmissionCauseId{static_cast<std::uint32_t>(omission_causes.size())};
              omission_causes.push_back(std::move(causes));
            }
          }
          if(gap.later_body) {
            // The published gap stays stable. A failed refinement is separate
            // diagnostic evidence, not a replacement for verified partial text.
            // Reserve both the projection scratch and retained domain strings
            // before projection; borrow the failure rather than copying it.
            retain(2*(kMaxFailureCauses+1)*kMaxFailureDomainBytes);
            const auto later=FamilyRouteFailure(*gap.later_body);
            analysis::detail::IdfScratch memory{budget.bytes};std::size_t charged=0;
            auto causes=std::allocate_shared<FailureCauseChain>(
                analysis::detail::PrepaidSharedAllocator<FailureCauseChain>{memory,charged});
            retain(charged);causes->Extend(later.causes);
            if(omission_causes.size()>=UINT32_MAX)throw fail(Error::kRecordBudget);
            grow_omissions(omission_causes);item.later_causes=OmissionCauseId{static_cast<std::uint32_t>(omission_causes.size())};
            omission_causes.push_back(std::move(causes));
            item.later_location=TreeLocation{.method=later.method,.pc=later.pc,.working_pc=later.working_pc};
            item.later_reference=later.call_reference;
          }
          omitted_members.push_back(item);
        };
        for (const auto& gap : definition.omissions) append(gap);
        if (definition.constructor) queue_family(definition.constructor->Family());
        for (const auto& member : definition.members) if (member) queue_family(member->Family());
        for (const auto& site : definition.definitions) {
          for (const auto& gap : site.omissions) if (gap) append(*gap);
          for (const auto& member : site.bodies) if (member) queue_family(member->Family());
        }
        if (definition.initializer) queue_tree(definition.initializer->tree);
      }
    }
    // The immutable root/family and class owners were retained above. This
    // mirror never grants more bytes: the original record ledger keeps that
    // charge until its owners die. Borrowing is limited to exact root identities.
    TreePrintTrace print_trace;
    print_trace.failure.site=TreePrintSite::kPublication;
    const auto& printed_root=module_family?module_family->members[module_family->root.value].tree:result->tree;
    print_trace.failure.location.method=printed_root.method;print_trace.failure.owner=printed_root.owner;
    PrintMemory held_record(record_limit);
    auto held=held_record.Reserve(record_limit-budget.bytes);
    PrintMemory memory(budget.bytes);
    BorrowedPrintTrees borrowed(module_family?nullptr:&result->tree,
        module_family?&*module_family:nullptr,held,memory,{budget.bytes});
    if (omission_work > analysis::kDefaultSemanticWork - borrowed.Work())
      throw RouteRefusal{FamilyRouteFailure(FamilyPrepareFailure{FamilyPrepareError::kWorkLimit})};
    std::uint64_t remaining_work=analysis::kDefaultSemanticWork-borrowed.Work()-omission_work;
    struct PrintPass {std::size_t bytes,peak;bool incomplete;bool arkts;};
    const auto write=[&](std::ostream& output)->PrintPass {
    // Reserve the nested phase allowance before it can allocate. Its own
    // ledgers enforce that allowance. The lease ends only after every returned
    // position table and checker has been destroyed; output lives outside it.
    auto scratch=memory.Reserve(memory.Remaining());
      ConcurrentPrint output_syntax;
      const auto prelude = enclosure->WriteImports(file, header, output, budget.output,
          {std::min(budget.strings.bytes, scratch.Bytes())});
      const auto* prelude_bytes = std::get_if<std::size_t>(&prelude);
      if (!prelude_bytes) {
        const auto import_error = std::get<ModuleImportFailure>(prelude).kind;
        switch (import_error) {
          case ModuleImportError::kOutputLimit: throw fail_with(Error::kOutputLimit, import_error);
          case ModuleImportError::kResourceLimit: throw fail_with(Error::kResourceLimit, import_error);
          case ModuleImportError::kWrite: throw fail_with(Error::kWrite, import_error);
          case ModuleImportError::kContract:
          case ModuleImportError::kRequestPhases:
          case ModuleImportError::kBinding:
          case ModuleImportError::kModuleKind:
          case ModuleImportError::kRequestIndex:
          case ModuleImportError::kString: throw fail_with(Error::kModule, import_error);
        }
      }
      bool incomplete = false;bool arkts=enclosure->Shared();
      std::size_t peak=0;std::uint64_t work=0;
      auto printed = [&]() -> std::variant<std::size_t, TreePrintFailure> {
        if (!module_family) {
          auto emitted = PrintModuleTree(result->tree, output,
              {budget.output.bytes - *prelude_bytes}, {scratch.Bytes(),remaining_work}, &class_scope, default_export,result->flow?result->flow->original.get():nullptr);
          if (const auto* error = std::get_if<TreePrintFailure>(&emitted)) return *error;
          const auto& positions = std::get<PrintedFunction>(emitted);
          incomplete = positions.incomplete;arkts|=positions.arkts;peak=positions.allocated_bytes;work=positions.work;
          return positions.bytes;
        }
        auto emitted = PrintModuleFamilyDraft(*module_family, output,
            {budget.output.bytes - *prelude_bytes}, {scratch.Bytes(),remaining_work}, &class_scope, default_export);
        if (const auto* error = std::get_if<TreePrintFailure>(&emitted)) return *error;
        const auto& positions = std::get<PrintedFamilyDraft>(emitted);
        incomplete = positions.members[module_family->root.value].incomplete;
        for(const auto& member:positions.members)arkts|=member.arkts;peak=positions.allocated_bytes;work=positions.work;
        return positions.bytes;
      }();
      const auto* body = std::get_if<std::size_t>(&printed);
      if (!body) {
        const auto print_error = std::get<TreePrintFailure>(printed);
        switch (print_error.kind) {
          case TreePrintError::kResourceLimit: throw fail_with(Error::kResourceLimit, print_error);
          case TreePrintError::kOutputLimit: throw fail_with(Error::kOutputLimit, print_error);
          case TreePrintError::kWrite: throw fail_with(Error::kWrite, print_error);
          case TreePrintError::kUnsupportedContext:
          case TreePrintError::kInvalidLiteral: throw fail_with(Error::kValue, print_error);
          case TreePrintError::kWorkLimit:
            throw RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{print_error})};
        }
      }
      if(enclosure->Shared()&&incomplete) {
        RecordProgramFailure failure{Error::kModule,{}};
        failure.shared_module=InspectSharedModule(context,file,record.offset,identity.contract);
        failure.shared_module->kind=SharedModuleError::kCorrespondence;
        AppendFailureCause(failure.causes,SharedModuleError::kCorrespondence);throw failure;
      }
      const auto arkts_binding_failure=[&] {
        RecordProgramFailure failure{Error::kModule,{}};
        failure.method=index.methods[entry.value].offset;
        SendableFailure detail;detail.kind=SendableError::kBinding;detail.record=record.offset;detail.method=*failure.method;
        failure.sendable=detail;AppendFailureCause(failure.causes,detail.kind);return failure;
      };
      // .ets uses the pinned dynamic TS parser, which cannot spell quoted
      // ModuleExportName imports. Refuse metadata syntax, never fake a read or
      // publish a file the required frontend cannot parse.
      if(arkts && output_syntax.HasQuotedModuleName())throw arkts_binding_failure();
      if(output_syntax.Method()&&output_syntax.HasQuotedModuleName()) {
        ConcurrentFailure failure;failure.kind=ConcurrentError::kModuleSyntax;
        failure.method=*output_syntax.Method();failure.record=record.offset;
        TreePrintFailure error{TreePrintError::kUnsupportedContext};error.concurrent=failure;
        throw RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{error})};
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
        if(output_syntax.Method() || arkts) {
          // Formatting of newly emitted Ark modules only; arbitrary
          // string names cannot be round-tripped by the pinned frontend.
          if(ClassifyMemberKey(aliases[i])!=MemberKeySyntax::kIdentifier) {
            if(arkts)throw arkts_binding_failure();
            ConcurrentFailure failure;failure.kind=ConcurrentError::kModuleSyntax;
            failure.method=*output_syntax.Method();failure.record=record.offset;
            TreePrintFailure error{TreePrintError::kUnsupportedContext};error.concurrent=failure;
            throw RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{error})};
          }
          for(const auto unit:aliases[i]){const char c=static_cast<char>(unit);append({&c,1});}
        }else {
          const auto literal = WriteStringLiteral(aliases[i], output, {remaining});
          if (!std::holds_alternative<std::size_t>(literal)) throw fail(Error::kOutputLimit);
          remaining -= std::get<std::size_t>(literal);
        }
        append(" };\n");
      }
      if(!output)throw fail(Error::kWrite);
      const auto bytes=budget.output.bytes-remaining;
      const auto framing=bytes-*body;
      if(work>remaining_work||framing>remaining_work-work)
        throw RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{TreePrintFailure{TreePrintError::kWorkLimit}})};
      remaining_work-=work+framing;
      return PrintPass{bytes,peak,incomplete,arkts};
    };
    CountingSink counter;std::ostream counted(&counter);
    TreePrintCensus census;
    if(diagnostics) {
      *diagnostics={};diagnostics->reached_print=true;
      diagnostics->available=budget.bytes;diagnostics->printer_scratch=memory.Remaining();
    }
    const auto census_result=[&] {
      if(!diagnostics)return;
      diagnostics->counted_bytes=counter.Bytes();diagnostics->methods=census.methods;
      diagnostics->expressions=census.expressions;diagnostics->statements=census.statements;
      diagnostics->operations=census.operations;diagnostics->anchors=census.anchors;
    };
    const auto measure=[&](){
      TreePrintCensusScope scope(census);
      try {auto result=write(counted);census_result();return result;}
      catch(const RecordProgramFailure& failure){census_result();if(diagnostics)diagnostics->count_failure=failure.printing;throw;}
      catch(const RouteRefusal& failure){census_result();if(diagnostics)diagnostics->count_failure=failure.failure.printing;throw;}
      catch(const TreePrintFailure& failure){census_result();if(diagnostics)diagnostics->count_failure=failure;throw;}
    }();
    if(counter.Bytes()!=measure.bytes)throw fail(Error::kValue);
    if(diagnostics) {
      diagnostics->count_complete=true;diagnostics->reservation=measure.bytes;
      diagnostics->printer_scratch=memory.Remaining()-std::min(memory.Remaining(),measure.bytes);
      diagnostics->counted_peak=measure.peak;
    }
    RecordPrintSink sink(memory,measure.bytes);std::ostream output(&sink);
    const auto printed=write(output);
    if(printed.bytes!=measure.bytes||printed.incomplete!=measure.incomplete||printed.arkts!=measure.arkts)throw fail(Error::kValue);
    if (file.CheckUnchanged()) return fail(Error::kFileChanged);
    PrintedSource source;
    // The selected contract owns the entire recovered family, including partial
    // output. Do not relabel language-mode code with PrintedSource's legacy
    // default when transferring it across the common record boundary.
    source.contract = identity.contract;
    source.text = sink.Finish();
    source.incomplete = printed.incomplete;source.arkts=printed.arkts;
    source.omitted_members = std::move(omitted_members);
    source.omission_causes=std::move(omission_causes);
    source.environment_cleanup = std::move(cleanup_sites);
    return source;
  } catch (const RecordProgramFailure& failure) { return failure; }
    catch (const RouteRefusal& failure) {return failure;}
    catch (const TreePrintFailure& failure) {
      switch(failure.kind) {
        case TreePrintError::kResourceLimit:return fail_with(Error::kResourceLimit,failure);
        case TreePrintError::kOutputLimit:return fail_with(Error::kOutputLimit,failure);
        case TreePrintError::kWrite:return fail_with(Error::kWrite,failure);
        case TreePrintError::kWorkLimit:return RouteRefusal{FamilyRouteFailure(FamilyEmitFailure{failure})};
        case TreePrintError::kUnsupportedContext:
        case TreePrintError::kInvalidLiteral:return fail_with(Error::kValue,failure);
      }
    }
    catch (const std::bad_alloc&) { return fail(Error::kAllocationFailure); }
    catch (const std::length_error&) { return fail(Error::kAllocationFailure); }
}
RecordSourceResult BuildRecordTree(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::RecordInfo& record,
    const RecordModuleContext& context, abc::MethodPosition entry,
    RouteInputIdentity identity, RecordProgramBudget budget, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites* functions,
    const analysis::ClassMethodSites* classes, RecordPrintDiagnostics* diagnostics) {
  bool proposed = false;
  auto result = BuildRecordTreeAttempt(file, header, index, record, context, entry, identity,
      budget, lookup, roles, functions, classes, true, proposed, diagnostics);
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
      budget, lookup, roles, functions, classes, false, unused, diagnostics);
}

}
