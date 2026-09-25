; The generated Eshkol function takes and returns this LLVM aggregate directly.
; A C/C++ by-value struct declaration uses a different platform ABI, so pass
; the same 16 bytes through pointers at the native test boundary.
%eshkol_tagged_value = type { i8, i8, i16, i32, i64 }

declare %eshkol_tagged_value @f32_conjugate_probe(%eshkol_tagged_value)

define void @f32_conjugate_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_conjugate_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}
