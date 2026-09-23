#include "abcd/analysis/constant_object.h"

namespace abcd::analysis {
ConstantObjectResult LiftConstantObjectInstruction(
    const abc::DecodedInstruction& instruction) {
  using abc::Opcode;
  using Kind = ConstantObjectKind;
  using Acc = AccumulatorEffect;
  using Context = ContextEffect;
  using Completion = ConstantCompletion;
  ConstantObjectInstruction result;
  switch (instruction.opcode) {
    case Opcode::kLDNAN:
      result = {Kind::kNaN, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDINFINITY:
      result = {Kind::kInfinity, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDUNDEFINED:
      result = {Kind::kUndefined, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDNULL:
      result = {Kind::kNull, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDSYMBOL:
      result = {Kind::kSymbolConstructor, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDGLOBAL:
      result = {Kind::kGlobalObject, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDTRUE:
      result = {Kind::kTrue, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDFALSE:
      result = {Kind::kFalse, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDHOLE:
      result = {Kind::kHole, Acc::kHole, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kDEPRECATED_LDLEXENV_PREF_NONE:
      result = {Kind::kLexicalEnvironment, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kLDNEWTARGET:
      result = {Kind::kNewTarget, Acc::kValue, Context::kNone, Completion::kUnimplemented};
      break;
    case Opcode::kLDTHIS:
      result = {Kind::kThis, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kPOPLEXENV:
      result = {Kind::kPopLexicalEnvironment, Acc::kPreserve,
                Context::kPopLexicalEnvironment, Completion::kNormal};
      break;
    case Opcode::kDEPRECATED_POPLEXENV_PREF_NONE:
      result = {Kind::kDeprecatedPopLexicalEnvironment, Acc::kPreserve,
                Context::kPopLexicalEnvironment, Completion::kNormal};
      break;
    case Opcode::kGETUNMAPPEDARGS:
      result = {Kind::kUnmappedArguments, Acc::kValue, Context::kNone, Completion::kMayThrow};
      break;
    case Opcode::kASYNCFUNCTIONENTER:
      result = {Kind::kAsyncFunctionEnter, Acc::kValue, Context::kNone, Completion::kMayThrow};
      break;
    case Opcode::kLDFUNCTION:
      result = {Kind::kCurrentFunction, Acc::kValue, Context::kNone, Completion::kNormal};
      break;
    case Opcode::kDEBUGGER:
      result = {Kind::kDebugger, Acc::kPreserve, Context::kDebugger, Completion::kNormal};
      break;
    default:
      return ConstantObjectError::kUnsupportedOpcode;
  }
  if (instruction.operand_count != 0)
    return ConstantObjectError::kMalformedInstruction;
  return result;
}
}  // namespace abcd::analysis
