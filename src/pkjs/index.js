/* global Pebble */
var keys = require('message_keys');

// GitHub Pages deployment is defined in .github/workflows/pages.yml.
var CONFIG_URL = 'https://ndiesslin.github.io/pebble-step-sequencer/';
var savedSettings = {};
var waitingToOpen = false;
var pendingSave = null;

var defaultSettings = {
  Pattern0: 0x1111, Pattern1: 0x2222, Pattern2: 0x4444, Pattern3: 0x8888,
  Synth0: 0x1111, Synth1: 0x8421,
  SynthNotes0: '0000000000000000', SynthNotes1: '0000000000000000',
  Bpm: 120, Transport: 0
};

function validNumber(value, min, max, fallback) {
  value = Number(value);
  return isFinite(value) && value >= min && value <= max ? Math.round(value) : fallback;
}

function validNotes(value, fallback) {
  value = String(value === undefined ? fallback : value);
  return /^[0-6]{16}$/.test(value) ? value : (fallback || '0000000000000000');
}

function normalise(settings) {
  var next = {};
  for (var i = 0; i < 4; i++) {
    next['Pattern' + i] = validNumber(settings['Pattern' + i], 0, 65535,
      savedSettings['Pattern' + i] === undefined ? defaultSettings['Pattern' + i] : savedSettings['Pattern' + i]);
  }
  for (var j = 0; j < 2; j++) {
    next['Synth' + j] = validNumber(settings['Synth' + j], 0, 65535,
      savedSettings['Synth' + j] === undefined ? defaultSettings['Synth' + j] : savedSettings['Synth' + j]);
  }
  for (var k = 0; k < 2; k++) {
    next['SynthNotes' + k] = validNotes(settings['SynthNotes' + k],
      savedSettings['SynthNotes' + k] === undefined ? defaultSettings['SynthNotes' + k] : savedSettings['SynthNotes' + k]);
  }
  next.Bpm = validNumber(settings.Bpm, 60, 240, savedSettings.Bpm === undefined ? defaultSettings.Bpm : savedSettings.Bpm);
  next.Transport = validNumber(settings.Transport, 0, 1, savedSettings.Transport === undefined ? defaultSettings.Transport : savedSettings.Transport);
  return next;
}

function openConfiguration() {
  if (!waitingToOpen) return;
  waitingToOpen = false;
  var separator = CONFIG_URL.indexOf('?') === -1 ? '?' : '&';
  var state = encodeURIComponent(JSON.stringify(savedSettings));
  Pebble.openURL(CONFIG_URL + separator + 'state=' + state +
    '&source=phone&notice=');
}

function sendSettings(settings) {
  var shouldSendTransport = Object.prototype.hasOwnProperty.call(settings, 'Transport');
  settings = normalise(settings);
  var messages = [], i, message;
  for (i = 0; i < 4; i++) { message = {}; message[keys['Pattern' + i]] = settings['Pattern' + i]; messages.push(message); }
  for (i = 0; i < 2; i++) { message = {}; message[keys['Synth' + i]] = settings['Synth' + i]; messages.push(message); }
  message = {}; message[keys.Bpm] = settings.Bpm; messages.push(message);
  for (i = 0; i < 2; i++) { message = {}; message[keys['SynthNotes' + i]] = settings['SynthNotes' + i]; messages.push(message); }
  if (shouldSendTransport) { message = {}; message[keys.Transport] = settings.Transport; messages.push(message); }
  pendingSave = {
    settings: settings,
    messages: messages,
    index: 0,
    attempt: 0,
    syncBase: Date.now() % 2000000000,
    ackTimer: null
  };
  sendPendingSave();
}

function finishPendingSave() {
  var save = pendingSave;
  if (!save) return;
  if (save.ackTimer) clearTimeout(save.ackTimer);
  savedSettings = save.settings;
  localStorage.setItem('pebbleStudioSettings', JSON.stringify(savedSettings));
  pendingSave = null;
  console.log('Pebble Studio settings saved on watch.');
}

function retryPendingSave(reason) {
  var save = pendingSave;
  if (!save) return;
  if (save.ackTimer) clearTimeout(save.ackTimer);
  if (save.attempt++ < 2) {
    console.log('Retrying Pebble Studio setting ' + save.index + ': ' + reason);
    setTimeout(sendPendingSave, 200);
  } else {
    console.log('Unable to save Pebble Studio setting ' + save.index + ': ' + reason);
    pendingSave = null;
  }
}

function sendPendingSave() {
  var save = pendingSave;
  if (!save) return;
  if (save.index >= save.messages.length) {
    finishPendingSave();
    return;
  }
  var payload = save.messages[save.index];
  var expectedIndex = save.index;
  payload[keys.SyncId] = save.syncBase + expectedIndex;
  Pebble.sendAppMessage(payload, function () {
    if (pendingSave !== save || save.index !== expectedIndex) return;
    save.ackTimer = setTimeout(function () {
      if (pendingSave === save) retryPendingSave('watch acknowledgement timed out');
    }, 2000);
  }, function (error) {
    if (pendingSave === save) retryPendingSave(JSON.stringify(error));
  });
}

Pebble.addEventListener('ready', function () {
  try { savedSettings = JSON.parse(localStorage.getItem('pebbleStudioSettings')) || {}; } catch (e) {}
});

Pebble.addEventListener('showConfiguration', function () {
  waitingToOpen = true;
  // The Pebble mobile configuration bridge does not reliably deliver watch-to-phone
  // replies on every Dev Connection. Open immediately from the safe phone copy;
  // saves still use the acknowledged phone-to-watch protocol below.
  savedSettings = normalise(savedSettings);
  openConfiguration();
});

Pebble.addEventListener('appmessage', function (event) {
  var payload = event.payload;
  var save = pendingSave;
  if (!save || payload[keys.SyncStatus] !== 1 ||
      payload[keys.SyncId] !== save.syncBase + save.index) return;
  if (save.ackTimer) clearTimeout(save.ackTimer);
  save.index++;
  save.attempt = 0;
  setTimeout(sendPendingSave, 0);
});

Pebble.addEventListener('webviewclosed', function (event) {
  if (!event.response) return;
  try {
    sendSettings(JSON.parse(decodeURIComponent(event.response)));
  }
  catch (error) { console.log('Invalid sequencer configuration: ' + error); }
});
