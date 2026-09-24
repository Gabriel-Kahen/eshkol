#!/usr/bin/env python3
"""Verify condition-5 success dominance for every admitted arena allocator."""

import re
import subprocess
import sys

NAME = r"[-a-zA-Z$._0-9]+"
FAMILIES = (
    "arena_allocate",
    "arena_allocate_ad_node_with_header",
    "arena_allocate_closure_with_header",
    "arena_allocate_cons_with_header",
    "arena_allocate_string_with_header",
    "arena_allocate_tensor_with_header",
    "arena_allocate_vector_with_header",
    "arena_allocate_with_header",
)
ALLOCATORS = "(?:" + "|".join(re.escape(name) for name in FAMILIES) + ")"
CALL = re.compile(
    r"(%" + NAME + r") = (?:tail )?call ptr @(" + ALLOCATORS + r")\([^\n]*"
)


def blocks_for(function):
    blocks = {}
    name = "entry"
    for line in function.splitlines()[1:-1]:
        label = re.match(r"^(" + NAME + r"):", line)
        if label:
            name = label[1]
            blocks.setdefault(name, [])
            continue
        blocks.setdefault(name, []).append(line)
    return blocks


def dominators(blocks):
    predecessors = {name: set() for name in blocks}
    for name, lines in blocks.items():
        for target in re.findall(r"label %(" + NAME + r")", "\n".join(lines)):
            if target in blocks:
                predecessors[target].add(name)
    entry = next(iter(blocks))
    dom = {name: ({entry} if name == entry else set(blocks)) for name in blocks}
    changed = True
    while changed:
        changed = False
        for name in blocks:
            if name == entry:
                continue
            parents = predecessors[name]
            new = {name} | (
                set.intersection(*(dom[parent] for parent in parents))
                if parents
                else set()
            )
            if new != dom[name]:
                dom[name] = new
                changed = True
    return dom


def verify(text, require_all=True):
    counts = {name: 0 for name in FAMILIES}
    for function in re.findall(r"^define .*?^}", text, re.M | re.S):
        blocks = blocks_for(function)
        dom = dominators(blocks)
        closure_publications = []
        for block_name, lines in blocks.items():
            body = "\n".join(lines)
            for call in CALL.finditer(body):
                pointer, family = call.groups()
                counts[family] += 1

                tail = body[call.end():]
                comparison_pattern = re.compile(
                    r"(%" + NAME + r") = icmp (eq|ne) ptr "
                    + re.escape(pointer) + r", null"
                )
                pointer_comparisons = comparison_pattern.findall(function)
                if family in {
                    "arena_allocate_cons_with_header",
                    "arena_allocate_vector_with_header",
                }:
                    assert len(pointer_comparisons) == 1, (
                        f"pre-guarded {family} gained a duplicate null comparison "
                        f"for {pointer}: found {len(pointer_comparisons)}"
                    )
                comparison = comparison_pattern.search(tail)
                assert comparison, (
                    f"missing immediate null check in {block_name}: {call[0]}"
                )
                between = tail[:comparison.start()]
                assert not any(
                    line.strip() and not line.lstrip().startswith(";")
                    for line in between.splitlines()
                ), f"work occurs between allocator call and null comparison in {block_name}"
                branch = re.search(
                    r"br i1 " + re.escape(comparison[1])
                    + r", label %(" + NAME + r"), label %(" + NAME + r")",
                    tail[comparison.end():],
                )
                assert branch, f"missing null-check branch in {block_name}"
                between_compare_and_branch = tail[
                    comparison.end():comparison.end() + branch.start()
                ]
                assert not any(
                    line.strip() and not line.lstrip().startswith(";")
                    for line in between_compare_and_branch.splitlines()
                ), f"work occurs between null comparison and branch in {block_name}"
                true_block, false_block = branch.groups()
                if comparison[2] == "ne":
                    success, failure = true_block, false_block
                else:
                    failure, success = true_block, false_block
                if family == "arena_allocate_closure_with_header":
                    closure_args = re.search(
                        r"@arena_allocate_closure_with_header\(ptr [^,]+, "
                        r"i(?:32|64) [^,]+, i(?:32|64) [^,]+, "
                        r"i(?:32|64) ([^,]+),",
                        call[0],
                    )
                    assert closure_args, f"could not parse closure call: {call[0]}"
                    closure_publications.append((closure_args[1].strip(), success))

                failure_body = "\n".join(blocks[failure])
                emergency = re.findall(
                    r"call void @eshkol_runtime_emergency_raise_v1\(i32(?: [a-z]+)* 5\)",
                    failure_body,
                )
                assert len(emergency) == 1, (
                    f"failure block {failure} has {len(emergency)} condition-5 calls"
                )
                instructions = [
                    line.strip() for line in blocks[failure]
                    if line.strip() and not line.lstrip().startswith(";")
                ]
                assert len(instructions) == 2, (
                    f"failure block {failure} contains non-emergency work: {instructions}"
                )
                assert "@eshkol_runtime_emergency_raise_v1" in instructions[0]
                assert instructions[1] == "unreachable", (
                    f"failure block {failure} does not terminate immediately unreachable"
                )

                token = re.compile(re.escape(pointer) + r"(?![-a-zA-Z$._0-9])")
                for user_block, user_lines in blocks.items():
                    for line in user_lines:
                        if not token.search(line) or line.strip() == call[0].strip():
                            continue
                        if re.search(
                            r"icmp (?:eq|ne) ptr " + re.escape(pointer) + r", null",
                            line,
                        ):
                            continue
                        if " phi " in line:
                            incoming = re.search(
                                r"\[\s*" + re.escape(pointer)
                                + r",\s*%(" + NAME + r")\s*\]",
                                line,
                            )
                            assert incoming and success in dom[incoming[1]], (
                                f"{family} phi incoming edge bypasses success: {line}"
                            )
                            continue
                        assert success in dom[user_block], (
                            f"{family} result used outside success dominance in {user_block}: {line}"
                        )

        # Lambda display state is externally visible publication even though
        # it does not use the closure pointer. Associate the stored S-expression
        # SSA value with the closure call that received that same value; an
        # unrelated earlier closure success must not mask premature publication.
        for store_block, store_lines in blocks.items():
            for line in store_lines:
                store = re.search(
                    r"store i64 ([^,]+), ptr @[-a-zA-Z$._0-9]+_sexpr", line
                )
                if not store:
                    continue
                matching = [
                    success for value, success in closure_publications
                    if value == store[1].strip()
                ]
                if matching:
                    assert all(success in dom[store_block] for success in matching), (
                        f"S-expression global published before its closure success: {line}"
                    )

    total = sum(counts.values())
    assert total, "no admitted arena allocator calls found"
    if require_all:
        missing = [name for name, count in counts.items() if count == 0]
        assert not missing, "missing allocator families: " + ", ".join(missing)
    return counts


def must_reject(text, description):
    try:
        verify(text)
    except AssertionError:
        return
    raise AssertionError(f"{description} mutant accepted")


def main():
    if sys.argv[1].endswith(".bc"):
        if len(sys.argv) != 3:
            raise SystemExit("bitcode input requires an llvm-dis executable")
        disassembly = subprocess.run(
            [sys.argv[2], sys.argv[1], "-o", "-"],
            check=True,
            stdout=subprocess.PIPE,
            text=True,
        )
        text = disassembly.stdout
    else:
        text = open(sys.argv[1], encoding="utf-8").read()
    counts = verify(text)

    call = CALL.search(text)
    assert call
    following = text[call.end():]
    comparison = re.search(
        r"(%" + NAME + r") = icmp (?:eq|ne) ptr "
        + re.escape(call[1]) + r", null",
        following,
    )
    assert comparison
    branch = re.search(
        r"br i1 " + re.escape(comparison[1])
        + r", label %(" + NAME + r"), label %(" + NAME + r")",
        following[comparison.end():],
    )
    assert branch
    comparison_start = call.end() + comparison.start()
    branch_start = call.end() + comparison.end() + branch.start()
    branch_end = call.end() + comparison.end() + branch.end()
    must_reject(
        text[:branch_start] + "br label %" + branch[1] + text[branch_end:],
        "removed branch",
    )

    first_emergency = re.search(
        r"call void @eshkol_runtime_emergency_raise_v1\(i32(?: [a-z]+)* 5\)", text
    )
    assert first_emergency
    must_reject(
        text[:first_emergency.end()] + "\n  " + first_emergency[0]
        + text[first_emergency.end():],
        "duplicate emergency",
    )
    must_reject(
        text[:first_emergency.start()] + "call void @publish_partial()\n  "
        + text[first_emergency.start():],
        "failure-path side effect",
    )
    must_reject(
        text[:branch_start] + "call void @publish_partial()\n  "
        + text[branch_start:],
        "side effect between comparison and branch",
    )
    must_reject(
        text[:comparison_start] + "call void @publish_partial()\n  "
        + text[comparison_start:],
        "side effect between allocator call and comparison",
    )
    must_reject(
        text[:comparison_start] + "%duplicate_arena_check = icmp eq ptr "
        + call[1] + ", null\n  " + text[comparison_start:],
        "duplicate allocator null comparison",
    )
    must_reject(
        text[:first_emergency.start()] + first_emergency[0].replace(" 5)", " 4)")
        + text[first_emergency.end():],
        "wrong condition",
    )

    closure_call = re.search(
        r"%" + NAME
        + r" = (?:tail )?call ptr @arena_allocate_closure_with_header\([^\n]*",
        text,
    )
    sexpr_global = re.search(r"@(" + NAME + r"_sexpr)\s*=", text)
    assert closure_call and sexpr_global
    must_reject(
        text[:closure_call.end()]
        + "\n  store i64 0, ptr @" + sexpr_global[1]
        + text[closure_call.end():],
        "pre-success S-expression publication",
    )

    print("PASS " + ", ".join(f"{name}={counts[name]}" for name in FAMILIES))


if __name__ == "__main__":
    main()
