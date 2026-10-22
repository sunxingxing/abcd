"""Test-only pinned dynamic-profile ABC writer; no production dependency.

Layout follows runtime_core/docs/file_format.md and libpandafile/file_items.cpp.
A counting pass resolves typed entities before the writing pass. Both passes use
identical actions; no copied compiler artifact, arbitrary offset patching, or
sample-dependent layout. ASCII strings are a deliberately checked first milestone.
"""
from dataclasses import dataclass
from enum import Enum, auto
import struct
import zlib

U32_MAX = (1 << 32) - 1
HEADER_BYTES = 60
REGION_BYTES = 40
PUBLIC_STATIC = 9
NO_PROTO = 65535
ECMASCRIPT = 0
MAX_BUILDER_BYTES = 128 * 1024 * 1024

class BuildErrorKind(Enum):
    BYTES = auto()
    INTEGER = auto()
    LAYOUT = auto()
    STRING = auto()
    INSTRUCTION = auto()
    BRANCH = auto()
    CATCH = auto()
    ALLOCATION = auto()
    PROFILE = auto()

class BuildError(Exception):
    def __init__(self, kind):
        self.kind = kind
        super().__init__(kind.name)

class Entity(Enum):
    HEADER = auto()
    CLASS_INDEX = auto()
    REGION = auto()
    CLASS_TYPES = auto()
    METHOD_INDEX = auto()
    LITERAL_INDEX = auto()
    METHOD_NAME = auto()
    MODULE_NAME = auto()
    MODULE = auto()
    RECORD = auto()
    METHOD = auto()
    CODE = auto()
    END = auto()

@dataclass(frozen=True, slots=True)
class Label:
    value: int

@dataclass(frozen=True, slots=True)
class Instruction:
    # Encoding is supplied by the generated catalog, not a handwritten ISA map.
    opcode: int
    operands: bytes = b''
    target: Label | None = None
    delta_width: int = 0
    opcode_width: int = 1

@dataclass(frozen=True, slots=True)
class Catch:
    start: Label
    end: Label
    handler: Label
    handler_end: Label

class Profile(Enum):
    # Pinned isa.yaml API-version map; 12.0.2 is an evidenced paired profile.
    # libpandafile/file.h ContainsLiteralArrayInHeader and header.cc reserve
    # the two literal-header words above 12.0.6. No incompatible 11.0.0/1.
    DYNAMIC9 = (9, 0, 0, 0)
    DYNAMIC1102 = (11, 0, 2, 0)
    DYNAMIC1202 = (12, 0, 2, 0)
    DYNAMIC1206 = (12, 0, 6, 0)
    DYNAMIC1301 = (13, 0, 1, 0)
    DYNAMIC2400 = (24, 0, 0, 0)

    @property
    def literal_arrays_in_header(self):
        return self.value <= Profile.DYNAMIC1206.value

# IndexAccessor/JSPandaFile function-kind byte at access_flags bits 8..15.
METHOD_KIND_SHIFT = 8
FUNCTION_LAYOUT_BYTES = 2048  # Conservative typed entity/name/code/index scratch.
class FunctionKind(Enum):
    METHOD = 0
    BASE_CONSTRUCTOR = 1
    ARROW = 2
    GENERATOR = 3
    ASYNC = 4
    ASYNC_GENERATOR = 5
    ASYNC_ARROW = 6
    CONCURRENT = 7

@dataclass(frozen=True, slots=True)
class FunctionCode:
    name: str
    instructions: tuple[Instruction | Label, ...]
    kind: FunctionKind = FunctionKind.BASE_CONSTRUCTOR
    registers: int = 1
    arguments: int = 3
    catches: tuple[Catch, ...] = ()

    # Function code uses the module regional pool; it owns no separate pool.
    @property
    def empty_literals(self):
        return 0

    @property
    def pool_strings(self):
        return ()

@dataclass(frozen=True, slots=True)
class Module:
    instructions: tuple[Instruction | Label, ...]
    registers: int = 1
    arguments: int = 3
    catches: tuple[Catch, ...] = ()
    name: str = 'isa_branch'
    pool_strings: tuple[str, ...] = ()
    # Empty tagged literal arrays for decode/lift probes (not executable class layouts).
    empty_literals: int = 0
    profile: Profile = Profile.DYNAMIC1206
    functions: tuple[FunctionCode, ...] = ()
    exports: tuple[str, ...] = ()

class Writer:
    def __init__(self, limit, offsets, output=None):
        self.limit, self.offsets, self.output, self.pos = limit, offsets, output, 0

    def put(self, value):
        if len(value) > self.limit - self.pos:
            raise BuildError(BuildErrorKind.BYTES)
        if self.output is not None:
            self.output[self.pos:self.pos + len(value)] = value
        self.pos += len(value)

    def mark(self, entity):
        if self.output is None:
            if entity in self.offsets:
                raise BuildError(BuildErrorKind.LAYOUT)
            self.offsets[entity] = self.pos
        elif self.offsets[entity] != self.pos:
            raise BuildError(BuildErrorKind.LAYOUT)

    def uint(self, value, width):
        if value < 0 or value >= 1 << (width * 8):
            raise BuildError(BuildErrorKind.INTEGER)
        self.put(value.to_bytes(width, 'little'))

    def ref(self, entity):
        self.uint(self.offsets.get(entity, 0), 4)

    def leb(self, value):
        if not 0 <= value <= U32_MAX:
            raise BuildError(BuildErrorKind.INTEGER)
        while value >= 128:
            self.uint((value & 127) | 128, 1)
            value >>= 7
        self.uint(value, 1)

    def string(self, value):
        # Check before encoding/allocation. MUTF-8 ASCII bit, length in UTF16.
        if len(value) > self.limit - self.pos or not value.isascii() or '\0' in value:
            raise BuildError(BuildErrorKind.STRING)
        self.leb(len(value) * 2 + 1)
        self.put(value.encode('ascii'))
        self.uint(0, 1)


def code_layout(module, limit):
    # Prepay the sparse label table and instruction positions before allocation.
    if not 0 <= module.empty_literals <= U32_MAX:
        raise BuildError(BuildErrorKind.INTEGER)
    charge = module.empty_literals * 256 + len(module.instructions) * 256 + len(module.catches) * 256 + len(module.pool_strings) * 256
    if charge > limit:
        raise BuildError(BuildErrorKind.BYTES)
    labels, pc = {}, 0
    for item in module.instructions:
        if isinstance(item, Label):
            if not 0 <= item.value <= U32_MAX:
                raise BuildError(BuildErrorKind.INTEGER)
            if item in labels:
                raise BuildError(BuildErrorKind.LAYOUT)
            labels[item] = pc
        elif isinstance(item, Instruction):
            if not 0 <= item.opcode <= 65535 or item.delta_width not in (0, 1, 2, 4) or item.opcode_width not in (1, 2):
                raise BuildError(BuildErrorKind.INSTRUCTION)
            if item.target is not None and (not isinstance(item.target, Label) or not 0 <= item.target.value <= U32_MAX):
                raise BuildError(BuildErrorKind.INTEGER)
            if (item.target is None) != (item.delta_width == 0):
                raise BuildError(BuildErrorKind.BRANCH)
            size = item.opcode_width + len(item.operands) + item.delta_width
            if size > U32_MAX - pc:
                raise BuildError(BuildErrorKind.INTEGER)
            pc += size
        else:
            raise BuildError(BuildErrorKind.INSTRUCTION)
    for item in module.instructions:
        if isinstance(item, Instruction) and item.target is not None:
            if item.target not in labels or labels[item.target] >= pc:
                raise BuildError(BuildErrorKind.BRANCH)
    return labels, pc, charge


def emit_code(w, module, labels):
    pc = 0
    for item in module.instructions:
        if isinstance(item, Label):
            continue
        width = item.opcode_width
        w.uint(item.opcode, width)
        w.put(item.operands)
        if item.target is not None:
            if item.target not in labels:
                raise BuildError(BuildErrorKind.BRANCH)
            delta = labels[item.target] - pc
            bound = 1 << (item.delta_width * 8 - 1)
            if not -bound <= delta < bound:
                raise BuildError(BuildErrorKind.BRANCH)
            w.put(delta.to_bytes(item.delta_width, 'little', signed=True))
        pc += width + len(item.operands) + item.delta_width


def layout(w, module, labels, code_size, functions):
    w.mark(Entity.HEADER)
    w.put(b'PANDA\0\0\0')
    w.uint(0, 4)  # checksum calculated after all bytes exist
    w.put(bytes(module.profile.value))
    w.ref(Entity.END)
    w.uint(0, 4); w.uint(0, 4)  # no foreign region
    w.uint(1, 4); w.ref(Entity.CLASS_INDEX)
    w.uint(0, 4); w.uint(0, 4)  # no debug programs
    if module.profile.literal_arrays_in_header:
        w.uint(1 + module.empty_literals, 4); w.ref(Entity.LITERAL_INDEX)
    else:
        w.uint(U32_MAX, 4); w.uint(U32_MAX, 4)
    w.uint(1, 4); w.ref(Entity.REGION)
    if w.pos != HEADER_BYTES:
        raise BuildError(BuildErrorKind.LAYOUT)
    w.mark(Entity.CLASS_INDEX); w.ref(Entity.RECORD)
    w.mark(Entity.REGION)
    w.uint(HEADER_BYTES, 4); w.ref(Entity.END)
    w.uint(2, 4); w.ref(Entity.CLASS_TYPES)
    w.uint(1 + len(module.functions) + len(module.pool_strings) + module.empty_literals, 4); w.ref(Entity.METHOD_INDEX)
    w.uint(0, 4); w.uint(0, 4); w.uint(0, 4); w.uint(0, 4)
    w.mark(Entity.CLASS_TYPES); w.ref(Entity.RECORD); w.uint(6, 4)  # U32 field type
    w.mark(Entity.METHOD_INDEX); w.ref(Entity.METHOD)
    for i in range(len(module.functions)):
        w.ref((Entity.METHOD, i))
    for i in range(len(module.pool_strings)):
        w.ref((Entity.METHOD_INDEX, i))
    for i in range(module.empty_literals):
        w.ref((Entity.LITERAL_INDEX, i))
    w.mark(Entity.LITERAL_INDEX); w.ref(Entity.MODULE)
    for i in range(module.empty_literals):
        w.ref((Entity.LITERAL_INDEX, i))
    for i, value in enumerate(module.pool_strings):
        w.mark((Entity.METHOD_INDEX, i)); w.string(value)
    w.mark(Entity.METHOD_NAME); w.string('func_main_0')
    w.mark(Entity.MODULE_NAME); w.string('moduleRecordIdx')
    for i, function in enumerate(module.functions):
        w.mark((Entity.METHOD_NAME, i)); w.string(function.name)
    for i, name in enumerate(module.exports):
        w.mark((Entity.MODULE_NAME, i)); w.string(name)
    w.mark(Entity.MODULE)
    for _ in range(4):
        w.uint(0, 4)  # encoded word, requests, imports, namespace imports
    w.uint(len(module.exports), 4)
    for i in range(len(module.exports)):
        w.ref((Entity.MODULE_NAME, i)); w.ref((Entity.MODULE_NAME, i))
    w.uint(0, 4); w.uint(0, 4)  # indirect and star exports
    for i in range(module.empty_literals):
        w.mark((Entity.LITERAL_INDEX, i)); w.uint(0, 4)
    w.mark(Entity.RECORD)
    # Descriptor is streamed: never allocate a concatenated uncharged name.
    if len(module.name) + 2 > w.limit - w.pos or not module.name.isascii() or '\0' in module.name:
        raise BuildError(BuildErrorKind.STRING)
    w.leb((len(module.name) + 2) * 2 + 1)
    w.put(b'L'); w.put(module.name.encode('ascii')); w.put(b';\0')
    w.uint(0, 4); w.leb(1); w.leb(1); w.leb(1 + len(module.functions))
    w.uint(2, 1); w.uint(ECMASCRIPT, 1); w.uint(0, 1)
    w.uint(0, 2); w.uint(1, 2); w.ref(Entity.MODULE_NAME); w.leb(PUBLIC_STATIC)
    w.uint(2, 1); w.ref(Entity.MODULE); w.uint(0, 1)
    w.mark(Entity.METHOD)
    w.uint(0, 2); w.uint(NO_PROTO, 2); w.ref(Entity.METHOD_NAME); w.leb(PUBLIC_STATIC)
    w.uint(1, 1); w.ref(Entity.CODE); w.uint(0, 1)
    for i in range(len(functions)):
        w.mark((Entity.METHOD, i))
        w.uint(0, 2); w.uint(NO_PROTO, 2); w.ref((Entity.METHOD_NAME, i)); w.leb(PUBLIC_STATIC | (module.functions[i].kind.value << METHOD_KIND_SHIFT))
        w.uint(1, 1); w.ref((Entity.CODE, i)); w.uint(0, 1)
    w.mark(Entity.CODE)
    w.leb(module.registers); w.leb(module.arguments); w.leb(code_size); w.leb(len(module.catches))
    emit_code(w, module, labels)
    for c in module.catches:
        try:
            start, end, handler, hend = (labels[x] for x in (c.start, c.end, c.handler, c.handler_end))
        except KeyError as e:
            raise BuildError(BuildErrorKind.CATCH) from e
        if not (0 <= start < end <= code_size and 0 <= handler < hend <= code_size):
            raise BuildError(BuildErrorKind.CATCH)
        w.leb(start); w.leb(end-start); w.leb(1)
        w.leb(0); w.leb(handler); w.leb(hend-handler)
    for i, (function, local_labels, local_size) in enumerate(functions):
        w.mark((Entity.CODE, i))
        w.leb(function.registers); w.leb(function.arguments); w.leb(local_size); w.leb(len(function.catches))
        emit_code(w, function, local_labels)
        for c in function.catches:
            try:
                start, end, handler, hend = (local_labels[x] for x in (c.start, c.end, c.handler, c.handler_end))
            except KeyError as e:
                raise BuildError(BuildErrorKind.CATCH) from e
            if not (0 <= start < end <= local_size and 0 <= handler < hend <= local_size):
                raise BuildError(BuildErrorKind.CATCH)
            w.leb(start); w.leb(end-start); w.leb(1)
            w.leb(0); w.leb(handler); w.leb(hend-handler)
    w.mark(Entity.END)


def _build(module, budget):
    if not isinstance(module.profile, Profile):
        raise BuildError(BuildErrorKind.PROFILE)
    if not 0 <= budget <= MAX_BUILDER_BYTES:
        raise BuildError(BuildErrorKind.BYTES)
    labels, size, scratch = code_layout(module, budget)
    # Account fixed entity table before constructing it. Output is charged twice
    # for bytearray + immutable returned bytes, plus label/index scratch.
    scratch += len(Entity) * 256 + len(module.exports) * 256
    if scratch > budget:
        raise BuildError(BuildErrorKind.BYTES)
    # Prepay per-function layouts and entity offsets before allocating them.
    if len(module.functions) > (budget - scratch) // FUNCTION_LAYOUT_BYTES:
        raise BuildError(BuildErrorKind.BYTES)
    scratch += len(module.functions) * FUNCTION_LAYOUT_BYTES
    functions = []
    for function in module.functions:
        if not isinstance(function.kind, FunctionKind):
            raise BuildError(BuildErrorKind.INTEGER)
        local_labels, local_size, local_scratch = code_layout(function, budget - scratch)
        scratch += local_scratch
        if scratch > budget:
            raise BuildError(BuildErrorKind.BYTES)
        functions.append((function, local_labels, local_size))
    offsets = {}
    counter = Writer((budget - scratch) // 2, offsets)
    layout(counter, module, labels, size, functions)
    output = bytearray(counter.pos)
    writer = Writer(counter.pos, offsets, output)
    layout(writer, module, labels, size, functions)
    checksum = zlib.adler32(memoryview(output)[12:]) & U32_MAX
    struct.pack_into('<I', output, 8, checksum)
    return bytes(output), {e.name: o for e, o in offsets.items() if isinstance(e, Entity)}


def build(module, budget=1024 * 1024):
    try:
        return _build(module, budget)
    except MemoryError as error:
        raise BuildError(BuildErrorKind.ALLOCATION) from error
