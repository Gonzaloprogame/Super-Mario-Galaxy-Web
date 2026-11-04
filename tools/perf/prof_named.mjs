// node prof_named.mjs <dump dir> <symbols> [top]: per profile, self time by named function, without the waits.
import { readFileSync, readdirSync } from 'node:fs';
const [dir, symPath, topN = '25'] = process.argv.slice(2);
const names = new Map();
for (const line of readFileSync(symPath, 'utf8').split('\n')) { const i = line.indexOf(':'); if (i > 0) names.set(line.slice(0, i), line.slice(i + 1)); }
const waits = /futex_wait|^\(idle\)|^\(program\)|^\(garbage|timedwait|nanosleep|readAsArrayBuffer/;
for (const f of readdirSync(dir).filter((f) => f.endsWith('.cpuprofile'))) {
  const p = JSON.parse(readFileSync(`${dir}/${f}`, 'utf8'));
  const byId = new Map(p.nodes.map((n) => [n.id, n]));
  const counts = new Map(); for (const s of p.samples) counts.set(s, (counts.get(s) || 0) + 1);
  const self = new Map(); let total = 0, work = 0;
  for (const [id, c] of counts) {
    const n = byId.get(id); let name = n.callFrame.functionName || '(anon)';
    const m = name.match(/wasm-function\[(\d+)\]/) || name.match(/^\$?f?(\d+)$/); if (m) name = names.get(m[1]) || name;
    total += c; if (waits.test(name)) continue; work += c; self.set(name, (self.get(name) || 0) + c);
  }
  console.log(`=== ${f}: ${total} samples, work ${(100 * work / total).toFixed(1)}%`);
  for (const [n, c] of [...self].sort((a, b) => b[1] - a[1]).slice(0, +topN)) console.log(`${(100 * c / total).toFixed(2).padStart(6)}%  ${n.slice(0, 140)}`);
}
