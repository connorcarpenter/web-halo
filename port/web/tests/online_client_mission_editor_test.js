'use strict';

/* Hazard Run mission authoring (Usher 61465, Finn acc0ed7 + 61476 bridge
contract): edit mission/encounter fields through the browser UI against
the canonical record, staged through platform_web_mission_author_*.
No C, no transport, no second serializer/validator/store. */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const webDirectory = path.join(__dirname, '..');

function element(overrides) {
  const listeners = {};
  return Object.assign({
    dataset: {},
    disabled: false,
    hidden: false,
    open: false,
    options: [],
    value: '',
    textContent: '',
    addEventListener(type, listener) { listeners[type] = listener; },
    close() { this.open = false; },
    focus() { this.focused = true; },
    removeAttribute(name) { delete this[name]; },
    select() {},
    setAttribute(name, value) { this[name] = String(value); },
    showModal() { this.open = true; },
    listeners,
  }, overrides || {});
}

const elements = {};
[
  'canvas', 'invite-copy', 'invite-copy-status', 'invite-link',
  'online', 'online-advanced-enabled',
  'online-advanced-fields', 'online-cancel', 'online-close', 'online-code',
  'online-description', 'online-detail', 'online-dialog',
  'online-health-percent', 'online-host', 'online-host-form',
  'online-human-verification', 'online-infinite-grenades',
  'online-invisible-players', 'online-invite', 'online-join',
  'online-join-confirm', 'online-join-form', 'online-join-profile',
  'online-join-summary', 'online-leave-host',
  'online-lives', 'online-map', 'online-map-next', 'online-map-options',
  'online-mode', 'online-mode-back', 'online-mode-options',
  'online-other-players-on-radar', 'online-player-name', 'online-profile',
  'online-profile-preview', 'online-profile-preview-name',
  'online-progress', 'online-respawn-seconds', 'online-score-to-win',
  'online-setup', 'online-shields', 'online-spartan-image', 'online-status',
  'online-step-link', 'online-step-map', 'online-step-mode',
  'online-style-options', 'online-turnstile', 'online-verification-retry',
  'online-verification-status', 'online-wizard', 'online-wizard-link',
  'online-wizard-map', 'online-wizard-mode', 'online-wizard-steps',
  'player-count', 'player-empty', 'player-list', 'player-sidebar',
  'player-sidebar-toggle',
].forEach(id => { elements[id] = element(); });

const missionCalls = [];
let beginResult = 1;
let nameCharResult = 1;
let encounterResult = 1;
let commitResult = 1;
let missionStatus = 0;
let missionRecorded = -1;

const context = {
  console,
  document: {
    readyState: 'complete',
    getElementById: id => elements[id],
    querySelector: () => null,
    execCommand: () => true,
  },
  HaloWebTransport: {
    configure() {},
    disconnectAll() {},
    getLocalIdentifier: () => '020000000001',
    isSupported: () => true,
  },
  history: { replaceState() {} },
  localStorage: {
    getItem: () => null,
    setItem() {},
  },
  location: {
    hash: '',
    hostname: 'halo.example',
    href: 'https://halo.example/halo.html',
    origin: 'https://halo.example',
    pathname: '/halo.html',
    port: '',
    protocol: 'https:',
    search: '',
  },
  Module: {
    _platform_web_online_get_error: () => 0,
    _platform_web_online_get_state: () => 0,
    _platform_web_online_request: () => 1,
    _platform_web_online_set_transport_state() {},
    _platform_web_campaign_mission_begin: episode => {
      missionCalls.push(['begin', episode]);
      return beginResult;
    },
    _platform_web_campaign_mission_set_name_char: (index, code) => {
      missionCalls.push(['name_char', index, code]);
      return nameCharResult;
    },
    _platform_web_campaign_mission_add_encounter: (volume, actor, count, anchor) => {
      missionCalls.push(['encounter', volume, actor, count, anchor]);
      return encounterResult;
    },
    _platform_web_campaign_mission_commit: () => {
      missionCalls.push(['commit']);
      return commitResult;
    },
    _platform_web_campaign_mission_reset: () => {
      missionCalls.push(['reset']);
      return 1;
    },
    _platform_web_campaign_mission_status: () => missionStatus,
    _platform_web_campaign_mission_recorded: () => missionRecorded,
  },
  navigator: {},
  URL,
  URLSearchParams,
  clearInterval() {},
  clearTimeout() {},
  setInterval: () => 0,
  setTimeout: () => 0,
};
context.window = context;
context.globalThis = context;
vm.createContext(context);
vm.runInContext(
  fs.readFileSync(path.join(webDirectory, 'online_client.js'), 'utf8'),
  context,
  { filename: 'online_client.js' });

function validMission() {
  return {
    name: 'Deck Breach',
    episodeIndex: 1,
    encounters: [
      { triggerVolumeIndex: 4, actorType: 0, actorCount: 3, anchorIndex: -1 },
      { triggerVolumeIndex: 9, actorType: 10, actorCount: 16, anchorIndex: 2 },
    ],
  };
}

function expectedCalls(mission) {
  const calls = [['begin', mission.episodeIndex]];
  Array.from(mission.name).forEach((character, index) => {
    calls.push(['name_char', index, character.charCodeAt(0)]);
  });
  mission.encounters.forEach(encounter => {
    calls.push(['encounter', encounter.triggerVolumeIndex,
      encounter.actorType, encounter.actorCount, encounter.anchorIndex]);
  });
  calls.push(['commit']);
  return calls;
}

(function () {
  context.HaloOnline.runtimeReady();

  /* Invalid missions never reach the bridge: zero staged calls. The name
     rule is slot fidelity only (codes fit one byte); the bridge setter
     carries no content policy, so printable ASCII and Latin-1 stage. */
  const badMissions = [
    [{}, /episode/i],
    [{ name: '', episodeIndex: 0, encounters: [validMission().encounters[0]] }, /name/i],
    [{ name: 'x'.repeat(32), episodeIndex: 0, encounters: [validMission().encounters[0]] }, /name/i],
    [{ name: 'Ābc', episodeIndex: 0, encounters: [validMission().encounters[0]] }, /name|byte|char/i],
    [{ name: 'Nope', episodeIndex: -1, encounters: [validMission().encounters[0]] }, /episode/i],
    [{ name: 'Nope', episodeIndex: 8, encounters: [validMission().encounters[0]] }, /episode/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [] }, /encounter/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: new Array(9).fill(validMission().encounters[0]) }, /encounter/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [{ triggerVolumeIndex: -1, actorType: 0, actorCount: 1, anchorIndex: -1 }] }, /volume/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [{ triggerVolumeIndex: 0, actorType: 6, actorCount: 1, anchorIndex: -1 }] }, /actor/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [{ triggerVolumeIndex: 0, actorType: 0, actorCount: 0, anchorIndex: -1 }] }, /count/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [{ triggerVolumeIndex: 0, actorType: 0, actorCount: 17, anchorIndex: -1 }] }, /count/i],
    [{ name: 'Nope', episodeIndex: 0, encounters: [{ triggerVolumeIndex: 0, actorType: 0, actorCount: 1, anchorIndex: -2 }] }, /anchor/i],
  ];
  for (const [mission, pattern] of badMissions) {
    assert.throws(() => context.HaloOnline.missionSave(mission), pattern);
  }
  assert.deepEqual(missionCalls, [],
    'invalid missions must not stage anything through the bridge');

  /* Every hostile actor type from campaign_mission_actor_hostile passes;
     every other type is refused before the bridge. */
  for (const actor of [0, 1, 2, 3, 9, 10, 11]) {
    const mission = validMission();
    mission.encounters = [{ triggerVolumeIndex: 0, actorType: actor, actorCount: 1, anchorIndex: -1 }];
    context.HaloOnline.missionSave(mission);
  }
  for (const actor of [4, 5, 6, 7, 8, 12, 13, 14, 15]) {
    const mission = validMission();
    mission.encounters = [{ triggerVolumeIndex: 0, actorType: actor, actorCount: 1, anchorIndex: -1 }];
    assert.throws(() => context.HaloOnline.missionSave(mission), /actor/i);
  }
  missionCalls.length = 0;

  /* A valid mission stages exactly: begin, indexed name chars,
     encounters, commit. A Latin-1 name stages its byte codes. */
  const mission = validMission();
  context.HaloOnline.missionSave(mission);
  assert.deepEqual(missionCalls, expectedCalls(mission));
  assert.match(elements.online.textContent, /mission/i);
  assert.match(elements['online-status'].textContent, /Deck Breach/);
  missionCalls.length = 0;
  const latinMission = validMission();
  latinMission.name = 'Café';
  context.HaloOnline.missionSave(latinMission);
  assert.deepEqual(missionCalls, expectedCalls(latinMission));
  missionCalls.length = 0;

  /* A refused begin stages nothing further; the prior draft stands. */
  beginResult = 0;
  assert.throws(() => context.HaloOnline.missionSave(validMission()), /stage/i);
  assert.deepEqual(missionCalls, [['begin', 1]]);
  beginResult = 1;
  missionCalls.length = 0;

  /* A refused name char aborts before any encounter or commit. */
  nameCharResult = 0;
  assert.throws(() => context.HaloOnline.missionSave(validMission()), /name/i);
  assert.deepEqual(missionCalls, [['begin', 1], ['name_char', 0, 68]]);
  nameCharResult = 1;
  missionCalls.length = 0;

  /* A refused encounter aborts before commit. */
  encounterResult = 0;
  assert.throws(() => context.HaloOnline.missionSave(validMission()), /encounter/i);
  assert.ok(missionCalls[missionCalls.length - 1][0] !== 'commit',
    'a refused encounter must not reach commit');
  encounterResult = 1;
  missionCalls.length = 0;

  /* A refused commit means no staged draft; nothing was requested. */
  commitResult = 0;
  assert.throws(() => context.HaloOnline.missionSave(validMission()), /staged|save/i);
  commitResult = 1;
  missionCalls.length = 0;

  /* The save is requested, not claimed: the drain disposition arrives
     through the status poll, which resets the draft on any refusal. */
  context.HaloOnline.missionSave(validMission());
  missionCalls.length = 0;
  missionStatus = 3;
  missionRecorded = -1;
  const refused = context.HaloOnline.missionSaveStatus();
  assert.equal(refused.saved, false);
  assert.deepEqual(missionCalls, [['reset']]);
  assert.match(elements['online-status'].textContent, /encounter/i);
  missionCalls.length = 0;

  /* A recorded header matching the pending save is the only saved proof. */
  context.HaloOnline.missionSave(validMission());
  missionCalls.length = 0;
  missionStatus = 0;
  missionRecorded = (1 << 8) | 2;
  const saved = context.HaloOnline.missionSaveStatus();
  assert.equal(saved.saved, true);
  assert.deepEqual(missionCalls, [],
    'a saved proof polls but stages nothing');
  assert.match(elements['online-status'].textContent, /Episode 2.*2 encounters/);
  context.HaloOnline.missionSaveStatus();
  assert.match(elements['online-status'].textContent, /no mission save is pending/i);

  /* Idle status with no recorded header is pending, never saved. */
  context.HaloOnline.missionSave(validMission());
  missionStatus = 0;
  missionRecorded = -1;
  const pending = context.HaloOnline.missionSaveStatus();
  assert.equal(pending.saved, false);
  assert.match(elements['online-status'].textContent, /hasn.t confirmed/i);

  console.log('online_client mission editor tests passed');
})();
