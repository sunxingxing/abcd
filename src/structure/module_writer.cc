#include <map>

#include "record_text.h"

namespace abcd::structure {
namespace {
enum class BindingKind { kImport, kNamespace };
struct Binding {
  std::size_t id;
  BindingKind kind;
};
// Symbol-table keys are actual external identifier text, not semantic tags.
// Imports and module-owned export slots are separate binding domains.
class Bindings {
 public:
  explicit Bindings(const RecordText& text)
      : text_(text), remaining_(text.budget.binding_bytes) {}
  void Add(abc::EntityOffset offset, BindingKind kind) {
    auto name = text_.Read(offset);
    if (entries.find(name) != entries.end()) {
      throw StructureFailure{offset, StructureFault::kBindings};
    }
    constexpr std::size_t kTreeNodeAllowance = 64;
    constexpr auto kNodeBytes =
        sizeof(std::pair<const std::u16string, Binding>) + kTreeNodeAllowance;
    if (kNodeBytes > remaining_ ||
        name.capacity() > (remaining_ - kNodeBytes) / sizeof(char16_t))
      throw StructureFailure{offset, StructureFault::kResourceLimit};
    remaining_ -= kNodeBytes + name.capacity() * sizeof(char16_t);
    const auto id = entries.size();
    entries.emplace(std::move(name), Binding{id, kind});
  }
  Binding Find(abc::EntityOffset offset) const {
    const auto found = entries.find(text_.Read(offset));
    if (found == entries.end())
      throw StructureFailure{offset, StructureFault::kBindings};
    return found->second;
  }
  std::map<std::u16string, Binding> entries;

 private:
  const RecordText& text_;
  std::size_t remaining_;
};
enum class ImportKind { kNamed, kNamespace };
ImportKind DecodeImport(std::u16string_view imported) {
  // v7 SourceTextModule import resolution treats this wire name as namespace.
  // ResolveIndirectExport does NOT apply this conversion.
  return imported == u"*" ? ImportKind::kNamespace : ImportKind::kNamed;
}
}  // namespace

void WriteModuleStructure(const RecordText& text, const abc::ModuleInfo& module,
                          std::ostream& metadata, std::ostream& declarations) {
  Bindings bindings(text);
  for (const auto& entry : module.imports)
    bindings.Add(entry.local, BindingKind::kImport);
  for (const auto& entry : module.namespace_imports)
    bindings.Add(entry.local, BindingKind::kNamespace);
  const auto request = [&](abc::ModuleRequestId id) {
    if (id.value >= module.requests.size())
      throw StructureFailure{{}, StructureFault::kBindings};
    return module.requests[id.value];
  };
  metadata << "{\"requests\":[";
  bool comma = false;
  for (const auto offset : module.requests) {
    if (comma) metadata << ',';
    comma = true;
    text.String(metadata, offset);
    declarations << "import ";
    text.String(declarations, offset);
    declarations << ";\n";
  }
  metadata << "],\"imports\":[";
  comma = false;
  for (const auto& entry : module.imports) {
    if (comma) metadata << ',';
    comma = true;
    const auto binding = bindings.Find(entry.local);
    metadata << "{\"binding\":" << binding.id << ",\"local\":";
    text.String(metadata, entry.local);
    metadata << ",\"imported\":";
    text.String(metadata, entry.imported);
    metadata << ",\"request\":" << entry.request.value << '}';
    const auto imported = text.Read(entry.imported);
    if (DecodeImport(imported) == ImportKind::kNamespace) {
      declarations << "import * as $binding" << binding.id << " from ";
    } else {
      declarations << "import { ";
      text.String(declarations, imported);
      declarations << " as $binding" << binding.id << " } from ";
    }
    text.String(declarations, request(entry.request));
    declarations << ";\n";
  }
  metadata << "],\"namespace_imports\":[";
  comma = false;
  for (const auto& entry : module.namespace_imports) {
    if (comma) metadata << ',';
    comma = true;
    const auto binding = bindings.Find(entry.local);
    metadata << "{\"binding\":" << binding.id << ",\"local\":";
    text.String(metadata, entry.local);
    metadata << ",\"request\":" << entry.request.value << '}';
    declarations << "import * as $binding" << binding.id << " from ";
    text.String(declarations, request(entry.request));
    declarations << ";\n";
  }
  metadata << "],\"local_exports\":[";
  comma = false;
  // ModuleDataAccessor::EnumerateLocalExportEntry assigns a new localIndex
  // only when the adjacent local name changes, not for each distinct name.
  // All supported dynamic profiles are newer than JSPandaFile::OLD_VERSION
  // (0.0.0.2). Do not extend this slot rule to legacy named-binding profiles.
  switch (text.header.profile) {
    case abc::ProfileId::kDynamic9:
    case abc::ProfileId::kDynamic1102:
    case abc::ProfileId::kDynamic1202:
    case abc::ProfileId::kDynamic1206:
    case abc::ProfileId::kDynamic1301:
    case abc::ProfileId::kDynamic2400:
      break;
    default:
      throw StructureFailure{
          {}, abc::ModuleFailure{abc::ModuleError::kUnsupportedProfile, {}}};
  }
  std::u16string previous;
  std::size_t slot = 0;
  for (const auto& entry : module.local_exports) {
    auto local = text.Read(entry.local);
    if (!comma || local != previous) {
      if (comma) ++slot;
      declarations << "// module local ";
      text.String(declarations, local);
      declarations << "\ndeclare let $local" << slot << ": unknown;\n";
    }
    previous = std::move(local);
    if (comma) metadata << ',';
    comma = true;
    metadata << "{\"local_slot\":" << slot << ",\"local\":";
    text.String(metadata, entry.local);
    metadata << ",\"exported\":";
    text.String(metadata, entry.exported);
    metadata << '}';
    declarations << "export { $local" << slot << " as ";
    text.String(declarations, entry.exported);
    declarations << " };\n";
  }
  metadata << "],\"indirect_exports\":[";
  comma = false;
  for (const auto& entry : module.indirect_exports) {
    if (comma) metadata << ',';
    comma = true;
    const auto imported = text.Read(entry.imported);
    metadata << "{\"imported\":";
    text.String(metadata, imported);
    metadata << ",\"exported\":";
    text.String(metadata, entry.exported);
    metadata << ",\"request\":" << entry.request.value << '}';
    declarations << "export { ";
    text.String(declarations, imported);
    declarations << " as ";
    text.String(declarations, entry.exported);
    declarations << " } from ";
    text.String(declarations, request(entry.request));
    declarations << ";\n";
  }
  metadata << "],\"star_exports\":[";
  comma = false;
  for (const auto entry : module.star_exports) {
    if (comma) metadata << ',';
    comma = true;
    metadata << entry.value;
    declarations << "export * from ";
    text.String(declarations, request(entry));
    declarations << ";\n";
  }
  metadata << "]}";
}
}  // namespace abcd::structure
