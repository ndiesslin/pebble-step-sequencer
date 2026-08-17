/* global Pebble */
var keys = require('message_keys');

// GitHub Pages deployment is defined in .github/workflows/pages.yml.
var CONFIG_URL = 'https://ndiesslin.github.io/pebble-step-sequencer/';
var savedSettings = {};
var waitingToOpen = false;
var fallbackTimer = null;
var settingsRequestFailed = false;

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
    '&source=' + (settingsRequestFailed ? 'cached' : 'watch'));
}

function sendSettings(settings) {
  settings = normalise(settings);
  var messages = [], i, message;
  for (i = 0; i < 4; i++) { message = {}; message[keys['Pattern' + i]] = settings['Pattern' + i]; messages.push(message); }
  for (i = 0; i < 2; i++) { message = {}; message[keys['Synth' + i]] = settings['Synth' + i]; messages.push(message); }
  message = {}; message[keys.Bpm] = settings.Bpm; messages.push(message);
  message = {}; message[keys.Transport] = settings.Transport; messages.push(message);
  for (i = 0; i < 2; i++) { message = {}; message[keys['SynthNotes' + i]] = settings['SynthNotes' + i]; messages.push(message); }
  function remember() {
    savedSettings = settings;
    localStorage.setItem('pebbleStudioSettings', JSON.stringify(settings));
  }
  function sendNext(index) {
    if (index >= messages.length) { remember(); return; }
    Pebble.sendAppMessage(messages[index], function () { sendNext(index + 1); }, function (error) {
      console.log('Unable to save sequencer setting ' + index + ': ' + JSON.stringify(error));
    });
  }
  sendNext(0);
}

Pebble.addEventListener('ready', function () {
  try { savedSettings = JSON.parse(localStorage.getItem('pebbleStudioSettings')) || {}; } catch (e) {}
});

Pebble.addEventListener('showConfiguration', function () {
  waitingToOpen = true;
  settingsRequestFailed = false;
  var request = (function () {
    var message = {}; message[keys.RequestSettings] = 1; return message;
  }());
  Pebble.sendAppMessage(request, function () {
    // Wait for the watch's state reply; use cache only if it does not arrive.
    fallbackTimer = setTimeout(function () {
      settingsRequestFailed = true;
      openConfiguration();
    }, 3000);
  }, function (error) {
    console.log('Unable to request watch settings: ' + JSON.stringify(error));
    settingsRequestFailed = true;
    openConfiguration();
  });
});

Pebble.addEventListener('appmessage', function (event) {
  var payload = event.payload;
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
  try { sendSettings(JSON.parse(decodeURIComponent(event.response))); }
  catch (error) { console.log('Invalid sequencer configuration: ' + error); }
});
