/* global Pebble */
var keys = require('message_keys');

// GitHub Pages deployment is defined in .github/workflows/pages.yml.
var CONFIG_URL = 'https://ndiesslin.github.io/pebble-step-sequencer/';
var savedSettings = {};
var waitingToOpen = false;
var fallbackTimer = null;
var settingsRequestFailed = false;
var settingsRequestAttempts = 0;
var settingsSource = 'phone';
var pendingSave = null;
var reopenAfterSave = false;
var settingsNotice = '';

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
      savedSettings['Pattern' + i] || 0);
  }
  for (var j = 0; j < 2; j++) {
    next['Synth' + j] = validNumber(settings['Synth' + j], 0, 65535,
      savedSettings['Synth' + j] || 0);
  }
  for (var k = 0; k < 2; k++) {
    next['SynthNotes' + k] = validNotes(settings['SynthNotes' + k],
      savedSettings['SynthNotes' + k]);
  }
  next.Bpm = validNumber(settings.Bpm, 60, 240, savedSettings.Bpm || 120);
  next.Transport = validNumber(settings.Transport, 0, 1, savedSettings.Transport || 0);
  return next;
}

function openConfiguration() {
  if (!waitingToOpen) return;
  waitingToOpen = false;
  if (fallbackTimer) clearTimeout(fallbackTimer);
  var separator = CONFIG_URL.indexOf('?') === -1 ? '?' : '&';
  var state = encodeURIComponent(JSON.stringify(savedSettings));
  Pebble.openURL(CONFIG_URL + separator + 'state=' + state +
    '&source=' + (settingsRequestFailed ? 'cached' : settingsSource) +
    '&notice=' + encodeURIComponent(settingsNotice));
}

function reopenEditor(notice) {
  waitingToOpen = true;
  settingsRequestFailed = false;
  settingsSource = 'phone';
  settingsNotice = notice;
  openConfiguration();
}

function finishPendingSave() {
  savedSettings = pendingSave.settings;
  localStorage.setItem('pebbleStudioSettings', JSON.stringify(savedSettings));
  console.log('Pebble Studio settings saved and acknowledged by watch.');
  pendingSave = null;
  if (reopenAfterSave) {
    reopenAfterSave = false;
    reopenEditor('Saved to Pebble.');
  }
}

function retryPendingSave(reason) {
  var save = pendingSave;
  if (!save) return;
  if (save.timer) { clearTimeout(save.timer); save.timer = null; }
  if (save.attempt++ >= 2) {
    console.log('Unable to save Pebble Studio setting ' + save.index + ': ' + (reason || 'acknowledgement timed out'));
    pendingSave = null;
    if (reopenAfterSave) {
      reopenAfterSave = false;
      reopenEditor('Could not confirm the save. Keep Pebble Studio open and try again.');
    }
    return;
  }
  console.log('Retrying Pebble Studio setting ' + save.index + ': ' + (reason || 'acknowledgement timed out'));
  sendPendingSave();
}

function sendPendingSave() {
  var save = pendingSave;
  if (!save) return;
  if (save.index >= save.messages.length) { finishPendingSave(); return; }
  var expectedIndex = save.index;
  var payload = save.messages[save.index];
  payload[keys.SyncId] = save.syncId + save.index;
  Pebble.sendAppMessage(payload, function () {
    if (pendingSave !== save || save.index !== expectedIndex) return;
    save.timer = setTimeout(function () {
      if (pendingSave === save && save.index === expectedIndex) retryPendingSave();
    }, 2500);
  }, function (error) {
    if (pendingSave === save && save.index === expectedIndex) retryPendingSave(JSON.stringify(error));
  });
}

function sendSettings(settings) {
  settings = normalise(settings);
  var messages = [], i, message;
  for (i = 0; i < 4; i++) { message = {}; message[keys['Pattern' + i]] = settings['Pattern' + i]; messages.push(message); }
  for (i = 0; i < 2; i++) { message = {}; message[keys['Synth' + i]] = settings['Synth' + i]; messages.push(message); }
  message = {}; message[keys.Bpm] = settings.Bpm; messages.push(message);
  for (i = 0; i < 2; i++) { message = {}; message[keys['SynthNotes' + i]] = settings['SynthNotes' + i]; messages.push(message); }
  message = {}; message[keys.Transport] = settings.Transport; messages.push(message);
  pendingSave = { settings: settings, messages: messages, index: 0, attempt: 0,
    syncId: Date.now() % 2147480000, timer: null };
  sendPendingSave();
}

Pebble.addEventListener('ready', function () {
  try { savedSettings = JSON.parse(localStorage.getItem('pebbleStudioSettings')) || {}; } catch (e) {}
});

Pebble.addEventListener('showConfiguration', function () {
  waitingToOpen = true;
  // The Pebble mobile configuration bridge does not reliably deliver watch-to-phone
  // replies on every Dev Connection. Open immediately from the safe phone copy;
  // saves still use the acknowledged phone-to-watch protocol below.
  settingsRequestFailed = false;
  settingsSource = 'phone';
  settingsNotice = '';
  savedSettings = normalise(savedSettings);
  openConfiguration();
});

Pebble.addEventListener('appmessage', function (event) {
  var payload = event.payload;
  if (pendingSave && payload[keys.SyncStatus] === 1 &&
      payload[keys.SyncId] === pendingSave.syncId + pendingSave.index) {
    if (pendingSave.timer) clearTimeout(pendingSave.timer);
    pendingSave.timer = null;
    pendingSave.index++;
    pendingSave.attempt = 0;
    sendPendingSave();
    return;
  }
  if (payload[keys.Pattern0] === undefined) return;
  savedSettings = normalise({
    Pattern0: payload[keys.Pattern0], Pattern1: payload[keys.Pattern1],
    Pattern2: payload[keys.Pattern2], Pattern3: payload[keys.Pattern3],
    Synth0: payload[keys.Synth0], Synth1: payload[keys.Synth1],
    SynthNotes0: payload[keys.SynthNotes0], SynthNotes1: payload[keys.SynthNotes1],
    Bpm: payload[keys.Bpm], Transport: payload[keys.Transport]
  });
  localStorage.setItem('pebbleStudioSettings', JSON.stringify(savedSettings));
  openConfiguration();
});

Pebble.addEventListener('webviewclosed', function (event) {
  if (!event.response) return;
  try {
    reopenAfterSave = true;
    sendSettings(JSON.parse(decodeURIComponent(event.response)));
  }
  catch (error) { console.log('Invalid sequencer configuration: ' + error); }
});
