#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <type_traits>

#include "abcd/source/module_enclosure.h"
#include "abcd/source/global_reference.h"
#include "../../../src/source/tree_globals.h"

namespace {
using namespace abcd;
using namespace abcd::source;
using Error = ModuleEnclosureError;
void Check(bool condition) { if (!condition) std::abort(); }
static_assert(!std::is_copy_constructible_v<ModuleEnclosure>);
// Encoded regular + namespace import order is independent of source order.
// The fixture uses a real file/module reader, never a forged ModuleInfo owner.
std::uint64_t Run(std::size_t count, abc::ProfileId profile, bool duplicate = false, bool invalid = false) {
  constexpr std::uint32_t kType = 128, kFieldName = 160, kRecord = 240, kModule = 256;
  constexpr std::uint8_t kI32 = 5, kAscii = 1;
  constexpr unsigned kByteBits = 8;
  std::vector<std::uint8_t> bytes(kModule);
  const auto word = [&](std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes.push_back(value >> (i * kByteBits));
  };
  const auto patch = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < sizeof(value); ++i) bytes[at + i] = value >> (i * kByteBits);
  };
  const auto string = [&](std::string_view value) {
    Check(value.size() < 64);
    const auto offset = static_cast<std::uint32_t>(bytes.size());
    bytes.push_back((value.size() << 1) | kAscii);
    bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0);
    return offset;
  };
  bytes[kType] = kI32;
  constexpr std::string_view kField = "moduleRecordIdx";
  bytes[kFieldName] = (kField.size() << 1) | kAscii;
  std::copy(kField.begin(), kField.end(), bytes.begin() + kFieldName + 1);
  word(0); word(1); const auto request = bytes.size(); word(0);
  word(count - 1);
  std::vector<std::size_t> entries;
  for (std::size_t i = 1; i < count; ++i) {
    entries.push_back(bytes.size()); word(0); word(0);
    bytes.insert(bytes.end(), sizeof(std::uint16_t), 0);
  }
  word(1); const auto namespace_entry = bytes.size(); word(0);
  bytes.insert(bytes.end(), sizeof(std::uint16_t), 0);
  word(0); word(0); word(0);
  patch(request, string("dep"));
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto name = string(invalid ? "not-valid" : "binding" + std::to_string(duplicate ? 0 : i));
    patch(entries[i], name); patch(entries[i] + sizeof(std::uint32_t), name);
  }
  patch(namespace_entry, string("namespaceBinding"));
  char path[] = "/tmp/abcd-module-enclosure-XXXXXX";
  const auto fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  const auto opened = corpus::AbcFile::Open(path);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = profile;
  header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kRecord, header.file_size, {1, kType}, {}, {}, {}});
  abc::RecordInfo record; record.offset = {kRecord}; record.field_count = 1;
  index.fields.push_back({{kRecord}, record.offset, {kFieldName}, {}, 0,
      abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  const auto context = std::get<RecordModuleContext>(ReadRecordModuleContext(
      file, header, index, record, abc::FilePackaging::kMerged));
  const auto identity = MakeRouteInputIdentity(std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  const auto build = [&](analysis::SemanticBudget budget = {}) {
    return ModuleEnclosure::Build(file, header, context, identity, {1}, budget);
  };
  auto built = build();
  if (duplicate || invalid) {
    Check(std::get<Error>(built) == (invalid ? Error::kName : Error::kDuplicate));
    Check(unlink(path) == 0); return 0;
  }
  auto enclosure = std::get<ModuleEnclosure>(std::move(built));
  Check(enclosure.Names().size() == count && enclosure.Names().back() == "namespaceBinding");
  Check(std::is_sorted(enclosure.SortedNames().begin(), enclosure.SortedNames().end()));
  const auto work = enclosure.Work();
  Check(std::holds_alternative<ModuleEnclosure>(build({analysis::SemanticBudget{}.bytes, work})));
  Check(std::get<Error>(build({analysis::SemanticBudget{}.bytes, work - 1})) == Error::kWorkLimit);
  Check(std::get<Error>(build({0})) == Error::kResourceLimit);
  std::ostringstream imports;
  Check(std::holds_alternative<std::size_t>(enclosure.WriteImports(file, header, imports)));
  Check(imports.str().find("namespaceBinding") != std::string::npos);
  auto wrong = identity; ++wrong.file_size;
  Check(std::get<Error>(ModuleEnclosure::Build(file, header, context, wrong, {1})) == Error::kIdentity);
  const auto second = corpus::AbcFile::Open(path);
  const auto& foreign = *std::get<std::unique_ptr<corpus::AbcFile>>(second);
  Check(std::get<Error>(ModuleEnclosure::Build(foreign, header, context, identity, {1})) == Error::kMissingModule);
  std::ostringstream denied;
  Check(std::holds_alternative<ModuleImportFailure>(enclosure.WriteImports(foreign, header, denied)));
  Check(denied.str().empty());
  GlobalScope scope{}; scope.module_enclosure = &enclosure;
  FunctionTree tree{}; tree.identity = identity; tree.module_scope = ScopeId{0};
  tree.scopes.push_back({ScopeKind::kModule, {}});
  tree.names.push_back(u"namespaceBinding");
  tree.symbols.push_back({{0}, {0}, BindingKind::kModule, DeclarationKind::kConst, NameEvidence::kMetadata, {}});
  tree.modules.push_back({{0}, {{1}, {static_cast<std::uint32_t>(count - 1)}}, ModuleAccess::kReadOnly});
  const auto verify = [&](const FunctionTree& candidate, const GlobalScope* destination) {
    return VerifyModuleDestination(candidate, record.offset, destination);
  };
  const auto checked = std::get<ModuleDestination>(verify(tree, &scope));
  Check(std::holds_alternative<ModuleDestination>(VerifyModuleDestination(tree, record.offset, &scope,
      {0, checked.work})));
  Check(std::get<Error>(VerifyModuleDestination(tree, record.offset, &scope, {0, checked.work - 1})) == Error::kWorkLimit);
  Check(std::get<Error>(verify(tree, nullptr)) == Error::kMissingModule);
  auto bad = tree; ++bad.identity.file_size;
  Check(std::get<Error>(verify(bad, &scope)) == Error::kIdentity);
  bad = tree; ++bad.modules[0].target.owner.value;
  Check(std::get<Error>(verify(bad, &scope)) == Error::kBinding);
  bad = tree; ++bad.modules[0].target.symbol.value;
  Check(std::get<Error>(verify(bad, &scope)) == Error::kBinding);  // Local slot is not an import.
  bad = tree; bad.modules[0].access = ModuleAccess::kMutable;
  Check(std::get<Error>(verify(bad, &scope)) == Error::kBinding);
  bad = tree; bad.names[0] = u"other";
  Check(std::get<Error>(verify(bad, &scope)) == Error::kName);
  const std::string_view names[] = {"namespaceBinding"};
  enum class Shadow { kClass, kSelf, kParameters, kEnclosing };
  for (const auto select : {Shadow::kClass, Shadow::kSelf, Shadow::kParameters, Shadow::kEnclosing}) {
    auto shadowed = scope;
    switch (select) {
      case Shadow::kClass: shadowed.class_binding = names[0]; break;
      case Shadow::kSelf: shadowed.self_binding = names[0]; break;
      case Shadow::kParameters: shadowed.parameter_bindings = names; break;
      case Shadow::kEnclosing: shadowed.enclosing_bindings = names; break;
    }
    Check(std::get<Error>(verify(tree, &shadowed)) == Error::kShadowed);
  }
  // A local read requires the record's declaration premise, but remains a
  // read-only alias to a live let binding. Intervening bindings still shadow it.
  const std::u16string local_names[] = {u"LocalClass"};
  const std::string_view record_names[] = {"LocalClass"};
  auto local = tree; local.names[0] = local_names[0];
  local.modules[0].target.symbol.value = static_cast<std::uint32_t>(count);
  Check(std::get<Error>(verify(local, &scope)) == Error::kBinding);
  auto declared = scope; declared.module_local_names = local_names;
  declared.record_bindings = record_names;
  Check(std::holds_alternative<ModuleDestination>(verify(local, &declared)));
  auto mismatch = local; mismatch.names[0] = u"OtherClass";
  Check(std::get<Error>(verify(mismatch, &declared)) == Error::kName);
  mismatch = local; ++mismatch.modules[0].target.owner.value;
  Check(std::get<Error>(verify(mismatch, &declared)) == Error::kBinding);
  mismatch = local; mismatch.modules[0].access = ModuleAccess::kMutable;
  Check(std::get<Error>(verify(mismatch, &declared)) == Error::kBinding);
  for (const auto select : {Shadow::kClass, Shadow::kSelf, Shadow::kParameters, Shadow::kEnclosing}) {
    auto shadowed = declared;
    switch (select) {
      case Shadow::kClass: shadowed.class_binding = record_names[0]; break;
      case Shadow::kSelf: shadowed.self_binding = record_names[0]; break;
      case Shadow::kParameters: shadowed.parameter_bindings = record_names; break;
      case Shadow::kEnclosing: shadowed.enclosing_bindings = record_names; break;
    }
    Check(std::get<Error>(verify(local, &shadowed)) == Error::kShadowed);
  }
  auto shadowed_import = scope; shadowed_import.record_bindings = names;
  Check(std::get<Error>(verify(tree, &shadowed_import)) == Error::kShadowed);
  Check(!AllowsGlobalReference(u"LocalClass", declared));
  FunctionTree local_global{}; local_global.identity = identity;
  local_global.names.push_back(u"LocalClass");
  local_global.expressions.push_back({GlobalNameExpr{{0}}, {}});
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(local_global, &declared)) == TreeGlobalError::kShadowed);
  FunctionTree global{}; global.identity = identity;
  global.names.push_back(u"namespaceBinding");
  global.expressions.push_back({GlobalNameExpr{{0}}, {}});
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(global, &scope)) == TreeGlobalError::kShadowed);
  Check(unlink(path) == 0);
  return work;
}
}  // namespace
int main() {
  for (const auto profile : {abcd::abc::ProfileId::kDynamic1202, abcd::abc::ProfileId::kDynamic1206,
                            abcd::abc::ProfileId::kDynamic1301}) {
    Run(3, profile, true); Run(3, profile, false, true);
    std::uint64_t previous = 0;
    for (const auto count : {1, 200, 201, 402, 804, 1608}) {
      const auto work = Run(count, profile);
      if (count > 201) Check(work < 3 * previous);
      previous = work;
    }
  }
}
