// node prof_js_under.mjs <cpuprofile> <symbols> <name substring>: the JS (WebGPU binding) functions called under it, by samples.
import { readFileSync } from 'node:fs';
const [file, symPath, want] = process.argv.slice(2);
const names = new Map();
for (const line of readFileSync(symPath, 'utf8').split('\n')) { const i = line.indexOf(':'); if (i > 0) names.set(line.slice(0, i), line.slice(i + 1)); }
const p = JSON.parse(readFileSync(file, 'utf8'));
const byId = new Map(p.nodes.map((n) => [n.id, n])); const parent = new Map();
for (const n of p.nodes) for (const c of n.children || []) parent.set(c, n.id);
const nm = (n) => { let s = n.callFrame.functionName || '(anon)'; const m = s.match(/wasm-function\[(\d+)\]/); return m ? names.get(m[1]) || s : s; };
const counts = new Map(); for (const s of p.samples) counts.set(s, (counts.get(s) || 0) + 1);
const out = new Map(); let all = 0;
for (const [id, c] of counts) {
  const chain = []; for (let x = id; x !== undefined; x = parent.get(x)) chain.push(nm(byId.get(x)));
  const at = chain.findIndex((s) => s.includes(want)); if (at < 0) continue; all += c;
  const js = chain.slice(0, at).reverse().find((s) => s.startsWith('_wgpu') || s.startsWith('_emwgpu')) || '(wasm)';
  out.set(js, (out.get(js) || 0) + c);
}
console.log('total', all); for (const [k, c] of [...out].sort((a, b) => b[1] - a[1]).slice(0, 15)) console.log(c, k);
