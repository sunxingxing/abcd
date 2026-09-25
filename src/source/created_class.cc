#include "abcd/source/created_class.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "abcd/abc/call_type.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/source/base_constructor.h"
#include "abcd/source/class_method.h"
#include "abcd/source/string_literal.h"
#include "abcd/source/semantic_source.h"
#include "capture_stream.h"
#include "creation_expansion.h"
#include "class_emission.h"
#include "parameter_names.h"
#include "record_literal.h"
#include "runtime_name.h"
#include "spread_field_constructor.h"
#include "member_key.h"

namespace abcd::source {
namespace {
FunctionEmitResult WriteDerivedConstructor(
    analysis::ForwardingDerivedConstructor forwarding,
    std::uint16_t length, const abc::FrameLayout& frame,
    std::span<const std::string_view> parameter_names,
    std::ostream& output, EmitBudget budget) {
  switch (forwarding.kind) {
    case analysis::ForwardingKind::kAllArguments:
      if (length != 0 || forwarding.argument_count != 0)
        return FunctionEmitError::kLength;
      break;
    case analysis::ForwardingKind::kParameters:
      if (length != frame.parameters ||
          forwarding.argument_count > frame.parameters)
        return FunctionEmitError::kLength;
      break;
  }
  std::size_t written = 0;
  std::optional<EmitError> error;
  const auto append = [&](std::string_view token) {
    if (error) return;
    if (token.size() > budget.bytes - written) {
      error = EmitError::kOutputLimit;
      return;
    }
    output.write(token.data(), static_cast<std::streamsize>(token.size()));
    if (!output) {
      error = EmitError::kWrite;
      return;
    }
    written += token.size();
  };
  const auto parameter = [&](std::uint32_t index) {
    if (parameter_names.size() == frame.parameters) {
      // RecoverParameterNames validated the complete ASCII binding set.
      append(parameter_names[index]);
      return;
    }
    append("$p");
    std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 1>
        digits{};
    const auto end = std::to_chars(digits.data(),
                                   digits.data() + digits.size(), index);
    if (end.ec != std::errc{}) {
      error = EmitError::kInvalidExpression;
      return;
    }
    append({digits.data(), static_cast<std::size_t>(end.ptr - digits.data())});
  };
  append("constructor(");
  switch (forwarding.kind) {
    case analysis::ForwardingKind::kAllArguments:
      break;
    case analysis::ForwardingKind::kParameters:
      for (std::uint32_t index = 0; index < length && !error; ++index) {
        if (index != 0) append(", ");
        parameter(index);
      }
      break;
  }
  append(") { super(");
  switch (forwarding.kind) {
    case analysis::ForwardingKind::kAllArguments:
      append("...arguments");
      break;
    case analysis::ForwardingKind::kParameters:
      for (std::uint32_t index = 0;
           index < forwarding.argument_count && !error; ++index) {
        if (index != 0) append(", ");
        parameter(index);
      }
      break;
  }
  append("); }");
  if (error) return *error;
  return written;
}
abc::ClassBuffer ReadMembers(const corpus::AbcFile& file,
                             const abc::Header& header,
                             const abc::MetadataIndex& index,
                             abc::EntityOffset caller, abc::PoolIndex literal,
                             std::size_t bytes) {
  const auto reference = TakeClassValue<abc::PoolReference>(
      abc::ResolvePoolReference(file, header, index, caller, literal,
                                abc::PoolEntityKind::kLiteralArray));
  const auto raw = TakeClassValue<abc::LiteralArray>(
      abc::ReadLiteralArray(file, header, reference.offset, {bytes}));
  AdmitClassBytes(raw.entries.capacity(), sizeof(abc::LiteralEntry), bytes);
  return TakeClassValue<abc::ClassBuffer>(abc::DecodeClassBuffer(raw, {bytes}));
}

void CheckAccessors(const corpus::AbcFile& file,
                    const abc::Header& header,
                    const abc::MetadataIndex& index,
                    const analysis::MethodRoleIndex* roles,
                    abc::EntityOffset owner,
                    const abc::ClassBuffer& members,
                    std::span<const ClassAccessor> accessors,
                    std::size_t& bytes, abc::StringBudget strings) {
  if (accessors.empty()) return;
  using Key = std::tuple<abc::ClassPlacement, std::u16string_view,
                         abc::ClassMethodKind>;
  // The pinned v7 RuntimeDefineGetterSetterByValue defines the same
  // non-enumerable class accessor descriptor for these dynamic profiles.
  if (header.profile != abc::ProfileId::kDynamic1202 &&
      header.profile != abc::ProfileId::kDynamic1206 &&
      header.profile != abc::ProfileId::kDynamic1301)
    throw ClassFailure{CreatedClassError::kAccessorTarget};
  AdmitClassBytes(accessors.size(), sizeof(Key), bytes);
  AdmitClassBytes(accessors.size(), sizeof(abc::MethodPosition), bytes);
  std::vector<Key> keys;
  std::vector<abc::MethodPosition> methods;
  keys.reserve(accessors.size());
  methods.reserve(accessors.size());
  AdmitClassBytes(keys.capacity() - accessors.size(), sizeof(Key), bytes);
  AdmitClassBytes(methods.capacity() - accessors.size(),
                  sizeof(abc::MethodPosition), bytes);
  for (const auto& accessor : accessors) {
    if (accessor.method.value >= index.methods.size() || !roles ||
        roles->At(accessor.method) != analysis::MethodRole::kUnseen ||
        (accessor.kind != abc::ClassMethodKind::kGetter &&
         accessor.kind != abc::ClassMethodKind::kSetter) ||
        accessor.length !=
            (accessor.kind == abc::ClassMethodKind::kGetter ? 0 : 1) ||
        (accessor.placement == abc::ClassPlacement::kStatic &&
         accessor.key == u"prototype"))
      throw ClassFailure{CreatedClassError::kAccessorTarget};
    const auto& method = index.methods[accessor.method.value];
    if (method.owner_offset != owner || !method.code)
      throw ClassFailure{CreatedClassError::kAccessorTarget};
    const auto attributes = TakeClassValue<abc::MethodAttributes>(
        abc::DecodeMethodAttributes(method.access_flags, header.profile));
    if (attributes.sendable || attributes.kind != abc::FunctionKind::kNone)
      throw ClassFailure{CreatedClassError::kAccessorTarget};
    const auto name = RuntimeName(method);
    if (!name || name->size() != accessor.key.size() ||
        !std::equal(name->begin(), name->end(), accessor.key.begin()))
      throw ClassFailure{CreatedClassError::kAccessorTarget};
    keys.emplace_back(accessor.placement, accessor.key, accessor.kind);
    methods.push_back(accessor.method);
  }
  std::sort(keys.begin(), keys.end());
  std::sort(methods.begin(), methods.end());
  if (std::adjacent_find(keys.begin(), keys.end()) != keys.end() ||
      std::adjacent_find(methods.begin(), methods.end()) != methods.end())
    throw ClassFailure{CreatedClassError::kAccessorTarget};
  // A later accessor must not replace a buffer method before its self-binding
  // is captured by ClassWriter; compare positions and keys in O(M log A).
  for (const auto& member : members.properties) {
    const auto name = ReadClassKey(file, header, member, strings);
    if (name.capacity() > bytes / sizeof(char16_t))
      throw ClassFailure{CreatedClassError::kResourceLimit};
    const auto wanted = std::pair{member.placement, std::u16string_view{name}};
    const auto found = std::lower_bound(
        keys.begin(), keys.end(), wanted,
        [](const Key& candidate, const auto& value) {
          return std::pair{std::get<0>(candidate), std::get<1>(candidate)} <
                 value;
        });
    if (found != keys.end() &&
        std::pair{std::get<0>(*found), std::get<1>(*found)} == wanted)
      throw ClassFailure{CreatedClassError::kAccessorOrder};
  }
}

class ClassWriter {
 public:
  ClassWriter(const corpus::AbcFile& file, const abc::Header& header,
              const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
              const analysis::MethodRoleIndex* roles,
              const abc::ModuleInfo* module, abc::EntityOffset owner,
              const GlobalScope* enclosing_scope,
              const StaticFields* static_fields,
              std::span<const ClassAccessor> accessors,
              std::span<const ClassAttachedMethod> attached_methods,
              std::optional<ClassParent> parent,
              CreationExpansion& expansion,
              std::ostream& output,
              LocalLexicalBudget budget, abc::StringBudget strings,
              analysis::CapturedValues captures)
      : file_(file),
        header_(header),
        index_(index),
        lookup_(lookup),
        roles_(roles),
        module_(module),
        owner_(owner),
        enclosing_scope_(enclosing_scope),
        static_fields_(static_fields),
        accessors_(accessors),
        attached_methods_(attached_methods),
        parent_(parent),
        captures_(captures),
        expansion_(expansion),
        output_(output),
        budget_(budget),
        strings_(strings) {}

  std::size_t Write(const analysis::ClassCreation& creation,
                    const abc::MethodTarget& target,
                    const abc::ClassBuffer& members, std::string_view name) {
    const bool has_members = !members.properties.empty() ||
                             !attached_methods_.empty();
    if (members.properties.size() > SIZE_MAX - attached_methods_.size())
      throw ClassFailure{CreatedClassError::kResourceLimit};
    const auto self_count = members.properties.size() + attached_methods_.size();
    AdmitClassBytes(self_count, sizeof(SelfBindingUse), budget_.bytes);
    std::vector<SelfBindingUse> self_uses(self_count, SelfBindingUse::kUnknown);
    AdmitClassBytes(self_uses.capacity() - self_count, sizeof(SelfBindingUse), budget_.bytes);
    if (has_members) Text("(() => {\nconst ClassValue = ");
    Text("(class ");
    Text(name);
    if (parent_) {
      std::visit([&](const auto& origin) {
        using Origin = std::decay_t<decltype(origin)>;
        if constexpr (std::is_same_v<Origin, ImportedClassParent>)
          Text(" extends $loaded");
        else if constexpr (std::is_same_v<Origin, GlobalClassParent>)
          Text(" extends $global");
        else {
          Text(" extends ");
          Text(origin.binding);
        }
        if constexpr (!std::is_same_v<Origin, BoundClassParent>)
          Number(origin.snapshot);
      }, *parent_);
    }
    Text(" {\n");
    Constructor(creation, target, name);
    for (std::size_t i = 0; i < members.properties.size(); ++i) {
      Text("\n");
      Member(members.properties[i], SelfName(i, name), name, self_uses[i]);
    }
    for (const auto& accessor : accessors_) {
      Text("\n");
      Accessor(accessor, name);
    }
    for (std::size_t i = 0; i < attached_methods_.size(); ++i) {
      Text("\n");
      Attached(attached_methods_[i],
               SelfName(members.properties.size() + i, name), name,
               self_uses[members.properties.size() + i]);
    }
    if (static_fields_)
      for (const auto& field : static_fields_->fields) {
        Text("\n");
        auto* positions = dynamic_cast<CaptureBuffer*>(output_.rdbuf());
        GeneratedPosition begin;
        const bool mapped = field.origin && positions && positions->Position(begin);
        Text("static ");
        switch (ClassifyStaticFieldKey(field.name)) {
          case MemberKeySyntax::kIdentifier:
            for (const auto unit : field.name) {
              const auto ch = static_cast<char>(unit);
              Text({&ch, 1});
            }
            break;
          case MemberKeySyntax::kQuoted:
            Text("[");
            Account(TakeClassValue<std::size_t>(WriteStringLiteral(
                field.name, output_, budget_.output)));
            Text("]");
            break;
        }
        Text(" = ");
        StaticValueText(static_fields_->values[field.value]);
        Text(";");
        if (mapped) {
          GeneratedPosition end;
          if (!positions->Position(end)) throw ClassFailure{EmitError::kWrite};
          positions->MappedSpan(field.origin->method,
                                {field.origin->pc, begin, end});
        }
      }
    Text("\n})");
    if (has_members) {
      Text(";\n");
      // All admitted keys are constant ordinary own data properties. No user
      // code can run before these captures, and the class has not escaped.
      // Never move these lookups into the method body: properties are mutable.
      for (std::size_t i = 0; i < members.properties.size(); ++i) {
        if (self_uses[i] == SelfBindingUse::kUnused) continue;
        const auto& property = members.properties[i];
        Text("const ");
        Text(SelfName(i, name));
        Text(" = ClassValue");
        if (property.placement == abc::ClassPlacement::kPrototype)
          Text(".prototype");
        Text("[");
        const auto key = ReadClassKey(file_, header_, property, strings_);
        Account(TakeClassValue<std::size_t>(
            WriteStringLiteral(key, output_, budget_.output)));
        Text("];\n");
      }
      for (std::size_t i = 0; i < attached_methods_.size(); ++i) {
        if (self_uses[members.properties.size() + i] == SelfBindingUse::kUnused) continue;
        const auto& method = attached_methods_[i];
        Text("const ");
        Text(SelfName(members.properties.size() + i, name));
        Text(" = ClassValue");
        if (method.placement == abc::ClassPlacement::kPrototype)
          Text(".prototype");
        Text("[");
        Account(TakeClassValue<std::size_t>(WriteStringLiteral(
            method.key, output_, budget_.output)));
        Text("];\n");
      }
      Text("return ClassValue;\n})()");
    }
    return written_;
  }

 private:
  void StaticAtomText(const StaticAtom& atom) {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, StaticUndefined>)
            Text("void 0");
          else if constexpr (std::is_same_v<T, StaticNull>)
            Text("null");
          else if constexpr (std::is_same_v<T, bool>)
            Text(value ? "true" : "false");
          else if constexpr (std::is_same_v<T, std::u16string>)
            Account(TakeClassValue<std::size_t>(WriteStringLiteral(
                value, output_, budget_.output)));
          else {
            const analysis::ScalarValue scalar = [&]() -> analysis::ScalarValue {
              if constexpr (std::is_same_v<T, std::int32_t>)
                return analysis::IntegerLiteral{value};
              else
                return analysis::Float64Bits{value.bits};
            }();
            Account(TakeClassValue<std::size_t>(WriteScalarExpression(
                scalar, {"", {}}, output_, budget_.output)));
          }
        },
        atom);
  }
  void StaticValueText(const StaticValue& value) {
    if (const auto* atom = std::get_if<StaticAtom>(&value)) {
      StaticAtomText(*atom);
      return;
    }
    if (const auto* node = std::get_if<RecordNodeRef>(&value)) {
      const auto written = WriteRecordLiteral(
          static_fields_->nodes, node->id, output_, budget_.output);
      if (const auto* error = std::get_if<EmitError>(&written))
        throw ClassFailure{*error};
      if (std::holds_alternative<RecordLiteralError>(written))
        throw ClassFailure{CreatedClassError::kStaticFieldValue};
      Account(std::get<std::size_t>(written));
      return;
    }
    Text("[");
    bool comma = false;
    for (const auto& element : std::get<StaticArray>(value).elements) {
      if (comma) Text(", ");
      comma = true;
      StaticAtomText(element);
    }
    Text("]");
  }
  static std::string SelfName(std::size_t ordinal, std::string_view name) {
    auto binding = "MethodSelf" + std::to_string(ordinal);
    if (binding == name) binding += '_';
    return binding;
  }
  void Account(std::size_t size) {
    if (size > budget_.output.bytes)
      throw ClassFailure{EmitError::kOutputLimit};
    budget_.output.bytes -= size;
    written_ += size;
  }
  void Text(std::string_view text) {
    Account(text.size());
    output_ << text;
    if (!output_) throw ClassFailure{EmitError::kWrite};
  }
  void Number(std::size_t value) {
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> digits{};
    const auto result = std::to_chars(digits.data(),
                                      digits.data() + digits.size(), value);
    if (result.ec != std::errc{})
      throw ClassFailure{CreatedClassError::kResourceLimit};
    Text({digits.data(), static_cast<std::size_t>(result.ptr - digits.data())});
  }
  template <typename Emit>
  void Body(const abc::MethodInfo& method, LocalLexicalBudget budget,
            std::string_view class_name, Emit emit) {
    if (module_ && method.owner_offset != owner_)
      throw ClassFailure{CreatedClassError::kModuleOwner};
    if (!method.code) throw ClassFailure{CreatedClassError::kMissingCode};
    // The grant remains live across decoding, body proof and nested printing.
    // All descendants receive this context; never reset ancestry per member.
    ExpansionFrame admission(expansion_, method.offset, budget);
    if (const auto error = admission.error())
      throw ClassFailure{*error == ExpansionError::kResourceLimit
                             ? CreatedClassError::kResourceLimit
                             : CreatedClassError::kRecursiveExpansion};
    const auto call = TakeClassValue<abc::CallType>(abc::ReadCallType(
        file_, header_, index_, method.annotations, {budget.bytes}));
    const auto frame = TakeClassValue<abc::FrameLayout>(
        abc::BuildFrameLayout(method.code->registers, method.code->arguments,
                              abc::DecodeCallFlags(call.value)));
    const auto body = TakeClassValue<abc::MethodBody>(abc::DecodeMethodBody(
        file_, header_.profile, *method.code, {budget.bytes}));
    AdmitClassBytes(body.instructions.capacity(), sizeof(abc::BodyInstruction),
                    budget.bytes);
    AdmitClassBytes(body.tries.capacity(), sizeof(abc::TryRegion),
                    budget.bytes);
    AdmitClassBytes(body.handlers.capacity(), sizeof(abc::CatchHandler),
                    budget.bytes);
    std::optional<RecoveredParameters> parameter_names;
    std::vector<std::string_view> parameter_views;
    auto recovered = RecoverParameterNames(file_, header_, method,
                                           frame,
                                           body, budget.bytes, strings_,
                                           class_name);
    if (const auto* found = std::get_if<RecoveredParameters>(&recovered)) {
      parameter_names.emplace(std::move(*found));
      const auto used = parameter_names->AllocatedBytes();
      if (used > budget.bytes)
        throw ClassFailure{CreatedClassError::kResourceLimit};
      budget.bytes -= used;
      AdmitClassBytes(parameter_names->names.size(), sizeof(std::string_view),
                      budget.bytes);
      parameter_views.reserve(parameter_names->names.size());
      if (parameter_views.capacity() > parameter_names->names.size())
        AdmitClassBytes(parameter_views.capacity() - parameter_names->names.size(),
                        sizeof(std::string_view), budget.bytes);
      for (const auto& name : parameter_names->names)
        parameter_views.push_back(name);
    } else if (const auto* error =
                   std::get_if<abc::MetadataError>(&recovered)) {
      throw ClassFailure{*error};
    } else if (std::get<ParameterNameError>(recovered) ==
               ParameterNameError::kResourceLimit) {
      throw ClassFailure{CreatedClassError::kResourceLimit};
    }
    std::optional<GlobalScope> global_scope;
    if (enclosing_scope_) {
      global_scope = *enclosing_scope_;
      global_scope->class_binding = class_name;
    }
    MethodConstants constants{file_, header_, index_, method.offset,
                                    strings_, module_, roles_, global_scope,
                                    parameter_views};
    constants.leaf_methods = &lookup_;
    constants.expansion = &expansion_;
    Account(TakeClassValue<std::size_t>(emit(frame, body, budget, constants)));
  }
  void Constructor(const analysis::ClassCreation& creation,
                   const abc::MethodTarget& target, std::string_view name) {
    // Preserve the caller's target index for nested DEFINEFUNC in class bodies;
    // each child still passes the leaf emitter's kind/capture/owner checks.
    Body(index_.methods[target.position.value], budget_, name,
         [&](const abc::FrameLayout& frame, const abc::MethodBody& body,
             LocalLexicalBudget budget, const MethodConstants& constants) {
           if (parent_) {
             const auto proved =
                 analysis::ProveForwardingDerivedConstructor(
                     body, frame, {budget.bytes});
             if (const auto* forwarding =
                     std::get_if<analysis::ForwardingDerivedConstructor>(
                         &proved))
               return WriteDerivedConstructor(
                   *forwarding, creation.length, frame,
                   constants.source_parameters, output_, budget.output);
             if (std::get<analysis::DerivedConstructorError>(proved) ==
                 analysis::DerivedConstructorError::kResourceLimit)
               return FunctionEmitResult{FunctionEmitError::kResourceLimit};
             const auto fields =
                 analysis::ProveSpreadFieldDerivedConstructor(
                     body, frame, {budget.bytes});
             if (const auto* spread =
                     std::get_if<analysis::SpreadFieldDerivedConstructor>(
                         &fields)) {
               const auto runtime = std::visit(
                   [](const auto& origin) { return origin.runtime; },
                   *parent_);
               if (runtime != ParentRuntimeInvariant::kAlwaysJsFunction)
                 return FunctionEmitResult{
                     FunctionEmitError::kSuperclass};
               return WriteSpreadFieldConstructor(
                   *spread, creation.length, constants, output_,
                   budget.output);
             }
             const auto range = WriteRangeDerivedConstructor(
                 {creation, target.attributes, frame, name}, body, output_,
                 budget, &constants, &lookup_);
             if (std::holds_alternative<std::size_t>(range) ||
                 !std::holds_alternative<FunctionEmitError>(range) ||
                 std::get<FunctionEmitError>(range) != FunctionEmitError::kKind)
               return range;
             return FunctionEmitResult{
                 std::get<analysis::DerivedConstructorError>(fields) ==
                         analysis::DerivedConstructorError::kResourceLimit
                     ? FunctionEmitError::kResourceLimit
                     : FunctionEmitError::kKind};
           }
           return WriteBaseConstructor(
               {creation, target.attributes, frame, name}, body, output_,
               budget, &constants, &lookup_);
         });
  }
  void Member(const abc::ClassProperty& property, std::string_view self,
              std::string_view class_name, SelfBindingUse& self_use) {
    const auto target =
        ResolveClassMember(header_, index_, lookup_, property, roles_);
    const auto key = ReadClassKey(file_, header_, property, strings_);
    auto budget = budget_;
    // Key survives body emission; body constant-string scratch uses a separate
    // admitted string allowance, so the retained key must also be accounted.
    AdmitClassBytes(key.capacity(), sizeof(char16_t), budget.bytes);
    Body(index_.methods[target.position.value], budget, class_name,
         [&](const abc::FrameLayout& frame, const abc::MethodBody& body,
             LocalLexicalBudget remaining, const MethodConstants& constants) {
           const ClassMethod method{std::get<abc::ClassMethod>(property.value),
                                    target.attributes,
                                    frame,
                                    property.placement,
                                    key,
                                    self,
                                    EffectiveMethodKind::kOrdinary};
           return WriteClassMethod(method, body, output_, remaining,
                                   &constants, &lookup_, captures_, &self_use);
         });
  }
  void Accessor(const ClassAccessor& accessor, std::string_view class_name) {
    const auto& target = index_.methods[accessor.method.value];
    const auto attributes = TakeClassValue<abc::MethodAttributes>(
        abc::DecodeMethodAttributes(target.access_flags, header_.profile));
    auto budget = budget_;
    AdmitClassBytes(accessor.key.capacity(), sizeof(char16_t), budget.bytes);
    Body(target, budget, class_name,
         [&](const abc::FrameLayout& frame, const abc::MethodBody& body,
             LocalLexicalBudget remaining, const MethodConstants& constants) {
           const ClassMethod method{{target.offset, accessor.kind,
                                     accessor.length},
                                    attributes,
                                    frame,
                                    accessor.placement,
                                    accessor.key,
                                    "AccessorSelf",
                                    accessor.kind == abc::ClassMethodKind::kGetter
                                        ? EffectiveMethodKind::kGetter
                                        : EffectiveMethodKind::kSetter};
           return WriteClassMethod(method, body, output_, remaining,
                                   &constants, &lookup_, captures_);
         });
  }
  void Attached(const ClassAttachedMethod& attached, std::string_view self,
                std::string_view class_name, SelfBindingUse& self_use) {
    const auto& target = index_.methods[attached.method.value];
    const auto attributes = TakeClassValue<abc::MethodAttributes>(
        abc::DecodeMethodAttributes(target.access_flags, header_.profile));
    auto budget = budget_;
    AdmitClassBytes(attached.key.capacity(), sizeof(char16_t), budget.bytes);
    Body(target, budget, class_name,
         [&](const abc::FrameLayout& frame, const abc::MethodBody& body,
             LocalLexicalBudget remaining, const MethodConstants& constants) {
           const ClassMethod method{{target.offset,
                                     abc::ClassMethodKind::kOrdinary,
                                     attached.length},
                                    attributes, frame, attached.placement,
                                    attached.key, self,
                                    EffectiveMethodKind::kOrdinary};
           return WriteClassMethod(method, body, output_, remaining,
                                   &constants, &lookup_, captures_, &self_use);
         });
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const analysis::MethodRoleIndex* roles_;
  const abc::ModuleInfo* module_;
  abc::EntityOffset owner_;
  const GlobalScope* enclosing_scope_;
  const StaticFields* static_fields_;
  std::span<const ClassAccessor> accessors_;
  std::span<const ClassAttachedMethod> attached_methods_;
  std::optional<ClassParent> parent_;
  analysis::CapturedValues captures_;
  CreationExpansion& expansion_;
  std::ostream& output_;
  LocalLexicalBudget budget_;
  abc::StringBudget strings_;
  std::size_t written_ = 0;
};
}  // namespace
CreatedClassResult WriteCreatedClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::BodyInstruction& instruction,
    analysis::ClassHeritage heritage, std::ostream& output,
    LocalLexicalBudget budget, abc::StringBudget strings,
    const analysis::MethodRoleIndex* roles, const abc::ModuleInfo* module,
    const GlobalScope* enclosing_scope,
    const StaticFields* static_fields,
    std::span<const ClassAccessor> accessors,
    std::optional<ClassParent> parent,
    std::span<const ClassAttachedMethod> attached_methods) {
  const auto lifted = analysis::LiftClassCreation(instruction.decoded);
  if (const auto* error = std::get_if<analysis::CreationError>(&lifted))
    return *error;
  return WriteCreatedClass(file, header, index, lookup, caller,
      std::get<analysis::ClassCreation>(lifted), instruction.offset, heritage,
      output, budget, strings, roles, module, enclosing_scope, static_fields,
      accessors, parent, attached_methods);
}

CreatedClassResult WriteCreatedClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const analysis::ClassCreation& creation,
    abc::CodeOffset origin, analysis::ClassHeritage heritage,
    std::ostream& output, LocalLexicalBudget budget, abc::StringBudget strings,
    const analysis::MethodRoleIndex* roles, const abc::ModuleInfo* module,
    const GlobalScope* enclosing_scope, const StaticFields* static_fields,
    std::span<const ClassAccessor> accessors, std::optional<ClassParent> parent,
    std::span<const ClassAttachedMethod> attached_methods,
    CreationExpansion* expansion) {
  try {
    // Admitted dynamic profiles use the pinned v7 class interpreter's
    // CreateClassWithBuffer/RuntimeSetClassInheritanceRelationship path.
    const bool derived_profile =
        header.profile == abc::ProfileId::kDynamic1202 ||
        header.profile == abc::ProfileId::kDynamic1206 ||
        header.profile == abc::ProfileId::kDynamic1301;
    if (heritage.pc != origin ||
        (parent ? (heritage.kind != analysis::HeritageKind::kUnknown ||
                   !derived_profile)
                : heritage.kind != analysis::HeritageKind::kAbsent))
      return CreatedClassError::kHeritage;
    const auto target =
        TakeClassValue<abc::MethodTarget>(abc::ResolveMethodTarget(
            file, header, index, lookup, caller, creation.constructor));
    if (target.attributes.sendable) return FunctionEmitError::kSendable;
    const auto members = ReadMembers(file, header, index, caller,
                                     creation.literal, budget.bytes);
    if (static_fields)
      for (const auto& field : static_fields->fields)
        if (field.value >= static_fields->values.size() ||
            field.name == u"prototype")
          return CreatedClassError::kStaticFieldValue;
    if (static_fields && !static_fields->fields.empty() &&
        (!members.properties.empty() || !attached_methods.empty()))
      return CreatedClassError::kStaticFieldOrder;
    if (!accessors.empty() && static_fields &&
        !static_fields->fields.empty())
      return CreatedClassError::kAccessorOrder;
    AdmitClassBytes(members.properties.capacity(), sizeof(abc::ClassProperty),
                    budget.bytes);
    CheckClassMembers(file, header, index, lookup, members, roles, budget.bytes,
                      strings);
    const auto name = RuntimeName(index.methods[target.position.value]);
    if (!name) return CreatedClassError::kName;
    if (parent) {
      if (const auto* bound = std::get_if<BoundClassParent>(&*parent);
          bound && (!IsSourceIdentifier(bound->binding) ||
                    bound->binding == *name || bound->binding == "ClassValue" ||
                    bound->binding.starts_with("MethodSelf")))
        return CreatedClassError::kHeritage;
    }
    abc::EntityOffset caller_owner{};
    if (module || !accessors.empty() || !attached_methods.empty()) {
      const auto caller_position = lookup.Find(caller);
      if (!caller_position) return abc::MethodTargetError::kCallerMissing;
      caller_owner = index.methods[caller_position->value].owner_offset;
    }
    CheckAccessors(file, header, index, roles, caller_owner, members,
                   accessors, budget.bytes, strings);
    CheckAttachedMethods(header, index, lookup, members, accessors,
                         attached_methods, roles, caller_owner, budget.bytes);
    CreationExpansion root(caller, budget.bytes, budget.work);
    auto& context = expansion ? *expansion : root;
    // Determine the real sink's capability once. The dry sink must not grant
    // capture support that the publication sink does not have.
    const auto captures = dynamic_cast<CaptureBuffer*>(output.rdbuf())
        ? analysis::CapturedValues::kLanguage : analysis::CapturedValues::kReject;
    if (!dynamic_cast<UnpublishedOutput*>(output.rdbuf())) {
      CaptureDiscard discard;
      std::ostream dry(&discard);
      ClassWriter(file, header, index, lookup, roles, module, caller_owner,
                  enclosing_scope, static_fields, accessors, attached_methods,
                  parent, context, dry, budget, strings, captures)
          .Write(creation, target, members, *name);
    }
    return ClassWriter(file, header, index, lookup, roles, module, caller_owner,
                       enclosing_scope, static_fields, accessors,
                       attached_methods, parent, context,
                       output, budget, strings, captures)
        .Write(creation, target, members, *name);
  } catch (const ClassFailure& failure) {
    return failure.result;
  } catch (const std::bad_alloc&) {
    return CreatedClassError::kResourceLimit;
  } catch (const std::ios_base::failure&) {
    return EmitError::kWrite;
  }
}
}  // namespace abcd::source
