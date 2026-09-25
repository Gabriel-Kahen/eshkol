; Bridge each Eshkol aggregate ABI to a pointer-based C ABI.
%eshkol_tagged_value = type { i8, i8, i16, i32, i64 }

declare %eshkol_tagged_value @f32_unary_plus_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_multiply_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_divide_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_min_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_max_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_numerator_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_denominator_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_unary_numerator_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_modulo_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_remainder_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_quotient_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_modulo_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_remainder_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_quotient_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_gcd_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_lcm_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_gcd_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_lcm_stored_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_gcd_stored_tail_probe(%eshkol_tagged_value)
declare %eshkol_tagged_value @f32_integer_lcm_stored_tail_probe(%eshkol_tagged_value)

define void @f32_unary_plus_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_plus_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_multiply_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_multiply_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_divide_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_divide_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_min_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_min_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_max_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_max_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_numerator_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_numerator_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_denominator_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_denominator_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_unary_numerator_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_unary_numerator_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_modulo_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_modulo_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_remainder_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_remainder_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_quotient_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_quotient_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_modulo_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_modulo_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_remainder_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_remainder_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_quotient_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_quotient_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_gcd_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_gcd_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_lcm_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_lcm_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_gcd_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_gcd_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_lcm_stored_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_lcm_stored_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_gcd_stored_tail_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_gcd_stored_tail_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}

define void @f32_integer_lcm_stored_tail_probe_bridge(ptr %out, ptr %in) {
entry:
  %value = load %eshkol_tagged_value, ptr %in, align 8
  %result = call %eshkol_tagged_value @f32_integer_lcm_stored_tail_probe(%eshkol_tagged_value %value)
  store %eshkol_tagged_value %result, ptr %out, align 8
  ret void
}
