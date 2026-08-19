/* Lightweight contract checks for the phone-editor/watch protocol. */
var fs = require('fs');
var assert = require('assert');
var pkjs = fs.readFileSync('src/pkjs/index.js', 'utf8');
var nativeCode = fs.readFileSync('src/c/main.c', 'utf8');
var config = fs.readFileSync('config/index.html', 'utf8');

assert(/SyncId/.test(pkjs) && /SyncStatus/.test(pkjs), 'phone saves must await watch acknowledgements');
assert(/settingsRequestAttempts/.test(pkjs) && /requestWatchSettings/.test(pkjs), 'phone should retry live watch settings');
assert(/dict_write_uint16\(iter, MESSAGE_KEY_Pattern0/.test(nativeCode), 'watch reply must include drum patterns');
assert(!/settingsRequestPart/.test(pkjs), 'opening Settings should not wait for a second state round-trip');
assert(/cachedState/.test(config) && /disabled = true/.test(config), 'cached editor must be view-only');
assert(/Four on the floor/.test(config) && /Clear all notes/.test(config), 'editor should offer presets and clearing');
assert(config.indexOf('innerHTML') === -1, 'editor should use textContent for text updates');
console.log('Phone/editor protocol checks passed.');
