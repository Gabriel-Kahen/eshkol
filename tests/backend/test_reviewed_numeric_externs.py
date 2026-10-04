"""Check the closed numeric-extern roster against native source signatures."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
ROSTER = ROOT / "lib/backend/reviewed_numeric_externs.inc"
ROW = re.compile(r'\s*\{"([A-Za-z_][A-Za-z_0-9]*)", "([^"]*)"\},')
EXTERN = re.compile(r"\(extern\s+(?:i64|long)\s+([^\s()]+)([^()]*)\)")


def read_roster(text):
    rows = {}
    for line in text.splitlines():
        if line.lstrip().startswith("//"):
            continue
        match = ROW.fullmatch(line)
        if not match:
            raise AssertionError(f"malformed roster row: {line!r}")
        name, signature = match.groups()
        if name in rows:
            raise AssertionError(f"duplicate native symbol: {name}")
        if any(part not in {"ptr", "i64", "i32", "f32", "f64", "double"}
               for part in signature.split(",") if part):
            raise AssertionError(f"unknown ABI in {name}")
        rows[name] = signature
    return rows


def native_type(declaration):
    declaration = declaration.strip()
    if declaration in {"", "void"}:
        return ""
    if "*" in declaration or "[" in declaration:
        return "ptr"
    if re.search(r"\b(?:int64_t|uint64_t|size_t|ssize_t)\b", declaration):
        return "i64"
    if re.search(r"\b(?:int32_t|uint32_t|int|unsigned)\b", declaration):
        return "i32"
    if re.search(r"\bdouble\b", declaration):
        return "double"
    if re.search(r"\bfloat\b", declaration):
        return "f32"
    return "unknown"


def main():
    text = ROSTER.read_text()
    rows = read_roster(text)
    assert len(rows) == 80, "roster cardinality changed without a source review"
    for broken in ('    {"strlen", "ptr"},\n    {"strlen", "ptr"},',
                   '    {"new_pointer", "..."},'):
        try:
            read_roster(broken)
        except AssertionError:
            pass
        else:
            raise AssertionError("malformed or duplicate roster was admitted")

    # These two libc functions have size_t returns; codegen admits them only
    # when the target's size_t width is 64. The source ABI is the C library.
    assert rows["strlen"] == "ptr"
    assert rows["fwrite"] == "ptr,i64,i64,ptr"
    jit = (ROOT / "lib/repl/repl_jit.cpp").read_text()
    assert re.search(r"int64_t\s+eshkol_ws_wrap_fd\s*\(int64_t\s+fd\)", jit)
    assert not re.search(r"eshkol_ws_wrap_fd\s*\(int32_t", jit)

    declarations = 0
    for tree in (ROOT / "lib", ROOT / "tests"):
        for source in tree.rglob("*.esk"):
            for match in EXTERN.finditer(source.read_text(errors="replace")):
                local, rest = match.groups()
                real = re.search(r":real\s+([^\s()]+)", rest)
                name = real.group(1) if real else local
                signature = ",".join(rest.split(":real")[0].split()).replace("f64", "double")
                assert rows.get(name) == signature, (source, name, signature)
                declarations += 1
    assert declarations == 85, "tracked numeric extern set changed"

    native_sources = [p.read_text(errors="replace") for tree in (ROOT / "lib", ROOT / "tests")
                      for p in tree.rglob("*") if p.suffix in {".c", ".cc", ".cpp", ".m"}]
    native_text = "\n".join(native_sources)
    for name, signature in rows.items():
        if name in {"strlen", "fwrite"}:
            continue
        if name in {"eshkol_tc_known_capability_mask",
                    "eshkol_tc_available_capability_mask",
                    "eshkol_tc_compiled_backend_mask",
                    "eshkol_tc_available_backend_mask"}:
            assert re.search(r"ESHKOL_TC_CAPABILITY_MASK_FUNCTION\(\s*" + name + r"\s*,",
                             native_text), name
            assert signature == "ptr", name
            continue
        pattern = re.compile(r"\b(?:int64_t|uint64_t|size_t)\s+" + re.escape(name) +
                             r"\s*\(([^()]*)\)\s*\{")
        expected = signature.replace("f64", "double")
        assert any(",".join(native_type(arg) for arg in m.group(1).split(",")) == expected
                   for m in pattern.finditer(native_text)), (name, signature)
    print(f"PASS: {len(rows)} reviewed numeric symbols; {declarations} tracked declarations")


if __name__ == "__main__":
    main()
