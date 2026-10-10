// Native Linux configure port. Keep the original ABI and source categories.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Writer, literal } from './ninja_syntax.mjs';

export const HELP = `Usage: sh configure.sh [configure] [--linux-cc BINARY]
  --compiler-launcher BINARY  Compile-only launcher
  --linux-nm BINARY           Symbol reader (use matching llvm-nm for LTO)
  --linux-ld BINARY           Explicit LTO linker (default: lld on PATH)
  --native-prefix DIRECTORY  Project-local SDL3 installation prefix
  --release                  Omit assertions/debug game data
  --portable                 SSE2 baseline instead of host tuning
  --lto full|thin|off         Default: thin
  --pgo off                  Only supported mode; no runtime asset prerequisite
Native target: ninja linux. Web/Android/authentic-Xbox/PGO tooling is not ported.
Unsupported options are refused, never silently ignored.`;
export function parseArgs(args) {
  const options = { cc: 'clang', nm: 'llvm-nm', ld: '', prefix: '', launcher: '', release: false, portable: false, lto: 'thin', pgo: 'off' };
  const values = { '--linux-cc': 'cc', '--linux-nm': 'nm', '--linux-ld': 'ld', '--native-prefix': 'prefix', '--compiler-launcher': 'launcher', '--lto': 'lto', '--pgo': 'pgo' };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--help' || arg === '-h') return { help: true };
    if (arg === 'configure' && i === 0) continue;
    if (arg === '--release') { options.release = true; continue; }
    if (arg === '--portable') { options.portable = true; continue; }
    if (values[arg]) {
      const value = args[++i];
      if (!value || value.startsWith('--')) throw new Error(`Missing value for ${arg}`);
      options[values[arg]] = value;
      continue;
    }
    throw new Error(`Unsupported native-port argument: ${arg}`);
  }
  if (!['full', 'thin', 'off'].includes(options.lto)) throw new Error('Invalid --lto');
  if (options.pgo !== 'off') throw new Error('PGO use/train is not ported; select --pgo off');
  return options;
}
export function files(root, suffixes, recursive = false) {
  return fs.readdirSync(root, { withFileTypes: true }).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0)
    .flatMap(entry => {
      const filename = path.posix.join(root, entry.name);
      if (entry.isDirectory()) return recursive ? files(filename, suffixes, true) : [];
      return entry.isFile() && suffixes.includes(path.extname(entry.name).toLowerCase()) ? [filename] : [];
    });
}
const shellQuote = value => `'${String(value).replaceAll("'", "'\\''")}'`;
const flags = values => values.map(literal).join(' ');
const wideNames = ['wcslen', 'wcsnlen', 'wcschr', 'wcsrchr', 'wcscmp', 'wcsncmp', 'wcscpy', 'wcsncpy', 'wcscat', 'wcsncat', 'wmemchr', 'wmemcmp', 'wmemcpy', 'wmemmove', 'wmemset'];
const abiBase = ['--target=i686-linux-gnu', '-m32', '-fms-extensions', '-fshort-wchar', '-malign-double', '-fcommon', '-fno-pic', '-fno-strict-aliasing', '-fwrapv', '-fno-delete-null-pointer-checks', '-freg-struct-return', '-fno-omit-frame-pointer', '-ffp-contract=off', '-O2', '-g', ...wideNames.map(name => `-fno-builtin-${name}`)];
const gameFlags = ['-std=gnu89', '-D__STRICT_ANSI__', '-w', ...['incompatible-pointer-types', 'incompatible-function-pointer-types', 'int-conversion', 'implicit-function-declaration', 'implicit-int', 'return-type'].map(name => `-Wno-error=${name}`)];
const platformFlags = ['-std=gnu11', '-D_GNU_SOURCE', '-DHALO_LINUX_PLATFORM_LAYER', '-Wall', ...['unused-function', 'unknown-pragmas', 'microsoft-anon-tag', 'pragma-pack', 'ignored-attributes', 'duplicate-decl-specifier', 'missing-braces', 'unused-variable', 'ignored-pragmas'].map(name => `-Wno-${name}`)];

export function generate(options, args = []) {
  const config = JSON.parse(fs.readFileSync('port/linux/port.json', 'utf8'));
  const solution = JSON.parse(fs.readFileSync('config/config.json', 'utf8'));
  const n = new Writer();
  const sdk = files('port/include/xdk', ['.h']);
  const sources = files('source', ['.c', '.h'], true);
  const prefix = 'port/linux/include/halo_linux_prefix.h';
  const semantics = 'build/linux/halo_msvc_semantics.h';
  const platformSemantics = 'build/linux/platform_msvc_semantics.h';
  n.rule('configure_native', { command: `sh configure.sh ${flags(args.map(shellQuote))}`, generator: '1', description: 'CONFIGURE native Linux' });
  n.build('build.ninja', 'configure_native', [], ['configure.sh', 'tools/linux_build.mjs', 'tools/ninja_syntax.mjs', 'config/config.json', 'port/linux/port.json', 'source/game', 'port/include/xdk', config.platform_sources, config.game_sources, 'port/third_party/mbedtls/library', 'port/third_party/musl-math/src']);
  n.variable('linux_cc', literal(shellQuote(options.cc)));
  n.variable('linux_nm', literal(shellQuote(options.nm)));
  n.rule('linux_msvc_semantics', { command: 'node tools/linux_msvc_semantics.mjs --output $out $scan', description: 'LINUX MSVC SEMANTICS $out', restat: '1' });
  n.build(semantics, 'linux_msvc_semantics', [], ['tools/linux_msvc_semantics.mjs', ...sdk, ...sources], { scan: '--all-inlines --tags source --inlines source --inlines port/include/xdk' });
  n.build(platformSemantics, 'linux_msvc_semantics', [], ['tools/linux_msvc_semantics.mjs', ...sdk], { scan: '--inlines port/include/xdk' });
  n.rule('linux_cc', {
    command: `mkdir -p "$$(dirname $out)" && ${options.launcher ? `${literal(shellQuote(options.launcher))} ` : ''}$linux_cc -MMD -MF $out.d $cflags -c $in -o $out`,
    description: 'LINUX CC $out', depfile: '$out.d', deps: 'gcc',
  });
  n.rule('linux_link', {
    command: '$linux_cc $ldflags -o $out @$out.rsp $libs && node tools/linux_link_check.mjs --nm $linux_nm $out.rsp $out',
    description: 'LINUX LINK $out', rspfile: '$out.rsp', rspfile_content: '$in_newline',
  });
  const march = options.portable ? '-march=x86-64' : '-march=native';
  const abi = [...abiBase, march, ...(options.release ? ['-DHALO_RELEASE'] : [])];
  const lto = options.lto === 'off' ? [] : [`-flto=${options.lto}`];
  const posix = ['--target=i686-linux-gnu', '-m32', '-std=gnu11', '-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64', '-fno-pic', '-O2', '-g', '-Wall', march];
  const objects = [];
  const seen = new Set();
  let campaignFlags;
  function add(source, cflags, native = false) {
    if (seen.has(source)) throw new Error(`Duplicate object source: ${source}`);
    seen.add(source);
    const object = `build/linux/obj/${source.replace(/\.c$/i, '.o')}`;
    objects.push(object);
    n.build(object, 'linux_cc', [source], [...sdk, prefix, semantics, platformSemantics], { cflags: flags([...cflags, ...(native ? [] : lto)]) });
  }
  for (const project of solution.projects.filter(project => config.projects.includes(project.name))) {
    const includes = (project.options.include_dirs ?? []).filter(dir => dir !== 'xbox/include').map(dir => `-I${shellQuote(dir)}`);
    const game = [...abi, ...gameFlags, '-include', prefix, '-include', semantics, ...(project.options.defines ?? []).map(value => `-D${value}`), '-Iport/linux/include', ...includes, '-idirafter', 'port/include/xdk'];
    if (project.name === 'halobetacache') campaignFlags = game;
    const selected = project.objects.filter(object => object.status !== 'MISSING' && /\.c$/i.test(object.name) && !config.exclude_sources.includes(object.name)).map(object => object.name);
    // Campaign slice is authored after the imported config; never omit its implementation.
    if (project.name === 'halobetacache') for (const authored of ['source/game/campaign_coop.c', 'source/game/campaign_mission.c']) {
      if (fs.existsSync(authored) && !selected.includes(authored)) selected.push(authored);
    }
    for (const source of selected) add(source, game);
    for (const source of files(config.game_sources, ['.c'])) add(source, game);
  }
  const platform = [...abi, ...platformFlags, '-include', prefix, '-include', platformSemantics, `-I${config.platform_sources}`, '-Iport/linux/include', '-Iport/third_party/tomlc17', '-Iport/third_party/kcp', '-Isource', '-Isource/cseries', ...(options.prefix ? [`-I${shellQuote(path.join(options.prefix, 'include'))}`] : []), '-idirafter', 'port/include/xdk'];
  for (const source of files(config.platform_sources, ['.c'])) {
    const name = path.basename(source);
    if (name === 'posix_update.c') add(source, [...posix, `-I${config.platform_sources}`, '-Iport/third_party/mbedtls/include'], true);
    else if (name.startsWith('posix_')) add(source, [...posix, `-I${config.platform_sources}`], true);
    else if (name === 'updater.c') {
      const number = process.env.HALO_BUILD_NUMBER ?? '0';
      add(source, [...platform, `-DHALO_BUILD_NUMBER=${/^\d+$/.test(number) ? number : '0'}`, `-DHALO_BUILD_FLAVOR=\\"${options.release ? 'release' : 'debug'}\\"`]);
    } else add(source, platform);
  }
  for (const source of files('port/third_party/mbedtls/library', ['.c'])) add(source, [...posix, '-Iport/third_party/mbedtls/include', '-Iport/third_party/mbedtls/library', '-fno-builtin-wcslen', '-w'], true);
  for (const source of ['port/third_party/tomlc17/tomlc17.c', 'port/third_party/kcp/ikcp.c']) add(source, [...abi, '-std=gnu11', '-w']);
  for (const source of files('port/third_party/musl-math/src', ['.c'])) add(source, [...abi, '-std=gnu11', '-w', '-Iport/third_party/musl-math/include', '-include', 'port/third_party/musl-math/include/libm.h']);
  n.build('build/linux/halo', 'linux_link', objects, ['tools/linux_link_check.mjs'], {
    ldflags: flags(['--target=i686-linux-gnu', '-m32', '-no-pie', '-g', ...lto, ...(lto.length ? [options.ld ? `--ld-path=${shellQuote(options.ld)}` : '-fuse-ld=lld', '-O2'] : []), ...(options.prefix ? [`-L${shellQuote(path.join(options.prefix, 'lib'))}`, `-Wl,-rpath,${shellQuote(path.resolve(options.prefix, 'lib'))}`] : []), ...(options.lto === 'thin' ? ['-Wl,--thinlto-cache-dir=build/linux/thinlto-cache'] : [])]),
    libs: flags(config.libraries.map(lib => `-l${lib}`)),
  });
  n.build('linux', 'phony', ['build/linux/halo']);
  const campaignControl = 'port/web/tests/campaign_coop_native_controls_test.c';
  if (campaignFlags && fs.existsSync(campaignControl)) {
    const controlObject = 'build/linux/obj/port/web/tests/campaign_coop_native_controls_test.o';
    n.build(controlObject, 'linux_cc', [campaignControl], [prefix, semantics, ...sdk], { cflags: flags([...campaignFlags, ...lto]) });
    n.rule('native_control_link', { command: '$linux_cc $ldflags $in -o $out', description: 'NATIVE CONTROL LINK $out' });
    const controlInputs = ['build/linux/obj/source/game/campaign_coop.o', controlObject];
    if (fs.existsSync('source/game/campaign_mission.c')) controlInputs.splice(1, 0, 'build/linux/obj/source/game/campaign_mission.o');
    n.build('build/linux/campaign_coop_native_controls', 'native_control_link', controlInputs, [], {
      ldflags: flags(['--target=i686-linux-gnu', '-m32', '-no-pie', ...lto, ...(lto.length ? [options.ld ? `--ld-path=${shellQuote(options.ld)}` : '-fuse-ld=lld'] : [])]),
    });
    n.rule('native_control_run', { command: '$in', description: 'RUN campaign native controls' });
    n.build('campaign-native-controls', 'native_control_run', ['build/linux/campaign_coop_native_controls']);
    const authorControl = 'port/web/tests/campaign_mission_author_test.c';
    if (fs.existsSync(authorControl)) {
      const authorObject = 'build/linux/obj/port/web/tests/campaign_mission_author_test.o';
      n.build(authorObject, 'linux_cc', [authorControl], [prefix, semantics, ...sdk], { cflags: flags([...campaignFlags, ...lto]) });
      n.build('build/linux/campaign_mission_author_controls', 'native_control_link', [authorObject, 'build/linux/obj/source/cseries/cseries.o', 'build/linux/obj/source/cseries/cseries_windows.o', 'build/linux/obj/source/cseries/stack_walk_windows.o', 'build/linux/obj/port/linux/src/msvc_crt.o'], [], {
        ldflags: flags(['--target=i686-linux-gnu', '-m32', '-no-pie', ...lto, ...(lto.length ? [options.ld ? `--ld-path=${shellQuote(options.ld)}` : '-fuse-ld=lld'] : [])]),
      });
      n.build('campaign-author-controls', 'native_control_run', ['build/linux/campaign_mission_author_controls']);
    }
  }
  n.lines.push('default linux');
  return n.toString();
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) console.log(HELP);
    else { fs.writeFileSync('build.ninja', generate(options, process.argv.slice(2))); console.log('Generated native Linux build.ninja (no Python, assets or PGO execution).'); }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
