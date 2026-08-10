#include <pebble.h>
#include "message_keys.auto.h"

extern uint32_t MESSAGE_KEY_Pattern0;
extern uint32_t MESSAGE_KEY_Pattern1;
extern uint32_t MESSAGE_KEY_Pattern2;
extern uint32_t MESSAGE_KEY_Pattern3;
extern uint32_t MESSAGE_KEY_Bpm;
extern uint32_t MESSAGE_KEY_RequestSettings;

#define TRACK_COUNT 4
#define STEP_COUNT 16
#define DEFAULT_BPM 120
#define MIN_BPM 60
#define MAX_BPM 240
#define PERSIST_PATTERN_BASE 100
#define PERSIST_BPM 110

static Window *s_window;
static Layer *s_canvas;
static uint16_t s_pattern[TRACK_COUNT];
static uint8_t s_cursor_track;
static uint8_t s_cursor_step;
static uint16_t s_bpm;
static bool s_playing;
static bool s_audio_error;
static uint8_t s_playhead;
static AppTimer *s_playhead_timer;

// C4, E4, G4, C5. A zero note is a rest in the Speaker API.
static const uint8_t s_notes[TRACK_COUNT] = { 60, 64, 67, 72 };
static const GColor s_track_colors[TRACK_COUNT] = {
  GColorRed, GColorOrange, GColorJaegerGreen, GColorVividCerulean
};

static bool play_pattern(void);

static uint16_t step_duration_ms(void) {
  return 60000 / s_bpm / 4;
}

static void redraw(void) { layer_mark_dirty(s_canvas); }

static void restart_playback(void) {
  if (!s_playing) return;
  speaker_stop();
  play_pattern();
}

static void cancel_playhead_timer(void) {
  if (s_playhead_timer) {
    app_timer_cancel(s_playhead_timer);
    s_playhead_timer = NULL;
  }
}

static void advance_playhead(void *context) {
  s_playhead_timer = NULL;
  if (!s_playing) return;
  s_playhead = (s_playhead + 1) % STEP_COUNT;
  redraw();
  s_playhead_timer = app_timer_register(step_duration_ms(), advance_playhead, NULL);
}

static void save_state(void) {
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    persist_write_int(PERSIST_PATTERN_BASE + track, s_pattern[track]);
  }
  persist_write_int(PERSIST_BPM, s_bpm);
}

static bool play_pattern(void) {
  if (!s_playing) {
    return false;
  }

  static SpeakerNote notes[TRACK_COUNT][STEP_COUNT];
  static SpeakerTrack tracks[TRACK_COUNT];
  const uint16_t duration = step_duration_ms();
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    for (uint8_t step = 0; step < STEP_COUNT; step++) {
      notes[track][step] = (SpeakerNote) {
        .midi_note = (s_pattern[track] & (1 << step)) ? s_notes[track] : 0,
        .waveform = track == 0 ? SpeakerWaveformSquare : SpeakerWaveformTriangle,
        .duration_ms = duration,
        .velocity = track == 0 ? 80 : 62,
      };
    }
    tracks[track] = (SpeakerTrack) {
      .notes = notes[track], .num_notes = STEP_COUNT, .sample = NULL,
    };
  }
  if (!speaker_play_tracks(tracks, TRACK_COUNT, 76)) {
    s_playing = false;
    s_audio_error = true;
    cancel_playhead_timer();
    redraw();
    return false;
  }
  s_audio_error = false;
  s_playhead = 0;
  cancel_playhead_timer();
  s_playhead_timer = app_timer_register(step_duration_ms(), advance_playhead, NULL);
  return true;
}

static void playback_finished(SpeakerFinishReason reason, void *context) {
  if (s_playing && reason == SpeakerFinishReasonDone) {
    play_pattern();
  } else if (s_playing && (reason == SpeakerFinishReasonPreempted ||
                           reason == SpeakerFinishReasonError)) {
    s_playing = false;
    s_audio_error = true;
    cancel_playhead_timer();
    redraw();
  }
}

static void draw_centered(GContext *ctx, const char *text, GRect box, GFont font, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_sequencer(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int top = 36;
  const int margin = 7;
  const int gap = 2;
  const int cell_w = (bounds.size.w - margin * 2 - gap * (STEP_COUNT - 1)) / STEP_COUNT;
  const int row_h = (bounds.size.h - top - 13) / TRACK_COUNT;

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  char header[20];
  snprintf(header, sizeof(header), "%s  %dbpm",
           s_audio_error ? "NO AUDIO" : (s_playing ? "PLAY" : "STOP"), s_bpm);
  draw_centered(ctx, header, GRect(0, 0, bounds.size.w, 20),
                fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorWhite);
  draw_centered(ctx, "SEL STEP  HOLD SEL PLAY", GRect(0, 19, bounds.size.w, 15),
                fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorLightGray);

  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    for (uint8_t step = 0; step < STEP_COUNT; step++) {
      const int x = margin + step * (cell_w + gap);
      const int y = top + track * row_h + 2;
      bool enabled = s_pattern[track] & (1 << step);
      bool selected = track == s_cursor_track && step == s_cursor_step;
      GColor color = s_track_colors[track];
      graphics_context_set_fill_color(ctx, enabled ? color : GColorDarkGray);
      graphics_fill_rect(ctx, GRect(x, y, cell_w, row_h - 5), 2, GCornersAll);
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
  s_pattern[s_cursor_track] ^= (1 << s_cursor_step);
  save_state();
  redraw();
  if (!s_playing) speaker_play_tone(660, 35, 35, SpeakerWaveformSquare);
  else restart_playback();
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  s_playing = !s_playing;
  if (s_playing) {
    s_audio_error = false;
    play_pattern();
  } else {
    speaker_stop();
    cancel_playhead_timer();
  }
  redraw();
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_track = (s_cursor_track + TRACK_COUNT - 1) % TRACK_COUNT;
  redraw();
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_track = (s_cursor_track + 1) % TRACK_COUNT;
  redraw();
}

static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_bpm < MAX_BPM) s_bpm += 5;
  save_state(); redraw();
  restart_playback();
}

static void down_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_bpm > MIN_BPM) s_bpm -= 5;
  save_state(); redraw();
  restart_playback();
}

static void back_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_step = (s_cursor_step + STEP_COUNT - 1) % STEP_COUNT;
  redraw();
}

static void forward_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor_step = (s_cursor_step + 1) % STEP_COUNT;
  redraw();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_long_click_subscribe(BUTTON_ID_UP, 700, up_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 700, down_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  // The hardware has four buttons; holding Back supplies the missing right move.
  window_long_click_subscribe(BUTTON_ID_BACK, 500, forward_click, NULL);
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
  dict_write_uint16(iter, MESSAGE_KEY_Pattern0, s_pattern[0]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern1, s_pattern[1]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern2, s_pattern[2]);
  dict_write_uint16(iter, MESSAGE_KEY_Pattern3, s_pattern[3]);
  dict_write_uint16(iter, MESSAGE_KEY_Bpm, s_bpm);
  dict_write_end(iter);
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  if (dict_find(iter, MESSAGE_KEY_RequestSettings)) {
    send_settings();
    return;
  }

  Tuple *pattern[TRACK_COUNT] = {
    dict_find(iter, MESSAGE_KEY_Pattern0), dict_find(iter, MESSAGE_KEY_Pattern1),
    dict_find(iter, MESSAGE_KEY_Pattern2), dict_find(iter, MESSAGE_KEY_Pattern3),
  };
  Tuple *bpm = dict_find(iter, MESSAGE_KEY_Bpm);
  bool changed = false;
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    if (pattern[track]) {
      s_pattern[track] = pattern[track]->value->uint16;
      changed = true;
    }
  }
  if (bpm) {
    uint32_t requested_bpm = bpm->value->uint32;
    s_bpm = requested_bpm < MIN_BPM ? MIN_BPM :
            (requested_bpm > MAX_BPM ? MAX_BPM : requested_bpm);
    changed = true;
  }
  if (changed) {
    save_state();
    restart_playback();
    redraw();
  }
}

static void load_state(void) {
  const uint16_t defaults[TRACK_COUNT] = { 0x1111, 0x2222, 0x4444, 0x8888 };
  for (uint8_t track = 0; track < TRACK_COUNT; track++) {
    s_pattern[track] = persist_exists(PERSIST_PATTERN_BASE + track)
      ? (uint16_t)persist_read_int(PERSIST_PATTERN_BASE + track) : defaults[track];
  }
  int stored_bpm = persist_exists(PERSIST_BPM) ? persist_read_int(PERSIST_BPM) : DEFAULT_BPM;
  s_bpm = (stored_bpm >= MIN_BPM && stored_bpm <= MAX_BPM) ? stored_bpm : DEFAULT_BPM;
}

static void init(void) {
  load_state();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load, .unload = window_unload,
  });
  window_set_click_config_provider(s_window, click_config_provider);
  speaker_set_finish_callback(playback_finished, NULL);
  app_message_register_inbox_received(inbox_received);
  app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM, APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  speaker_stop();
  cancel_playhead_timer();
  speaker_set_finish_callback(NULL, NULL);
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
