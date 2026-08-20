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
extern uint32_t MESSAGE_KEY_RequestSettings;
extern uint32_t MESSAGE_KEY_SyncId;
extern uint32_t MESSAGE_KEY_SyncStatus;

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
#define PERSIST_BPM 110
#define MIX_BUFFER_SAMPLES 160
#define MIX_PRIME_BUFFERS 2
#define MIX_PUMP_INTERVAL_MS 5
#define SYNTH_NOTE_COUNT 7

static Window *s_window;
static Layer *s_canvas;
typedef enum { PageDrums, PageSynths } SequencerPage;

static uint16_t s_drum_pattern[TRACK_COUNT];
static uint16_t s_synth_pattern[SYNTH_TRACK_COUNT];
static uint8_t s_synth_note_index[SYNTH_TRACK_COUNT][STEP_COUNT];
static SequencerPage s_page;
static uint8_t s_cursor_track;
static uint8_t s_cursor_step;
static uint16_t s_bpm;
static bool s_playing;
static bool s_audio_error;
static uint8_t s_playhead;
static AppTimer *s_audio_timer;
static uint8_t s_mix_step;
static uint16_t s_mix_step_sample;
static uint16_t s_mix_step_length;
static uint16_t s_mix_step_remainder;
static int8_t s_mix_buffer[MIX_BUFFER_SAMPLES];
static uint16_t s_pending_offset;
static uint16_t s_pending_length;
static uint8_t s_empty_write_count;
static uint32_t s_stream_bytes_written;
static uint16_t s_stream_zero_writes;
static uint32_t s_estimated_queued_samples;
static uint32_t s_estimated_emitted_samples;
static uint16_t s_visual_step_sample;
static uint16_t s_visual_step_length;
static uint16_t s_visual_step_remainder;
static uint16_t s_visual_last_ms;
static uint16_t s_visual_ms_remainder;
static bool s_visual_started;

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

static uint8_t active_track_count(void) {
  return s_page == PageDrums ? TRACK_COUNT : SYNTH_TRACK_COUNT;
}

static uint16_t *active_patterns(void) {
  return s_page == PageDrums ? s_drum_pattern : s_synth_pattern;
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

static void cancel_audio_timer(void) {
  if (s_audio_timer) {
    app_timer_cancel(s_audio_timer);
    s_audio_timer = NULL;
  }
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

static int16_t triangle_sample(uint16_t phase) {
  uint8_t high = phase >> 8;
  return high < 128 ? (int16_t)high * 2 - 128 : 383 - (int16_t)high * 2;
}

static int16_t drum_sample(uint8_t track, uint16_t offset) {
  if (track == 0 && offset < KICK_SAMPLE_COUNT) return s_kick_pcm[offset];
  if (track == 1 && offset < SNARE_SAMPLE_COUNT) return s_snare_pcm[offset];
  if (track == 2 && offset < HAT_SAMPLE_COUNT) return s_hat_pcm[offset];
  if (track == 3 && offset < RIM_SAMPLE_COUNT) return s_rim_pcm[offset];
  return 0;
}

static int8_t finalize_mix(int32_t mixed) {
  // Keep the current punch at normal levels, but smoothly compress rare six-voice peaks.
  int32_t sample = mixed / 3;
  int32_t sign = sample < 0 ? -1 : 1;
  int32_t magnitude = sample < 0 ? -sample : sample;
  if (magnitude > 96) magnitude = 96 + (magnitude - 96) * 31 / (31 + magnitude - 96);
  return clamp_sample(sign * magnitude);
}

static void render_mix(int8_t *buffer, uint16_t count) {
  uint8_t step = s_mix_step;
  uint16_t position = s_mix_step_sample;
  uint16_t step_length = s_mix_step_length;
  uint16_t remainder = s_mix_step_remainder;
  for (uint16_t i = 0; i < count; i++) {
    int32_t mixed = 0;
    for (uint8_t track = 0; track < TRACK_COUNT; track++) {
      if (s_drum_pattern[track] & (1 << step)) {
        int32_t voice = drum_sample(track, position);
        // The tiny speaker favors a clear kick attack; keep bright percussion below it.
        if (track == 0) mixed += voice * 5 / 2;
        else if (track == 2) mixed += voice * 2 / 3;
        else if (track == 3) mixed += voice * 3 / 4;
        else mixed += voice;
      }
    }
    for (uint8_t track = 0; track < SYNTH_TRACK_COUNT; track++) {
      if (s_synth_pattern[track] & (1 << step)) {
        uint16_t phase = (uint32_t)position * s_synth_phase_increments[track]
          [s_synth_note_index[track][step]];
        int16_t voice = track == 0
          ? triangle_sample(phase) + (phase & 0x8000 ? 28 : -28)
          : (int16_t)(phase >> 8) - 128;
        uint16_t attack = position < 32 ? position * 4 : 127;
        uint16_t release = position + 16 >= step_length ? (step_length - position) * 8 : 127;
        uint16_t envelope_level = attack < release ? attack : release;
        mixed += voice * envelope_level / 127;
      }
    }
    buffer[i] = finalize_mix(mixed);
    position++;
    if (position >= step_length) {
      position = 0;
      step = (step + 1) % STEP_COUNT;
      step_length = next_step_samples(&remainder);
    }
  }
}

static void advance_mix_position(uint16_t count) {
  while (count > 0) {
    uint16_t remaining = s_mix_step_length - s_mix_step_sample;
    uint16_t advance = count < remaining ? count : remaining;
    s_mix_step_sample += advance;
    count -= advance;
    if (s_mix_step_sample >= s_mix_step_length) {
      s_mix_step_sample = 0;
      s_mix_step = (s_mix_step + 1) % STEP_COUNT;
      s_mix_step_length = next_step_samples(&s_mix_step_remainder);
    }
  }
}

// The API reports queued bytes, not the speaker's hardware playhead. Model PCM
// consumption after playback begins so the cursor follows emitted—not admitted—audio.
static void update_visual_playhead(void) {
  uint16_t now;
  time_ms(NULL, &now);
  if (speaker_get_status() != SpeakerStatusPlaying) {
    s_visual_last_ms = now;
    return;
  }
  if (!s_visual_started) {
    s_visual_started = true;
    s_visual_last_ms = now;
    return;
  }
  uint16_t elapsed_ms = now - s_visual_last_ms;
  s_visual_last_ms = now;
  uint32_t samples = s_visual_ms_remainder + (uint32_t)elapsed_ms * PCM_SAMPLE_RATE;
  uint16_t count = samples / 1000;
  s_visual_ms_remainder = samples % 1000;
  if (count > s_estimated_queued_samples) count = s_estimated_queued_samples;
  s_estimated_queued_samples -= count;
  s_estimated_emitted_samples += count;
  bool changed = false;
  while (count > 0) {
    uint16_t remaining = s_visual_step_length - s_visual_step_sample;
    uint16_t advance = count < remaining ? count : remaining;
    s_visual_step_sample += advance;
    count -= advance;
    if (s_visual_step_sample >= s_visual_step_length) {
      s_visual_step_sample = 0;
      s_playhead = (s_playhead + 1) % STEP_COUNT;
      s_visual_step_length = next_step_samples(&s_visual_step_remainder);
      changed = true;
    }
  }
  if (changed) redraw();
}

static void prepare_pending_audio(void) {
  if (s_pending_length != 0) return;
  render_mix(s_mix_buffer, MIX_BUFFER_SAMPLES);
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
    advance_mix_position(written);
    s_empty_write_count = 0;
    s_stream_bytes_written += written;
    s_estimated_queued_samples += written;
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
  speaker_stop();
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
  update_visual_playhead();
  if (write_pending_audio() == 0) {
    s_empty_write_count++;
    if (s_empty_write_count >= 8 && speaker_get_status() == SpeakerStatusIdle) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "PCM underrun: %lu bytes, %u zero writes",
              (unsigned long)s_stream_bytes_written, s_stream_zero_writes);
      APP_LOG(APP_LOG_LEVEL_ERROR, "PCM model: %lu queued, %lu emitted",
              (unsigned long)s_estimated_queued_samples,
              (unsigned long)s_estimated_emitted_samples);
      stop_for_audio_error();
      return;
    }
  }
  // A short refill cadence keeps enough queued PCM to bridge scheduler jitter without re-rendering.
  s_audio_timer = app_timer_register(MIX_PUMP_INTERVAL_MS, pump_audio, NULL);
}

static bool play_pattern(void) {
  if (!s_playing) return false;
  s_mix_step = 0;
  s_mix_step_sample = 0;
  s_mix_step_remainder = 0;
  s_mix_step_length = next_step_samples(&s_mix_step_remainder);
  s_playhead = 0;
  s_visual_step_sample = 0;
  s_visual_step_remainder = 0;
  s_visual_step_length = next_step_samples(&s_visual_step_remainder);
  s_visual_ms_remainder = 0;
  s_visual_started = false;
  time_ms(NULL, &s_visual_last_ms);
  s_pending_offset = 0;
  s_pending_length = 0;
  s_empty_write_count = 0;
  s_stream_bytes_written = 0;
  s_stream_zero_writes = 0;
  s_estimated_queued_samples = 0;
  s_estimated_emitted_samples = 0;
  if (!speaker_stream_open(SpeakerPcmFormat_8kHz_8bit, 76)) {
    s_playing = false;
    s_audio_error = true;
    redraw();
    return false;
  }
  uint32_t queued = 0;
  for (uint8_t buffer = 0; buffer < MIX_PRIME_BUFFERS; buffer++) {
    uint16_t written = write_pending_audio();
    queued += written;
    if (written == 0 || s_pending_length != 0) break;
  }
  if (queued == 0) {
    speaker_stream_close();
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
    speaker_stop();
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
  } else {
    snprintf(header, sizeof(header), "DRUM %s %dbpm", active_track_name(s_cursor_track), s_bpm);
  }
  draw_centered(ctx, header, GRect(0, 0, bounds.size.w, 20),
                fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite);
  draw_centered(ctx, s_page == PageSynths ? "HLD ROW  DBL PITCH" : "HLD ROW  DBL BPM",
                GRect(0, 19, bounds.size.w, 15),
                fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorLightGray);

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
      if (s_playing && step == s_playhead) {
        graphics_context_set_stroke_color(ctx, GColorYellow);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_rect(ctx, GRect(x - 1, y - 1, cell_w + 2, row_h - 3));
      }
      if (selected) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 2);
        graphics_draw_rect(ctx, GRect(x - 1, y - 1, cell_w + 2, row_h - 3));
      }
    }
  }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  if (click_number_of_clicks_counted(recognizer) == 2) {
    s_page = s_page == PageDrums ? PageSynths : PageDrums;
    s_cursor_track = 0;
    redraw();
    return;
  }
  uint16_t *patterns = active_patterns();
  patterns[s_cursor_track] ^= (1 << s_cursor_step);
  save_state(s_page == PageDrums ? 1 << s_cursor_track : 0,
             s_page == PageSynths ? 1 << s_cursor_track : 0, false);
  redraw();
  if (!s_playing) speaker_play_tone(660, 35, 35, SpeakerWaveformSquare);
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  set_playing(!s_playing);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (click_number_of_clicks_counted(recognizer) == 2) {
    if (s_page == PageSynths) {
      uint8_t *note = &s_synth_note_index[s_cursor_track][s_cursor_step];
      if (*note < SYNTH_NOTE_COUNT - 1) {
        (*note)++;
        persist_write_int(PERSIST_SYNTH_NOTE_BASE + s_cursor_track * STEP_COUNT + s_cursor_step, *note);
      }
    } else if (s_bpm < MAX_BPM) {
      s_bpm += 5;
      save_state(0, 0, true);
    }
  } else {
    s_cursor_step = (s_cursor_step + STEP_COUNT - 1) % STEP_COUNT;
  }
  redraw();
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (click_number_of_clicks_counted(recognizer) == 2) {
    if (s_page == PageSynths) {
      uint8_t *note = &s_synth_note_index[s_cursor_track][s_cursor_step];
      if (*note > 0) {
        (*note)--;
        persist_write_int(PERSIST_SYNTH_NOTE_BASE + s_cursor_track * STEP_COUNT + s_cursor_step, *note);
      }
    } else if (s_bpm > MIN_BPM) {
      s_bpm -= 5;
      save_state(0, 0, true);
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
}

static void window_unload(Window *window) { layer_destroy(s_canvas); }

static void send_settings(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint16(iter, MESSAGE_KEY_Pattern0, s_drum_pattern[0]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern1, s_drum_pattern[1]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern2, s_drum_pattern[2]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern3, s_drum_pattern[3]);
  dict_write_uint16(iter, MESSAGE_KEY_Synth0, s_synth_pattern[0]);
  dict_write_uint16(iter, MESSAGE_KEY_Synth1, s_synth_pattern[1]);
  dict_write_uint16(iter, MESSAGE_KEY_Bpm, s_bpm);
  dict_write_uint8(iter, MESSAGE_KEY_Transport, s_playing ? 1 : 0);
  dict_write_end(iter);
  app_message_outbox_send();
}

static void send_sync_ack(uint32_t sync_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint32(iter, MESSAGE_KEY_SyncId, sync_id);
  dict_write_uint8(iter, MESSAGE_KEY_SyncStatus, 1);
  dict_write_end(iter);
  app_message_outbox_send();
}

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
  Tuple *request_settings = dict_find(iter, MESSAGE_KEY_RequestSettings);
  if (request_settings) {
    send_settings();
    return;
  }

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
  Tuple *sync_id = dict_find(iter, MESSAGE_KEY_SyncId);
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
      for (uint8_t step = 0; step < STEP_COUNT; step++) {
        if (s_synth_note_index[track][step] != requested_notes[step]) {
          s_synth_note_index[track][step] = requested_notes[step];
          persist_write_int(PERSIST_SYNTH_NOTE_BASE + track * STEP_COUNT + step, requested_notes[step]);
          synth_notes_changed = true;
        }
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
    redraw();
  }
  uint32_t requested_transport;
  if (tuple_to_uint32(transport, &requested_transport)) set_playing(requested_transport != 0);
  uint32_t requested_sync_id;
  if (tuple_to_uint32(sync_id, &requested_sync_id)) send_sync_ack(requested_sync_id);
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
    for (uint8_t step = 0; step < STEP_COUNT; step++) {
      int stored_note = persist_exists(PERSIST_SYNTH_NOTE_BASE + track * STEP_COUNT + step)
        ? persist_read_int(PERSIST_SYNTH_NOTE_BASE + track * STEP_COUNT + step) : 0;
      s_synth_note_index[track][step] = stored_note >= 0 && stored_note < SYNTH_NOTE_COUNT
        ? stored_note : 0;
    }
  }
  int stored_bpm = persist_exists(PERSIST_BPM) ? persist_read_int(PERSIST_BPM) : DEFAULT_BPM;
  s_bpm = (stored_bpm >= MIN_BPM && stored_bpm <= MAX_BPM) ? stored_bpm : DEFAULT_BPM;
}

static void init(void) {
  load_state();
  init_drum_samples();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load, .unload = window_unload,
  });
  window_set_click_config_provider(s_window, click_config_provider);
  speaker_set_finish_callback(playback_finished, NULL);
  app_message_register_inbox_received(inbox_received);
  // The phone editor sends six patterns, 32 pitch values, tempo, and transport together.
  app_message_open(256, 256);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  speaker_stop();
  cancel_audio_timer();
  speaker_set_finish_callback(NULL, NULL);
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
