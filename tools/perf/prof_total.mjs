// node prof_total.mjs <cpuprofile> <symbols> [top]: inclusive time per named function (each function once per sample).
import { readFileSync } from 'node:fs';
const [file, symPath, topN = '40'] = process.argv.slice(2);
const names = new Map();
for (const line of readFileSync(symPath, 'utf8').split('\n')) { const i = line.indexOf(':'); if (i > 0) names.set(line.slice(0, i), line.slice(i + 1)); }
const p = JSON.parse(readFileSync(file, 'utf8'));
const byId = new Map(p.nodes.map((n) => [n.id, n])); const parent = new Map();
for (const n of p.nodes) for (const c of n.children || []) parent.set(c, n.id);
const nm = (n) => { let s = n.callFrame.functionName || '(anon)'; const m = s.match(/wasm-function\[(\d+)\]/); return m ? names.get(m[1]) || s : s; };
const waits = /futex_wait|timedwait|nanosleep|^\(idle\)|^\(program\)/;
const counts = new Map(); for (const s of p.samples) counts.set(s, (counts.get(s) || 0) + 1);
const tot = new Map(); let work = 0;
for (const [id, c] of counts) {
  if (waits.test(nm(byId.get(id)))) continue; work += c; const seen = new Set();
  for (let x = id; x !== undefined; x = parent.get(x)) { const k = nm(byId.get(x)); if (!seen.has(k)) { seen.add(k); tot.set(k, (tot.get(k) || 0) + c); } }
}
console.log(`work samples ${work}`);
for (const [n, c] of [...tot].sort((a, b) => b[1] - a[1]).slice(0, +topN)) console.log(`${(100 * c / work).toFixed(1).padStart(6)}%  ${n.slice(0, 130)}`);
