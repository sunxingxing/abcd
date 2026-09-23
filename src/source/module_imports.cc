#include "abcd/source/module_imports.h"

#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "abcd/source/string_literal.h"

namespace abcd::source {
namespace {
enum class ImportForm { kNamed, kNamespace };

// The pinned SourceTextModule loader uses the wire imported name "*" as a
// namespace import. This is a validated wire-boundary conversion only.
ImportForm ClassifyImport(std::u16string_view name) {
  return name == u"*" ? ImportForm::kNamespace : ImportForm::kNamed;
}

class ImportWriter {
 public:
  ImportWriter(const corpus::AbcFile& file, const abc::Header& header,
               const abc::ModuleInfo& module, EmitBudget budget,
               abc::StringBudget strings)
      : file_(file), header_(header), module_(module), remaining_(budget.bytes),
        strings_(strings) {}

  void Write() {
    for (const auto request : module_.requests) {
      Text("import ");
      Literal(request);
      Text(";\n");
    }
    std::size_t slot = 0;
    for (const auto& entry : module_.imports) {
      const auto imported = Read(entry.imported);
      if (ClassifyImport(imported) == ImportForm::kNamespace) {
        Text("import * as ");
        Alias(slot);
      } else {
        Text("import { ");
        Literal(imported, entry.imported);
        Text(" as ");
        Alias(slot);
        Text(" }");
      }
      From(entry.request);
      ++slot;
    }
    for (const auto& entry : module_.namespace_imports) {
      Text("import * as ");
      Alias(slot);
      From(entry.request);
      ++slot;
    }
  }

  std::string Take() { return std::move(stream_).str(); }
  std::size_t Size() const { return used_; }

 private:
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
  std::size_t used_ = 0;
  std::ostringstream stream_;
};
}  // namespace

ModuleImportResult WriteModuleImports(const corpus::AbcFile& file,
                                      const abc::Header& header,
                                      const abc::ModuleInfo& module,
                                      std::ostream& output, EmitBudget budget,
                                      abc::StringBudget strings) {
  try {
    ImportWriter writer(file, header, module, budget, strings);
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
