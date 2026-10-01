// Cycle the whole display through red, green, blue and white to check each colour line.
#include "led-matrix.h"
#include <unistd.h>
#include <signal.h>
using namespace rgb_matrix;
static volatile bool stop = false;
static void on_sig(int) { stop = true; }
int main(int argc, char **argv) {
  RGBMatrix::Options opts; RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
  const uint8_t cols[][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
  for (int i = 0; !stop; i = (i + 1) % 4) {
    m->Fill(cols[i][0], cols[i][1], cols[i][2]);
    for (int t = 0; t < 40 && !stop; ++t) usleep(100000);
  }
  delete m;
  return 0;
}
