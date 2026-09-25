const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

for (const [path, className] of [
    ['web/eshkol-repl.js', 'EshkolRepl'],
    ['site/static/eshkol-runtime.js', 'EshkolRuntime'],
]) {
    const source = fs.readFileSync(path, 'utf8') + `\nthis.__TestClass = ${className};`;
    const context = {
        console, WebAssembly, TextEncoder, TextDecoder, Map, Set, Uint8Array,
        DataView, document: { body: {} }, window: {},
    };
    vm.runInNewContext(source, context, { filename: path });
    const runtime = new context.__TestClass();
    const env = runtime.createImports().env;
    const bytes = new Uint8Array(env.__linear_memory.buffer);
    const out = 4096, value = 8192;
    bytes.fill(0x5a, out, out + 16);
    for (let i = 0; i < 16; i++) bytes[value + i] = i + 1;

    assert.equal(env.eshkol_region_write_barrier_checked_v1(out, 0, value), 0, path);
    assert.deepEqual(Array.from(bytes.slice(out, out + 16)),
                     Array.from(bytes.slice(value, value + 16)), path);
    bytes.fill(0x5a, out, out + 16);
    assert.equal(env.eshkol_region_write_barrier_checked_v1(out, 0, 0), 4, path);
    assert.equal(env.eshkol_region_write_barrier_checked_v1(0, 0, value), 4, path);
    assert.equal(env.eshkol_region_write_barrier_checked_v1(out, 0, bytes.length - 8), 4, path);
    assert.deepEqual(Array.from(bytes.slice(out, out + 16)), Array(16).fill(0x5a), path);

    for (const name of [
        'eshkol_format_float32_bits',
        'eshkol_runtime_emergency_raise_v1',
        'eshkol_runtime_emergency_rethrow_if_v1',
        'eshkol_type_of_ref_v1_store',
    ]) {
        assert.throws(() => env[name](1, 2, 3), /unsupported in WASM/, `${path}: ${name}`);
    }
    console.log(`${path}: F32 imports PASS`);
}
