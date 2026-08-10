# Pebble Steps

A four-track, 16-step music-sequencer watchface for speaker-equipped Pebble watches.

## Supported hardware

Pebble Steps intentionally targets only the watches with a hardware speaker:

- Pebble 2 Duo (`flint`)
- Pebble Time 2 (`emery`)

It excludes legacy Pebble 2 (microphone only) and Pebble Round 2 (no speaker).
It requires Pebble SDK 4.9 or newer because that is when the Speaker API arrived.

## Controls

| Control | Action |
| --- | --- |
| Up / Down | Select track |
| Back / hold Back | Previous / next step |
| Select | Toggle the selected step |
| Hold Select | Start / stop playback |
| Hold Up / Down | Raise / lower tempo by 5 BPM |

The four pitched tracks are C4, E4, G4, and C5. Each lit cell is a note; unlit cells are rests. Patterns and tempo persist on the watch. A yellow outline follows the playback position; the white outline is the edit cursor. Pattern or tempo changes made while playing restart the bar immediately so they are heard right away.

## Edit from the Pebble mobile app

The companion app opens an editor from the watchface's **Settings** screen in the Pebble mobile app. It synchronizes the current watch pattern before opening, offers all 64 steps and tempo, and writes the saved values back to the watch.

The configuration webview needs a public HTTPS host. The editor is included at `config/index.html`; deploy that directory (for example, through GitHub Pages) and replace `CONFIG_URL` near the top of `src/pkjs/index.js` with its final URL before building. No server or account credentials are needed by the editor.

## Build and install

Install the current Pebble CLI and SDK, then run:

```sh
pebble sdk install latest
pebble build
pebble install --emulator emery
```

Use a physical Pebble 2 Duo or Pebble Time 2 for audio. The emulator is useful for checking layout and controls, but cannot reproduce the watch speaker.

## Design notes

The app builds one 16-step `SpeakerTrack` per voice and sends the four tracks together with `speaker_play_tracks()`, producing aligned polyphonic playback. When the pattern finishes, the speaker completion callback queues the next loop. A failed or preempted start changes the header to `NO AUDIO` and stops transport. System speaker mute / Quiet Time applies normally; firmware controls it and this SDK revision does not expose its mute state to the app.

## Sources

- [Pebble hardware capability matrix](https://developer.repebble.com/guides/tools-and-resources/hardware-information/)
- [Pebble Speaker API](https://developer.repebble.com/docs/c/User_Interface/Speaker/)
- [Pebble SDK installation guide](https://developer.repebble.com/sdk/)
