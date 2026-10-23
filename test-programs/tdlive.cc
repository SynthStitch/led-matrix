// Shows frames streamed live from TouchDesigner (or anything) over UDP.
// Each datagram is one raw frame: W*H*3 bytes of rgb24, rows top-first. Port 5005, any address (IPv4/IPv6).
// Usage: tdlive [rpi-rgb-led-matrix flags] [width height]   (default 96 96)
#include "led-matrix.h"
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
  const int w = argc > 2 ? atoi(argv[1]) : 96, h = argc > 2 ? atoi(argv[2]) : 96;
  const size_t need = (size_t)w * h * 3;

  int s = socket(AF_INET6, SOCK_DGRAM, 0);
  int off6 = 0, rcv = 1 << 20;
  setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &off6, sizeof off6);  // accept IPv4 too
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);         // room for a few fragmented frames
  sockaddr_in6 a = {};
  a.sin6_family = AF_INET6;
  a.sin6_port = htons(5005);
  a.sin6_addr = in6addr_any;
  if (s < 0 || bind(s, (sockaddr *)&a, sizeof a) < 0) { perror("tdlive: bind :5005"); return 1; }
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
  fprintf(stderr, "tdlive: waiting for %dx%d rgb24 frames on UDP 5005\n", w, h);

  std::vector<uint8_t> buf(65536), frame(need);
  FrameCanvas *canvas = m->CreateFrameCanvas();
  double last = 0;
  bool blank = true;
  while (!stop) {
    pollfd p = {s, POLLIN, 0};
    if (poll(&p, 1, 100) <= 0) {  // nothing for 100 ms
      if (!blank && now() - last > 2) { canvas->Clear(); canvas = m->SwapOnVSync(canvas); blank = true; }
      continue;
    }
    bool got = false;
    ssize_t n;
    while ((n = recv(s, buf.data(), buf.size(), MSG_DONTWAIT)) >= 0)  // drain: keep only the newest frame
      if ((size_t)n == need) { memcpy(frame.data(), buf.data(), need); got = true; }
    if (!got) continue;
    last = now(); blank = false;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const uint8_t *px = &frame[(y * w + x) * 3];
        canvas->SetPixel(x, y, px[0], px[1], px[2]);
      }
    canvas = m->SwapOnVSync(canvas);
  }
  delete m;
  return 0;
}
