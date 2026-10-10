import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { generate, parseArgs } from '../linux_build.mjs';
import { escapePath, literal } from '../ninja_syntax.mjs';

test('unsupported runtime/toolchain modes refuse rather than executing Python', () => {
  for (const args of [['--pgo', 'train'], ['--pgo', 'use'], ['--android-ndk', '/tmp/ndk'], ['--web-cc', 'emcc'], ['progress'], ['--lto', 'bad'], ['--linux-cc']]) assert.throws(() => parseArgs(args));
});
test('native route selects exact flags and keeps paths distinct from variables', () => {
  assert.equal(escapePath('a $b:c'), 'a$ $$b$:c');
  assert.equal(literal('$HOME'), '$$HOME');
  const output = generate(parseArgs(['--linux-cc', 'clang', '--pgo', 'off', '--lto', 'thin']));
  assert.match(output, /-flto=thin/);
  assert.match(output, /--thinlto-cache-dir/);
  assert.match(output, /source\/saved\$ games\/game_state\.o/);
  assert.match(output, /node tools\/linux_msvc_semantics\.mjs/);
  assert.match(output, /node tools\/linux_link_check\.mjs/);
  assert.doesNotMatch(output, /python|\.py\b|\.iso\b|\.profdata\b/);
});
test('every configured available C input appears exactly once, with platform dependencies', () => {
  const output = generate(parseArgs(['--lto', 'off']));
  const config = JSON.parse(fs.readFileSync('config/config.json', 'utf8'));
  const port = JSON.parse(fs.readFileSync('port/linux/port.json', 'utf8'));
  for (const project of config.projects.filter(project => port.projects.includes(project.name))) {
    for (const object of project.objects.filter(object => object.status !== 'MISSING' && /\.c$/i.test(object.name) && !port.exclude_sources.includes(object.name))) {
      const edge = `build ${escapePath(`build/linux/obj/${object.name.replace(/\.c$/i, '.o')}`)}:`;
      assert.equal(output.split('\n').filter(line => line.startsWith(edge)).length, 1, object.name);
    }
  }
  assert.match(output, /port\/third_party\/mbedtls\/library\/ssl_tls\.o/);
  assert.match(output, /-fno-builtin-wcslen/);
  assert.doesNotMatch(output, /-flto=/);
});
test('authored campaign units join the production and native control graph exactly once', () => {
  const output = generate(parseArgs(['--lto', 'off']));
  const authored = ['campaign_coop', 'campaign_mission'].filter(name => fs.existsSync(`source/game/${name}.c`));
  for (const name of authored) {
    const edge = `build build/linux/obj/source/game/${name}.o:`;
    assert.equal(output.split('\n').filter(line => line.startsWith(edge)).length, 1);
  }
  const controlEdge = output.split('\n').find(line => line.startsWith('build build/linux/campaign_coop_native_controls:'));
  for (const name of authored) assert.ok(controlEdge.includes(`build/linux/obj/source/game/${name}.o`));
  assert.ok(controlEdge.includes('build/linux/obj/port/web/tests/campaign_coop_native_controls_test.o'));
});
