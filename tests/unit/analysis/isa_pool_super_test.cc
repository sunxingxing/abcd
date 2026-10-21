#include "abcd/analysis/isa_catalog.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/value_import.h"
#include "../../../src/analysis/value_operands.h"
#include "../../../src/source/bigint_literal.h"
#include "../../../src/source/tree_structure.h"
#include "analysis/ssa_fixture.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using test::Check;
namespace {
abc::DecodedInstruction Instruction(abc::Opcode opcode) {
  const auto* spec=FindPinnedInstruction(opcode);Check(spec!=nullptr);
  std::vector<std::uint8_t> bytes(spec->size);bytes[0]=static_cast<std::uint8_t>(opcode);
  return std::get<abc::DecodedInstruction>(abc::DecodeInstruction(bytes,abc::ProfileId::kDynamic1206));
}
void Raw() {
  for(const auto opcode:{abc::Opcode::kLDSUPERBYVALUE_IMM8_V8,abc::Opcode::kLDSUPERBYVALUE_IMM16_V8,abc::Opcode::kLDBIGINT_ID16}) {
    const auto decoded=Instruction(opcode);
    const auto lift=[&](const auto& d){return LiftSemanticInstruction({{0},d},abc::ProfileId::kDynamic1206,1);};
    const auto good=std::get<SemanticInstruction>(lift(decoded));
    Check(good.effects.exceptional==ExceptionalCompletion::kMayThrow);
    Check(good.effects.normal_registers.write.has_value());

    if(opcode==abc::Opcode::kLDBIGINT_ID16) {
      Check(std::get<StringLoad>(good.operation).kind==PoolTextKind::kBigInt);
      Check(std::get<ValueString>(ImportValueOperation(good.operation)).kind==PoolTextKind::kBigInt);
      Check(good.effects.normal==LanguageEffect::kAllocation);
    } else {
      Check(std::get<ValuePropertyRead>(ImportValueOperation(good.operation)).lookup==PropertyLookup::kSuper);
      Check(std::get<SemanticFailure>(LiftSemanticInstruction({{0},decoded},abc::ProfileId::kDynamic1206,0)).kind==SemanticError::kMalformedInstruction);
    }
    for(std::size_t i=0;i<decoded.operand_count;++i) {
      auto bad=decoded;bad.operands[i].raw_bits=UINT64_MAX;
      Check(std::get<SemanticFailure>(lift(bad)).kind==SemanticError::kMalformedInstruction);
      bad=decoded;bad.operands[i].kind=abc::OperandKind::kMethod;
      Check(std::get<SemanticFailure>(lift(bad)).kind==SemanticError::kMalformedInstruction);
    }
  }
}
source::FunctionTree Tree(std::uint32_t n) {
  using namespace source;
  FunctionTree t{};t.callable=CallableKind::kFunction;t.strictness=Strictness::kStrict;
  t.parameter_scope={0};t.body_scope={1};t.body={0};
  t.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  t.origins.emplace_back(OriginalSite{{0},{0}});t.texts={u"123456789012345678901234567890"};
  t.statements.push_back({BlockStmt{{1},0,n},{0}});
  for(std::uint32_t i=0;i<n;++i){t.expressions.push_back({BigIntExpr{{0}},{0}});t.statements.push_back({ExpressionStmt{ExprId{i}},{0}});t.children.push_back({i+1});}
  return t;
}
void TextAndScale() {
  using namespace source;
  for(const auto* text:{u"0",u"1",u"123456789012345678901234567890",u"0xabcdef",u"0XABC",u"0o777",u"0b1010"})Check(ValidBigIntLiteral(text));
  for(const auto* text:{u"",u"00",u"01",u"-1",u"+1",u" 1",u"1n",u"0x",u"0b2",u"0o8",u"1_2",u"1.0",u"1;f()",u"\u0661",u"1\n"}) {
    Check(!ValidBigIntLiteral(text));auto t=Tree(1);t.texts[0]=text;
    Check(std::get<TreeStructureError>(VerifyTreeStructure(t))==TreeStructureError::kUnsupportedSyntax);
  }
  for(const auto n:{200U,201U,400U,800U}) {
    std::vector<std::uint8_t> bytes;
    for(std::uint32_t i=0;i<n;++i){bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kLDBIGINT_ID16));bytes.push_back(0);bytes.push_back(0);}
    bytes.push_back(static_cast<std::uint8_t>(abc::Opcode::kRETURN));
    const auto body=test::Body(bytes);
    const auto run=[&](SemanticBudget b){return BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,0,b);};
    Check(std::holds_alternative<SemanticMethod>(run({})));
    for(const bool work:{false,true}) {
      const auto limit=[&](std::size_t v){SemanticBudget b;if(work)b.work=v;else b.bytes=v;return b;};
      std::size_t lo=0,hi=64*1024*1024;
      while(lo<hi){const auto mid=lo+(hi-lo)/2;if(std::holds_alternative<SemanticMethod>(run(limit(mid))))hi=mid;else lo=mid+1;}
      Check(lo>0&&std::holds_alternative<SemanticMethod>(run(limit(lo))));Check(std::holds_alternative<SemanticFailure>(run(limit(lo-1))));
      std::cout<<n<<(work?" lift work ":" lift bytes ")<<lo<<'\n';
    }
    auto t=Tree(n);const auto valid=std::get<TreeStructure>(VerifyTreeStructure(t));
    const SemanticBudget exact{valid.allocated_bytes,valid.work};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(t,exact)));
    for(const bool work:{false,true}){auto b=exact;if(work)--b.work;else --b.bytes;Check(std::get<TreeStructureError>(VerifyTreeStructure(t,b))==(work?TreeStructureError::kWorkLimit:TreeStructureError::kResourceLimit));}
    t.expressions[0].expression=BigIntExpr{{1}};Check(std::get<TreeStructureError>(VerifyTreeStructure(t))==TreeStructureError::kRange);
  }
}
}
int main(){Raw();TextAndScale();}
