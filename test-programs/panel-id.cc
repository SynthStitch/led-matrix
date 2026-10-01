// Fill each chained 32x32 panel with its own colour and draw its chain index.
#include "led-matrix.h"
#include "graphics.h"
#include <unistd.h>
#include <signal.h>
#include <string>
using namespace rgb_matrix;
static volatile bool stop = false;
static void on_sig(int) { stop = true; }
int main(int argc, char **argv) {
  RGBMatrix::Options opts; RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  Font font; font.LoadFont("/opt/rpi-rgb-led-matrix/fonts/7x13B.bdf");
  const Color cols[] = {{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255}};
  const int panels = m->width() / 32;
  for (int p = 0; p < panels; ++p) {
    const Color &c = cols[p % 6];
    for (int x = 0; x < 32; ++x)
      for (int y = 0; y < m->height(); ++y)
        m->SetPixel(p * 32 + x, y, c.r, c.g, c.b);
    std::string n = std::to_string(p + 1);
    DrawText(m, font, p * 32 + 12, 20, Color(0, 0, 0), n.c_str());
  }
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
  while (!stop) sleep(1);
  delete m;
  return 0;
}
