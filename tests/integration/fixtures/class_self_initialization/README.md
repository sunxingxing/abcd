# Class-self initialization evidence fixtures

These are real dynamic es2abc outputs. Adjacent JS is an independent test
oracle and is never an engine input. This increment proves raw initialization,
read-only capture and cleanup facts; it does **not** admit source class scope.
The Closed initializer reader must still refuse these fixtures. No engine-output
Node differential or recovery gate is claimed yet.

Compiler: DevEco Studio es2abc, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
From the repository root (NAME = outer / throwing / reentry / branches / copies / accessors):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=class_self_NAME \
  --output tests/integration/fixtures/class_self_initialization/NAME.abc \
  tests/integration/fixtures/class_self_initialization/NAME.js
```

For `self`, use `--record-name=class_self_initialization` instead. The unit test
checks actual creation/store/call identities, every literal/defined member root,
transitive capture edges, original guard sites, exception exits, and the original
cleanup dispatch. It does not recognize any fixture by name, register or PC.
`outer` has distinct self and outer cells, `reentry` includes a method's self
read, `throwing` includes a descendant closure, and `branches`/`copies` exercise
conditional paths and argument copies. Missing descendants and extra roots are
negative tests, alongside storage/guard/cleanup/ordering mutations.

The increment-2 scope-view tests additionally check real incoming addresses,
complete relative owner ranges, self-vs-outer requirements, separate internal and
public commit ports, and the private accessor-installation corridor. `accessors`
contains instance getter/setter, a static getter, and self-reading static-field
initialization. These fixtures still assert the original Closed refusal: the
scope view is not yet consumed by ClassTree, so no recovery gate is claimed.

`class_self_initialization_scale_test` generates 200/201/400/800 fields under the
build directory. Each size checks exact work and the minimum admitted byte
boundary plus one-byte-below refusal. Nested passes' peak scratch can exceed
the retained proof's conservative byte charge; the test measures the actual
admission boundary rather than mistaking retained size for peak memory.

The retained cleanup semantics are pinned at
`references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`:
`POPLEXENV` (4648–4653) changes the frame environment to its parent, without
clearing the captured environment object; `STLEXVAR` (6118–6134) writes the
specified ancestor's slot; `THROW_UNDEFINEDIFHOLEWITHNAME` (4107 onward) retains
the Hole test. EnvironmentCleanup independently reconstructs original dispatch,
Catch input and unchanged-payload rethrow; it is not exception forwarding.

SHA-256 (ABC):

```
b0da4004a63e7238b7d11ea41eb6b5f08546886e08592f640c84f5d081cb6402  branches.abc
6c984d98b30f6dc86c576dc92d0e5a3600db7056e0e7428b633edd52d05a54ca  copies.abc
1d7cd40c0ab9453b3fa344727be2d616d5f5055277069e0f982425de7daec0a5  outer.abc
f4569c1ae90785a99def173d50bc45e46a5b1ae787e2290435ed4cdadeb6da34  reentry.abc
860b2d1d0c6131d015b815fbba7fae061c785475b54d6fadb0388f9794753bda  self.abc
b8e5b7a6cf1454882bbc4e03a774d5e2b0a3d913d53505359e7364c3800f1f68  throwing.abc
f5deb7625c27493f4b4b83539487f1adf071dce5517b06b19c12a5f3156e2777  accessors.abc
```
