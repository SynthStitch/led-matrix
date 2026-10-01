// Red fill, white border and a label (argv after the flags) to judge a multiplexing setting at a glance.
#include "led-matrix.h"
#include "graphics.h"
#include <unistd.h>
#include <signal.h>
using namespace rgb_matrix;
static volatile bool stop = false;
static void on_sig(int) { stop = true; }
int main(int argc, char **argv) {
  RGBMatrix::Options opts; RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  const char *label = argc > 1 ? argv[1] : "?";
  Font font; font.LoadFont("/opt/rpi-rgb-led-matrix/fonts/9x18B.bdf");
  const int w = m->width(), h = m->height();
  m->Fill(80, 0, 0);
  const Color white(255, 255, 255);
  DrawLine(m, 0, 0, w - 1, 0, white); DrawLine(m, 0, h - 1, w - 1, h - 1, white);
  DrawLine(m, 0, 0, 0, h - 1, white); DrawLine(m, w - 1, 0, w - 1, h - 1, white);
  DrawText(m, font, 4, 22, white, label);
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
  while (!stop) usleep(100000);
  delete m;
  return 0;
}
