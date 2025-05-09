#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <vector>
#include <sstream>

#define LOG_LEVEL 2
#define endl "\n"

#define CHUNK_SIZE 512
#define TRACKERS 2

using namespace std;

template <typename T> void print(const T &t) { cout << t << endl; }
template <typename T, typename... F> void print(const T &t, const F &...f) {
  cout << t << " ";
  print(f...);
}
template <typename T> void print_stderr(const T &t) { cerr << t << endl; }
template <typename T, typename... F> void print_stderr(const T &t, const F &...f) {
  cerr << t << " ";
  print(f...);
}
template <typename T> string sprint(const T &t) {
  stringstream ss;
  ss << t;
  return ss.str();
}
template <typename T, typename... F> string sprint(const T &t, const F &...f) {
  stringstream ss;
  ss << t << " " << sprint(f...);
  return ss.str();
}

template <typename... F> void log_error(const F &...f) {
  if (LOG_LEVEL) print_stderr("[ERROR]", f...);
}
template <typename... F> void log_info(const F &...f) {
  if (LOG_LEVEL > 1) print("[INFO]", f...);
}
template <typename... F> void panic(const F &...f) {
  log_error(f...);
  exit(EXIT_FAILURE);
}

struct PortAddress {
  uint32_t ip;
  uint16_t port;
  string sprint();
};

vector<string> split(const string &str, char delimiter);
vector<string> read_n_file_lines(string file_path, size_t n);
PortAddress parse_port_address(string port_address);
void listen_for_peers(PortAddress self_info, void (*handle_peer)(int sock));
void send_msg(int sock, string msg);
string recv_msg(int sock);
