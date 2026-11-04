// Samples every worker of the engine's page with the browser's profiler (CDP over the remote debugging port) for
// `seconds`; prints, per worker with samples, its top functions by self time and by total time.
//   node prof.mjs <port> <seconds> [top]
const [port, seconds = "20", topN = "30"] = process.argv.slice(2);
const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r, { once: true }));
let nextId = 1;
const pending = new Map();
const workers = [];
function send(method, params = {}, sessionId) {
  const id = nextId++;
  ws.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.delete(id)) reject(new Error(`${method}: no answer in 30 s`)); }, 30000);
  });
}
ws.addEventListener('message', (event) => {
  const msg = JSON.parse(event.data);
  if (msg.id && pending.has(msg.id)) {
    const p = pending.get(msg.id);
    pending.delete(msg.id);
    msg.error ? p.reject(new Error(JSON.stringify(msg.error))) : p.resolve(msg.result);
    return;
  }
  if (msg.method === 'Target.attachedToTarget') {
    const { sessionId, targetInfo } = msg.params;
    if (targetInfo.type === 'worker') {
      workers.push({ sessionId, url: targetInfo.url });
    }
    send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, sessionId).catch(() => {});
    send('Runtime.runIfWaitingForDebugger', {}, sessionId).catch(() => {});
  }
});
const { targetInfos } = await send('Target.getTargets');
const pageTarget = targetInfos.find((t) => t.type === 'page' && t.url.startsWith('http://127.0.0.1:'));
const { sessionId: pageSession } = await send('Target.attachToTarget', { targetId: pageTarget.targetId, flatten: true });
await send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, pageSession);
await new Promise((r) => setTimeout(r, 3000));
console.log(`${workers.length} workers`);
for (const w of workers) {
  try {
    await send('Profiler.enable', {}, w.sessionId);
    await send('Profiler.setSamplingInterval', { interval: 200 }, w.sessionId);
    await send('Profiler.start', {}, w.sessionId);
    w.started = true;
  } catch (e) { w.error = String(e); }
}
for (const w of workers) if (w.error) console.log(`start ${w.url}: ${w.error}`);
await new Promise((r) => setTimeout(r, Number(seconds) * 1000));
for (const w of workers.filter((x) => x.started)) {
  let profile;
  try { ({ profile } = await send('Profiler.stop', {}, w.sessionId)); } catch (e) { console.log(`=== worker ${w.url}: ${e}`); continue; }
  if (process.env.PROF_DUMP) (await import('node:fs')).writeFileSync(`${process.env.PROF_DUMP}/worker_${workers.indexOf(w)}.cpuprofile`, JSON.stringify(profile));
  const byId = new Map(profile.nodes.map((n) => [n.id, n]));
  const parent = new Map();
  for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
  const self = new Map(), total = new Map();
  let samples = 0, idle = 0;
  for (const n of profile.nodes) {
    const name = n.callFrame.functionName || `(anonymous ${n.callFrame.url.split('/').pop()})`;
    const hits = n.hitCount || 0;
    samples += hits;
    if (name === '(idle)' || name === '(program)') idle += hits;
    self.set(name, (self.get(name) || 0) + hits);
    // total: each sample counts once per distinct function on its stack
    if (hits) {
      const seen = new Set();
      for (let id = n.id; id !== undefined; id = parent.get(id)) {
        const nm = byId.get(id).callFrame.functionName || `(anonymous ${byId.get(id).callFrame.url.split('/').pop()})`;
        if (!seen.has(nm)) { seen.add(nm); total.set(nm, (total.get(nm) || 0) + hits); }
      }
    }
  }
  if (samples - idle < 200) continue;
  console.log(`\n=== worker ${w.url.split('/').pop()} — ${samples} samples, busy ${(100 * (samples - idle) / samples).toFixed(0)}%`);
  const top = (m) => [...m.entries()].sort((a, b) => b[1] - a[1]).slice(0, Number(topN));
  console.log('-- self');
  for (const [n, h] of top(self)) console.log(`${(100 * h / samples).toFixed(1).padStart(5)}%  ${n}`);
  console.log('-- total');
  for (const [n, h] of top(total)) console.log(`${(100 * h / samples).toFixed(1).padStart(5)}%  ${n}`);
}
ws.close();
