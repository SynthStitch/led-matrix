// Light one row (argv after the flags) in white, to see where each address actually lands.
#include "led-matrix.h"
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
using namespace rgb_matrix;
static volatile bool stop = false;
static void on_sig(int) { stop = true; }
int main(int argc, char **argv) {
  RGBMatrix::Options opts; RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  const int row = argc > 1 ? atoi(argv[1]) : 0;
  for (int y = row; y < m->height(); y += 32)  // same row on every parallel port
    for (int x = 0; x < m->width(); ++x) m->SetPixel(x, y, 255, 255, 255);
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
  while (!stop) usleep(100000);
  delete m;
  return 0;
}
