// The story route: segments of the game's progress, each a tour of its own (tools/galaxy_tour.mjs) with the save as the
// segment needs it (Grand Stars, event flags) and test events that take its stars, run against the user's disc. Per
// segment it checks what the game itself did next: the scenes it went through, the movie frames it decoded, and that
// nothing stopped or hung. It proves the game's progress logic runs in the web engine (stars, domes, bosses, ending),
// not that each level can be played through by hand: that is the real play test (docs/PRUEBA_DE_PARTIDA.md).
//   node tools/story_route.mjs --disc <your .rvz> --route tools/routes/historia.txt --out <folder> [--nand <file>]
//        [--site <folder>] [--only N[,N...]] [--port 18800]
// The route file: segments separated by "---" lines. A segment's lines are a stages file for galaxy_tour.mjs (stops
// with their test events, run settings), plus these, read here and not sent to the engine:
//   expect scenes A B ...   the last stop goes through these scenes, in this order (after the ones before)
//   expect movie            the segment decodes movie frames (needs graphics mode: the diagnostic build has no decoder)
//   mode graphics|diagnostic, frames N, hang S   for this segment (diagnostic, 600 and 600 by default)
// Output: <out>/NN/ (galaxy_tour's report, log and screenshots: game data, kept out of the repository) and
// <out>/route.md, the table of segments.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
if (!args.disc || !args.route || !args.out) {
  console.error('usage: node tools/story_route.mjs --disc <rvz> --route <file> --out <folder> [--nand <file>] [--site <folder>] [--only N,...]');
  process.exit(2);
}

const segments = readFileSync(args.route, 'utf8').split(/^---\s*$/m).map((text, index) => {
  const segment = { index: index + 1, title: '', lines: [], expectScenes: [], expectMovie: false, mode: 'diagnostic',
                    frames: '600', hang: '600' };
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith('#')) {
      if (!segment.title) segment.title = line.replace(/^#\s*/, '');
      continue;
    }
    let m;
    if ((m = /^expect scenes (.+)$/.exec(line))) segment.expectScenes = m[1].split(/\s+/);
    else if (line === 'expect movie') segment.expectMovie = true;
    else if ((m = /^mode (graphics|diagnostic)$/.exec(line))) segment.mode = m[1];
    else if ((m = /^frames (\d+)$/.exec(line))) segment.frames = m[1];
    else if ((m = /^hang (\d+)$/.exec(line))) segment.hang = m[1];
    else segment.lines.push(line);
  }
  return segment;
}).filter((s) => s.lines.length > 0);

const only = args.only ? new Set(args.only.split(',').map(Number)) : null;
const rows = [];
mkdirSync(args.out, { recursive: true });
for (const segment of segments) {
  if (only && !only.has(segment.index)) continue;
  const dir = join(args.out, String(segment.index).padStart(2, '0'));
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, 'stages.txt'), segment.lines.join('\n') + '\n');
  const tourArgs = [join(root, 'tools/galaxy_tour.mjs'), '--disc', args.disc, '--stages', join(dir, 'stages.txt'),
    '--out', join(dir, 'out'), '--frames', segment.frames, '--hang', segment.hang, '--mode', segment.mode,
    '--port', String(Number(args.port || 18800) + segment.index)];
  if (args.nand) tourArgs.push('--nand', args.nand);
  if (args.site) tourArgs.push('--site', args.site);
  const started = Date.now();
  console.log(`segmento ${segment.index}: ${segment.title || segment.lines[0]} (${segment.mode})`);
  const run = spawnSync('node', tourArgs, { encoding: 'utf8', timeout: 2 * 3600 * 1000 });
  const reportPath = join(dir, 'out', 'tour.json');
  const report = existsSync(reportPath) ? JSON.parse(readFileSync(reportPath, 'utf8')) : null;
  const problems = [];
  if (!report) problems.push(`sin informe de la gira (salida ${run.status}): ${(run.stderr || '').slice(-300)}`);
  const stops = report ? report.stages : [];
  const last = stops[stops.length - 1];
  if (report && report.end !== 'gira terminada') problems.push(`la gira terminó así: ${report.end}`);
  for (const stop of stops) {
    if (stop.outcome !== 'cargó y dibujó') problems.push(`${stop.stop}: ${stop.outcome}`);
    for (const p of stop.problems) problems.push(`${stop.stop}: ${p}`);
  }
  if (report) for (const p of report.tourProblems || []) problems.push(`lista: ${p}`);
  // The expected scenes, in order, among the last stop's
  if (segment.expectScenes.length && last) {
    // From the scene the game was in when the stop began (a stop to that same stage changes nothing)
    let at = 0;
    for (const scene of [last.startScene, ...last.scenes].filter(Boolean)) if (scene === segment.expectScenes[at]) at++;
    if (at < segment.expectScenes.length) {
      problems.push(`esperaba las escenas ${segment.expectScenes.join(' → ')}; hubo ${last.scenes.join(' → ') || 'ninguna'}`);
    }
  }
  const movieFrames = stops.reduce((sum, s) => sum + (s.movieFrames || 0), 0);
  if (segment.expectMovie && movieFrames === 0) problems.push('esperaba fotogramas de vídeo y no hubo ninguno');
  rows.push({
    index: segment.index, title: segment.title, mode: segment.mode, ok: problems.length === 0,
    settings: report ? report.settings : [], events: stops.flatMap((s) => s.events),
    scenes: last ? [...(last.startScene ? [`(${last.startScene})`] : []), ...last.scenes] : [], movieFrames, minutes: Math.round((Date.now() - started) / 6000) / 10, problems,
  });
  writeFileSync(join(args.out, 'route.json'), JSON.stringify(rows, null, 1));
  writeFileSync(join(args.out, 'route.md'), [
    `Ruta de la historia: ${rows.filter((r) => r.ok).length} de ${rows.length} segmentos sin problemas.`, '',
    '| # | Segmento | Modo | Ajustes | Eventos | Escenas (última parada) | Vídeo | Min | Resultado |',
    '|---|---|---|---|---|---|---|---|---|',
    ...rows.map((r) => `| ${r.index} | ${r.title.replace(/\|/g, '/')} | ${r.mode} | ${r.settings.join('; ') || '—'} | ` +
      `${r.events.join(', ') || '—'} | ${r.scenes.join(' → ') || '—'} | ${r.movieFrames} | ${r.minutes} | ` +
      `${r.ok ? 'bien' : r.problems.join('; ').replace(/\|/g, '/').slice(0, 300)} |`), ''].join('\n'));
  console.log(`  ${rows[rows.length - 1].ok ? 'bien' : 'PROBLEMAS: ' + problems.join('; ').slice(0, 400)}`);
}
console.log(`ruta: ${join(args.out, 'route.md')}`);
process.exit(rows.every((r) => r.ok) ? 0 : 1);
