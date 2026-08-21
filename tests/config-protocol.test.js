/* Lightweight contract checks for the phone-editor/watch protocol. */
var fs = require('fs');
var assert = require('assert');
var pkjs = fs.readFileSync('src/pkjs/index.js', 'utf8');
var nativeCode = fs.readFileSync('src/c/main.c', 'utf8');
var config = fs.readFileSync('config/index.html', 'utf8');

assert(/sendPendingSave/.test(pkjs) && /watch acknowledgement timed out/.test(pkjs), 'phone should wait for each watch acknowledgement before sending the next field');
assert(/shouldSendTransport/.test(pkjs) && /transportTouched/.test(config), 'normal saves should preserve watch playback state');
assert(/&source=phone&notice=/.test(pkjs), 'Settings should open immediately from the saved phone copy');
assert(/defaultSettings/.test(pkjs), 'phone editor should start with the app defaults rather than blank patterns');
assert(/Save &amp; Close/.test(config), 'editor should offer one explicit close-to-save action');
assert(/PCM underrun/.test(nativeCode) && /voice \* 5 \/ 2/.test(nativeCode), 'audio should log real underruns and use per-voice drum gains');
assert(/MIX_BUFFER_SAMPLES 160/.test(nativeCode) && /MIX_PUMP_INTERVAL_MS 5/.test(nativeCode), 'audio should retain its proven full-quality stream block size');
assert(/timing-sensitive grid animation/.test(nativeCode) && !/s_playhead/.test(nativeCode), 'playback should use a simple static status indicator');
assert(/queue_sync_ack/.test(nativeCode) && /s_sync_ack_retries/.test(nativeCode), 'watch should queue and retry save acknowledgements');
assert(/close_speaker_stream/.test(nativeCode) && /speaker_stream_close\(\)/.test(nativeCode), 'PCM streams should close on stop and teardown');
assert(/persist_write_data\(PERSIST_SYNTH_NOTES_BLOB_BASE/.test(nativeCode), 'synth note edits should persist as one compact track blob');
assert(!/RequestSettings/.test(nativeCode), 'obsolete watch-to-phone settings request should be removed');
assert(!/settingsRequestPart/.test(pkjs), 'opening Settings should not wait for a second state round-trip');
assert(/cachedState/.test(config) && /disabled = true/.test(config), 'cached editor must be view-only');
assert(/Four on the floor/.test(config) && /Clear all notes/.test(config), 'editor should offer presets and clearing');
assert(config.indexOf('innerHTML') === -1, 'editor should use textContent for text updates');
console.log('Phone/editor protocol checks passed.');
