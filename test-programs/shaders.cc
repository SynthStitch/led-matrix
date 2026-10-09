// Shader-style effects for the LED frame, computed per pixel on the CPU (Shadertoy-style f(x, y, time) -> colour).
// Usage: shaders [rpi-rgb-led-matrix flags] [visible-height] [effect]
//   effect: 0-6 shows one effect; omitted or -1 cycles through all of them with crossfades.
//   shaders --bench   renders every effect off-screen at 96x64 and checks it keeps up (no panels needed).
#include "led-matrix.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <thread>
#include <vector>
using namespace rgb_matrix;

static const float SECONDS_PER_EFFECT = 20;  // when cycling
static const float FADE_SECONDS = 1.5f;
static const int THREADS = 3;                // the 4th core belongs to the panel refresh
static const int FPS_CAP = 40;

static volatile bool stop = false;
static void on_sig(int) { stop = true; }

struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 norm(V3 a) { return a * (1 / len(a)); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static const float PI = 3.14159265f, TAU = 6.2831853f;

// Inigo Quilez's cosine palette: smooth rainbow-ish colours from one number.
static V3 palette(float t, V3 d = {0, 0.33f, 0.67f}) {
  return {0.5f + 0.5f * cosf(TAU * (t + d.x)), 0.5f + 0.5f * cosf(TAU * (t + d.y)), 0.5f + 0.5f * cosf(TAU * (t + d.z))};
}
static void rot(float &x, float &y, float a) { float c = cosf(a), s = sinf(a), nx = c * x - s * y; y = s * x + c * y; x = nx; }

// Each effect: u, v centred on the frame, v from -1 (top) to 1 (bottom), u scaled to keep pixels square.

static V3 neon_shapes(float u, float v, float t) {
  V3 col = {0, 0, 0};
  for (int k = 0; k < 6; ++k) {
    float x = u, y = v;
    rot(x, y, t * (0.4f + 0.15f * k) * (k % 2 ? 1 : -1));
    int n = 3 + k;  // triangle out to octagon
    float r = 0.18f + 0.16f * k, seg = TAU / n, a = atan2f(x, y) + PI;
    float d = cosf(floorf(0.5f + a / seg) * seg - a) * sqrtf(x * x + y * y) - r * cosf(seg / 2);
    float glow = 0.012f / (fabsf(d) + 0.004f);
    col = col + palette(k / 6.0f + t * 0.08f) * clampf(glow * glow, 0, 1.5f);
  }
  return col;
}

static float torus_sdf(V3 p, float t) {
  rot(p.y, p.z, t * 0.7f);
  rot(p.x, p.y, t * 0.45f);
  float q = sqrtf(p.x * p.x + p.z * p.z) - 1.0f;
  return sqrtf(q * q + p.y * p.y) - 0.4f;
}
static V3 torus(float u, float v, float t) {
  V3 ro = {0, 0, -3.2f}, rd = norm({u, -v, 1.6f});
  float d = 0;
  for (int i = 0; i < 40; ++i) {
    float s = torus_sdf(ro + rd * d, t);
    if (s < 0.002f) {
      V3 p = ro + rd * d;
      const float e = 0.002f;  // normal from the SDF gradient
      V3 n = norm({torus_sdf(p + V3{e, 0, 0}, t) - torus_sdf(p - V3{e, 0, 0}, t),
                   torus_sdf(p + V3{0, e, 0}, t) - torus_sdf(p - V3{0, e, 0}, t),
                   torus_sdf(p + V3{0, 0, e}, t) - torus_sdf(p - V3{0, 0, e}, t)});
      V3 light = norm({0.6f, 0.8f, -0.6f});
      float diff = clampf(dot(n, light), 0, 1), spec = powf(clampf(dot(norm(light - rd), n), 0, 1), 24);
      V3 base = palette(atan2f(p.y, p.x) / TAU + t * 0.1f);
      return base * (0.15f + 0.85f * diff) + V3{1, 1, 1} * spec;
    }
    d += s;
    if (d > 8) break;
  }
  return palette(v * 0.3f + t * 0.05f, {0.6f, 0.7f, 0.9f}) * 0.08f;  // dim background
}

static V3 kaleidoscope(float u, float v, float t) {
  float r = sqrtf(u * u + v * v), a = atan2f(v, u) + t * 0.3f, seg = TAU / 6;
  a = fabsf(fmodf(a + 100 * seg, seg) - seg / 2);  // fold into one mirrored slice
  float x = cosf(a) * r, y = sinf(a) * r;
  float f = fabsf(sinf(x * 9 + t * 1.5f) * cosf(y * 9 - t * 1.1f));
  return palette(r * 0.7f - t * 0.15f) * powf(1 - f, 3) * 1.4f;
}

static V3 voronoi(float u, float v, float t) {
  float d1 = 9, d2 = 9;
  int best = 0;
  for (int i = 0; i < 9; ++i) {
    float px = sinf(t * (0.3f + 0.07f * i) + i * 1.3f) * 1.3f, py = cosf(t * (0.25f + 0.05f * i) + i * 2.1f) * 0.85f;
    float d = sqrtf((u - px) * (u - px) + (v - py) * (v - py));
    if (d < d1) { d2 = d1; d1 = d; best = i; }
    else if (d < d2) d2 = d;
  }
  V3 col = palette(best / 9.0f + t * 0.03f) * clampf(1 - d1 * 0.9f, 0.15f, 1);
  float edge = clampf(1 - (d2 - d1) * 25, 0, 1);
  return col + V3{1, 1, 1} * edge * 0.8f;
}

static V3 tunnel(float u, float v, float t) {
  float r = sqrtf(u * u + v * v) + 1e-4f, a = atan2f(v, u);
  float tu = a / PI + t * 0.08f, tv = 0.5f / r + t * 0.8f;
  bool check = ((int)floorf(tu * 8) + (int)floorf(tv * 3)) & 1;
  return palette(tv * 0.15f) * (check ? 1.0f : 0.25f) * clampf(r * 1.6f, 0, 1);
}

static V3 metaballs(float u, float v, float t) {
  float sum = 0;
  for (int i = 0; i < 6; ++i) {
    float bx = sinf(t * (0.5f + 0.13f * i) + i) * 1.1f, by = cosf(t * (0.4f + 0.11f * i) + i * 2.3f) * 0.7f;
    sum += 0.06f / ((u - bx) * (u - bx) + (v - by) * (v - by) + 1e-3f);
  }
  float inside = clampf((sum - 0.9f) * 4, 0, 1), rim = clampf(1 - fabsf(sum - 1) * 6, 0, 1);
  return palette(sum * 0.15f + t * 0.1f) * inside + V3{1, 1, 1} * rim * 0.6f;
}

static V3 plasma(float u, float v, float t) {
  float cx = u + 0.5f * sinf(t / 5), cy = v + 0.5f * cosf(t / 3);
  float s = sinf(u * 3 + t) + sinf(3 * (u * sinf(t / 2) + v * cosf(t / 3)) + t) + sinf(sqrtf(100 * (cx * cx + cy * cy) + 1) + t);
  return palette(s / 6 + 0.5f + t * 0.05f);
}

typedef V3 (*Effect)(float, float, float);
static const Effect EFFECTS[] = {neon_shapes, torus, kaleidoscope, voronoi, tunnel, metaballs, plasma};
static const char *NAMES[] = {"neon shapes", "torus", "kaleidoscope", "voronoi", "tunnel", "metaballs", "plasma"};
static const int COUNT = sizeof EFFECTS / sizeof EFFECTS[0];

// Renders effect `a` (blended toward `b` by `mix`) into an RGB buffer, rows split across threads.
static void render(std::vector<uint8_t> &buf, int W, int H, int a, int b, float mix, float t) {
  auto rows = [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < W; ++x) {
        float u = (2.0f * x - W + 1) / H, v = (2.0f * y - H + 1) / H;
        V3 c = EFFECTS[a](u, v, t);
        if (mix > 0) c = c * (1 - mix) + EFFECTS[b](u, v, t) * mix;
        uint8_t *p = &buf[(y * W + x) * 3];
        float ch[3] = {c.x, c.y, c.z};
        for (int k = 0; k < 3; ++k) { float s = clampf(ch[k], 0, 1); p[k] = (uint8_t)(s * s * 255); }  // gamma 2: deeper colours on LEDs
      }
  };
  std::vector<std::thread> pool;
  for (int i = 0; i < THREADS; ++i) pool.emplace_back(rows, H * i / THREADS, H * (i + 1) / THREADS);
  for (auto &th : pool) th.join();
}

static double now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static int bench() {
  const int W = 96, H = 64, FRAMES = 60;
  std::vector<uint8_t> buf(W * H * 3);
  bool ok = true;
  for (int e = 0; e < COUNT; ++e) {
    double t0 = now();
    for (int f = 0; f < FRAMES; ++f) render(buf, W, H, e, e, 0, f / 30.0f);
    double fps = FRAMES / (now() - t0);
    long lit = 0;
    for (uint8_t c : buf) lit += c > 20;
    bool good = fps >= 30 && lit > 0;
    ok &= good;
    printf("%-13s %6.1f fps  %s\n", NAMES[e], fps, good ? "ok" : lit ? "TOO SLOW" : "BLANK");
  }
  printf(ok ? "bench OK: every effect draws and keeps 30 fps\n" : "bench FAIL\n");
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--bench")) return bench();
  RGBMatrix::Options opts;
  RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  const int W = m->width(), H = argc > 1 ? atoi(argv[1]) : m->height();
  const int only = argc > 2 ? atoi(argv[2]) : -1;
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);

  std::vector<uint8_t> buf(W * H * 3);
  FrameCanvas *off = m->CreateFrameCanvas();
  const double start = now();
  while (!stop) {
    float t = now() - start;
    int a = only, b = only;
    float mix = 0;
    if (only < 0 || only >= COUNT) {
      float phase = fmodf(t, SECONDS_PER_EFFECT * COUNT);
      a = (int)(phase / SECONDS_PER_EFFECT);
      b = (a + 1) % COUNT;
      mix = clampf((fmodf(phase, SECONDS_PER_EFFECT) - (SECONDS_PER_EFFECT - FADE_SECONDS)) / FADE_SECONDS, 0, 1);
    }
    render(buf, W, H, a, b, mix, t);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const uint8_t *p = &buf[(y * W + x) * 3];
        off->SetPixel(x, y, p[0], p[1], p[2]);
      }
    off = m->SwapOnVSync(off);
    double spare = 1.0 / FPS_CAP - (now() - start - t);  // cap the frame rate so three cores aren't pinned
    if (spare > 0) usleep(spare * 1e6);
  }
  delete m;
  return 0;
}
