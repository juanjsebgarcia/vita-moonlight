/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "bench.h"
#include "config.h"
#include "connection.h"
#include "debug.h"
#include "device.h"
#include "gui/guilib.h"
#include "gui/ui_connect.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>
#include <psp2/rtc.h>

// The page's machine-readable strip, as fractions of the frame. These must
// match LAYOUT in tools/bench/bench.html.
#define MARKER_CX (0.010f + 0.060f / 2)
#define MARKER_CY (0.015f + 0.070f / 2)
#define BARCODE_X 0.100f
#define BARCODE_BLOCK_W 0.045f
#define BARCODE_CY (0.020f + 0.060f / 2)
#define FRAME_BITS 16
#define TICK_MASK ((1 << FRAME_BITS) - 1)

// The white and black reference blocks must differ by at least this much
// luminance for a frame to count as showing the page
#define MIN_REFERENCE_CONTRAST 80

// Windows virtual key codes, which is what GameStream hosts expect
#define VK_SPACE 0x20
#define VK_R 0x52

#define CONNECT_TIMEOUT_US (20 * 1000000ULL)
// A browser started by Sunshine can take a while to show the page
#define PAGE_TIMEOUT_US (45 * 1000000ULL)
#define RESET_SETTLE_US 1500000ULL
// Slightly off one second, so presses drift across the page's frame phase
#define PRESS_INTERVAL_US 1013000ULL
#define KEY_HOLD_US 30000
#define POLL_US 5000
// A press whose marker hasn't appeared by now counts as missed
#define PRESS_TIMEOUT_US 800000ULL
// Room for frames at up to 120 fps
#define MAX_FPS_RECORDED 120

enum {
  PHASE_IDLE,
  PHASE_WARMUP,
  PHASE_MEASURING,
};

typedef struct {
  float *values;
  int count;
  int capacity;
} series;

static pthread_mutex_t bench_mutex;
static volatile int phase = PHASE_IDLE;

// Written by the decode thread under bench_mutex
static struct {
  series decode_ms, network_ms, queue_ms, render_ms, pipeline_ms, input_lag_ms;
  uint32_t frames_received, frames_rendered, frames_lost, idr_frames;
  uint64_t bytes;
  uint32_t ticks_read, ticks_unreadable, ticks_repeated, ticks_skipped, ticks_backwards;
  int last_tick;
  bool have_tick;
  int last_frame_number;
  bool page_visible;
  int last_marker;
  // The frame being decoded, built up until it is rendered
  uint64_t frame_partial_us;
  uint64_t frame_decoded_us;
  bool frame_pending;
  // Input lag: the press being waited for, and one whose marker was decoded
  // but not yet shown
  uint64_t press_sent_us;
  bool awaiting_marker;
  uint64_t lag_send_us;
  bool lag_pending;
  uint32_t presses_sent, presses_seen, presses_missed;
} state;

static void series_init(series *s, int capacity) {
  s->values = malloc(sizeof(float) * capacity);
  s->count = 0;
  s->capacity = s->values ? capacity : 0;
}

static void series_free(series *s) {
  free(s->values);
  s->values = NULL;
  s->count = s->capacity = 0;
}

static void series_add(series *s, float value) {
  if (s->count < s->capacity) {
    s->values[s->count++] = value;
  }
}

static int compare_floats(const void *a, const void *b) {
  float x = *(const float *)a;
  float y = *(const float *)b;
  return (x > y) - (x < y);
}

// Sorts in place, so call only after measuring has stopped
static float series_percentile(series *s, float p) {
  if (s->count == 0) {
    return 0;
  }
  qsort(s->values, s->count, sizeof(float), compare_floats);
  int index = (int)(p * (s->count - 1) + 0.5f);
  return s->values[index];
}

static float series_max(series *s) {
  float max = 0;
  for (int i = 0; i < s->count; i++) {
    if (s->values[i] > max) {
      max = s->values[i];
    }
  }
  return max;
}

bool vitabench_init(void) {
  return pthread_mutex_init(&bench_mutex, NULL) == 0;
}

bool vitabench_active(void) {
  return phase != PHASE_IDLE;
}

// Average luminance of a 3x3 patch at a point given as fractions of the region
static int probe(const uint8_t *pixels, int stride_bytes, float x1, float y1, float x2, float y2,
                 float fx, float fy) {
  int cx = (int)(x1 + fx * (x2 - x1));
  int cy = (int)(y1 + fy * (y2 - y1));
  int total = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      const uint8_t *p = pixels + (cy + dy) * stride_bytes + (cx + dx) * 4;
      // The channel order doesn't matter for telling black from white
      total += (p[0] + p[1] + p[2]) / 3;
    }
  }
  return total / 9;
}

void vitabench_frame_decoded(const DECODE_UNIT *du, uint64_t submit_us, uint64_t decoded_us,
                             const uint8_t *pixels, int stride_bytes,
                             float region_x1, float region_y1, float region_x2, float region_y2) {
  if (phase == PHASE_IDLE || pixels == NULL) {
    return;
  }

  // Read the strip outside the lock; it's the only part that touches pixels
  int white = probe(pixels, stride_bytes, region_x1, region_y1, region_x2, region_y2,
                    BARCODE_X + BARCODE_BLOCK_W * 0.5f, BARCODE_CY);
  int black = probe(pixels, stride_bytes, region_x1, region_y1, region_x2, region_y2,
                    BARCODE_X + BARCODE_BLOCK_W * 1.5f, BARCODE_CY);
  bool readable = white - black >= MIN_REFERENCE_CONTRAST;
  int threshold = (white + black) / 2;
  int tick = 0;
  int marker = 0;
  if (readable) {
    for (int i = 0; i < FRAME_BITS; i++) {
      int lum = probe(pixels, stride_bytes, region_x1, region_y1, region_x2, region_y2,
                      BARCODE_X + BARCODE_BLOCK_W * (i + 2.5f), BARCODE_CY);
      tick = (tick << 1) | (lum > threshold ? 1 : 0);
    }
    marker = probe(pixels, stride_bytes, region_x1, region_y1, region_x2, region_y2,
                   MARKER_CX, MARKER_CY) > threshold;
  }

  pthread_mutex_lock(&bench_mutex);
  state.page_visible = readable;
  if (phase == PHASE_MEASURING) {
    state.frames_received++;
    state.bytes += du->fullLength;
    if (du->frameType == FRAME_TYPE_IDR) {
      state.idr_frames++;
    }
    if (state.last_frame_number > 0 && du->frameNumber > state.last_frame_number + 1) {
      state.frames_lost += du->frameNumber - state.last_frame_number - 1;
    }
    state.last_frame_number = du->frameNumber;

    uint64_t network_us = du->enqueueTimeUs >= du->receiveTimeUs ? du->enqueueTimeUs - du->receiveTimeUs : 0;
    uint64_t queue_us = submit_us >= du->enqueueTimeUs ? submit_us - du->enqueueTimeUs : 0;
    uint64_t host_us = (uint64_t)du->frameHostProcessingLatency * 100;
    uint64_t decode_us = decoded_us - submit_us;
    series_add(&state.network_ms, network_us / 1000.0f);
    series_add(&state.queue_ms, queue_us / 1000.0f);
    series_add(&state.decode_ms, decode_us / 1000.0f);
    state.frame_partial_us = host_us + network_us + queue_us + decode_us;
    state.frame_decoded_us = decoded_us;
    state.frame_pending = true;

    if (readable) {
      state.ticks_read++;
      if (state.have_tick) {
        int step = (tick - state.last_tick) & TICK_MASK;
        if (step == 0) {
          state.ticks_repeated++;
        } else if (step < (1 << (FRAME_BITS - 1))) {
          state.ticks_skipped += step - 1;
        } else {
          state.ticks_backwards++;
        }
      }
      state.last_tick = tick;
      state.have_tick = true;

      // The marker turning white after a press means the press got through
      if (marker && state.last_marker == 0 && state.awaiting_marker) {
        state.awaiting_marker = false;
        state.lag_send_us = state.press_sent_us;
        state.lag_pending = true;
        state.presses_seen++;
      }
      state.last_marker = marker;
    } else {
      state.ticks_unreadable++;
    }
  }
  pthread_mutex_unlock(&bench_mutex);
}

void vitabench_frame_rendered(uint64_t rendered_us) {
  if (phase != PHASE_MEASURING) {
    return;
  }
  pthread_mutex_lock(&bench_mutex);
  state.frames_rendered++;
  if (state.frame_pending) {
    uint64_t render_us = rendered_us - state.frame_decoded_us;
    series_add(&state.render_ms, render_us / 1000.0f);
    series_add(&state.pipeline_ms, (state.frame_partial_us + render_us) / 1000.0f);
    state.frame_pending = false;
  }
  // Input lag ends when the frame showing the marker is on screen. A frame
  // dropped by the pacer is completed by the next one that is shown.
  if (state.lag_pending) {
    series_add(&state.input_lag_ms, (rendered_us - state.lag_send_us) / 1000.0f);
    state.lag_pending = false;
  }
  pthread_mutex_unlock(&bench_mutex);
}

static void reset_measurements(void) {
  pthread_mutex_lock(&bench_mutex);
  state.decode_ms.count = state.network_ms.count = state.queue_ms.count = 0;
  state.render_ms.count = state.pipeline_ms.count = state.input_lag_ms.count = 0;
  state.frames_received = state.frames_rendered = state.frames_lost = state.idr_frames = 0;
  state.bytes = 0;
  state.ticks_read = state.ticks_unreadable = state.ticks_repeated = 0;
  state.ticks_skipped = state.ticks_backwards = 0;
  state.have_tick = false;
  state.last_frame_number = 0;
  state.last_marker = -1;
  state.frame_pending = false;
  state.awaiting_marker = false;
  state.lag_pending = false;
  state.presses_sent = state.presses_seen = state.presses_missed = 0;
  pthread_mutex_unlock(&bench_mutex);
}

static void send_key(short key) {
  LiSendKeyboardEvent(key, KEY_ACTION_DOWN, 0);
  sceKernelDelayThread(KEY_HOLD_US);
  LiSendKeyboardEvent(key, KEY_ACTION_UP, 0);
}

// For unattended runs only, which have no host menu to start from: the host
// named in bench_host, or the first paired one
static device_info_t *pick_host_for_automation(void) {
  for (int i = 0; i < known_devices.count; i++) {
    device_info_t *d = &known_devices.devices[i];
    if (!d->paired) {
      continue;
    }
    if (config.bench_host == NULL || config.bench_host[0] == '\0' || strcmp(d->name, config.bench_host) == 0) {
      return d;
    }
  }
  return NULL;
}

static bool wait_for(bool (*condition)(void), uint64_t timeout_us) {
  uint64_t start = LiGetMicroseconds();
  while (!condition()) {
    if (LiGetMicroseconds() - start > timeout_us) {
      return false;
    }
    sceKernelDelayThread(POLL_US * 10);
  }
  return true;
}

static bool is_connected(void) {
  return connection_get_status() == LI_CONNECTED;
}

static bool page_is_visible(void) {
  pthread_mutex_lock(&bench_mutex);
  bool visible = state.page_visible;
  pthread_mutex_unlock(&bench_mutex);
  return visible;
}

typedef struct {
  double rtt_sum;
  uint32_t rtt_max;
  int rtt_samples;
  int rssi_sum;
  int rssi_min;
  int rssi_samples;
  int channel;
} link_samples;

static void sample_link(link_samples *link) {
  uint32_t rtt = 0, variance = 0;
  if (LiGetEstimatedRttInfo(&rtt, &variance)) {
    link->rtt_sum += rtt;
    link->rtt_max = rtt > link->rtt_max ? rtt : link->rtt_max;
    link->rtt_samples++;
  }
  SceNetCtlInfo info;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) >= 0) {
    int rssi = info.rssi_percentage;
    link->rssi_sum += rssi;
    link->rssi_min = link->rssi_samples == 0 || rssi < link->rssi_min ? rssi : link->rssi_min;
    link->rssi_samples++;
  }
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_CHANNEL, &info) >= 0) {
    link->channel = info.channel;
  }
}

// JSON strings here are app and host names, so escape the few characters that matter
static void write_json_string(FILE *f, const char *s) {
  fputc('"', f);
  for (; s && *s; s++) {
    if (*s == '"' || *s == '\\') {
      fputc('\\', f);
      fputc(*s, f);
    } else if ((unsigned char)*s < 0x20) {
      fputc(' ', f);
    } else {
      fputc(*s, f);
    }
  }
  fputc('"', f);
}

static void write_timing(FILE *f, const char *name, series *s, bool last) {
  fprintf(f, "  \"%s\": {\"p50\": %.2f, \"p95\": %.2f, \"max\": %.2f}%s\n", name,
          series_percentile(s, 0.5f), series_percentile(s, 0.95f), series_max(s), last ? "" : ",");
}

typedef struct {
  char path[512];
  float seconds;
  float input_lag_p50, input_lag_p95, latency_p50;
  float fps_rendered;
} report_summary;

static void write_report(FILE *f, const char *started, const char *host, const char *app, const char *error,
                         float seconds, const link_samples *link, report_summary *summary) {
  float one_way_ms = link->rtt_samples ? (float)(link->rtt_sum / link->rtt_samples) / 2.0f : 0;
  summary->seconds = seconds;
  summary->fps_rendered = seconds > 0 ? state.frames_rendered / seconds : 0;

  fprintf(f, "{\n");
  fprintf(f, "  \"format\": 1,\n");
  fprintf(f, "  \"version\": \"%d.%d.%d\",\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
  fprintf(f, "  \"label\": ");
  write_json_string(f, config.bench_label);
  fprintf(f, ",\n  \"started\": \"%s\",\n", started);
  fprintf(f, "  \"ok\": %s,\n  \"error\": ", error[0] ? "false" : "true");
  write_json_string(f, error);
  fprintf(f, ",\n  \"host\": ");
  write_json_string(f, host);
  fprintf(f, ",\n  \"app\": ");
  write_json_string(f, app);
  fprintf(f, ",\n  \"seconds\": %.1f,\n", seconds);
  fprintf(f, "  \"settings\": {\"width\": %d, \"height\": %d, \"fps\": %d, \"bitrate_kbps\": %d, "
             "\"packet_size\": %d, \"direct_decode\": %s, \"input_sync\": %s, \"boost_clocks\": %s, "
             "\"frame_pacer\": %s, \"vblank_wait\": %s, \"stats_overlay\": %d, \"debug_log\": %s},\n",
          config.stream.width, config.stream.height, config.stream.fps, config.stream.bitrate,
          config.stream.packetSize, config.direct_decode ? "true" : "false",
          config.input_sync_to_controller ? "true" : "false", config.boost_clocks ? "true" : "false",
          config.enable_frame_pacer ? "true" : "false", config.enable_vita_vblank_wait ? "true" : "false",
          config.stats_overlay, config.save_debug_log ? "true" : "false");
  fprintf(f, "  \"frames\": {\"received\": %u, \"rendered\": %u, \"fps_received\": %.2f, \"fps_rendered\": %.2f, "
             "\"lost_network\": %u, \"idr\": %u},\n",
          state.frames_received, state.frames_rendered,
          seconds > 0 ? state.frames_received / seconds : 0, summary->fps_rendered,
          state.frames_lost, state.idr_frames);
  fprintf(f, "  \"page\": {\"ticks_read\": %u, \"unreadable\": %u, \"repeated\": %u, \"skipped\": %u, \"backwards\": %u},\n",
          state.ticks_read, state.ticks_unreadable, state.ticks_repeated, state.ticks_skipped, state.ticks_backwards);
  fprintf(f, "  \"bitrate_mbps\": %.2f,\n", seconds > 0 ? state.bytes * 8 / seconds / 1000000.0f : 0);

  // Latency estimate: the per-frame pipeline plus half the round trip
  float pipeline_p50 = series_percentile(&state.pipeline_ms, 0.5f);
  float pipeline_p95 = series_percentile(&state.pipeline_ms, 0.95f);
  summary->latency_p50 = pipeline_p50 + one_way_ms;
  fprintf(f, "  \"latency_estimate_ms\": {\"p50\": %.2f, \"p95\": %.2f, \"max\": %.2f},\n",
          summary->latency_p50, pipeline_p95 + one_way_ms, series_max(&state.pipeline_ms) + one_way_ms);

  summary->input_lag_p50 = series_percentile(&state.input_lag_ms, 0.5f);
  summary->input_lag_p95 = series_percentile(&state.input_lag_ms, 0.95f);
  float lag_min = state.input_lag_ms.count ? state.input_lag_ms.values[0] : 0;
  fprintf(f, "  \"input_lag_ms\": {\"presses\": %u, \"seen\": %u, \"missed\": %u, \"p50\": %.2f, \"p95\": %.2f, "
             "\"min\": %.2f, \"max\": %.2f},\n",
          state.presses_sent, state.presses_seen, state.presses_missed,
          summary->input_lag_p50, summary->input_lag_p95, lag_min, series_max(&state.input_lag_ms));

  write_timing(f, "network_ms", &state.network_ms, false);
  write_timing(f, "queue_ms", &state.queue_ms, false);
  write_timing(f, "decode_ms", &state.decode_ms, false);
  write_timing(f, "render_ms", &state.render_ms, false);
  fprintf(f, "  \"rtt_ms\": {\"avg\": %.2f, \"max\": %u},\n",
          link->rtt_samples ? link->rtt_sum / link->rtt_samples : 0.0, link->rtt_max);
  fprintf(f, "  \"wifi\": {\"rssi_avg_percent\": %d, \"rssi_min_percent\": %d, \"channel\": %d}\n",
          link->rssi_samples ? link->rssi_sum / link->rssi_samples : -1,
          link->rssi_samples ? link->rssi_min : -1, link->channel);
  fprintf(f, "}\n");
}

static void save_report(const char *started_compact, const char *started, const char *host, const char *app,
                        const char *error, float seconds, const link_samples *link, report_summary *summary) {
  char dir[512];
  snprintf(dir, sizeof(dir), "%sreports", config.key_dir);
  sceIoMkdir(dir, 0777);
  snprintf(summary->path, sizeof(summary->path), "%s/bench-%s.json", dir, started_compact);

  FILE *f = fopen(summary->path, "w");
  if (f) {
    write_report(f, started, host, app, error, seconds, link, summary);
    fclose(f);
  }
  // A fixed name too, so tools can always find the latest run
  char latest[512];
  snprintf(latest, sizeof(latest), "%s/last-bench.json", dir);
  f = fopen(latest, "w");
  if (f) {
    // write_report sorts the series in place, which percentiles already did
    write_report(f, started, host, app, error, seconds, link, summary);
    fclose(f);
  }
  vita_debug_log("[BENCH] report written to %s", summary->path);
}

int vitabench_run(bool automated) {
  char error[512] = "";
  char started[32];
  char started_compact[32];
  SceDateTime now;
  sceRtcGetCurrentClockLocalTime(&now);
  snprintf(started, sizeof(started), "%04d-%02d-%02dT%02d:%02d:%02d",
           now.year, now.month, now.day, now.hour, now.minute, now.second);
  snprintf(started_compact, sizeof(started_compact), "%04d%02d%02d-%02d%02d%02d",
           now.year, now.month, now.day, now.hour, now.minute, now.second);

  link_samples link = {0};
  float measured_seconds = 0;
  bool streamed = false;

  int capacity = (config.bench_seconds + 5) * MAX_FPS_RECORDED;
  series_init(&state.decode_ms, capacity);
  series_init(&state.network_ms, capacity);
  series_init(&state.queue_ms, capacity);
  series_init(&state.render_ms, capacity);
  series_init(&state.pipeline_ms, capacity);
  series_init(&state.input_lag_ms, config.bench_seconds + 5);
  reset_measurements();

  char app_name[256] = "";
  if (automated) {
    device_info_t *host = pick_host_for_automation();
    if (host == NULL) {
      snprintf(error, sizeof(error), "No paired computer%s%s",
               config.bench_host[0] ? " called " : "", config.bench_host);
      goto done;
    }
    flash_message("Benchmark: connecting to %s...", host->name);
    if (ui_connect_open_headless(host, error, sizeof(error)) != 0) {
      goto done;
    }
  } else if (!ui_connect_server_ready()) {
    snprintf(error, sizeof(error), "Connect to a computer first, then open Settings from its menu.");
    goto done;
  }

  // The benchmark only makes sense against the benchmark page, so make sure
  // the host has an app for it before streaming anything
  int app_id = ui_connect_find_bench_app(config.bench_app, app_name, sizeof(app_name));
  if (app_id < 0) {
    snprintf(error, sizeof(error),
             "%s has no benchmark app. Add one called \"%s\" in Sunshine that opens "
             "tools/bench/bench.html (see tools/bench/README.md).",
             ui_connect_server_name(), config.bench_app);
    goto done;
  }

  vita_debug_log("[BENCH] starting %s on %s for %d s", app_name, ui_connect_server_name(), config.bench_seconds);
  flash_message("Benchmark: starting %s on %s...", app_name, ui_connect_server_name());

  // Probe from the first frame, so the page can be detected
  phase = PHASE_WARMUP;
  if (ui_connect_start_app_by_id(app_id, error, sizeof(error)) != 0) {
    goto done;
  }
  streamed = true;
  if (!wait_for(is_connected, CONNECT_TIMEOUT_US)) {
    snprintf(error, sizeof(error), "The stream didn't start within %llu s", CONNECT_TIMEOUT_US / 1000000);
    goto done;
  }
  if (!wait_for(page_is_visible, PAGE_TIMEOUT_US)) {
    snprintf(error, sizeof(error), "The benchmark page didn't appear within %llu s. Is %s showing bench.html?",
             PAGE_TIMEOUT_US / 1000000, app_name);
    goto done;
  }

  // Restart the page's animation, so every run shows the same pictures
  send_key(VK_R);
  sceKernelDelayThread(RESET_SETTLE_US);
  reset_measurements();

  phase = PHASE_MEASURING;
  uint64_t start = LiGetMicroseconds();
  uint64_t end = start + (uint64_t)config.bench_seconds * 1000000;
  uint64_t next_press = start + 500000;
  uint64_t next_sample = start;
  uint64_t t;
  while ((t = LiGetMicroseconds()) < end) {
    if (connection_get_status() != LI_CONNECTED) {
      snprintf(error, sizeof(error), "The stream ended early, after %.0f s", (t - start) / 1000000.0f);
      break;
    }
    if (t >= next_sample) {
      sample_link(&link);
      next_sample += 1000000;
    }
    if (t >= next_press) {
      pthread_mutex_lock(&bench_mutex);
      if (state.awaiting_marker && t - state.press_sent_us > PRESS_TIMEOUT_US) {
        state.presses_missed++;
        state.awaiting_marker = false;
      }
      bool waiting = state.awaiting_marker;
      pthread_mutex_unlock(&bench_mutex);
      if (!waiting) {
        uint64_t sent = LiGetMicroseconds();
        LiSendKeyboardEvent(VK_SPACE, KEY_ACTION_DOWN, 0);
        pthread_mutex_lock(&bench_mutex);
        state.press_sent_us = sent;
        state.awaiting_marker = true;
        state.presses_sent++;
        pthread_mutex_unlock(&bench_mutex);
        sceKernelDelayThread(KEY_HOLD_US);
        LiSendKeyboardEvent(VK_SPACE, KEY_ACTION_UP, 0);
      }
      next_press += PRESS_INTERVAL_US;
    }
    sceKernelDelayThread(POLL_US);
  }
  measured_seconds = (LiGetMicroseconds() - start) / 1000000.0f;

done:
  phase = PHASE_IDLE;
  pthread_mutex_lock(&bench_mutex);
  if (state.awaiting_marker) {
    state.presses_missed++;
    state.awaiting_marker = false;
  }
  pthread_mutex_unlock(&bench_mutex);

  if (streamed) {
    connection_terminate();
    if (config.bench_quit_app) {
      ui_connect_quit_app();
    }
    ui_connect_return_to_server();
  }

  report_summary summary = {0};
  save_report(started_compact, started, ui_connect_server_name(), app_name, error, measured_seconds, &link, &summary);
  if (error[0]) {
    vita_debug_log("[BENCH] failed: %s", error);
  } else {
    vita_debug_log("[BENCH] done: input lag p50 %.1f ms p95 %.1f ms, latency estimate p50 %.1f ms, %.1f fps",
                   summary.input_lag_p50, summary.input_lag_p95, summary.latency_p50, summary.fps_rendered);
  }

  series_free(&state.decode_ms);
  series_free(&state.network_ms);
  series_free(&state.queue_ms);
  series_free(&state.render_ms);
  series_free(&state.pipeline_ms);
  series_free(&state.input_lag_ms);

  if (!automated) {
    if (error[0]) {
      display_error("Benchmark failed:\n%s", error);
    } else {
      display_error("Benchmark done (%.0f s)\nInput lag: %.0f ms typical, %.0f ms worst 5%%\n"
                    "Latency estimate: %.0f ms\nDrawn: %.1f fps\nReport: %s",
                    summary.seconds, summary.input_lag_p50, summary.input_lag_p95,
                    summary.latency_p50, summary.fps_rendered, summary.path);
    }
  }
  return error[0] ? -1 : 0;
}
