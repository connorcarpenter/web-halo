'use strict';

/* Hazard Run campaign-session surface: create a four-seat campaign room and
show the actual admitted roster. Multiplayer paths are pinned unchanged. */

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
  'online-join-status', 'online-join-summary', 'online-leave-host',
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
elements['online-join-confirm'].hidden = true;
elements['online-invite'].hidden = true;
elements['online-progress'].hidden = true;

const storage = new Map([
  ['halo.web.player-profile.v1', JSON.stringify({ name: 'TestSpartan', style: 'rose' })],
]);
const configuredHosts = [];
const campaignHosts = [];
const legacyCommands = [];
const socketMessages = [];
const intervals = new Map();
let nextTimer = 1;
let gameState = 2;
let fetchCount = 0;
let latestSocket = null;

class FakeWebSocket {
  static OPEN = 1;

  constructor() {
    this.readyState = FakeWebSocket.OPEN;
    latestSocket = this;
    queueMicrotask(() => this.onopen && this.onopen());
  }

  close() { this.readyState = 3; }
  send(value) { socketMessages.push(JSON.parse(value)); }
}

const context = {
  console,
  document: {
    readyState: 'complete',
    getElementById: id => elements[id],
    querySelector: selector => selector === 'meta[name="halo-build-id"]' ?
      { content: 'test-build' } :
      selector === 'meta[name="halo-signaling-url"]' ?
        { content: 'https://signal.example' } : null,
    execCommand: () => true,
  },
  fetch: async () => {
    fetchCount++;
    return {
      ok: true,
      async json() {
        return {
          v: 1,
          room: { id: 'room' },
          host: {
            ticket: '1234567890abcdef',
            session: {
              peerId: 'h_0123456789abcdef',
              websocketUrl: 'wss://signal.example/v1/socket',
            },
          },
          invite: {
            code: 'room.1234567890abcdef',
            url: 'https://canonical.example/#join=room.1234567890abcdef',
          },
          iceServers: [],
        };
      },
    };
  },
  HaloWebTransport: {
    configure() {},
    disconnectAll() {},
    getLocalIdentifier: () => '020000000001',
    isSupported: () => true,
  },
  history: { replaceState() {} },
  localStorage: {
    getItem: key => storage.has(key) ? storage.get(key) : null,
    setItem: (key, value) => storage.set(key, value),
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
    _platform_web_online_get_state: () => gameState,
    _platform_web_online_campaign_host_configured: (episodeIndex, seatCount) => {
      campaignHosts.push([episodeIndex, seatCount]);
      return 1;
    },
    _platform_web_online_host_configured: (mapIndex, modeIndex) => {
      configuredHosts.push([mapIndex, modeIndex]);
      return 1;
    },
    _platform_web_online_request: command => {
      legacyCommands.push(command);
      return 1;
    },
    _platform_web_online_set_player_customization: () => 1,
    _platform_web_online_set_transport_state() {},
  },
  navigator: {},
  URL,
  URLSearchParams,
  WebSocket: FakeWebSocket,
  clearInterval: id => intervals.delete(id),
  clearTimeout() {},
  setInterval: (callback, milliseconds) => {
    const id = nextTimer++;
    intervals.set(id, { callback, milliseconds });
    return id;
  },
  setTimeout: () => nextTimer++,
};
context.window = context;
context.globalThis = context;
vm.createContext(context);
vm.runInContext(
  fs.readFileSync(path.join(webDirectory, 'online_client.js'), 'utf8'),
  context,
  { filename: 'online_client.js' });

(async () => {
  context.HaloOnline.runtimeReady();

  /* Invalid campaign settings never reach the room service or the game. */
  await assert.rejects(
    context.HaloOnline.campaignHost({ episodeIndex: 0, seatCount: 0 }),
    /seat/i);
  await assert.rejects(
    context.HaloOnline.campaignHost({ episodeIndex: 0, seatCount: 5 }),
    /seat/i);
  await assert.rejects(
    context.HaloOnline.campaignHost({ episodeIndex: -1, seatCount: 4 }),
    /episode/i);
  await assert.rejects(
    context.HaloOnline.campaignHost({ episodeIndex: 256, seatCount: 4 }),
    /episode/i);
  assert.equal(fetchCount, 0);
  assert.deepEqual(campaignHosts, []);
  assert.deepEqual(legacyCommands, []);

  /* A four-seat Hazard Run room opens through the room protocol. */
  await context.HaloOnline.campaignHost({ episodeIndex: 2, seatCount: 4 });
  assert.deepEqual(campaignHosts, [[2, 4]]);
  assert.deepEqual(configuredHosts, [],
    'campaign hosting must not pass through the multiplayer host ABI');
  assert.deepEqual(legacyCommands, []);
  assert.equal(elements['online-invite'].hidden, false);
  assert.equal(elements['player-count'].textContent, '1/4 seats');
  assert.equal(elements['player-count']['aria-label'],
    'Players in campaign session: 1 of 4 seats');

  /* The roster shows actual admitted peers against the four-seat contract. */
  latestSocket.onmessage({
    data: JSON.stringify({
      v: 1,
      type: 'roster',
      players: [
        {
          peerId: 'h_0123456789abcdef',
          role: 'host',
          profile: { name: 'TestSpartan', style: 'rose' },
        },
        {
          peerId: 'g_fedcba9876543210',
          role: 'guest',
          profile: { name: 'Friend', style: 'pink' },
        },
        {
          peerId: 'g_0123456789abcdef',
          role: 'guest',
          profile: { name: 'Third', style: 'blue' },
        },
      ],
    }),
  });
  await Promise.resolve();
  await Promise.resolve();
  assert.equal(elements['player-count'].textContent, '3/4 seats');
  assert.equal(elements['player-count']['aria-label'],
    'Players in campaign session: 3 of 4 seats');

  gameState = 3;
  const gamePoll = Array.from(intervals.values()).find(
    timer => timer.milliseconds === 200);
  assert(gamePoll, 'game-state polling was not started');
  gamePoll.callback();
  assert.match(elements['online-status'].textContent,
    /Episode 3 · 4 seats is ready/);

  /* Leaving a campaign room restores the idle roster; MP hosting is intact. */
  await context.HaloOnline.leave();
  assert.equal(elements['player-count'].textContent, '0/128');
  await context.HaloOnline.campaignHost();
  assert.deepEqual(campaignHosts, [[2, 4], [0, 1]],
    'a bare campaign host opens the solo default without touching MP state');
  await context.HaloOnline.leave();

  console.log('online_client campaign setup tests passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
