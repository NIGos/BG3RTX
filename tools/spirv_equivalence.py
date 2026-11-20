"""Shared specialization, optimization and structural comparison for offline SPIR-V gates.

Equality proves identical optimized instructions up to numeric IDs, global declaration
order, control-flow block layout and simultaneous phi declaration order. Inequality is
conservative: it does not imply the two programs compute different values.
"""
import hashlib
from itertools import zip_longest
from pathlib import Path
import re
import subprocess
import struct
import tempfile

RX = re.compile(r"%\d+")
DEF = re.compile(r"\s*(%\d+) = (Op\w+)(.*)$")


# Windows NTSTATUS exit codes of a crashed process (Python reports them unsigned): a crash prints nothing.
CRASH_CODES = {0xC0000005: "access violation", 0xC00000FD: "stack overflow", 0xC0000409: "fail-fast / stack buffer overrun",
               0xC0000017: "out of memory", 0xC000001D: "illegal instruction", 0xC0000094: "integer divide by zero",
               0xC0000374: "heap corruption"}


class OptError(RuntimeError):
    """spirv-opt failed: the passes, the exit code (named when it is a crash), its output, and the stage of
    `specialize` that ran it. A crash has no output; the code says which."""

    def __init__(self, passes, returncode, output, stage=""):
        self.passes, self.returncode, self.output, self.stage = tuple(passes), returncode, output.strip(), stage
        code = returncode & 0xFFFFFFFF
        self.crashed = code >= 0xC0000000 or returncode < 0
        what = f"exit {returncode}" + (f" (0x{code:08X} {CRASH_CODES.get(code, 'crash')})" if self.crashed else "")
        super().__init__(f"spirv-opt{' ' + stage if stage else ''} [{' '.join(self.passes)}]: {what}: "
                         f"{self.output or '(no output)'}")


def opt(tool, src, dst, *passes, stage=""):
    r = subprocess.run([str(tool.dxc.with_name("spirv-opt.exe")), *passes, str(src), "-o", str(dst)],
                       capture_output=True, text=True)
    if r.returncode:
        raise OptError(passes, r.returncode, f"{r.stdout}{r.stderr}", stage)
    return dst


LABEL = re.compile(r"\s*(%\d+) = OpLabel")


def block_order(body):
    """The function bodies with each function's blocks in the order they are first referenced from the entry
    block (breadth first over the instruction text), not DXC's layout order: two layouts of the same control flow
    (seen: an if/else whose arms DXC emitted in the other order in rt_shadow) then compare equal."""
    out, fn = [], []

    def flush():
        if not fn:
            return
        head, blocks, cur = [], {}, None
        order = []
        for l in fn:
            m = LABEL.match(l)
            if m:
                cur = m.group(1)
                blocks[cur] = [l]
                order.append(cur)
            elif cur is None:
                head.append(l)
            else:
                blocks[cur].append(l)
        tail = []
        if order and blocks[order[-1]] and "OpFunctionEnd" in blocks[order[-1]][-1]:
            tail = [blocks[order[-1]].pop()]
        seen, queue = [], order[:1]
        while queue:
            b = queue.pop(0)
            if b in seen:
                continue
            seen.append(b)
            for l in blocks[b][1:]:
                # Phi predecessors are dataflow inputs, not successor/control references.
                if " OpPhi " in l:
                    continue
                for x in RX.findall(l):
                    if x in blocks and x not in seen and x not in queue:
                        queue.append(x)
        seen += [b for b in order if b not in seen]   # unreachable blocks keep their order
        out.extend(head + [l for b in seen for l in blocks[b]] + tail)
        fn.clear()
    for l in body:
        fn.append(l)
        if "OpFunctionEnd" in l:
            flush()
    flush()
    return out


def phi_order(body, global_names):
    """Order parallel phi declarations and their intact predecessor/value pairs.

    SPIR-V phis at the start of a block execute simultaneously. DXC's SROA can
    emit their types in a different order after specialization. Like global
    declaration order, that order is not dataflow. Equal-type phis retain their
    order; no operation, operand, incoming edge or result ID is discarded.
    Incoming pairs follow the control-flow block order, never raw ID numbering.
    """
    out, phis = [], []
    labels = {m.group(1): i for i, line in enumerate(body) if (m := LABEL.match(line))}

    def flush():
        out.extend(sorted(phis, key=lambda line: global_names.get(DEF.match(line).group(3).split()[0],
                                                            DEF.match(line).group(3).split()[0])))
        phis.clear()
    for line in body:
        match = DEF.match(line)
        if match and match.group(2) == "OpPhi":
            args = match.group(3).split()
            # A phi chooses a value by predecessor identity. Permute complete pairs,
            # never values or predecessor edges independently.
            pairs = list(zip(args[1::2], args[2::2]))
            assert len(args) == 1 + 2 * len(pairs), "malformed phi"
            pairs.sort(key=lambda pair: labels.get(pair[1], len(body)))
            line = line[:match.start(3)] + " " + " ".join([args[0], *[x for pair in pairs for x in pair]])
            phis.append(line)
        else:
            flush()
            out.append(line)
    flush()
    return out


def canonical(tool, spv):
    """(sorted global lines, body lines) of a module up to id numbering and declaration order: globals
    (types, constants, undefs, variables) are named by their hash-consed definition, function-body ids by
    order of appearance; debug names dropped."""
    t = subprocess.run([str(tool.dxc.with_name("spirv-dis.exe")), "--raw-id", "--no-header", str(spv)],
                       capture_output=True, text=True, check=True).stdout
    return canonical_text(t)


def canonical_text(t):
    """Canonicalize raw-id disassembly; instruction operands and multiplicity are preserved."""
    lines = [l for l in t.splitlines() if l.strip() and "OpName" not in l and "OpMemberName" not in l]
    first = next(i for i, l in enumerate(lines) if " OpFunction " in l)
    glob, body = lines[:first], lines[first:]
    defs = {m.group(1): (m.group(2), m.group(3)) for m in map(DEF.match, glob) if m}
    # Decorations are semantic identity: equal pointer declarations can bind different
    # descriptors, and equal structure declarations can carry different memory layouts.
    decorations = {}
    for line in glob:
        match = re.match(r"\s*(OpDecorate(?:Id)?|OpMemberDecorate) (%\d+)(.*)$", line)
        if match:
            decorations.setdefault(match.group(2), []).append(match.group(1) + match.group(3))
    body = block_order(body)
    variables = {x for x, (op, _) in defs.items() if op == "OpVariable"}
    # Mutable objects must never be hash-consed, even when undecorated. Number by
    # first executable use, then declaration order for unused objects (conservative).
    variable_order = {}
    for line in body + glob:
        for x in RX.findall(line):
            if x in variables:
                variable_order.setdefault(x, len(variable_order))
    canon = {}

    def name(x, depth=0):
        if x not in defs or depth > 64:
            return x
        if x not in canon:
            canon[x] = "rec"
            op, rest = defs[x]
            annotations = "|".join(sorted(decorations.get(x, [])))
            identity = f"|object:{variable_order[x]}" if x in variables else ""
            signature = op + rest + "|" + annotations
            signature = RX.sub(lambda m: name(m.group(0), depth + 1), signature)
            canon[x] = "g" + hashlib.sha256((signature + identity).encode()).hexdigest()
        return canon[x]
    for x in defs:
        name(x)
    body = phi_order(body, canon)
    ren = {}
    b = [RX.sub(lambda m: canon.get(m.group(0)) or ren.setdefault(m.group(0), f"%v{len(ren)}"), l).strip() for l in body]
    g = sorted(RX.sub(lambda m: canon.get(m.group(0)) or ren.get(m.group(0), m.group(0)), l).strip() for l in glob)
    return g, b


def differ(a, b):
    (ga, ba), (gb, bb) = a, b
    return sum(x != y for x, y in zip_longest(ga, gb)) + sum(x != y for x, y in zip_longest(ba, bb))


def first_difference(a, b):
    """First changed canonical instruction, including insertions/deletions (empty iff equal)."""
    for section, left, right in zip(("global", "body"), a, b):
        for i, (x, y) in enumerate(zip_longest(left, right, fillvalue="<absent>"), 1):
            if x != y:
                return f"{section} instruction {i}: baseline {x!r}; current {y!r}"
    return ""


def freeze_except(src, dst, preserve):
    """SPIRV-Tools freeze-spec-const semantics, excluding symbolic SpecId inputs.

    Freeze only scalar OpSpecConstantTrue/False/Constant, as the upstream pass
    does. Derived OpSpecConstantOp/Composite stay for the fold pass, which can
    fold only those whose inputs are now ordinary constants. Symbolic values
    retain both their scalar instruction and their SpecId decoration.
    """
    data = Path(src).read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise ValueError("malformed SPIR-V module")
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError("invalid SPIR-V magic")
    instructions, symbolic, i = [], set(), 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 65535
        if not count or i + count > len(words):
            raise ValueError("malformed SPIR-V instruction length")
        instruction = list(words[i:i + count])
        instructions.append(instruction)
        if op == 71 and count == 4 and instruction[2] == 1 and instruction[3] in preserve:
            symbolic.add(instruction[1])
        i += count
    result = list(words[:5])
    for instruction in instructions:
        op = instruction[0] & 65535
        if op == 71 and len(instruction) == 4 and instruction[2] == 1 and instruction[1] not in symbolic:
            continue
        if op in (48, 49, 50) and instruction[2] not in symbolic:
            instruction[0] = (instruction[0] & 0xffff0000) | (op - 7)
        result.extend(instruction)
    Path(dst).write_bytes(struct.pack("<" + "I" * len(result), *result))
    return dst


OPTIMIZE = ("-O", "--remove-unused-interface-variables", "--trim-capabilities")
# The schedules a caller may pick. Any schedule is sound for equality when BOTH sides run the same one (every pass
# preserves semantics; equal canonical output then proves equal programs); a shorter one only finds fewer equalities.
# "single": one optimizer round instead of two, for a module on which the second round crashes spirv-opt.
SCHEDULES = {"full": 2, "single": 1}


def specialize(tool, src, dst, assignments, preserve=(), schedule="full"):
    """Set release values, freeze constants except symbolic ``preserve`` IDs, fold and optimize.

    Call on both sides with the same complete assignment and the same ``schedule``. Unused interface variables
    and capabilities describe no live operations after folding and are trimmed equally.
    Each invocation owns its intermediates, including when called concurrently. A failing spirv-opt raises
    OptError with its stage ("assign", "freeze", "optimize 1", "optimize 2").
    """
    rounds = SCHEDULES[schedule]
    preserve = set(preserve)
    if preserve & assignments.keys():
        raise ValueError("cannot assign a preserved symbolic specialization constant")
    dst = Path(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="spv-equiv-", dir=dst.parent) as tmp:
        tmp = Path(tmp)
        assigned = src
        if assignments:
            assigned = opt(tool, src, tmp / "assigned.spv",
                           "--set-spec-const-default-value=" + " ".join(
                               f"{k}:{v}" for k, v in sorted(assignments.items())), stage="assign")
        if preserve:
            frozen = freeze_except(assigned, tmp / "selective.spv", preserve)
            frozen = opt(tool, frozen, tmp / "frozen.spv", "--fold-spec-const-op-composite", stage="freeze")
        else:
            frozen = opt(tool, assigned, tmp / "frozen.spv", "--freeze-spec-const", "--fold-spec-const-op-composite",
                         stage="freeze")
        # Specialization can expose constant branches only after the first optimizer
        # schedule has visited CFG cleanup. Repeat the same semantics-preserving
        # schedule on both sides so those dead selections do not cause false failures.
        current = frozen
        for n in range(1, rounds + 1):
            current = opt(tool, current, dst if n == rounds else tmp / f"optimized{n}.spv", *OPTIMIZE,
                          stage=f"optimize {n}")
        return current
