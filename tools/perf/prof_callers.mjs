// node prof_callers.mjs <cpuprofile> <symbols> <name substring>: the stacks (callers chain) leading to that function, by samples.
import { readFileSync } from 'node:fs';
const [file, symPath, want] = process.argv.slice(2);
const names = new Map();
for (const line of readFileSync(symPath, 'utf8').split('\n')) { const i = line.indexOf(':'); if (i > 0) names.set(line.slice(0, i), line.slice(i + 1)); }
const p = JSON.parse(readFileSync(file, 'utf8'));
const byId = new Map(p.nodes.map((n) => [n.id, n])); const parent = new Map();
for (const n of p.nodes) for (const c of n.children || []) parent.set(c, n.id);
const nm = (n) => { let s = n.callFrame.functionName || '(anon)'; const m = s.match(/wasm-function\[(\d+)\]/); return m ? names.get(m[1]) || s : s; };
const counts = new Map(); for (const s of p.samples) counts.set(s, (counts.get(s) || 0) + 1);
const stacks = new Map();
for (const [id, c] of counts) {
  const chain = []; for (let x = id; x !== undefined; x = parent.get(x)) chain.push(nm(byId.get(x)));
  const at = chain.findIndex((s) => s.includes(want)); if (at < 0) continue;
  const key = chain.slice(at, at + 8).map((s) => s.slice(0, 60)).join(' <- ') + ' | self: ' + chain[0].slice(0, 40);
  stacks.set(key, (stacks.get(key) || 0) + c);
}
for (const [k, c] of [...stacks].sort((a, b) => b[1] - a[1]).slice(0, 12)) console.log(c, k);
