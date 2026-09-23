#include "abcd/source/created_class.h"

#include <new>
#include <ostream>

#include "abcd/abc/call_type.h"
#include "abcd/source/base_constructor.h"
#include "abcd/source/class_method.h"
#include "abcd/source/string_literal.h"
#include "capture_stream.h"
#include "class_emission.h"
#include "runtime_name.h"

namespace abcd::source {
namespace {
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

class ClassWriter {
 public:
  ClassWriter(const corpus::AbcFile& file, const abc::Header& header,
              const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
              const analysis::MethodRoleIndex* roles,
              const abc::ModuleInfo* module, abc::EntityOffset owner,
              std::ostream& output,
              LocalLexicalBudget budget, abc::StringBudget strings)
      : file_(file),
        header_(header),
        index_(index),
        lookup_(lookup),
        roles_(roles),
        module_(module),
        owner_(owner),
        output_(output),
        budget_(budget),
        strings_(strings) {}

  std::size_t Write(const analysis::ClassCreation& creation,
                    const abc::MethodTarget& target,
                    const abc::ClassBuffer& members, std::string_view name) {
    const bool has_members = !members.properties.empty();
    if (has_members) Text("(() => {\nconst ClassValue = ");
    Text("(class ");
    Text(name);
    Text(" {\n");
    Constructor(creation, target, name);
    for (std::size_t i = 0; i < members.properties.size(); ++i) {
      Text("\n");
      Member(members.properties[i], SelfName(i, name));
    }
    Text("\n})");
    if (has_members) {
      Text(";\n");
      // All admitted keys are constant ordinary own data properties. No user
      // code can run before these captures, and the class has not escaped.
      // Never move these lookups into the method body: properties are mutable.
      for (std::size_t i = 0; i < members.properties.size(); ++i) {
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
      Text("return ClassValue;\n})()");
    }
    return written_;
  }

 private:
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
  template <typename Emit>
  void Body(const abc::MethodInfo& method, LocalLexicalBudget budget,
            Emit emit) {
    if (module_ && method.owner_offset != owner_)
      throw ClassFailure{CreatedClassError::kModuleOwner};
    if (!method.code) throw ClassFailure{CreatedClassError::kMissingCode};
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
    const MethodConstants constants{file_, header_, index_, method.offset,
                                    strings_, module_};
    Account(TakeClassValue<std::size_t>(emit(frame, body, budget, constants)));
  }
  void Constructor(const analysis::ClassCreation& creation,
                   const abc::MethodTarget& target, std::string_view name) {
    Body(index_.methods[target.position.value], budget_,
         [&](const abc::FrameLayout& frame, const abc::MethodBody& body,
             LocalLexicalBudget budget, const MethodConstants& constants) {
           return WriteBaseConstructor(
               {creation, target.attributes, frame, name}, body, output_,
               budget, &constants);
         });
  }
  void Member(const abc::ClassProperty& property, std::string_view self) {
    const auto target =
        ResolveClassMember(header_, index_, lookup_, property, roles_);
    const auto key = ReadClassKey(file_, header_, property, strings_);
    auto budget = budget_;
    // Key survives body emission; body constant-string scratch uses a separate
    // admitted string allowance, so the retained key must also be accounted.
    AdmitClassBytes(key.capacity(), sizeof(char16_t), budget.bytes);
    Body(index_.methods[target.position.value], budget,
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
                                   &constants);
         });
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const analysis::MethodRoleIndex* roles_;
  const abc::ModuleInfo* module_;
  abc::EntityOffset owner_;
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
    const analysis::MethodRoleIndex* roles, const abc::ModuleInfo* module) {
  try {
    if (heritage.pc != instruction.offset ||
        heritage.kind != analysis::HeritageKind::kAbsent)
      return CreatedClassError::kHeritage;
    const auto creation = TakeClassValue<analysis::ClassCreation>(
        analysis::LiftClassCreation(instruction.decoded));
    const auto target =
        TakeClassValue<abc::MethodTarget>(abc::ResolveMethodTarget(
            file, header, index, lookup, caller, creation.constructor));
    if (target.attributes.sendable) return FunctionEmitError::kSendable;
    const auto members = ReadMembers(file, header, index, caller,
                                     creation.literal, budget.bytes);
    AdmitClassBytes(members.properties.capacity(), sizeof(abc::ClassProperty),
                    budget.bytes);
    CheckClassMembers(file, header, index, lookup, members, roles, budget.bytes,
                      strings);
    const auto name = RuntimeName(index.methods[target.position.value]);
    if (!name) return CreatedClassError::kName;
    abc::EntityOffset caller_owner{};
    if (module) {
      const auto caller_position = lookup.Find(caller);
      if (!caller_position) return abc::MethodTargetError::kCallerMissing;
      caller_owner = index.methods[caller_position->value].owner_offset;
    }
    CaptureDiscard discard;
    std::ostream dry(&discard);
    ClassWriter(file, header, index, lookup, roles, module, caller_owner, dry,
                budget, strings)
        .Write(creation, target, members, *name);
    return ClassWriter(file, header, index, lookup, roles, module, caller_owner,
                       output, budget, strings)
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
