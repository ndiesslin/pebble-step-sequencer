/* Lightweight contract checks for the phone-editor/watch protocol. */
var fs = require('fs');
var assert = require('assert');
var pkjs = fs.readFileSync('src/pkjs/index.js', 'utf8');
var nativeCode = fs.readFileSync('src/c/main.c', 'utf8');
var config = fs.readFileSync('config/index.html', 'utf8');

assert(/SyncId/.test(pkjs) && /SyncStatus/.test(pkjs), 'phone saves must await watch acknowledgements');
assert(/dict_write_cstring\(iter, MESSAGE_KEY_SynthNotes0/.test(nativeCode), 'watch reply must include bass pitches');
assert(/dict_write_cstring\(iter, MESSAGE_KEY_SynthNotes1/.test(nativeCode), 'watch reply must include lead pitches');
assert(/cachedState/.test(config) && /disabled = true/.test(config), 'cached editor must be view-only');
assert(/Four on the floor/.test(config) && /Clear all notes/.test(config), 'editor should offer presets and clearing');
assert(config.indexOf('innerHTML') === -1, 'editor should use textContent for text updates');
console.log('Phone/editor protocol checks passed.');
