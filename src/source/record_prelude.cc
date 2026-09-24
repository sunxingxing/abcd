#include "record_prelude.h"

#include <algorithm>
#include <array>
#include <new>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "abcd/abc/pool_reference.h"
#include "abcd/source/string_literal.h"

namespace abcd::source {
namespace {
using Error = RecordPreludeError;
Error Metadata(abc::MetadataError error) {
  switch (error) {
    case abc::MetadataError::kResourceLimit:
      return Error::kResourceLimit;
    case abc::MetadataError::kRead:
      return Error::kRead;
    case abc::MetadataError::kFileChanged:
      return Error::kFileChanged;
    default:
      return Error::kMetadata;
  }
}
class PreludeWriter {
 public:
  PreludeWriter(const corpus::AbcFile& file, const abc::Header& header,
                const abc::MetadataIndex& index,
                const abc::MethodLookup& lookup,
                const abc::MethodInfo& entry, const abc::MethodBody& body,
                const analysis::GuardedGlobalCall& guard,
                const abc::ModuleInfo& module,
                const analysis::MethodRoleIndex& roles, GlobalScope scope,
                std::ostream& output, LocalLexicalBudget budget,
                abc::StringBudget strings)
      : file_(file), header_(header), index_(index), lookup_(lookup),
        entry_(entry), body_(body), guard_(guard), module_(module),
        roles_(roles), scope_(scope), output_(output), budget_(budget),
        strings_(strings) {}

  std::size_t Write() {
    if ((header_.profile != abc::ProfileId::kDynamic1206 &&
         header_.profile != abc::ProfileId::kDynamic1301) ||
        guard_.after >= body_.instructions.size() ||
        guard_.function >= guard_.after)
      throw Error::kProfile;
    auto key = ReadName(guard_.key);
    auto receiver_global = ReadName(guard_.receiver_global);
    auto receiver_property = ReadName(guard_.receiver_property);
    auto call_global = ReadName(guard_.call_global);
    auto call_property = ReadName(guard_.call_property);
    if (!AllowsGlobalReference(receiver_global, scope_) ||
        !AllowsGlobalReference(call_global, scope_))
      throw Error::kGlobalName;
    // Bracket reads are used below. Restrict to the same non-index names as
    // the ordinary named-read emitter until numeric fast paths are proven.
    if (!AllowsGlobalReference(receiver_property, {}) ||
        !AllowsGlobalReference(call_property, {}))
      throw Error::kPropertyName;
    Text("if (!(");
    String(key);
    Text(" in ");
    Global(receiver_global);
    Text("[");
    String(receiver_property);
    Text("])) {\n");
    Global(call_global);
    Text("[");
    String(call_property);
    Text("](");
    Global(receiver_global);
    Text("[");
    String(receiver_property);
    Text("], ");
    String(key);
    Text(", ");
    LocalLexicalBudget child;
    child.bytes = budget_.bytes;
    child.output.bytes = Remaining();
    const auto created = WriteCreatedFunction(
        file_, header_, index_, lookup_, entry_.offset,
        body_.instructions[guard_.function].decoded, output_, child,
        strings_, &module_, &roles_, &scope_);
    if (const auto* size = std::get_if<std::size_t>(&created)) {
      Account(*size);
    } else if (const auto* error = std::get_if<EmitError>(&created)) {
      throw *error == EmitError::kOutputLimit
                ? Error::kOutputLimit
                : *error == EmitError::kWrite ? Error::kWrite
                                               : Error::kFunction;
    } else if (const auto* error =
                   std::get_if<CreatedFunctionError>(&created)) {
      throw *error == CreatedFunctionError::kResourceLimit
                ? Error::kResourceLimit
                : Error::kFunction;
    } else {
      throw Error::kFunction;
    }
    Text(");\n}\n");
    return used_;
  }

 private:
  std::size_t Remaining() const { return budget_.output.bytes - used_; }
  void Account(std::size_t size) {
    if (size > Remaining()) throw Error::kOutputLimit;
    used_ += size;
  }
  void Text(std::string_view text) {
    if (text.size() > Remaining()) throw Error::kOutputLimit;
    output_.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output_) throw Error::kWrite;
    used_ += text.size();
  }
  void String(std::u16string_view value) {
    const auto written =
        WriteStringLiteral(value, output_, {Remaining()});
    if (const auto* size = std::get_if<std::size_t>(&written)) {
      Account(*size);
      return;
    }
    const auto error = std::get<EmitError>(written);
    throw error == EmitError::kOutputLimit
              ? Error::kOutputLimit
              : error == EmitError::kWrite ? Error::kWrite
                                           : Error::kPropertyName;
  }
  void Global(std::u16string_view name) {
    constexpr std::size_t kChunkBytes = 256;
    std::array<char, kChunkBytes> chunk{};
    std::size_t used = 0;
    for (const char16_t ch : name) {
      chunk[used++] = static_cast<char>(ch);
      if (used == chunk.size()) {
        Text({chunk.data(), used});
        used = 0;
      }
    }
    Text({chunk.data(), used});
  }
  std::u16string ReadName(abc::PoolIndex slot) {
    const auto reference = abc::ResolvePoolReference(
        file_, header_, index_, entry_.offset, slot,
        abc::PoolEntityKind::kString);
    const auto* resolved = std::get_if<abc::PoolReference>(&reference);
    if (!resolved) throw Metadata(std::get<abc::MetadataError>(reference));
    auto decoded = abc::ReadStringEntity(
        file_, header_, resolved->offset,
        {std::min(strings_.bytes, budget_.bytes)});
    auto* name = std::get_if<std::u16string>(&decoded);
    if (!name) throw Metadata(std::get<abc::MetadataError>(decoded));
    if (name->capacity() > budget_.bytes / sizeof(char16_t))
      throw Error::kResourceLimit;
    budget_.bytes -= name->capacity() * sizeof(char16_t);
    return std::move(*name);
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const abc::MethodInfo& entry_;
  const abc::MethodBody& body_;
  const analysis::GuardedGlobalCall& guard_;
  const abc::ModuleInfo& module_;
  const analysis::MethodRoleIndex& roles_;
  GlobalScope scope_;
  std::ostream& output_;
  LocalLexicalBudget budget_;
  abc::StringBudget strings_;
  std::size_t used_ = 0;
};
}  // namespace

RecordPreludeResult WriteGuardedGlobalCall(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const abc::MethodInfo& entry, const abc::MethodBody& body,
    const analysis::GuardedGlobalCall& guard,
    const abc::ModuleInfo& module, const analysis::MethodRoleIndex& roles,
    GlobalScope scope, std::ostream& output, LocalLexicalBudget budget,
    abc::StringBudget strings) {
  try {
    return PreludeWriter(file, header, index, lookup, entry, body, guard,
                         module, roles, scope, output, budget, strings)
        .Write();
  } catch (Error error) {
    return error;
  } catch (const std::bad_alloc&) {
    return Error::kResourceLimit;
  } catch (const std::length_error&) {
    return Error::kResourceLimit;
  } catch (const std::ios_base::failure&) {
    return Error::kWrite;
  }
}
}  // namespace abcd::source
