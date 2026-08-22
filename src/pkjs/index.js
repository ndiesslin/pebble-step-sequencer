/* global Pebble */
var keys = require('message_keys');

// GitHub Pages deployment is defined in .github/workflows/pages.yml.
var CONFIG_URL = 'https://ndiesslin.github.io/pebble-studio/';
var savedSettings = {};
var waitingToOpen = false;

var defaultSettings = {
  Pattern0: 0x1111, Pattern1: 0x2222, Pattern2: 0x4444, Pattern3: 0x8888,
  Synth0: 0x1111, Synth1: 0x8421,
  SynthNotes0: '0000000000000000', SynthNotes1: '0000000000000000',
  Bpm: 120, Volume: 90, Drive: 0, Space: 0,
  BassAttack: 10, BassDecay: 50, LeadAttack: 5, LeadDecay: 65,
  DriveTargets: 7, SpaceTargets: 7, Transport: 0
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
  next.Volume = validNumber(settings.Volume, 0, 100, savedSettings.Volume === undefined ? defaultSettings.Volume : savedSettings.Volume);
  next.Drive = validNumber(settings.Drive, 0, 100, savedSettings.Drive === undefined ? defaultSettings.Drive : savedSettings.Drive);
  next.Space = validNumber(settings.Space, 0, 100, savedSettings.Space === undefined ? defaultSettings.Space : savedSettings.Space);
  next.BassAttack = validNumber(settings.BassAttack, 0, 100, savedSettings.BassAttack === undefined ? defaultSettings.BassAttack : savedSettings.BassAttack);
  next.BassDecay = validNumber(settings.BassDecay, 0, 100, savedSettings.BassDecay === undefined ? defaultSettings.BassDecay : savedSettings.BassDecay);
  next.LeadAttack = validNumber(settings.LeadAttack, 0, 100, savedSettings.LeadAttack === undefined ? defaultSettings.LeadAttack : savedSettings.LeadAttack);
  next.LeadDecay = validNumber(settings.LeadDecay, 0, 100, savedSettings.LeadDecay === undefined ? defaultSettings.LeadDecay : savedSettings.LeadDecay);
  next.DriveTargets = validNumber(settings.DriveTargets, 0, 7, savedSettings.DriveTargets === undefined ? defaultSettings.DriveTargets : savedSettings.DriveTargets);
  next.SpaceTargets = validNumber(settings.SpaceTargets, 0, 7, savedSettings.SpaceTargets === undefined ? defaultSettings.SpaceTargets : savedSettings.SpaceTargets);
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
  message = {}; message[keys.Volume] = settings.Volume; messages.push(message);
  message = {}; message[keys.Drive] = settings.Drive; messages.push(message);
  message = {}; message[keys.Space] = settings.Space; messages.push(message);
  message = {}; message[keys.BassAttack] = settings.BassAttack; messages.push(message);
  message = {}; message[keys.BassDecay] = settings.BassDecay; messages.push(message);
  message = {}; message[keys.LeadAttack] = settings.LeadAttack; messages.push(message);
  message = {}; message[keys.LeadDecay] = settings.LeadDecay; messages.push(message);
  message = {}; message[keys.DriveTargets] = settings.DriveTargets; messages.push(message);
  message = {}; message[keys.SpaceTargets] = settings.SpaceTargets; messages.push(message);
  for (i = 0; i < 2; i++) { message = {}; message[keys['SynthNotes' + i]] = settings['SynthNotes' + i]; messages.push(message); }
  if (shouldSendTransport) { message = {}; message[keys.Transport] = settings.Transport; messages.push(message); }
  function sendNext(index, attempt) {
    if (index >= messages.length) {
      savedSettings = settings;
      localStorage.setItem('pebbleStudioSettings', JSON.stringify(savedSettings));
      console.log('Pebble Studio settings sent to watch.');
      return;
    }
    Pebble.sendAppMessage(messages[index], function () {
      // The physical Dev Connection can drop watch-to-phone replies, so pace writes by delivery.
      setTimeout(function () { sendNext(index + 1, 0); }, 180);
    }, function (error) {
      if (attempt < 2) {
        console.log('Retrying Pebble Studio setting ' + index + ': ' + JSON.stringify(error));
        setTimeout(function () { sendNext(index, attempt + 1); }, 250);
      } else {
        console.log('Unable to save Pebble Studio setting ' + index + ': ' + JSON.stringify(error));
      }
    });
  }
  sendNext(0, 0);
}

Pebble.addEventListener('ready', function () {
  try { savedSettings = JSON.parse(localStorage.getItem('pebbleStudioSettings')) || {}; } catch (e) {}
});

Pebble.addEventListener('showConfiguration', function () {
  waitingToOpen = true;
  // The Pebble mobile configuration bridge does not reliably deliver watch-to-phone
  // replies on every Dev Connection. Open immediately from the safe phone copy;
  // saves use paced one-way writes because this bridge can drop watch-to-phone replies.
  savedSettings = normalise(savedSettings);
  openConfiguration();
});

Pebble.addEventListener('webviewclosed', function (event) {
  if (!event.response) return;
  try {
    sendSettings(JSON.parse(decodeURIComponent(event.response)));
  }
  catch (error) { console.log('Invalid sequencer configuration: ' + error); }
});
