# Constant Object Loaders: Generic-Path Evidence

This is group 1 of the pinned v7 ISA plan, not a group-completion certificate.
The dedicated lifter accounts for all 18 encodings; that alone does not prove
that their effects survive the common source pipeline.

## Verified Slice

`tests/integration/constant_group_fixture.cc` covers LDNAN, LDINFINITY,
LDUNDEFINED, LDNULL, LDTRUE and LDFALSE with two bodies each:

1. Load and return the constant.
2. Save the constant, pass it to a callback, discard the call result and return
   the original constant. A throwing callback must prevent normal return.

The same encoded bodies run through profiles 12.0.2.0, 12.0.6.0 and 13.0.1.0.
All 36 positive cases pass the file-backed PreparedFamily/FamilyEmission route,
including shared semantic/SSA/value preparation, AST verification and printer.
Each requires a Printed/Complete method receipt and a nonempty generated
expression interval derived from the original constant instruction at PC 0.
The enclosing test module supplies the strict destination required by the API.
This is not a complete-record receipt.

The independent Node assertions check exact primitive results with Object.is,
function arity, lexical shadowing of NaN/Infinity/undefined, callback argument,
receiver, invocation count, ignored result, thrown-object identity and failure
for a non-callable input. Six additional LDHOLE cases require preparation to
reject direct return and host-call exposure of an internal marker. These are
negative domain tests, not positive LDHOLE decompilation coverage.

The fixture provides encoded method bodies and creation sites with explicit
metadata; it is not a whole-container parser test or a real-application sample.
It does not execute the original methods in an Ark VM, prove all historical
runtime behavior, or compare engine-specific error messages. Evaluation source
and assertions remain outside production inputs. No RSS or benchmark is run.

## Remaining Group Work

- LDGLOBAL and LDSYMBOL: bind realm identity without replacing intrinsic
  identity by a mutable or shadowed global identifier.
- LDTHIS and LDFUNCTION: preserve callable ownership, constructor initialization
  and captured receiver/function identity in the shared callable pipeline.
- LDNEWTARGET: retain the pinned handler's unimplemented classification until
  profile-specific runtime evidence justifies a source representation.
- LDHOLE: verify supported internal consumption without exposing VM markers.
- Deprecated LDLEXENV and both POPLEXENV encodings: track environment ownership
  and actual runtime accumulator behavior, not merely YAML accumulator flags.
- GETUNMAPPEDARGS: preserve arguments identity, descriptors and frame ownership;
  do not substitute an ordinary rest array.
- ASYNCFUNCTIONENTER: integrate the complete async owner and exception phases,
  not a standalone Promise expression.
- DEBUGGER: carry an explicit observable statement through all common stages;
  do not erase it as NOP.

These are obligations, not assertions that no supporting implementation exists.
Each needs common-path positive/negative evidence and context validation before
the group can be marked complete.
