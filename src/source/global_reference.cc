#include "abcd/source/global_reference.h"

#include <algorithm>
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
std::optional<std::u16string_view> ResolveArgumentSnapshotBinding(const GlobalScope* scope) {
  if (!scope || !scope->argument_snapshot_binding ||
      !IsSourceIdentifier(scope->argument_snapshot_binding->name)) return {};
  switch (scope->argument_snapshot_binding->abi) {
    case ArgumentSnapshotAbi::kV7CurrentSlots: return scope->argument_snapshot_binding->name;
  }
  return {};
}
std::optional<std::u16string_view> ResolveSymbolBinding(const GlobalScope* scope) {
  if (!scope || !scope->symbol_binding || !IsSourceIdentifier(scope->symbol_binding->name)) return {};
  return scope->symbol_binding->name;
}
std::optional<RealmBindingView> ResolveRealmBinding(SourceContract contract, const GlobalScope* scope) {
  if (!scope) return {};
  if (scope->realm_binding) {
    const auto name = scope->realm_binding->name;
    if (!IsSourceIdentifier(name) || name == u"arguments") return {};
    return RealmBindingView{RealmBindingPolicy::kExplicitHost, name};
  }
  switch (RealmBinding(contract)) {
    case RealmBindingPolicy::kExplicitHost: return {};
    case RealmBindingPolicy::kStableGlobalThis:
      return RealmBindingView{RealmBindingPolicy::kStableGlobalThis, u"globalThis"};
  }
  return {};
}
namespace {
bool EqualsAscii(std::u16string_view name, std::string_view ascii) {
  return name.size() == ascii.size() &&
         std::equal(name.begin(), name.end(), ascii.begin());
}
bool IdentifierStart(char16_t ch) {
  return (ch >= u'a' && ch <= u'z') ||
         (ch >= u'A' && ch <= u'Z') || ch == u'_';
}
bool IdentifierPart(char16_t ch) {
  return IdentifierStart(ch) || (ch >= u'0' && ch <= u'9');
}
bool ContainsAscii(std::span<const std::string_view> bindings,
                   std::u16string_view name) {
  const auto found = std::lower_bound(
      bindings.begin(), bindings.end(), name,
      [](std::string_view binding, std::u16string_view candidate) {
        return std::lexicographical_compare(binding.begin(), binding.end(),
                                            candidate.begin(), candidate.end(),
                                            [](char left, char16_t right) {
                                              return static_cast<unsigned char>(left) <
                                                     right;
                                            });
      });
  return found != bindings.end() && EqualsAscii(name, *found);
}
}  // namespace

std::string_view ImportBinding(const GlobalScope* scope, std::size_t slot) {
  return scope && slot < scope->import_bindings.size()
      ? std::string_view{scope->import_bindings[slot]} : std::string_view{};
}
bool ShadowsImport(std::string_view name, const GlobalScope& scope) {
  return std::binary_search(scope.import_names.begin(), scope.import_names.end(), name);
}

bool AllowsGlobalReference(std::u16string_view name, GlobalScope scope) {
  if (name.empty() || !IdentifierStart(name.front()) ||
      !std::all_of(name.begin() + 1, name.end(), IdentifierPart) ||
      EqualsAscii(name, scope.class_binding) ||
      EqualsAscii(name, scope.self_binding) || name == u"ClassValue" ||
      name.starts_with(u"MethodSelf") ||
      (scope.realm_binding && name == scope.realm_binding->name) ||
      (scope.symbol_binding && name == scope.symbol_binding->name) ||
      (scope.argument_snapshot_binding && name == scope.argument_snapshot_binding->name))
    return false;
  if (ContainsAscii(scope.enclosing_bindings, name) ||
      ContainsAscii(scope.record_bindings, name) ||
      ContainsAscii(scope.import_names, name) ||
      ContainsAscii(scope.parameter_bindings, name))
    return false;
  // Matches the strict/module reserved binding set. Generated private names
  // all start with '$', rejected by IdentifierStart above.
  constexpr std::u16string_view reserved[]{
      u"arguments", u"await",      u"break",     u"case",    u"catch",
      u"class",     u"const",      u"continue",  u"debugger", u"default",
      u"delete",    u"do",         u"else",      u"enum",     u"eval",
      u"export",    u"extends",    u"false",     u"finally",  u"for",
      u"function",  u"if",         u"implements", u"import",  u"in",
      u"instanceof", u"interface", u"let",       u"new",     u"null",
      u"package",   u"private",    u"protected", u"public",  u"return",
      u"static",    u"super",      u"switch",    u"this",    u"throw",
      u"true",      u"try",        u"typeof",    u"var",      u"void",
      u"while",     u"with",       u"yield"};
  return std::find(std::begin(reserved), std::end(reserved), name) ==
         std::end(reserved);
}
}  // namespace abcd::source
