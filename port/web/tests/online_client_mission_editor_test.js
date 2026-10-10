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

function matchesSelector(node, selector) {
  const attribute = selector.match(/^([a-z]*)?\[name="([^"]+)"\]$/);
  if (attribute) {
    if (attribute[1] && node.tagName !== attribute[1].toUpperCase()) return false;
    return node.name === attribute[2];
  }
  if (selector[0] === '.') {
    return (node.className || '').split(' ').includes(selector.slice(1));
  }
  return node.tagName === selector.toUpperCase();
}

function selectAll(root, selector) {
  const found = [];
  (function walk(node) {
    for (const child of node.children) {
      if (matchesSelector(child, selector)) found.push(child);
      walk(child);
    }
  })(root);
  return found;
}

function element(overrides) {
  const listeners = {};
  const node = Object.assign({
    tagName: 'DIV',
    className: '',
    dataset: {},
    disabled: false,
    hidden: false,
    open: false,
    options: [],
    value: '',
    textContent: '',
    children: [],
    parentNode: null,
    addEventListener(type, listener) { listeners[type] = listener; },
    appendChild(child) {
      child.parentNode = node;
      node.children.push(child);
      return child;
    },
    removeChild(child) {
      node.children = node.children.filter(candidate => candidate !== child);
      child.parentNode = null;
      return child;
    },
    remove() {
      if (node.parentNode) node.parentNode.removeChild(node);
    },
    querySelector(selector) { return selectAll(node, selector)[0] || null; },
    querySelectorAll(selector) { return selectAll(node, selector); },
    close() { this.open = false; },
    focus() { this.focused = true; },
    removeAttribute(name) { delete this[name]; },
    select() {},
    setAttribute(name, value) { this[name] = String(value); },
    showModal() { this.open = true; },
    listeners,
  }, overrides || {});
  Object.defineProperty(node, 'firstChild', {
    get() { return node.children[0] || null; },
    configurable: true,
  });
  return node;
}

function createElement(tag) {
  return element({ tagName: String(tag).toUpperCase() });
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
  'online-mission-add-encounter', 'online-mission-editor',
  'online-mission-encounters', 'online-mission-episode', 'online-mission-name',
  'online-mission-save', 'online-mode', 'online-mode-back', 'online-mode-options',
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
const authorPollTimers = [];
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
    createElement,
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
  setTimeout: callback => {
    authorPollTimers.push(callback);
    return authorPollTimers.length;
  },
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

(function () {
  const encounters = () => elements['online-mission-encounters']
    .querySelectorAll('.mission-encounter');
  const field = (row, name) => row.querySelector('[name="' + name + '"]');
  const click = id => elements[id].listeners.click({ target: elements[id] });
  function fillRow(row, volume, actor, count, anchor) {
    field(row, 'mission-volume').value = String(volume);
    field(row, 'mission-actor').value = String(actor);
    field(row, 'mission-count').value = String(count);
    field(row, 'mission-anchor').value = String(anchor);
  }

  /* The editor opens with one default encounter row at canonical defaults. */
  assert.equal(encounters().length, 1);
  assert.equal(field(encounters()[0], 'mission-volume').value, '0');
  assert.equal(field(encounters()[0], 'mission-actor').value, '0');
  assert.equal(field(encounters()[0], 'mission-count').value, '1');
  assert.equal(field(encounters()[0], 'mission-anchor').value, '-1');

  /* Rows add up to the eight-encounter cap, then the add stops. */
  for (let added = 0; added < 7; added++) click('online-mission-add-encounter');
  assert.equal(encounters().length, 8);
  click('online-mission-add-encounter');
  assert.equal(encounters().length, 8);
  assert.equal(elements['online-mission-add-encounter'].disabled, true);

  /* Removing a row drops it and re-enables adding. */
  const removeButtons = encounters()[7].querySelectorAll('.mission-encounter-remove');
  assert.equal(removeButtons.length, 1);
  elements['online-mission-encounters'].listeners.click({ target: removeButtons[0] });
  assert.equal(encounters().length, 7);
  assert.equal(elements['online-mission-add-encounter'].disabled, false);

  /* A filled editor saves exactly through the staged bridge. */
  missionCalls.length = 0;
  elements['online-mission-name'].value = 'Deck Breach';
  elements['online-mission-episode'].value = '1';
  fillRow(encounters()[0], 4, 0, 3, -1);
  fillRow(encounters()[1], 9, 10, 16, 2);
  for (let extra = 2; extra < 7; extra++) fillRow(encounters()[extra], extra, 2, 1, -1);
  click('online-mission-save');
  const staged = missionCalls.filter(call => call[0] !== 'reset');
  assert.equal(staged[0][0], 'begin');
  assert.equal(staged[staged.length - 1][0], 'commit');
  assert.equal(staged.filter(call => call[0] === 'encounter').length, 7);
  assert.match(elements['online-status'].textContent, /Deck Breach/);

  /* A refused field reports the refusal and preserves every staged edit. */
  missionCalls.length = 0;
  field(encounters()[0], 'mission-count').value = '99';
  click('online-mission-save');
  assert.deepEqual(missionCalls, [],
    'an invalid editor field must not reach the bridge');
  assert.match(elements['online-status'].textContent, /count/i);
  assert.equal(elements['online-mission-name'].value, 'Deck Breach');
  assert.equal(field(encounters()[0], 'mission-count').value, '99');
  assert.equal(encounters().length, 7);

  console.log('online_client mission editor DOM tests passed');
})();

(function () {
  /* Hazard Run editor roundtrip (Usher 61730): a requested save closes its
     own loop. The drain confirms on the game thread, so the browser must
     re-poll to terminal by itself: saved (recorded header matches the
     staged episode + encounter count), refused (draft reset, disposition
     reported), or bounded expiry (pending kept for a manual check). A
     single immediate poll is not the roundtrip. */
  function drainAuthorTimers() {
    let ran = 0;
    while (authorPollTimers.length) {
      authorPollTimers.shift()();
      ran++;
    }
    return ran;
  }

  function requestSave() {
    authorPollTimers.length = 0;
    missionCalls.length = 0;
    missionStatus = 0;
    missionRecorded = -1;
    context.HaloOnline.missionSave(validMission());
  }

  /* A requested save schedules its own author poll. */
  requestSave();
  assert.equal(authorPollTimers.length, 1,
    'a requested save must schedule its own author poll');
  const immediate = context.HaloOnline.missionSaveStatus();
  assert.equal(immediate.pending, true);

  /* One tick while the drain is silent stays pending and rearms. */
  authorPollTimers.shift()();
  assert.equal(authorPollTimers.length, 1,
    'a silent drain must leave the loop armed');

  /* The recorded header matching the staged save closes the loop with the
     staged identity, and nothing stays armed. */
  missionRecorded = (1 << 8) | 2;
  drainAuthorTimers();
  assert.equal(authorPollTimers.length, 0,
    'a terminal save must disarm the loop');
  assert.match(elements['online-status'].textContent, /Deck Breach.*Episode 2.*2 encounters/);
  const closed = context.HaloOnline.missionSaveStatus();
  assert.equal(closed.pending, false);
  assert.match(elements['online-status'].textContent, /no mission save is pending/i);

  /* A refusal mid-sequence resets the draft and closes the loop. */
  requestSave();
  missionStatus = 3;
  drainAuthorTimers();
  assert.equal(authorPollTimers.length, 0,
    'a refused save must disarm the loop');
  assert.deepEqual(missionCalls.filter(call => call[0] === 'reset').length, 1);
  assert.match(elements['online-status'].textContent, /encounter/i);

  /* A stale timer from an overwritten save polls nothing. */
  requestSave();
  context.HaloOnline.missionSave(validMission());
  assert.equal(authorPollTimers.length, 2);
  missionRecorded = (1 << 8) | 2;
  authorPollTimers.shift()();
  assert.equal(authorPollTimers.length, 1,
    'a stale save timer must not close the newer save');
  assert.doesNotMatch(elements['online-status'].textContent, /Mission saved/);
  drainAuthorTimers();
  assert.match(elements['online-status'].textContent, /Deck Breach.*Episode 2.*2 encounters/);

  /* A silent drain exhausts the bound (450 ticks of 200 ms = 90 s), stops
     re-arming, and keeps the save pending for a manual check. */
  requestSave();
  const ran = drainAuthorTimers();
  assert.equal(ran, 450);
  assert.equal(authorPollTimers.length, 0);
  const expired = context.HaloOnline.missionSaveStatus();
  assert.equal(expired.pending, true);
  assert.match(elements['online-status'].textContent, /hasn.t confirmed/i);

  console.log('online_client mission editor roundtrip tests passed');
})();

(function () {
  /* Hazard Run saved-receipt binding (Finn 61813): the terminal saved
     receipt is retained for the future activated-episode readback, which
     compares against exactly this shape. A new save request supersedes
     it; a refusal leaves the last proven receipt standing. */
  missionCalls.length = 0;
  missionStatus = 0;
  missionRecorded = -1;
  authorPollTimers.length = 0;

  /* Receipts cross the vm-context boundary, so compare field-wise:
     deepEqual sees two realms' prototypes, not the values. */
  function receiptEqual(actual, expected) {
    if (expected === null) return actual === null;
    return !!actual && actual.name === expected.name &&
      actual.episodeIndex === expected.episodeIndex &&
      actual.encounterCount === expected.encounterCount;
  }

  /* Earlier blocks share this session, so the entry receipt is whatever
     they last proved: null or a well-shaped receipt, never undefined. */
  const entry = context.HaloOnline.missionLastSaved();
  assert.ok(entry === null || receiptEqual(entry, {
    name: entry.name,
    episodeIndex: entry.episodeIndex,
    encounterCount: entry.encounterCount,
  }));

  context.HaloOnline.missionSave(validMission());
  assert.ok(receiptEqual(context.HaloOnline.missionLastSaved(), entry),
    'a requested save must not move the retained receipt');
  missionRecorded = (1 << 8) | 2;
  while (authorPollTimers.length) authorPollTimers.shift()();
  assert.ok(receiptEqual(context.HaloOnline.missionLastSaved(), {
    name: 'Deck Breach',
    episodeIndex: 1,
    encounterCount: 2,
  }));

  /* A refused save keeps the last proven receipt: nothing was written. A
     new request does not supersede it either; only a terminal save does. */
  const proven = context.HaloOnline.missionLastSaved();
  context.HaloOnline.missionSave(validMission());
  assert.ok(receiptEqual(context.HaloOnline.missionLastSaved(), proven),
    'a new request must not supersede the last proven receipt');
  missionStatus = 3;
  while (authorPollTimers.length) authorPollTimers.shift()();
  missionStatus = 0;
  context.HaloOnline.missionSave(validMission());
  missionRecorded = (1 << 8) | 2;
  while (authorPollTimers.length) authorPollTimers.shift()();
  const first = context.HaloOnline.missionLastSaved();
  context.HaloOnline.missionSave(validMission());
  missionStatus = 5;
  while (authorPollTimers.length) authorPollTimers.shift()();
  missionStatus = 0;
  assert.ok(receiptEqual(context.HaloOnline.missionLastSaved(), first),
    'a refused save must not move the retained receipt');

  console.log('online_client mission saved-receipt tests passed');
})();
