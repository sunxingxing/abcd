#include "concurrent_print.h"
#include "abcd/source/module_imports.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include "abcd/source/string_literal.h"
#include "member_key.h"

namespace abcd::source {
namespace {
enum class ImportForm { kNamed, kDefault, kNamespace };
enum class DirectiveKind { kImport, kNamespace };
struct ModuleDirective {
  abc::ModuleRequestId request;
  DirectiveKind kind;
  std::size_t index;
};

// The pinned SourceTextModule loader uses the wire imported name "*" as a
// namespace import. This is a validated wire-boundary conversion only.
ImportForm ClassifyImport(std::u16string_view name) {
  if (name == u"*") return ImportForm::kNamespace;
  if (name == u"default") return ImportForm::kDefault;
  return ImportForm::kNamed;
}

class ImportWriter {
 public:
  ImportWriter(const corpus::AbcFile& file, const abc::Header& header,
               const abc::ModuleInfo& module, EmitBudget budget,
               abc::StringBudget strings, std::span<const std::string> names, SourceContract contract)
      : file_(file), header_(header), module_(module), remaining_(budget.bytes),
        strings_(strings), names_(names), contract_(contract) {}

  void Write() {
    if (!module_.request_phases.empty() && module_.request_phases.size() != module_.requests.size())
      throw ModuleImportFailure{ModuleImportError::kRequestPhases, {}, {}};
    if (!IsSourceContract(contract_))
      throw ModuleImportFailure{ModuleImportError::kContract, {}, {}};
    for (const auto& entry : module_.indirect_exports) RejectLazy(entry.request);
    for (const auto request : module_.star_exports) RejectLazy(request);
    std::size_t count = 0;
    const auto limit = strings_.bytes / sizeof(ModuleDirective);
    for (const auto size : {module_.imports.size(), module_.namespace_imports.size()}) {
      if (size > limit - count)
        throw ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
      count += size;
    }
    std::vector<ModuleDirective> directives;
    directives.reserve(count);
    if (directives.capacity() > limit)
      throw ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
    strings_.bytes -= directives.capacity() * sizeof(ModuleDirective);
    const auto add = [&](abc::ModuleRequestId request, DirectiveKind kind,
                         std::size_t index) {
      if (request.value >= module_.requests.size())
        throw ModuleImportFailure{ModuleImportError::kRequestIndex, {}, {}};
      directives.push_back({request, kind, index});
    };
    for (std::size_t i = 0; i < module_.imports.size(); ++i)
      add(module_.imports[i].request, DirectiveKind::kImport, i);
    for (std::size_t i = 0; i < module_.namespace_imports.size(); ++i)
      add(module_.namespace_imports[i].request, DirectiveKind::kNamespace, i);
    std::sort(directives.begin(), directives.end(), [](const auto& a, const auto& b) {
      return std::tie(a.request, a.kind, a.index) < std::tie(b.request, b.kind, b.index);
    });
    // ESM dependency traversal follows first source occurrence, not import
    // binding slot order. Keep ABC requests ordered while retaining every
    // original slot; requests with no import binding keep their bare import.
    std::size_t next = 0;
    for (std::size_t request = 0; request < module_.requests.size(); ++request) {
      if (next == directives.size() || directives[next].request.value != request) {
        if (!module_.request_phases.empty() && module_.request_phases[request] == abc::ModuleRequestPhase::kLazy)
          throw ModuleImportFailure{ModuleImportError::kModuleKind, {}, {}};
        Text("import ");
        Literal(module_.requests[request]);
        Text(";\n");
      }
      while (next < directives.size() && directives[next].request.value == request) {
        const auto& directive = directives[next++];
        switch (directive.kind) {
          case DirectiveKind::kImport: Import(directive.index); break;
          case DirectiveKind::kNamespace: Namespace(directive.index); break;
        }
      }
    }
    // Preserve reexport table order as well as dependency traversal order,
    // including cyclic star resolution. Reexport-only requests stay bare above.
    for (std::size_t i = 0; i < module_.indirect_exports.size(); ++i) Reexport(i);
    for (const auto request : module_.star_exports) {
      Text("export *");
      From(request);
    }
  }

  std::string Take() { return std::move(stream_).str(); }
  std::size_t Size() const { return used_; }

 private:
  bool Lazy(abc::ModuleRequestId request) const {
    if (request.value >= module_.requests.size()) throw ModuleImportFailure{ModuleImportError::kBinding, {}, {}};
    return !module_.request_phases.empty() && module_.request_phases[request.value] == abc::ModuleRequestPhase::kLazy;
  }
  void RejectLazy(abc::ModuleRequestId request) const {
    if (Lazy(request)) throw ModuleImportFailure{ModuleImportError::kModuleKind, {}, {}};
  }
  void Import(std::size_t slot) {
    const auto& entry = module_.imports[slot];
    const auto imported = Read(entry.imported);
    const bool lazy = Lazy(entry.request);
    switch (ClassifyImport(imported)) {
      case ImportForm::kNamespace:
        RejectLazy(entry.request);
        Text("import * as ");
        Alias(slot);
        break;
      case ImportForm::kDefault:
        Text(lazy ? "import lazy " : "import ");
        Alias(slot);
        break;
      case ImportForm::kNamed:
        Text(lazy ? "import lazy { " : "import { ");
        ModuleName(imported, entry.imported);
        if (slot >= names_.size() || names_[slot].empty() ||
            !std::equal(imported.begin(), imported.end(),
                        names_[slot].begin(), names_[slot].end())) {
          Text(" as ");
          Alias(slot);
        }
        Text(" }");
        break;
    }
    From(entry.request);
  }
  void Namespace(std::size_t index) {
    const auto& entry = module_.namespace_imports[index];
    RejectLazy(entry.request);
    Text("import * as ");
    Alias(module_.imports.size() + index);
    From(entry.request);
  }
  void Reexport(std::size_t index) {
    const auto& entry = module_.indirect_exports[index];
    const auto imported = Read(entry.imported);
    switch (ClassifyImport(imported)) {
      case ImportForm::kNamespace:
        Text("export * as ");
        ModuleName(Read(entry.exported), entry.exported);
        break;
      case ImportForm::kDefault:
      case ImportForm::kNamed:
        Text("export { ");
        ModuleName(imported, entry.imported);
        Text(" as ");
        ModuleName(Read(entry.exported), entry.exported);
        Text(" }");
        break;
    }
    From(entry.request);
  }
  void ModuleName(std::u16string_view value, abc::EntityOffset offset) {
    // ModuleExportName accepts IdentifierName (including keywords), not a
    // binding identifier. Non-ASCII/arbitrary names retain exact escaping.
    // This spelling choice never changes the private local import alias.
    switch (ClassifyMemberKey(value)) {
      case MemberKeySyntax::kIdentifier:
        for (const auto unit : value) {
          const char ch = static_cast<char>(unit);
          Text({&ch, 1});
        }
        break;
      case MemberKeySyntax::kQuoted:
        ConcurrentPrint::QuotedModuleName();
        Literal(value, offset);
        break;
    }
  }
  std::u16string Read(abc::EntityOffset offset) const {
    const auto parsed = abc::ReadStringEntity(file_, header_, offset, strings_);
    if (const auto* failure = std::get_if<abc::MetadataError>(&parsed))
      throw ModuleImportFailure{
          *failure == abc::MetadataError::kResourceLimit
              ? ModuleImportError::kResourceLimit
              : ModuleImportError::kString,
          offset, *failure};
    return std::get<std::u16string>(parsed);
  }

  void Text(std::string_view value) {
    if (value.size() > remaining_)
      throw ModuleImportFailure{ModuleImportError::kOutputLimit, {}, {}};
    stream_.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream_)
      throw ModuleImportFailure{ModuleImportError::kWrite, {}, {}};
    remaining_ -= value.size();
    used_ += value.size();
  }

  void Literal(std::u16string_view value, abc::EntityOffset offset) {
    const auto emitted = WriteStringLiteral(value, stream_, {remaining_});
    if (const auto* failure = std::get_if<EmitError>(&emitted)) {
      const auto kind = *failure == EmitError::kOutputLimit
                            ? ModuleImportError::kOutputLimit
                            : ModuleImportError::kWrite;
      throw ModuleImportFailure{kind, offset, {}};
    }
    const auto size = std::get<std::size_t>(emitted);
    remaining_ -= size;
    used_ += size;
  }

  void Literal(abc::EntityOffset offset) {
    const auto value = Read(offset);
    Literal(value, offset);
  }

  void Alias(std::size_t slot) {
    if (slot < names_.size() && !names_[slot].empty()) {
      Text(names_[slot]);
      return;
    }
    Text("$import");
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> digits{};
    const auto [end, error] =
        std::to_chars(digits.data(), digits.data() + digits.size(), slot);
    if (error != std::errc{})
      throw ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
    Text({digits.data(), static_cast<std::size_t>(end - digits.data())});
  }

  void From(abc::ModuleRequestId request) {
    if (request.value >= module_.requests.size())
      throw ModuleImportFailure{ModuleImportError::kRequestIndex, {}, {}};
    Text(" from ");
    Literal(module_.requests[request.value]);
    Text(";\n");
  }

  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::ModuleInfo& module_;
  std::size_t remaining_;
  abc::StringBudget strings_;
  std::span<const std::string> names_;
  SourceContract contract_;
  std::size_t used_ = 0;
  std::ostringstream stream_;
};
}  // namespace

ModuleImportResult WriteModuleImports(const corpus::AbcFile& file,
                                      const abc::Header& header,
                                      const abc::ModuleInfo& module,
                                      std::ostream& output, EmitBudget budget,
                                      abc::StringBudget strings,
                                      std::span<const std::string> names, SourceContract contract) {
  try {
    ImportWriter writer(file, header, module, budget, strings, names, contract);
    writer.Write();
    const auto size = writer.Size();
    if (size > static_cast<std::size_t>(
                   std::numeric_limits<std::streamsize>::max()))
      return ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
    const auto source = writer.Take();
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!output)
      return ModuleImportFailure{ModuleImportError::kWrite, {}, {}};
    return size;
  } catch (const ModuleImportFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
  } catch (const std::length_error&) {
    return ModuleImportFailure{ModuleImportError::kResourceLimit, {}, {}};
  } catch (const std::ios_base::failure&) {
    return ModuleImportFailure{ModuleImportError::kWrite, {}, {}};
  }
}
}  // namespace abcd::source
