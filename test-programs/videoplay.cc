// Loops a .ledv video (made on a PC with tools/ledvideo.py) on the frame.
// Usage: videoplay [rpi-rgb-led-matrix flags] video.ledv
#include "led-matrix.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
using namespace rgb_matrix;

static volatile bool stop = false;
static void on_sig(int) { stop = true; }
static double now() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
  RGBMatrix::Options opts;
  RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  if (argc < 2) { fprintf(stderr, "usage: videoplay [flags] video.ledv\n"); return 1; }
  FILE *f = fopen(argv[1], "rb");
  uint8_t head[10];
  if (!f || fread(head, 1, 10, f) != 10 || memcmp(head, "LEDV", 4)) { fprintf(stderr, "not a .ledv file: %s\n", argv[1]); return 1; }
  const int w = head[4] | head[5] << 8, h = head[6] | head[7] << 8, fps = head[8] | head[9] << 8;
  if (w <= 0 || h <= 0 || fps <= 0) { fprintf(stderr, "bad header in %s\n", argv[1]); return 1; }
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);

  std::vector<uint8_t> frame(w * h * 3);
  FrameCanvas *off = m->CreateFrameCanvas();
  double due = now();
  long shown = 0;
  while (!stop) {
    if (fread(frame.data(), 1, frame.size(), f) != frame.size()) {  // end of video: loop
      if (!shown) { fprintf(stderr, "no frames in %s\n", argv[1]); return 1; }
      fseek(f, 10, SEEK_SET);
      shown = 0;
      continue;
    }
    ++shown;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const uint8_t *p = &frame[(y * w + x) * 3];
        off->SetPixel(x, y, p[0], p[1], p[2]);
      }
    off = m->SwapOnVSync(off);
    due += 1.0 / fps;
    double wait = due - now();
    if (wait > 0) usleep(wait * 1e6);
    else if (wait < -1) due = now();  // fell far behind (slow SD card?): resync instead of racing
  }
  delete m;
  return 0;
}
