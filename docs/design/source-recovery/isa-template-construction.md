# Tagged-template construction checkpoint (ISA coverage 6)

Recovery is still closed. This round implements RegExp first; it does not claim
GETTEMPLATEOBJECT support or import a Node oracle for it.

The real pinned SDK probe is retained at `build/review/isa-r5-work/tag/tag.{js,abc,pa}`.
The plain case allocates raw array R and cooked array C, interleaves each index's
own definition into R and C, allocates carrier P, defines P[0]=R and P[1]=C,
then passes P to GETTEMPLATEOBJECT and passes the resulting object as the first
argument to the tag. A member tag saves its property Get before these events and
uses the same receiver in CALLTHIS. The invalid-escape case stores Undefined in
C[0], while R[0] contains `\xG`.

`ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:5300–5330` has both modern
widths and the deprecated register form. `ecmascript/template_string.cpp:22–70`
reads carrier[0], checks the identity-keyed template map, reads carrier[1], copies
each cooked/raw entry, freezes the result/raw arrays and caches by R's identity.
The V7 premise covers descriptor enumerability and fresh-array/per-site identity
differences; it does not authorize arbitrary carrier objects or duplicated tag
lookups/substitutions.

A complete construction certificate needs one shared alias/use/order index and
explicit allocation/own-commit/GETTEMPLATEOBJECT/call ports. It must certify:

1. Fresh R/C/P allocation identities, dense equal-length R/C definitions, P's
   exact two references, and absence of all external observations or escapes.
2. Every interleaved original commit, not a source-literal nesting fiction.
   Original exception dispatch and conditional execution stay associated with
   each event; resource-only private operations use the existing resource premise.
3. Raw token spelling and its independently cooked string, or Undefined for an
   invalid tagged escape. Blindly escaping a raw backtick or `${` changes the
   observable raw string. A raw sequence with no equivalent source token refuses.
4. A unique first-argument consumer, the saved tag Reference/receiver, and exact
   substitution ordering after template-object creation and before invocation.
5. Independent re-derivation at evaluation, tree and recovery projections. The
   ordinary literal plan cannot simply flatten the interleaved arrays, and the
   general allocation exclusion must not be removed to make it accept them.

This is an implementation gap, not a request for another language premise. No
partial tagged-template production hook or opcode/sequence admission was added.
