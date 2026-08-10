/* global Pebble */
var keys = require('message_keys');

// Deploy config/index.html (for example with GitHub Pages), then replace this URL.
var CONFIG_URL = 'https://YOUR-GITHUB-USERNAME.github.io/pebble-step-sequencer/config/';
var savedSettings = {};
var waitingToOpen = false;
var fallbackTimer = null;

function validNumber(value, min, max, fallback) {
  value = Number(value);
  return isFinite(value) && value >= min && value <= max ? Math.round(value) : fallback;
}

function normalise(settings) {
  var next = {};
  for (var i = 0; i < 4; i++) {
    next['Pattern' + i] = validNumber(settings['Pattern' + i], 0, 65535,
      savedSettings['Pattern' + i] || 0);
  }
  next.Bpm = validNumber(settings.Bpm, 60, 240, savedSettings.Bpm || 120);
  return next;
}

function openConfiguration() {
  if (!waitingToOpen) return;
  waitingToOpen = false;
  if (fallbackTimer) clearTimeout(fallbackTimer);
  var state = encodeURIComponent(JSON.stringify(savedSettings));
  Pebble.openURL(CONFIG_URL + '?state=' + state);
}

function sendSettings(settings) {
  settings = normalise(settings);
  var message = {};
  for (var i = 0; i < 4; i++) message[keys['Pattern' + i]] = settings['Pattern' + i];
  message[keys.Bpm] = settings.Bpm;
  Pebble.sendAppMessage(message, function () {
    savedSettings = settings;
    localStorage.setItem('pebbleStepsSettings', JSON.stringify(settings));
  }, function (error) {
    console.log('Unable to save sequencer settings: ' + JSON.stringify(error));
  });
}

Pebble.addEventListener('ready', function () {
  try { savedSettings = JSON.parse(localStorage.getItem('pebbleStepsSettings')) || {}; } catch (e) {}
});

Pebble.addEventListener('showConfiguration', function () {
  waitingToOpen = true;
  Pebble.sendAppMessage((function () {
    var message = {}; message[keys.RequestSettings] = 1; return message;
  }()));
  // Still open from the phone's cached state if the watch is disconnected.
  fallbackTimer = setTimeout(openConfiguration, 1200);
});

Pebble.addEventListener('appmessage', function (event) {
  var payload = event.payload;
  if (payload[keys.Pattern0] === undefined) return;
  savedSettings = normalise({
    Pattern0: payload[keys.Pattern0], Pattern1: payload[keys.Pattern1],
    Pattern2: payload[keys.Pattern2], Pattern3: payload[keys.Pattern3], Bpm: payload[keys.Bpm]
  });
  localStorage.setItem('pebbleStepsSettings', JSON.stringify(savedSettings));
  openConfiguration();
});

Pebble.addEventListener('webviewclosed', function (event) {
  if (!event.response) return;
  try { sendSettings(JSON.parse(decodeURIComponent(event.response))); }
  catch (error) { console.log('Invalid sequencer configuration: ' + error); }
});
