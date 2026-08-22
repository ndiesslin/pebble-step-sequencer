#include <pebble.h>
#include "message_keys.auto.h"

extern uint32_t MESSAGE_KEY_Pattern0;
extern uint32_t MESSAGE_KEY_Pattern1;
extern uint32_t MESSAGE_KEY_Pattern2;
extern uint32_t MESSAGE_KEY_Pattern3;
extern uint32_t MESSAGE_KEY_Synth0;
extern uint32_t MESSAGE_KEY_Synth1;
extern uint32_t MESSAGE_KEY_SynthNotes0;
extern uint32_t MESSAGE_KEY_SynthNotes1;
extern uint32_t MESSAGE_KEY_Bpm;
extern uint32_t MESSAGE_KEY_Transport;
extern uint32_t MESSAGE_KEY_Volume;
extern uint32_t MESSAGE_KEY_Drive;
extern uint32_t MESSAGE_KEY_Space;
extern uint32_t MESSAGE_KEY_BassAttack;
extern uint32_t MESSAGE_KEY_BassDecay;
extern uint32_t MESSAGE_KEY_LeadAttack;
extern uint32_t MESSAGE_KEY_LeadDecay;
extern uint32_t MESSAGE_KEY_DriveTargets;
extern uint32_t MESSAGE_KEY_SpaceTargets;

#define TRACK_COUNT 4
#define SYNTH_TRACK_COUNT 2
#define STEP_COUNT 16
#define PCM_SAMPLE_RATE 8000
#define KICK_SAMPLE_COUNT 720
#define SNARE_SAMPLE_COUNT 800
#define HAT_SAMPLE_COUNT 200
#define RIM_SAMPLE_COUNT 400
#define DEFAULT_BPM 120
#define MIN_BPM 60
#define MAX_BPM 240
#define PERSIST_PATTERN_BASE 100
#define PERSIST_SYNTH_PATTERN_BASE 104
#define PERSIST_SYNTH_NOTE_BASE 120
#define PERSIST_SYNTH_NOTES_BLOB_BASE 130
#define PERSIST_BPM 110
#define PERSIST_VOLUME 111
#define PERSIST_DRIVE 112
#define PERSIST_SPACE 113
#define PERSIST_BASS_ATTACK 114
#define PERSIST_BASS_DECAY 115
#define PERSIST_LEAD_ATTACK 116
#define PERSIST_LEAD_DECAY 117
#define PERSIST_DRIVE_TARGETS 118
#define PERSIST_SPACE_TARGETS 119
#define MIX_BUFFER_SAMPLES 160
#define MIX_PRIME_BUFFERS 2
#define MIX_PUMP_INTERVAL_MS 5
#define SYNTH_NOTE_COUNT 7
#define EFFECT_COUNT 3
#define SHAPE_COUNT 4
#define ROUTING_EFFECT_COUNT 2
#define ROUTING_TARGET_COUNT 3
#define TARGET_DRUMS 0x01
#define TARGET_BASS 0x02
#define TARGET_LEAD 0x04
#define TARGET_ALL (TARGET_DRUMS | TARGET_BASS | TARGET_LEAD)
#define SPACE_DELAY_SAMPLES 256
#define LOOP_MAX_SAMPLES 32000
#define DEFAULT_VOLUME 90

static Window *s_window;
static Layer *s_canvas;
typedef enum { PageDrums, PageSynths, PageShape, PageEffects, PageRouting } SequencerPage;

static uint16_t s_drum_pattern[TRACK_COUNT];
static uint16_t s_synth_pattern[SYNTH_TRACK_COUNT];
static uint8_t s_synth_note_index[SYNTH_TRACK_COUNT][STEP_COUNT];
static SequencerPage s_page;
static uint8_t s_cursor_track;
static uint8_t s_cursor_step;
static uint16_t s_bpm;
static uint8_t s_volume;
static uint8_t s_drive;
static uint8_t s_space;
static uint8_t s_synth_attack[SYNTH_TRACK_COUNT];
static uint8_t s_synth_decay[SYNTH_TRACK_COUNT];
static uint16_t s_synth_attack_gain[SYNTH_TRACK_COUNT];
static uint16_t s_synth_decay_gain[SYNTH_TRACK_COUNT];
static uint8_t s_drive_targets;
static uint8_t s_space_targets;
static bool s_playing;
static bool s_audio_error;
static AppTimer *s_audio_timer;
static AppTimer *s_loading_sound_timer;
static uint8_t s_loading_sound_note;
static bool s_stream_open;
static uint8_t s_mix_step;
static uint16_t s_mix_step_sample;
static uint16_t s_mix_step_length;
static uint16_t s_mix_step_remainder;
static int8_t s_mix_buffer[MIX_BUFFER_SAMPLES];
// The full 16-step bar is rendered before playback.  The speaker pump only
// copies this cache, which keeps the time-critical PCM stream inexpensive.
static int8_t s_loop_pcm[LOOP_MAX_SAMPLES];
static uint16_t s_loop_length;
static uint16_t s_loop_read_index;
static uint16_t s_step_offsets[STEP_COUNT + 1];
static bool s_loop_cache_ready;
static bool s_loop_cache_dirty;
static uint16_t s_pending_offset;
static uint16_t s_pending_length;
static uint8_t s_empty_write_count;
static uint32_t s_stream_bytes_written;
static uint16_t s_stream_zero_writes;
static int8_t s_space_delay[SPACE_DELAY_SAMPLES];
static uint16_t s_space_delay_index;

static const char *s_track_names[TRACK_COUNT] = { "KICK", "SNARE", "HAT", "RIM" };
static const GColor s_track_colors[TRACK_COUNT] = {
  GColorRed, GColorOrange, GColorJaegerGreen, GColorVividCerulean
};
static const char *s_synth_names[SYNTH_TRACK_COUNT] = { "BASS", "LEAD" };
#ifndef PBL_BW
static const GColor s_synth_colors[SYNTH_TRACK_COUNT] = { GColorPurple, GColorVividCerulean };
#endif
static const char *s_synth_note_names[SYNTH_TRACK_COUNT][SYNTH_NOTE_COUNT] = {
  { "C4", "D4", "E4", "G4", "A4", "B4", "C5" },
  { "C5", "D5", "E5", "G5", "A5", "B5", "C6" },
};
// 16-bit phase increments at 8 kHz, kept above the Pebble speaker's weak low end.
static const uint16_t s_synth_phase_increments[SYNTH_TRACK_COUNT][SYNTH_NOTE_COUNT] = {
  { 2144, 2404, 2700, 3212, 3604, 4048, 4288 },
  { 4288, 4808, 5400, 6424, 7208, 8096, 8576 },
};

static int8_t s_kick_pcm[KICK_SAMPLE_COUNT];
static int8_t s_snare_pcm[SNARE_SAMPLE_COUNT];
static int8_t s_hat_pcm[HAT_SAMPLE_COUNT];
static int8_t s_rim_pcm[RIM_SAMPLE_COUNT];

// One sine period, used for the tonal bodies of the kick, snare, and rim shot.
static const int8_t s_sine[64] = {
  0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 118, 122, 125, 127,
  127, 127, 125, 122, 118, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
  0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -118, -122,
  -125, -127, -127, -127, -125, -122, -118, -112, -106, -98, -90, -81,
  -71, -60, -49, -37, -25, -12,
};

static bool play_pattern(void);
static bool tuple_to_uint32(const Tuple *tuple, uint32_t *value);
static void invalidate_loop_cache(void);

static uint8_t active_track_count(void) {
  if (s_page == PageDrums) return TRACK_COUNT;
  if (s_page == PageSynths) return SYNTH_TRACK_COUNT;
  if (s_page == PageShape) return SHAPE_COUNT;
  return s_page == PageEffects ? EFFECT_COUNT : ROUTING_EFFECT_COUNT;
}

static uint16_t *active_patterns(void) {
  return s_page == PageSynths ? s_synth_pattern : s_drum_pattern;
}

static const char *active_track_name(uint8_t track) {
  return s_page == PageDrums ? s_track_names[track] : s_synth_names[track];
}

static GColor active_track_color(uint8_t track) {
  // Red maps to black on the monochrome Pebble 2, making Kick hits look like rests.
  return PBL_IF_BW_ELSE(GColorWhite,
                         (s_page == PageDrums ? s_track_colors[track] : s_synth_colors[track]));
}

static int8_t clamp_sample(int32_t value) {
  return value > 127 ? 127 : (value < -128 ? -128 : (int8_t)value);
}

static int8_t next_noise(void) {
  static uint16_t state = 0xACE1;
  state = (state >> 1) ^ (-(state & 1) & 0xB400);
  return (int8_t)(state >> 8);
}

static uint16_t envelope(uint16_t index, uint16_t count, uint16_t peak) {
  uint32_t remaining = count - index;
  return peak * remaining * remaining / (count * count);
}

static void init_drum_samples(void) {
  uint16_t phase = 0;
  uint16_t frequency = 500;
  for (uint16_t i = 0; i < KICK_SAMPLE_COUNT; i++) {
    uint16_t amplitude = envelope(i, KICK_SAMPLE_COUNT, 127);
    // Keep the body in the tiny speaker's audible low-mid range, then add a beater attack.
    int32_t body = s_sine[phase >> 10] * amplitude / 127;
    int32_t click = i < 28 ? next_noise() * (28 - i) / 56 : 0;
    s_kick_pcm[i] = clamp_sample(body + click);
    phase += frequency * 8;
    frequency = frequency * 1999 / 2000;  // 500 Hz attack settles near a clear ~350 Hz electronic kick.
  }

  phase = 0;
  for (uint16_t i = 0; i < SNARE_SAMPLE_COUNT; i++) {
    uint16_t noise_amp = envelope(i, SNARE_SAMPLE_COUNT, 104);
    uint16_t body_amp = i < SNARE_SAMPLE_COUNT / 2 ?
      envelope(i, SNARE_SAMPLE_COUNT / 2, 50) : 0;
    int32_t noise = next_noise() * noise_amp / 127;
    int32_t body = i < SNARE_SAMPLE_COUNT / 2 ? s_sine[phase >> 10] * body_amp / 127 : 0;
    s_snare_pcm[i] = clamp_sample(noise + body);
    phase += 200 * 8;
  }

  int8_t previous_noise = 0;
  for (uint16_t i = 0; i < HAT_SAMPLE_COUNT; i++) {
    int8_t noise = next_noise();
    uint16_t amplitude = envelope(i, HAT_SAMPLE_COUNT, 120);
    // A short, filtered burst keeps the closed hat crisp without masking the snare body.
    s_hat_pcm[i] = clamp_sample((noise - previous_noise) * amplitude / 127);
    previous_noise = noise;
  }

  phase = 0;
  for (uint16_t i = 0; i < RIM_SAMPLE_COUNT; i++) {
    uint16_t amplitude = envelope(i, RIM_SAMPLE_COUNT, 100);
    int32_t click = i < 24 ? next_noise() * (24 - i) / 24 : 0;
    int32_t tone = s_sine[phase >> 10] * amplitude / 127;
    s_rim_pcm[i] = clamp_sample(click + tone);
    phase += 1500 * 8;
  }

}

static void redraw(void) { layer_mark_dirty(s_canvas); }

static void play_loading_sound(void *context) {
  static const uint16_t notes[] = { 880, 1175, 1480 };
  static const uint16_t delays[] = { 70, 70, 0 };
  s_loading_sound_timer = NULL;
  if (s_loading_sound_note >= ARRAY_LENGTH(notes) || s_playing) return;
  speaker_play_tone(notes[s_loading_sound_note], 48, 20, SpeakerWaveformSine);
  uint16_t delay = delays[s_loading_sound_note++];
  if (delay) s_loading_sound_timer = app_timer_register(delay, play_loading_sound, NULL);
}

static void cancel_audio_timer(void) {
  if (s_audio_timer) {
    app_timer_cancel(s_audio_timer);
    s_audio_timer = NULL;
  }
}

static void close_speaker_stream(void) {
  if (!s_stream_open) return;
  speaker_stop();
  speaker_stream_close();
  s_stream_open = false;
}

static void save_state(uint8_t changed_drum_tracks, uint8_t changed_synth_tracks,
                       bool bpm_changed) {
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    if (changed_drum_tracks & (1 << track)) {
      persist_write_int(PERSIST_PATTERN_BASE + track, s_drum_pattern[track]);
    }
  }
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    if (changed_synth_tracks & (1 << track)) {
      persist_write_int(PERSIST_SYNTH_PATTERN_BASE + track, s_synth_pattern[track]);
    }
  }
  if (bpm_changed) persist_write_int(PERSIST_BPM, s_bpm);
}

static void save_synth_notes(uint8_t track) {
  persist_write_data(PERSIST_SYNTH_NOTES_BLOB_BASE + track,
                     s_synth_note_index[track], STEP_COUNT);
}

static void save_effects(void) {
  persist_write_int(PERSIST_VOLUME, s_volume);
  persist_write_int(PERSIST_DRIVE, s_drive);
  persist_write_int(PERSIST_SPACE, s_space);
}

static void save_synth_shape(void) {
  persist_write_int(PERSIST_BASS_ATTACK, s_synth_attack[0]);
  persist_write_int(PERSIST_BASS_DECAY, s_synth_decay[0]);
  persist_write_int(PERSIST_LEAD_ATTACK, s_synth_attack[1]);
  persist_write_int(PERSIST_LEAD_DECAY, s_synth_decay[1]);
}

static void update_synth_envelope_gains(void) {
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    uint16_t attack_samples = 8 + s_synth_attack[track] * 8;
    uint16_t decay_samples = 24 + s_synth_decay[track] * 8;
    s_synth_attack_gain[track] = 127 * 256 / attack_samples;
    s_synth_decay_gain[track] = 127 * 256 / decay_samples;
  }
}

static void save_routing(void) {
  persist_write_int(PERSIST_DRIVE_TARGETS, s_drive_targets);
  persist_write_int(PERSIST_SPACE_TARGETS, s_space_targets);
}

static uint8_t *active_effect_value(void) {
  if (s_cursor_track == 0) return &s_volume;
  if (s_cursor_track == 1) return &s_drive;
  return &s_space;
}

static void adjust_active_effect(int8_t amount) {
  uint8_t *value = active_effect_value();
  int16_t next = *value + amount;
  *value = next < 0 ? 0 : (next > 100 ? 100 : next);
  if (s_cursor_track == 0 && s_stream_open) speaker_set_volume(s_volume);
  if (s_cursor_track != 0) invalidate_loop_cache();
  save_effects();
}

static uint8_t *active_shape_value(void) {
  if (s_cursor_track == 0) return &s_synth_attack[0];
  if (s_cursor_track == 1) return &s_synth_decay[0];
  if (s_cursor_track == 2) return &s_synth_attack[1];
  return &s_synth_decay[1];
}

static void adjust_active_shape(int8_t amount) {
  uint8_t *value = active_shape_value();
  int16_t next = *value + amount;
  *value = next < 0 ? 0 : (next > 100 ? 100 : next);
  save_synth_shape();
  update_synth_envelope_gains();
  invalidate_loop_cache();
}

static uint8_t *active_routing_targets(void) {
  return s_cursor_track == 0 ? &s_drive_targets : &s_space_targets;
}

static void toggle_active_routing_target(void) {
  uint8_t *targets = active_routing_targets();
  *targets ^= 1 << s_cursor_step;
  save_routing();
  invalidate_loop_cache();
}

static uint16_t next_step_samples(uint16_t *remainder) {
  const uint16_t denominator = s_bpm * 4;
  const uint32_t numerator = PCM_SAMPLE_RATE * 60;
  uint16_t samples = numerator / denominator;
  *remainder += numerator % denominator;
  if (*remainder >= denominator) {
    (*remainder -= denominator);
    samples++;
  }
  return samples;
}

static int16_t drum_sample(uint8_t track, uint16_t offset) {
  if (track == 0 && offset < KICK_SAMPLE_COUNT) return s_kick_pcm[offset];
  if (track == 1 && offset < SNARE_SAMPLE_COUNT) return s_snare_pcm[offset];
  if (track == 2 && offset < HAT_SAMPLE_COUNT) return s_hat_pcm[offset];
  if (track == 3 && offset < RIM_SAMPLE_COUNT) return s_rim_pcm[offset];
  return 0;
}

static int32_t soft_limit(int32_t sample) {
  int32_t sign = sample < 0 ? -1 : 1;
  int32_t magnitude = sample < 0 ? -sample : sample;
  if (magnitude > 96) magnitude = 96 + (magnitude - 96) * 31 / (31 + magnitude - 96);
  return sign * magnitude;
}

static int32_t apply_drive(int32_t sample, bool enabled) {
  if (!enabled || s_drive == 0) return sample;
  return soft_limit(sample * (100 + s_drive * 2) / 100);
}

typedef struct {
  uint8_t step;
  uint16_t position;
  uint16_t length;
} RenderCursor;

static void advance_render_cursor(RenderCursor *cursor) {
  cursor->position++;
  if (cursor->position >= cursor->length) {
    cursor->step = (cursor->step + 1) % STEP_COUNT;
    cursor->position = 0;
    cursor->length = s_step_offsets[cursor->step + 1] - s_step_offsets[cursor->step];
  }
}

static void init_render_cursor(RenderCursor *cursor, uint16_t sample_offset) {
  cursor->step = 0;
  while (cursor->step < STEP_COUNT - 1 && sample_offset >= s_step_offsets[cursor->step + 1]) {
    cursor->step++;
  }
  cursor->position = sample_offset - s_step_offsets[cursor->step];
  cursor->length = s_step_offsets[cursor->step + 1] - s_step_offsets[cursor->step];
}

static int8_t render_cached_sample(RenderCursor *cursor) {
  int32_t drums = 0;
  int32_t bass = 0;
  int32_t lead = 0;
  const uint8_t step = cursor->step;
  const uint16_t position = cursor->position;
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    if (s_drum_pattern[track] & (1 << step)) {
      int32_t voice = drum_sample(track, position);
      if (track == 0) drums += voice * 5 / 2;
      else if (track == 2) drums += voice * 2 / 3;
      else if (track == 3) drums += voice * 3 / 4;
      else drums += voice;
    }
  }
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    if (s_synth_pattern[track] & (1 << step)) {
      uint16_t phase = (uint32_t)position * s_synth_phase_increments[track]
        [s_synth_note_index[track][step]];
      int16_t voice = track == 0
        ? ((phase >> 8) < 128 ? (int16_t)(phase >> 8) * 2 - 128 : 383 - (int16_t)(phase >> 8) * 2) + (phase & 0x8000 ? 28 : -28)
        : (int16_t)(phase >> 8) - 128;
      uint16_t attack = position * s_synth_attack_gain[track] >> 8;
      if (attack > 127) attack = 127;
      uint16_t release = (cursor->length - position) * s_synth_decay_gain[track] >> 8;
      if (release > 127) release = 127;
      int32_t shaped = voice * (attack < release ? attack : release) / 127;
      if (track == 0) bass += shaped;
      else lead += shaped;
    }
  }
  drums = apply_drive(drums, s_drive_targets & TARGET_DRUMS);
  bass = apply_drive(bass, s_drive_targets & TARGET_BASS);
  lead = apply_drive(lead, s_drive_targets & TARGET_LEAD);
  int32_t dry = soft_limit((drums + bass + lead) / 3);
  int32_t send = 0;
  if (s_space_targets & TARGET_DRUMS) send += drums;
  if (s_space_targets & TARGET_BASS) send += bass;
  if (s_space_targets & TARGET_LEAD) send += lead;
  int8_t delayed = s_space_delay[s_space_delay_index];
  if (s_space != 0) {
    s_space_delay[s_space_delay_index] = clamp_sample(send / 3 + delayed * s_space / 160);
    dry += delayed * s_space / 250;
  } else {
    s_space_delay[s_space_delay_index] = 0;
  }
  s_space_delay_index = (s_space_delay_index + 1) & (SPACE_DELAY_SAMPLES - 1);
  advance_render_cursor(cursor);
  return clamp_sample(soft_limit(dry));
}

static bool build_loop_cache(void) {
  uint16_t remainder = 0;
  s_step_offsets[0] = 0;
  for (uint8_t step = 0; step < STEP_COUNT; step++) {
    s_step_offsets[step + 1] = s_step_offsets[step] + next_step_samples(&remainder);
  }
  s_loop_length = s_step_offsets[STEP_COUNT];
  if (s_loop_length == 0 || s_loop_length > LOOP_MAX_SAMPLES) return false;
  memset(s_space_delay, 0, sizeof(s_space_delay));
  s_space_delay_index = 0;
  // Prime the feedback state from the end of the repeating bar so the cache
  // itself has a seamless delay tail at its loop boundary.
  uint16_t pre_roll = SPACE_DELAY_SAMPLES * 12;
  if (pre_roll > s_loop_length) pre_roll = s_loop_length;
  RenderCursor cursor;
  init_render_cursor(&cursor, s_loop_length - pre_roll);
  for (uint16_t i = 0; i < pre_roll; i++) render_cached_sample(&cursor);
  init_render_cursor(&cursor, 0);
  for (uint16_t i = 0; i < s_loop_length; i++) s_loop_pcm[i] = render_cached_sample(&cursor);
  s_loop_cache_ready = true;
  s_loop_cache_dirty = false;
  return true;
}

static void invalidate_loop_cache(void) {
  s_loop_cache_dirty = true;
}

static void advance_mix_position(uint16_t count) {
  bool step_changed = false;
  while (count > 0) {
    uint16_t remaining = s_mix_step_length - s_mix_step_sample;
    uint16_t advance = count < remaining ? count : remaining;
    s_mix_step_sample += advance;
    count -= advance;
    if (s_mix_step_sample >= s_mix_step_length) {
      s_mix_step_sample = 0;
      s_mix_step = (s_mix_step + 1) % STEP_COUNT;
      s_mix_step_length = next_step_samples(&s_mix_step_remainder);
      // Redraw once per beat instead of every sixteenth: clear feedback with less UI work.
      step_changed = (s_mix_step % 4) == 0;
    }
  }
  if (step_changed) redraw();
}

static void prepare_pending_audio(void) {
  if (s_pending_length != 0) return;
  uint16_t first = s_loop_length - s_loop_read_index;
  if (first > MIX_BUFFER_SAMPLES) first = MIX_BUFFER_SAMPLES;
  memcpy(s_mix_buffer, s_loop_pcm + s_loop_read_index, first);
  if (first < MIX_BUFFER_SAMPLES) {
    memcpy(s_mix_buffer + first, s_loop_pcm, MIX_BUFFER_SAMPLES - first);
  }
  s_pending_offset = 0;
  s_pending_length = MIX_BUFFER_SAMPLES;
}

static uint16_t write_pending_audio(void) {
  prepare_pending_audio();
  uint32_t written = speaker_stream_write(s_mix_buffer + s_pending_offset, s_pending_length);
  if (written > s_pending_length) written = s_pending_length;
  if (written > 0) {
    s_pending_offset += written;
    s_pending_length -= written;
    s_loop_read_index = (s_loop_read_index + written) % s_loop_length;
    advance_mix_position(written);
    s_empty_write_count = 0;
    s_stream_bytes_written += written;
  } else {
    s_stream_zero_writes++;
  }
  return written;
}

static void stop_for_audio_error(void) {
  s_playing = false;
  s_audio_error = true;
  s_pending_length = 0;
  cancel_audio_timer();
  close_speaker_stream();
  redraw();
}

static void playback_finished(SpeakerFinishReason reason, void *context) {
  if (s_playing && (reason == SpeakerFinishReasonPreempted || reason == SpeakerFinishReasonError)) {
    stop_for_audio_error();
  }
}

static void pump_audio(void *context) {
  s_audio_timer = NULL;
  if (!s_playing) return;
  if (write_pending_audio() == 0) {
    s_empty_write_count++;
    if (s_empty_write_count >= 8 && speaker_get_status() == SpeakerStatusIdle) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "PCM underrun: %lu bytes, %u zero writes",
              (unsigned long)s_stream_bytes_written, s_stream_zero_writes);
      stop_for_audio_error();
      return;
    }
  }
  // A short refill cadence keeps enough queued PCM to bridge scheduler jitter without re-rendering.
  s_audio_timer = app_timer_register(MIX_PUMP_INTERVAL_MS, pump_audio, NULL);
}

static bool play_pattern(void) {
  if (!s_playing) return false;
  if (!s_loop_cache_ready || s_loop_cache_dirty) {
    if (!build_loop_cache()) {
      s_playing = false;
      s_audio_error = true;
      redraw();
      return false;
    }
  }
  s_mix_step = 0;
  s_mix_step_sample = 0;
  s_mix_step_remainder = 0;
  s_mix_step_length = next_step_samples(&s_mix_step_remainder);
  s_pending_offset = 0;
  s_pending_length = 0;
  s_empty_write_count = 0;
  s_stream_bytes_written = 0;
  s_stream_zero_writes = 0;
  s_loop_read_index = 0;
  if (!speaker_stream_open(SpeakerPcmFormat_8kHz_8bit, s_volume)) {
    s_playing = false;
    s_audio_error = true;
    redraw();
    return false;
  }
  s_stream_open = true;
  uint32_t queued = 0;
  for (uint8_t buffer = 0; buffer < MIX_PRIME_BUFFERS; buffer++) {
    uint16_t written = write_pending_audio();
    queued += written;
    if (written == 0 || s_pending_length != 0) break;
  }
  if (queued == 0) {
    close_speaker_stream();
    s_playing = false;
    s_audio_error = true;
    redraw();
    return false;
  }
  s_audio_error = false;
  cancel_audio_timer();
  s_audio_timer = app_timer_register(MIX_PUMP_INTERVAL_MS, pump_audio, NULL);
  return true;
}

static void set_playing(bool playing) {
  if (s_playing == playing) return;
  s_playing = playing;
  if (s_playing) {
    s_audio_error = false;
    play_pattern();
  } else {
    cancel_audio_timer();
    s_pending_length = 0;
    close_speaker_stream();
  }
  redraw();
}

static void draw_centered(GContext *ctx, const char *text, GRect box, GFont font, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_drum_icon(GContext *ctx, uint8_t track, int center_y) {
  const GPoint center = GPoint(9, center_y);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  if (track == 0) {  // Kick: filled bass drum.
    graphics_context_set_fill_color(ctx, s_track_colors[track]);
    graphics_fill_circle(ctx, center, 4);
    graphics_draw_circle(ctx, center, 5);
  } else if (track == 1) {  // Snare: drum head and wires.
    graphics_draw_circle(ctx, center, 5);
    graphics_context_set_stroke_color(ctx, s_track_colors[track]);
    graphics_draw_line(ctx, GPoint(3, center_y + 3), GPoint(15, center_y + 3));
  } else if (track == 2) {  // Hi-hat: two cymbals on a stand.
    graphics_draw_line(ctx, GPoint(3, center_y - 3), GPoint(15, center_y - 3));
    graphics_draw_line(ctx, GPoint(3, center_y + 1), GPoint(15, center_y + 1));
    graphics_draw_line(ctx, GPoint(9, center_y + 1), GPoint(9, center_y + 6));
  } else {  // Rim shot: a small struck ring.
    graphics_draw_circle(ctx, center, 4);
    graphics_context_set_stroke_color(ctx, s_track_colors[track]);
    graphics_draw_line(ctx, GPoint(4, center_y + 5), GPoint(14, center_y - 5));
  }
}

static void draw_synth_icon(GContext *ctx, uint8_t track, int center_y) {
  GColor color = PBL_IF_BW_ELSE(GColorWhite, s_synth_colors[track]);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  if (track == 0) {  // Bass: speaker cabinet and woofer.
    graphics_draw_rect(ctx, GRect(3, center_y - 5, 11, 11));
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(8, center_y), 3);
  } else {  // Lead: a clear musical note.
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(6, center_y + 3), 3);
    graphics_draw_line(ctx, GPoint(9, center_y + 3), GPoint(9, center_y - 6));
    graphics_draw_line(ctx, GPoint(9, center_y - 6), GPoint(15, center_y - 4));
  }
}

static void draw_effects(GContext *ctx, GRect bounds) {
  static const char *names[EFFECT_COUNT] = { "VOLUME", "DRIVE", "SPACE" };
  const uint8_t values[EFFECT_COUNT] = { s_volume, s_drive, s_space };
  const int top = 42;
  const int row_h = (bounds.size.h - top - 12) / EFFECT_COUNT;
  for (uint8_t effect = 0; effect < EFFECT_COUNT; effect++) {
    const int y = top + effect * row_h;
    char label[18];
    snprintf(label, sizeof(label), "%s %u", names[effect], values[effect]);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(10, y, 90, 20), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    const int bar_left = 103;
    const int bar_width = bounds.size.w - bar_left - 11;
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_rect(ctx, GRect(bar_left, y + 4, bar_width, 12));
    // Purple maps to black on Pebble 2; keep every FX amount visible on monochrome screens.
    GColor effect_color = PBL_IF_BW_ELSE(GColorWhite,
      (effect == 0 ? GColorVividCerulean : (effect == 1 ? GColorOrange : GColorPurple)));
    graphics_context_set_fill_color(ctx, effect_color);
    graphics_fill_rect(ctx, GRect(bar_left + 1, y + 5,
                                  (bar_width - 2) * values[effect] / 100, 10),
                       1, GCornersAll);
    if (effect == s_cursor_track) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_rect(ctx, GRect(5, y - 2, bounds.size.w - 10, 25));
    }
  }
}

static void draw_shape(GContext *ctx, GRect bounds) {
  static const char *names[SHAPE_COUNT] = { "B ATK", "B DEC", "L ATK", "L DEC" };
  const uint8_t values[SHAPE_COUNT] = {
    s_synth_attack[0], s_synth_decay[0], s_synth_attack[1], s_synth_decay[1]
  };
  const int top = 40;
  const int row_h = (bounds.size.h - top - 8) / SHAPE_COUNT;
  for (uint8_t shape = 0; shape < SHAPE_COUNT; shape++) {
    int y = top + shape * row_h;
    char label[16];
    snprintf(label, sizeof(label), "%s %u", names[shape], values[shape]);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(9, y, 88, 19), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    int bar_left = 98;
    int bar_width = bounds.size.w - bar_left - 10;
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_rect(ctx, GRect(bar_left, y + 3, bar_width, 11));
    graphics_context_set_fill_color(ctx, PBL_IF_BW_ELSE(GColorWhite,
      (shape < 2 ? GColorPurple : GColorVividCerulean)));
    graphics_fill_rect(ctx, GRect(bar_left + 1, y + 4,
                                  (bar_width - 2) * values[shape] / 100, 9), 1, GCornersAll);
    if (shape == s_cursor_track) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_rect(ctx, GRect(4, y - 2, bounds.size.w - 8, 23));
    }
  }
}

static void draw_routing(GContext *ctx, GRect bounds) {
  static const char *names[ROUTING_EFFECT_COUNT] = { "DRIVE", "SPACE" };
  static const char *targets[ROUTING_TARGET_COUNT] = { "D", "B", "L" };
  const uint8_t masks[ROUTING_EFFECT_COUNT] = { s_drive_targets, s_space_targets };
  const int top = 48;
  const int row_h = (bounds.size.h - top - 10) / ROUTING_EFFECT_COUNT;
  for (uint8_t effect = 0; effect < ROUTING_EFFECT_COUNT; effect++) {
    int y = top + effect * row_h;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, names[effect], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(9, y + 4, 56, 20), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    for (uint8_t target = 0; target < ROUTING_TARGET_COUNT; target++) {
      int x = 72 + target * 25;
      bool enabled = masks[effect] & (1 << target);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_rect(ctx, GRect(x, y, 18, 18));
      if (enabled) {
        graphics_context_set_fill_color(ctx, PBL_IF_BW_ELSE(GColorWhite,
          (target == 0 ? GColorJaegerGreen : (target == 1 ? GColorPurple : GColorVividCerulean))));
        graphics_fill_rect(ctx, GRect(x + 4, y + 4, 10, 10), 1, GCornersAll);
      }
      draw_centered(ctx, targets[target], GRect(x, y + 20, 18, 12),
                    fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorLightGray);
      if (effect == s_cursor_track && target == s_cursor_step) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(x - 2, y - 2, 22, 22));
      }
    }
  }
}

static void draw_sequencer(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int top = 36;
  const int grid_left = 18;
  const int grid_right = 4;
  const int gap = 2;
  const int grid_width = bounds.size.w - grid_left - grid_right;
  const uint8_t track_count = active_track_count();
  const uint16_t *patterns = active_patterns();
  const int row_h = (bounds.size.h - top - 13) / track_count;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  char header[20];
  if (s_audio_error) {
    snprintf(header, sizeof(header), "AUDIO ERROR");
  } else if (s_page == PageSynths) {
    snprintf(header, sizeof(header), "%s %s", active_track_name(s_cursor_track),
             s_synth_note_names[s_cursor_track][s_synth_note_index[s_cursor_track][s_cursor_step]]);
  } else if (s_page == PageShape) {
    snprintf(header, sizeof(header), "SYNTH SHAPE");
  } else if (s_page == PageEffects) {
    snprintf(header, sizeof(header), "EFFECTS");
  } else if (s_page == PageRouting) {
    snprintf(header, sizeof(header), "FX ROUTING");
  } else {
    snprintf(header, sizeof(header), "DRUM %s %dbpm", active_track_name(s_cursor_track), s_bpm);
  }
  draw_centered(ctx, header, GRect(0, 0, bounds.size.w, 20),
                fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite);
  if (s_playing) {
    // A static indicator confirms transport without a timing-sensitive grid animation.
    graphics_context_set_fill_color(ctx, PBL_IF_BW_ELSE(GColorWhite, GColorGreen));
    graphics_fill_circle(ctx, GPoint(bounds.size.w - 7, 9), 3);
  }
  draw_centered(ctx, s_loop_cache_dirty ? "CHANGES NEXT START" :
                         s_page == PageRouting ? "UP/DN TARGET HLD ROW" :
                         (s_page == PageEffects || s_page == PageShape) ? "UP/DN VALUE  HLD ROW" :
                         (s_page == PageSynths ? "HLD ROW  DBL PITCH" : "HLD ROW  DBL BPM"),
                GRect(0, 19, bounds.size.w, 15),
                fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorLightGray);

  if (s_page == PageEffects) {
    draw_effects(ctx, bounds);
    return;
  }
  if (s_page == PageShape) {
    draw_shape(ctx, bounds);
    return;
  }
  if (s_page == PageRouting) {
    draw_routing(ctx, bounds);
    return;
  }

  for (uint8_t track = 0; track < track_count; track++) {
    if (track == s_cursor_track) {
      // A solid marker stays clear on Flint's monochrome display as well as color watches.
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(0, top + track * row_h + 2, 2, row_h - 5), 0, GCornerNone);
    }
    if (s_page == PageDrums) draw_drum_icon(ctx, track, top + track * row_h + row_h / 2);
    else draw_synth_icon(ctx, track, top + track * row_h + row_h / 2);
    for (uint8_t step = 0; step < STEP_COUNT; step++) {
      const int x = grid_left + step * grid_width / STEP_COUNT;
      const int next_x = grid_left + (step + 1) * grid_width / STEP_COUNT;
      const int cell_w = next_x - x - gap;
      const int y = top + track * row_h + 2;
      bool enabled = patterns[track] & (1 << step);
      bool selected = track == s_cursor_track && step == s_cursor_step;
      GColor color = active_track_color(track);
      // GColorDarkGray can map to a lit pixel on Flint. Black makes a rest unambiguous.
      graphics_context_set_fill_color(ctx, enabled ? color : GColorBlack);
      graphics_fill_rect(ctx, GRect(x, y, cell_w, row_h - 5), 2, GCornersAll);
      if (step > 0 && step % 4 == 0) {
        // Four steps are one beat; the divider makes the 4/4 bar immediately scannable.
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_line(ctx, GPoint(x - 2, y), GPoint(x - 2, y + row_h - 6));
      }
      if (!enabled) {
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_rect(ctx, GRect(x, y, cell_w, row_h - 5));
      }
      if (selected) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(x - 1, y - 1, cell_w + 2, row_h - 3));
      }
      if (s_playing && step == s_mix_step) {
        graphics_context_set_stroke_color(ctx, PBL_IF_BW_ELSE(GColorWhite, GColorGreen));
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_rect(ctx, GRect(x, y, cell_w, row_h - 5));
      }
    }
  }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  if (click_number_of_clicks_counted(recognizer) == 2) {
    s_page = s_page == PageDrums ? PageSynths :
             (s_page == PageSynths ? PageShape :
             (s_page == PageShape ? PageEffects :
             (s_page == PageEffects ? PageRouting : PageDrums)));
    s_cursor_track = 0;
    s_cursor_step = 0;
    redraw();
    return;
  }
  if (s_page == PageRouting) {
    toggle_active_routing_target();
    redraw();
    return;
  }
  if (s_page == PageEffects || s_page == PageShape) return;
  uint16_t *patterns = active_patterns();
  patterns[s_cursor_track] ^= (1 << s_cursor_step);
  save_state(s_page == PageDrums ? 1 << s_cursor_track : 0,
             s_page == PageSynths ? 1 << s_cursor_track : 0, false);
  invalidate_loop_cache();
  redraw();
  if (!s_playing) speaker_play_tone(660, 35, 35, SpeakerWaveformSquare);
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  set_playing(!s_playing);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_page == PageRouting) {
    s_cursor_step = (s_cursor_step + ROUTING_TARGET_COUNT - 1) % ROUTING_TARGET_COUNT;
    redraw();
    return;
  }
  if (s_page == PageShape) {
    adjust_active_shape(click_number_of_clicks_counted(recognizer) == 2 ? 10 : 5);
    redraw();
    return;
  }
  if (s_page == PageEffects) {
    adjust_active_effect(click_number_of_clicks_counted(recognizer) == 2 ? 10 : 5);
    redraw();
    return;
  }
  if (click_number_of_clicks_counted(recognizer) == 2) {
    if (s_page == PageSynths) {
      uint8_t *note = &s_synth_note_index[s_cursor_track][s_cursor_step];
      if (*note < SYNTH_NOTE_COUNT - 1) {
        (*note)++;
        save_synth_notes(s_cursor_track);
        invalidate_loop_cache();
      }
    } else if (s_bpm < MAX_BPM) {
      s_bpm += 5;
      save_state(0, 0, true);
      invalidate_loop_cache();
    }
  } else {
    s_cursor_step = (s_cursor_step + STEP_COUNT - 1) % STEP_COUNT;
  }
  redraw();
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_page == PageRouting) {
    s_cursor_step = (s_cursor_step + 1) % ROUTING_TARGET_COUNT;
    redraw();
    return;
  }
  if (s_page == PageShape) {
    adjust_active_shape(click_number_of_clicks_counted(recognizer) == 2 ? -10 : -5);
    redraw();
    return;
  }
  if (s_page == PageEffects) {
    adjust_active_effect(click_number_of_clicks_counted(recognizer) == 2 ? -10 : -5);
    redraw();
    return;
  }
  if (click_number_of_clicks_counted(recognizer) == 2) {
    if (s_page == PageSynths) {
      uint8_t *note = &s_synth_note_index[s_cursor_track][s_cursor_step];
      if (*note > 0) {
        (*note)--;
        save_synth_notes(s_cursor_track);
        invalidate_loop_cache();
      }
    } else if (s_bpm > MIN_BPM) {
      s_bpm -= 5;
      save_state(0, 0, true);
      invalidate_loop_cache();
    }
  } else {
    s_cursor_step = (s_cursor_step + 1) % STEP_COUNT;
  }
  redraw();
}

static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_track = (s_cursor_track + active_track_count() - 1) % active_track_count();
  redraw();
}

static void down_long_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_track = (s_cursor_track + 1) % active_track_count();
  redraw();
}

static void click_config_provider(void *context) {
  window_multi_click_subscribe(BUTTON_ID_SELECT, 1, 2, 450, true, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long_click, NULL);
  window_multi_click_subscribe(BUTTON_ID_UP, 1, 2, 450, true, up_click);
  window_multi_click_subscribe(BUTTON_ID_DOWN, 1, 2, 450, true, down_click);
  window_long_click_subscribe(BUTTON_ID_UP, 700, up_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, down_long_click, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw_sequencer);
  layer_add_child(root, s_canvas);
  s_loading_sound_note = 0;
  s_loading_sound_timer = app_timer_register(90, play_loading_sound, NULL);
}

static void window_unload(Window *window) { layer_destroy(s_canvas); }

static bool tuple_to_uint32(const Tuple *tuple, uint32_t *value) {
  if (!tuple || !value || (tuple->type != TUPLE_UINT && tuple->type != TUPLE_INT)) return false;
  if (tuple->type == TUPLE_INT) {
    if (tuple->length == 1 && tuple->value->int8 >= 0) *value = tuple->value->int8;
    else if (tuple->length == 2 && tuple->value->int16 >= 0) *value = tuple->value->int16;
    else if (tuple->length == 4 && tuple->value->int32 >= 0) *value = tuple->value->int32;
    else return false;
  } else if (tuple->length == 1) *value = tuple->value->uint8;
  else if (tuple->length == 2) *value = tuple->value->uint16;
  else if (tuple->length == 4) *value = tuple->value->uint32;
  else return false;
  return true;
}

static bool tuple_to_synth_notes(const Tuple *tuple, uint8_t values[STEP_COUNT]) {
  if (!tuple || tuple->type != TUPLE_CSTRING || tuple->length < STEP_COUNT) return false;
  const char *text = tuple->value->cstring;
  for (uint8_t step = 0; step < STEP_COUNT; step++) {
    if (text[step] < '0' || text[step] >= '0' + SYNTH_NOTE_COUNT) return false;
    values[step] = text[step] - '0';
  }
  return true;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *pattern[TRACK_COUNT] = {
    dict_find(iter, MESSAGE_KEY_Pattern0), dict_find(iter, MESSAGE_KEY_Pattern1),
    dict_find(iter, MESSAGE_KEY_Pattern2), dict_find(iter, MESSAGE_KEY_Pattern3),
  };
  Tuple *bpm = dict_find(iter, MESSAGE_KEY_Bpm);
  Tuple *synth_pattern[SYNTH_TRACK_COUNT] = {
    dict_find(iter, MESSAGE_KEY_Synth0), dict_find(iter, MESSAGE_KEY_Synth1),
  };
  Tuple *synth_notes[SYNTH_TRACK_COUNT] = {
    dict_find(iter, MESSAGE_KEY_SynthNotes0), dict_find(iter, MESSAGE_KEY_SynthNotes1),
  };
  Tuple *transport = dict_find(iter, MESSAGE_KEY_Transport);
  Tuple *volume = dict_find(iter, MESSAGE_KEY_Volume);
  Tuple *drive = dict_find(iter, MESSAGE_KEY_Drive);
  Tuple *space = dict_find(iter, MESSAGE_KEY_Space);
  Tuple *bass_cutoff = dict_find(iter, MESSAGE_KEY_BassAttack);
  Tuple *bass_bite = dict_find(iter, MESSAGE_KEY_BassDecay);
  Tuple *lead_cutoff = dict_find(iter, MESSAGE_KEY_LeadAttack);
  Tuple *lead_bite = dict_find(iter, MESSAGE_KEY_LeadDecay);
  Tuple *drive_targets = dict_find(iter, MESSAGE_KEY_DriveTargets);
  Tuple *space_targets = dict_find(iter, MESSAGE_KEY_SpaceTargets);
  uint8_t changed_tracks = 0;
  uint8_t changed_synth_tracks = 0;
  bool bpm_changed = false;
  bool synth_notes_changed = false;
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    uint32_t value;
    if (tuple_to_uint32(pattern[track], &value) && value <= UINT16_MAX &&
        s_drum_pattern[track] != (uint16_t)value) {
      s_drum_pattern[track] = (uint16_t)value;
      changed_tracks |= 1 << track;
    }
  }
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    uint32_t value;
    if (tuple_to_uint32(synth_pattern[track], &value) && value <= UINT16_MAX &&
        s_synth_pattern[track] != (uint16_t)value) {
      s_synth_pattern[track] = (uint16_t)value;
      changed_synth_tracks |= 1 << track;
    }
  }
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    uint8_t requested_notes[STEP_COUNT];
    if (tuple_to_synth_notes(synth_notes[track], requested_notes)) {
      bool track_notes_changed = false;
      for (uint8_t step = 0; step < STEP_COUNT; step++) {
        if (s_synth_note_index[track][step] != requested_notes[step]) {
          s_synth_note_index[track][step] = requested_notes[step];
          track_notes_changed = true;
        }
      }
      if (track_notes_changed) {
        save_synth_notes(track);
        synth_notes_changed = true;
      }
    }
  }
  uint32_t requested_bpm;
  if (tuple_to_uint32(bpm, &requested_bpm)) {
    uint16_t clamped_bpm = requested_bpm < MIN_BPM ? MIN_BPM :
                           (requested_bpm > MAX_BPM ? MAX_BPM : requested_bpm);
    if (s_bpm != clamped_bpm) {
      s_bpm = clamped_bpm;
      bpm_changed = true;
    }
  }
  if (changed_tracks || changed_synth_tracks || bpm_changed || synth_notes_changed) {
    save_state(changed_tracks, changed_synth_tracks, bpm_changed);
    invalidate_loop_cache();
    redraw();
  }
  uint32_t requested_transport;
  if (tuple_to_uint32(transport, &requested_transport)) set_playing(requested_transport != 0);
  uint32_t requested_effect;
  bool effects_changed = false;
  if (tuple_to_uint32(volume, &requested_effect) && requested_effect <= 100 && s_volume != requested_effect) {
    s_volume = requested_effect;
    if (s_stream_open) speaker_set_volume(s_volume);
    effects_changed = true;
  }
  if (tuple_to_uint32(drive, &requested_effect) && requested_effect <= 100 && s_drive != requested_effect) {
    s_drive = requested_effect;
    effects_changed = true;
  }
  if (tuple_to_uint32(space, &requested_effect) && requested_effect <= 100 && s_space != requested_effect) {
    s_space = requested_effect;
    effects_changed = true;
  }
  if (effects_changed) {
    save_effects();
    if (drive || space) invalidate_loop_cache();
    redraw();
  }
  bool shape_changed = false;
  Tuple *shape_tuples[SHAPE_COUNT] = { bass_cutoff, bass_bite, lead_cutoff, lead_bite };
  uint8_t *shape_values[SHAPE_COUNT] = {
    &s_synth_attack[0], &s_synth_decay[0], &s_synth_attack[1], &s_synth_decay[1]
  };
  for (uint8_t shape = 0; shape < SHAPE_COUNT; shape++) {
    if (tuple_to_uint32(shape_tuples[shape], &requested_effect) && requested_effect <= 100 &&
        *shape_values[shape] != requested_effect) {
      *shape_values[shape] = requested_effect;
      shape_changed = true;
    }
  }
  if (shape_changed) {
    save_synth_shape();
    update_synth_envelope_gains();
    invalidate_loop_cache();
  }
  bool routing_changed = false;
  if (tuple_to_uint32(drive_targets, &requested_effect) && requested_effect <= TARGET_ALL &&
      s_drive_targets != requested_effect) {
    s_drive_targets = requested_effect;
    routing_changed = true;
  }
  if (tuple_to_uint32(space_targets, &requested_effect) && requested_effect <= TARGET_ALL &&
      s_space_targets != requested_effect) {
    s_space_targets = requested_effect;
    routing_changed = true;
  }
  if (routing_changed) {
    save_routing();
    invalidate_loop_cache();
  }
  if (shape_changed || routing_changed) redraw();
}

static void load_state(void) {
  const uint16_t defaults[TRACK_COUNT] = { 0x1111, 0x2222, 0x4444, 0x8888 };
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    s_drum_pattern[track] = persist_exists(PERSIST_PATTERN_BASE + track)
      ? (uint16_t)persist_read_int(PERSIST_PATTERN_BASE + track) : defaults[track];
  }
  const uint16_t synth_defaults[SYNTH_TRACK_COUNT] = { 0x1111, 0x8421 };
  for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
    s_synth_pattern[track] = persist_exists(PERSIST_SYNTH_PATTERN_BASE + track)
      ? (uint16_t)persist_read_int(PERSIST_SYNTH_PATTERN_BASE + track) : synth_defaults[track];
    int blob_size = persist_get_size(PERSIST_SYNTH_NOTES_BLOB_BASE + track);
    if (blob_size == STEP_COUNT &&
        persist_read_data(PERSIST_SYNTH_NOTES_BLOB_BASE + track, s_synth_note_index[track], STEP_COUNT) == STEP_COUNT) {
      for (uint8_t step = 0; step < STEP_COUNT; step++) {
        if (s_synth_note_index[track][step] >= SYNTH_NOTE_COUNT) s_synth_note_index[track][step] = 0;
      }
    } else {
      for (uint8_t step = 0; step < STEP_COUNT; step++) {
        int stored_note = persist_exists(PERSIST_SYNTH_NOTE_BASE + track * STEP_COUNT + step)
          ? persist_read_int(PERSIST_SYNTH_NOTE_BASE + track * STEP_COUNT + step) : 0;
        s_synth_note_index[track][step] = stored_note >= 0 && stored_note < SYNTH_NOTE_COUNT
          ? stored_note : 0;
      }
      save_synth_notes(track);
    }
  }
  int stored_bpm = persist_exists(PERSIST_BPM) ? persist_read_int(PERSIST_BPM) : DEFAULT_BPM;
  s_bpm = (stored_bpm >= MIN_BPM && stored_bpm <= MAX_BPM) ? stored_bpm : DEFAULT_BPM;
  int stored_volume = persist_exists(PERSIST_VOLUME) ? persist_read_int(PERSIST_VOLUME) : DEFAULT_VOLUME;
  int stored_drive = persist_exists(PERSIST_DRIVE) ? persist_read_int(PERSIST_DRIVE) : 0;
  int stored_space = persist_exists(PERSIST_SPACE) ? persist_read_int(PERSIST_SPACE) : 0;
  s_volume = stored_volume >= 0 && stored_volume <= 100 ? stored_volume : DEFAULT_VOLUME;
  s_drive = stored_drive >= 0 && stored_drive <= 100 ? stored_drive : 0;
  s_space = stored_space >= 0 && stored_space <= 100 ? stored_space : 0;
  s_synth_attack[0] = 10; s_synth_decay[0] = 50;
  s_synth_attack[1] = 5; s_synth_decay[1] = 65;
  int stored_drive_targets = persist_exists(PERSIST_DRIVE_TARGETS) ? persist_read_int(PERSIST_DRIVE_TARGETS) : TARGET_ALL;
  int stored_space_targets = persist_exists(PERSIST_SPACE_TARGETS) ? persist_read_int(PERSIST_SPACE_TARGETS) : TARGET_ALL;
  s_drive_targets = stored_drive_targets >= 0 && stored_drive_targets <= TARGET_ALL ? stored_drive_targets : TARGET_ALL;
  s_space_targets = stored_space_targets >= 0 && stored_space_targets <= TARGET_ALL ? stored_space_targets : TARGET_ALL;
}

static void init(void) {
  load_state();
  update_synth_envelope_gains();
  init_drum_samples();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load, .unload = window_unload,
  });
  window_set_click_config_provider(s_window, click_config_provider);
  speaker_set_finish_callback(playback_finished, NULL);
  app_message_register_inbox_received(inbox_received);
  // The phone editor sends one durable setting at a time, paced for the physical connection.
  app_message_open(256, 256);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  if (s_loading_sound_timer) app_timer_cancel(s_loading_sound_timer);
  cancel_audio_timer();
  close_speaker_stream();
  speaker_set_finish_callback(NULL, NULL);
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
