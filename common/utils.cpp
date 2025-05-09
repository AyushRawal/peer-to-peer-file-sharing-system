#include "utils.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std;

string PortAddress::sprint() {
  char str[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &(this->ip), str, sizeof(str)) == NULL) {
    log_error("Could not convert ip to string:", strerror(errno));
    return "";
  }
  return string(str) + ":" + to_string(this->port);
}

vector<string> split(const string &str, char delimiter) {
  vector<string> result;
  size_t start = 0;
  size_t end = str.find(delimiter);
  while (end != string::npos) {
    result.push_back(str.substr(start, end - start));
    start = end + 1;
    end = str.find(delimiter, start);
  }
  result.push_back(str.substr(start));

  return result;
}

PortAddress parse_port_address(string port_address) {
  PortAddress res;
  vector<string> tokens = split(port_address, ':');
  if (tokens.size() < 2) panic("invalid port address");
  if (inet_pton(AF_INET, tokens[0].c_str(), &res.ip) <= 0) panic("invalid ip address");
  res.port = (uint16_t)strtol(tokens[1].c_str(), nullptr, 10);
  if (res.port == 0) { panic("Invalid port number"); }
  return res;
}

vector<string> read_n_file_lines(string file_path, size_t n) {
  int fd = open(file_path.c_str(), O_RDONLY);
  char buf[256];
  ssize_t bytes = read(fd, buf, sizeof(buf));
  if (bytes < 0) panic("Could not read file:", strerror(errno));
  close(fd);
  string in(buf, (size_t)bytes);
  vector<string> lines = split(in, '\n');
  if (lines.size() < n) panic("Invalid tracker info");
  return lines;
}

PortAddress read_tracker_info(string file_path, size_t tracker_count) {
  PortAddress res = parse_port_address(read_n_file_lines(file_path, TRACKERS)[tracker_count - 1]);
  return res;
}

void listen_for_peers(PortAddress self_info, void (*handle_peer)(int sock)) {
  struct sockaddr_in addr;
  addr.sin_addr.s_addr = self_info.ip;
  addr.sin_port = htons(self_info.port); // convert from host to network byte order
  addr.sin_family = AF_INET;

  int listen_sock;
  if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log_error("Could not create socket:", strerror(errno));
    return;
  }

  int opt = 1;
  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    log_error("setsockopt failed", strerror(errno));
    close(listen_sock);
    return;
  }

  if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(listen_sock);
    log_error("Could not bind socket:", strerror(errno));
    return;
  }

  if (listen(listen_sock, 5) < 0) {
    log_error("Could not start listening:", strerror(errno));
    close(listen_sock);
    return;
  }

  log_info("Listening on port:", self_info.port);

  while (true) {
    int sock;
    if ((sock = accept(listen_sock, NULL, NULL)) < 0) {
      log_error("Could not accept connection:", strerror(errno));
      continue;
    }
    thread t(handle_peer, sock);
    t.detach();
  }
  close(listen_sock);
}

void send_msg(int sock, string msg) {
  if (msg.size() == 0) msg += " ";
  size_t msg_size = htonl(msg.size());
  if (send(sock, &msg_size, sizeof(msg_size), 0) < 0) return log_error("error sending message:", strerror(errno));
  if (send(sock, msg.c_str(), msg.size(), 0) < 0) return log_error("error sending message:", strerror(errno));
}

string recv_msg(int sock) {
  size_t msg_size = 0;
  ssize_t n_bytes = 0;
  if ((n_bytes = read(sock, &msg_size, sizeof(msg_size))) < 0) {
    log_error("Could not read from socket:", strerror(errno));
    return "";
  }
  if (n_bytes == 0) {
    log_error(sock, "disconnected");
    return "quit";
  }
  msg_size = ntohl(msg_size);
  vector<char> msg_buf(msg_size);
  ssize_t recieved = 0;
  while (recieved < msg_size) {
    if ((n_bytes = read(sock, msg_buf.data() + recieved, msg_size - recieved)) < 0) {
      log_error("Could not read from socket:", strerror(errno));
      return "";
    }
    if (n_bytes == 0) { return "quit"; }
    recieved += n_bytes;
  }
  string res = string(msg_buf.begin(), msg_buf.end());
  return res;
}
