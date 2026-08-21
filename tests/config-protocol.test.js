/* Lightweight contract checks for the phone-editor/watch protocol. */
var fs = require('fs');
var assert = require('assert');
var pkjs = fs.readFileSync('src/pkjs/index.js', 'utf8');
var nativeCode = fs.readFileSync('src/c/main.c', 'utf8');
var config = fs.readFileSync('config/index.html', 'utf8');

assert(/sendNext\(index, attempt\)/.test(pkjs) && /120/.test(pkjs), 'phone should pace every saved field instead of blocking on an acknowledgement');
assert(/shouldSendTransport/.test(pkjs) && /transportTouched/.test(config), 'normal saves should preserve watch playback state');
assert(/settingsSource = 'phone'/.test(pkjs), 'Settings should open immediately from the saved phone copy');
assert(/defaultSettings/.test(pkjs), 'phone editor should start with the app defaults rather than blank patterns');
assert(/Save &amp; Close/.test(config), 'editor should offer one explicit close-to-save action');
assert(/PCM underrun/.test(nativeCode) && /voice \* 5 \/ 2/.test(nativeCode), 'audio should log real underruns and use per-voice drum gains');
assert(/MIX_BUFFER_SAMPLES 160/.test(nativeCode) && /MIX_PUMP_INTERVAL_MS 5/.test(nativeCode), 'audio should retain its proven full-quality stream block size');
assert(/timing-sensitive grid animation/.test(nativeCode) && !/s_playhead/.test(nativeCode), 'playback should use a simple static status indicator');
assert(/animate_playback_indicator/.test(nativeCode) && /app_timer_register\(250/.test(nativeCode), 'playback should include a lightweight visual-only line animation');
assert(/dict_write_uint16\(iter, MESSAGE_KEY_Pattern0/.test(nativeCode), 'watch reply must include drum patterns');
assert(!/settingsRequestPart/.test(pkjs), 'opening Settings should not wait for a second state round-trip');
assert(/cachedState/.test(config) && /disabled = true/.test(config), 'cached editor must be view-only');
assert(/Four on the floor/.test(config) && /Clear all notes/.test(config), 'editor should offer presets and clearing');
assert(config.indexOf('innerHTML') === -1, 'editor should use textContent for text updates');
console.log('Phone/editor protocol checks passed.');
