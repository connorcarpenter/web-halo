import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { stripCplusplus, sourceFiles, scanTags, scanInlineFunctions, render, main as generate } from '../linux_msvc_semantics.mjs';
import { splitResponseLine, parseSymbolTable, missingWeakReferences, wideImports } from '../linux_link_check.mjs';

test('C++ and Win64 branches, nesting and elif follow original stripping rules', () => {
  assert.equal(stripCplusplus('#ifdef __cplusplus\ncpp\n#if X\nnested\n#endif\n#else\nc\n#endif\n#ifndef _WIN64\nwin32\n#else\nwin64\n#endif\n#if defined (__cplusplus)\ncpp2\n#elif OTHER\nother\n#endif\n'), 'c\nwin32\nother');
  assert.equal(stripCplusplus('#if ! defined(_WIN64)\nkeep\n#endif\n#if SOMETHING\none\n#else\ntwo\n#endif'), 'keep\none\ntwo');
});

test('tag and inline scans remove comments and excluded branches and preserve linkage selection', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'halo-native-'));
  t.after(() => fs.rmSync(root, { recursive: true }));
  fs.writeFileSync(path.join(root, 'a.H'), '/* struct Comment */\n#ifdef __cplusplus\nstruct Cpp;\n#endif\nstruct Zebra; union Ambiguous; struct Ambiguous;\n__inline int alone(void) { return 0; }\nFORCEINLINE int called(int x) { return x; }\ncalled(1);\n__forceinline int declared(void) { return 2; }\nint declared(void);\n');
  fs.writeFileSync(path.join(root, 'ignored.cpp'), 'struct Ignored;');
  const files = sourceFiles([root]);
  assert.equal(files.length, 1);
  const tags = scanTags(files);
  assert.deepEqual([...tags.keys()], ['Zebra', 'Ambiguous']);
  assert.deepEqual([...scanInlineFunctions(files)].sort(), ['called', 'declared']);
  assert.deepEqual([...scanInlineFunctions(files, true)].sort(), ['alone', 'called', 'declared']);
  const header = render(tags, scanInlineFunctions(files));
  assert.match(header, /struct Zebra;/);
  assert.doesNotMatch(header, /(?:struct|union) Ambiguous;/);
  assert.match(header, /#pragma weak called\n#pragma weak declared/);
  const output = path.join(root, 'generated', 'tags.h');
  generate(['--output', output, '--tags', root, '--inlines', root]);
  assert.equal(fs.readFileSync(output, 'utf8'), header);
  const before = fs.statSync(output).mtimeMs;
  generate(['--output', output]);
  const empty = fs.readFileSync(output, 'utf8');
  const unchangedTime = fs.statSync(output).mtimeMs;
  generate(['--output', output]);
  assert.equal(fs.readFileSync(output, 'utf8'), empty);
  assert.equal(fs.statSync(output).mtimeMs, unchangedTime);
  assert.ok(fs.statSync(output).mtimeMs >= before);
  assert.throws(() => generate(['--output', output, '--tags', path.join(root, 'missing')]), /not a directory/);
});

test('response quoting follows POSIX shlex without comment expansion', () => {
  assert.deepEqual(splitResponseLine('one.o "two spaces.o" three\\ spaces.o \'four.o\' "" #literal'), ['one.o', 'two spaces.o', 'three spaces.o', 'four.o', '', '#literal']);
  assert.deepEqual(splitResponseLine('"a\\qb" "a\\\"b"'), ['a\\qb', 'a"b']);
  assert.throws(() => splitResponseLine('"unterminated'), /quotation/);
  assert.throws(() => splitResponseLine('trailing\\'), /escaped/);
});

test('nm only global definitions satisfy weak references and runtime exceptions remain allowed', () => {
  const symbols = parseSymbolTable('first.o:\nmissing w\nlocal w\nlocal t 0 1\nresolved w\nresolved T 0 1\nvariable v\nvariable V 0 4\n__gmon_start__ w\nordinary U\ninvalid long 0\n');
  assert.deepEqual(missingWeakReferences(symbols), ['local', 'missing']);
  for (const type of 'TDBRCVW') assert.deepEqual(missingWeakReferences(parseSymbolTable(`x w\nx ${type} 0 1`)), []);
});

test('wide imports include versioned and formatted variants while narrow imports pass', () => {
  assert.deepEqual(wideImports(' U wcslen@GLIBC_2.2.5\n U swprintf\n U towlower\n U iswalpha\n U printf\n U memcpy'), ['iswalpha', 'swprintf', 'towlower', 'wcslen@GLIBC_2.2.5']);
});
