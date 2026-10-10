#!/usr/bin/env node
// Reject unresolved weak references and glibc wide-character imports.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

export const RUNTIME_WEAK_REFERENCES = new Set(['__gmon_start__', '_ITM_deregisterTMCloneTable', '_ITM_registerTMCloneTable', '__cxa_finalize', '_Jv_RegisterClasses', '__pthread_key_create', 'pthread_cancel']);
export function splitResponseLine(line) {
  const words = [];
  let word = '', quote = null, started = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (quote === "'") { if (ch === "'") quote = null; else word += ch; }
    else if (quote === '"') {
      if (ch === '"') quote = null;
      else if (ch === '\\' && ['"', '\\'].includes(line[i + 1])) word += line[++i];
      else word += ch;
    } else if (ch === "'" || ch === '"') { quote = ch; started = true; }
    else if (ch === '\\') { if (++i === line.length) throw new Error('No escaped character'); word += line[i]; started = true; }
    else if (/\s/.test(ch)) { if (started) { words.push(word); word = ''; started = false; } }
    else { word += ch; started = true; }
  }
  if (quote) throw new Error('No closing quotation');
  if (started) words.push(word);
  return words;
}
export function parseSymbolTable(output) {
  const symbols = new Map();
  for (const line of output.split(/\r?\n/)) {
    const fields = line.trim().split(/\s+/);
    if (fields.length >= 2 && !fields[0].endsWith(':') && fields[1].length === 1) {
      if (!symbols.has(fields[0])) symbols.set(fields[0], new Set());
      symbols.get(fields[0]).add(fields[1]);
    }
  }
  return symbols;
}
export function missingWeakReferences(symbols) {
  return [...symbols].filter(([name, types]) => [...types].some(type => 'wv'.includes(type)) && ![...types].some(type => 'TDBRCVW'.includes(type)) && !RUNTIME_WEAK_REFERENCES.has(name)).map(([name]) => name).sort();
}
export function wideImports(output) {
  return output.split(/\s+/).filter(name => /^(wcs|wmem|[a-z]*wprintf|towlower|towupper|isw)/.test(name.split('@')[0])).sort();
}
function nmCommand() {
  for (const dir of (process.env.PATH ?? '').split(path.delimiter)) {
    const filename = path.join(dir, 'llvm-nm');
    try { fs.accessSync(filename, fs.constants.X_OK); if (fs.statSync(filename).isFile()) return filename; } catch {}
  }
  return 'nm';
}
function runNm(nm, args) {
  const result = spawnSync(nm, args, { encoding: 'utf8', maxBuffer: 1024 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${nm} failed (${result.status}): ${result.stderr.trim()}`);
  return result.stdout;
}
export function main(args = process.argv.slice(2)) {
  let selectedNm;
  if (args[0] === '--nm') {
    selectedNm = args[1];
    if (!selectedNm) throw new Error('--nm requires a binary');
    args = args.slice(2);
  }
  if (!args[0]) throw new Error('Usage: linux_link_check.mjs <response file listing the objects> [executable]');
  const objects = fs.readFileSync(args[0], 'utf8').split('\n').flatMap(splitResponseLine);
  const nm = selectedNm ?? nmCommand();
  const missing = missingWeakReferences(parseSymbolTable(runNm(nm, ['--format=posix', ...objects])));
  if (args.length > 1) {
    const wide = wideImports(runNm(nm, ['--undefined-only', args[1]]));
    if (wide.length) { console.log('the executable imports glibc wide character functions, which assume a 32-bit wchar_t: ' + wide.join(', ')); return 1; }
  }
  if (missing.length) {
    console.log(`${missing.length} weak reference(s) that no object defines (they would silently resolve to address 0):`);
    for (const name of missing) console.log(`  ${name}`);
    return 1;
  }
  return 0;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { process.exitCode = main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
