# Object-header lexical ratchet rebaseline at `07f1579d`

The previous baseline described `11cf759a`, before the composed F32 and upstream
runtime changes. The lexical scan at `07f1579d` has 896 non-public layout sites
in 109 files. The public-ABI view (`I_public_abi`, 87 sites) is derived from the
source classes and is not separately baselined. The eight exact decreases are
explained in `.icc/abi-header-ratchet-allowlist.json`; no wildcard is allowed.

| Class | Old → new | Source review |
| --- | ---: | --- |
| A macro API | 113 → 141 | `type-of` moved from introspection to symbol interning; tagged KB scanning, region promotion, and F32/emergency/promotion/type-symbol tests add uses. |
| B header type | 73 → 71 | VM fact classification now uses `VM_TERM_KIND_FACT`; promotion drops a redundant destination-header read. Emergency exception and ABI tests add typed uses. |
| C header size | 70 → 75 | VM fact header reconstruction was removed. Promotion extent checks, the emergency exception layout, and allocation/promotion/ABI tests add sites. |
| D2 IR field offset | 10 → 11 | `read-string` now emits the `-4` GEP to set `header.size` to the actual byte count. |
| E allocation with header | 185 → 215 | A duplicate declaration and a duplicated closure fixture call were removed; checked barrier, constructor emergency, F32, and promotion tests add allocation sites. |
| G header field | 168 → 171 | VM fact header reads were removed; `type-of` reads moved to symbol interning. Tagged KB scanning and guarded promotion add reads. |
| L layout prose | 86 → 87 | The LLVM codegen header gained a byte-layout comment for the four header fields and payload. |

D IR constant offsets, F parallel header declarations, H WASM glue, J secondary
prefix ABI, and K raw byte offsets have unchanged totals. Every changed
file/class count was compared against the source; the allowlist records only
decreases, while the new per-file baseline records the complete current scan.
The pinned header contract remains eight bytes with `u8/u8/u16/u32` fields.

The existing S1/S2/S3 semantic counts (2/6/2) are preserved as historical
baseline data. The pinned LLVM 21 Docker image lacks Python libclang bindings
matched to its compiler, so this rebaseline makes no claim that the semantic
ratchet passes at `07f1579d`. The existing combined inventory snapshot is not
rewritten by a lexical-only scan, which cannot reproduce its semantic section.
