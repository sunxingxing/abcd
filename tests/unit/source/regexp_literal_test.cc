#include "../../../src/source/regexp_literal.h"

#include <cstdlib>
#include <iostream>

#include "../../../src/source/tree_print_failure_json.h"
#include "abcd/analysis/regexp.h"
using namespace abcd;
using namespace abcd::source;
void Check(bool ok) {
  if (!ok) std::abort();
}
int main() {
  for (auto cause :
       {TreeStructureError::kRegExpSyntax, TreeStructureError::kRegExpUnproven,
        TreeStructureError::kRegExpSource}) {
    TreePrintFailure failure{TreePrintError::kInvalidLiteral};
    failure.cause = cause;
    auto parsed = ParseTreePrintFailure(TreePrintFailureJson(failure));
    Check(parsed && parsed->cause == failure.cause);
  }
  for (bool wide : {false, true}) {
    auto code = wide ? abc::Opcode::kCREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8
                     : abc::Opcode::kCREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8;
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(code),
                                    wide ? std::uint8_t{44} : std::uint8_t{1}};
    if (wide) bytes.push_back(1);
    bytes.insert(bytes.end(), {9, 0, 127});
    auto d = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(bytes, abc::ProfileId::kDynamic2400));
    auto raw =
        std::get<analysis::RegExpLiteral>(analysis::LiftRegExpLiteral(d));
    Check(raw.pattern.value == 9 && raw.flags.bits == 127);
    auto lifted = std::get<analysis::SemanticInstruction>(
        analysis::LiftSemanticInstruction({{0}, d},
                                          abc::ProfileId::kDynamic2400, 3));
    const auto& load = std::get<analysis::StringLoad>(lifted.operation);
    Check(load.kind == analysis::PoolTextKind::kRegExp &&
          load.flags.bits == 127 &&
          lifted.effects.normal == analysis::LanguageEffect::kAllocation &&
          lifted.effects.exceptional ==
              analysis::ExceptionalCompletion::kMayThrow);
    d.operands[2].raw_bits = 128;
    Check(std::get<analysis::ScalarError>(analysis::LiftRegExpLiteral(d)) ==
          analysis::ScalarError::kMalformedInstruction);
    d.operands[2].raw_bits = 0;
    d.operands[1].kind = abc::OperandKind::kImmediate;
    Check(std::get<analysis::ScalarError>(analysis::LiftRegExpLiteral(d)) ==
          analysis::ScalarError::kMalformedInstruction);
  }

  for (const auto text :
       {u"", u"abc", u"a/b", u"a\\/b", u"[/]", u"[a-z]+", u"^\\d+(?:abc|def)?$",
        u"\\u0061", u"\\u{1F600}", u"(?<=a)b"}) {
    auto built = BuildRegExpLiteral(text, {analysis::kRegExpUnicode});
    Check(std::holds_alternative<RegExpLiteralPlan>(built));
    const auto& proof = std::get<RegExpLiteralPlan>(built);
    Check(VerifyRegExpLiteral(text, {analysis::kRegExpUnicode}, proof));
    auto forged = proof;
    forged.literal += "x";
    Check(!VerifyRegExpLiteral(text, {analysis::kRegExpUnicode}, forged));
  }
  const auto identity =
      std::get<RegExpLiteralPlan>(BuildRegExpLiteral(u"a", {1}));
  Check(!VerifyRegExpLiteral(u"b", {1}, identity));
  Check(!VerifyRegExpLiteral(u"a", {2}, identity));
  Check(std::get<RegExpLiteralPlan>(BuildRegExpLiteral(u"", {})).literal ==
        "/(?:)/");
  Check(std::get<RegExpLiteralPlan>(BuildRegExpLiteral(u"[/]", {})).literal ==
        "/[\\/]/");
  Check(std::get<RegExpLiteralPlan>(BuildRegExpLiteral(u"a", {127})).literal ==
        "/a/dgimsuy");
  for (const auto text :
       {u"(", u"[", u"*a", u"a**", u"[z-a]", u"a{3,2}", u"\\"})
    Check(std::holds_alternative<RegExpLiteralError>(
        BuildRegExpLiteral(text, {})));
  Check(std::get<RegExpLiteralError>(BuildRegExpLiteral(u"a", {128})) ==
        RegExpLiteralError::kFlags);
  Check(std::get<RegExpLiteralError>(BuildRegExpLiteral(u"a\nb", {})) ==
        RegExpLiteralError::kSourceRepresentation);
  Check(std::get<RegExpLiteralError>(BuildRegExpLiteral(u"\\p{L}", {16})) ==
        RegExpLiteralError::kUnproven);
  for (auto count : {200U, 201U, 400U, 800U})
    for (bool depth : {false, true}) {
      std::u16string text;
      if (depth) text.assign(count, u'(');
      text += u"a";
      if (depth)
        text.append(count, u')');
      else
        text.append(count, u'b');
      Check(std::holds_alternative<RegExpLiteralPlan>(
          BuildRegExpLiteral(text, {})));
      for (bool bytes : {false, true}) {
        std::uint64_t low = 0, high = 1000000;
        while (low < high) {
          auto mid = (low + high) / 2;
          analysis::SemanticBudget q{};
          if (bytes)
            q.bytes = mid;
          else
            q.work = mid;
          if (std::holds_alternative<RegExpLiteralPlan>(
                  BuildRegExpLiteral(text, {}, q)))
            high = mid;
          else
            low = mid + 1;
        }
        analysis::SemanticBudget q{};
        if (bytes)
          q.bytes = low;
        else
          q.work = low;
        Check(std::holds_alternative<RegExpLiteralPlan>(
            BuildRegExpLiteral(text, {}, q)));
        if (count == 800)
          std::cout << (depth ? "depth" : "width") << "800 "
                    << (bytes ? "bytes=" : "work=") << low << '\n';
        if (bytes)
          --q.bytes;
        else
          --q.work;
        Check(std::get<RegExpLiteralError>(BuildRegExpLiteral(text, {}, q)) ==
              (bytes ? RegExpLiteralError::kResourceLimit
                     : RegExpLiteralError::kWorkLimit));
      }
    }
  std::cout
      << "RegExp grammar, flags, source spelling and exact budgets passed\n";
}
