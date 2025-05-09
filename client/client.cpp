#include "../common/utils.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <libgen.h>
#include <openssl/evp.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using namespace std;

#define LOG_LEVEL 2
#define CHUNK_SIZE 512
#define TRACKERS 2
#define PIECE_SIZE 524288 // 512 KB; 512 * 1024 bytes

struct File {
  __off_t size;
  vector<string> hashes;
  string hash;
  int fd;
  size_t rem;
  string path;
  bool open;
};

unordered_map<string, File> groupFiles; // group, file-name -> File

void handle_peer(int sock) {
  log_info("Peer connected:", sock);

  while (true) {
    string msg = recv_msg(sock);
    if (msg == "") {
      close(sock);
      return;
    }
    if (msg == "quit") {
      log_info("peer disconnected:", sock);
      close(sock);
      return;
    }
    log_info("Client", sock, msg);
    vector<string> cmd = split(msg, ' ');
    if (cmd[0] != "request_file_piece" || cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    size_t piece = strtoul(cmd[2].c_str(), nullptr, 10);
    if (piece == 0) return send_msg(sock, "invalid input, piece value should be positive");
    piece = piece - 1;
    if (groupFiles.find(cmd[1]) == groupFiles.end()) return send_msg(sock, "file does not exist");
    File &f = groupFiles[cmd[1]];
    log_info("opening", f.path, "for sharing");
    f.fd = open(f.path.c_str(), O_RDONLY);
    if (f.fd < 0) {
      log_error("could not open file for sharing");
      return;
    }
    log_info("opened", f.path, "for sharing");
    char buf[PIECE_SIZE];

    if (lseek(f.fd, piece * PIECE_SIZE, SEEK_SET) < 0) {
      log_error("error seeking file", strerror(errno));
      close(f.fd);
      return;
    }
    ssize_t n_bytes;
    if ((n_bytes = read(f.fd, &buf, sizeof(buf))) < 0) {
      log_error("error reading file", strerror(errno));
      close(f.fd);
      return;
    }
    close(f.fd);
    send_msg(sock, "Success");
    // size_t msg_size = htonl(sizeof(buf));
    size_t msg_size = htonl(n_bytes);
    if (send(sock, &msg_size, sizeof(msg_size), 0) < 0) return log_error("error sending message:", strerror(errno));
    if (send(sock, buf, n_bytes, 0) < 0) return log_error("error sending message:", strerror(errno));
  }
  close(sock);
}

void connect_to_tracker(string tracker_info_file_path, int &tracker_sock) {
  PortAddress tracker_info[TRACKERS];
  vector<string> file_lines = read_n_file_lines(tracker_info_file_path, TRACKERS);
  for (size_t i = 0; i < TRACKERS; i++) tracker_info[i] = parse_port_address(file_lines[i]);

  if ((tracker_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) panic("Could not create socket:", strerror(errno));

  bool success = false;
  for (int i = 0; i < TRACKERS; i++) {
    log_info("Connecting to tracker", i + 1);
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = tracker_info[i].ip;
    server_addr.sin_port = htons(tracker_info[i].port); // convert from host to network byte order
    server_addr.sin_family = AF_INET;

    if (connect(tracker_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
      log_error("Could not connect to server:", strerror(errno));
    } else {
      success = true;
      break;
    }
  }
  if (!success) {
    close(tracker_sock);
    exit(EXIT_FAILURE);
  }

  log_info("Connected to server");
}

string hash_to_hex(unsigned char *hash, unsigned int hash_len) {
  stringstream ss;
  for (unsigned int i = 0; i < hash_len; i++) ss << hex << setw(2) << setfill('0') << (int)hash[i];
  return ss.str();
}

bool get_file_hashes(File &f) {
  char buffer[PIECE_SIZE];
  ssize_t n_bytes;
  EVP_MD_CTX *md_chunk_ctx = EVP_MD_CTX_new();
  EVP_MD_CTX *md_total_ctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_sha1();
  EVP_DigestInit_ex(md_total_ctx, md, nullptr);
  while ((n_bytes = read(f.fd, buffer, sizeof(buffer))) > 0) {
    unsigned char chunk_hash[EVP_MAX_MD_SIZE];
    unsigned int chunk_hash_len = 0;
    EVP_DigestInit_ex(md_chunk_ctx, md, nullptr);
    EVP_DigestUpdate(md_chunk_ctx, buffer, (size_t)n_bytes);
    EVP_DigestFinal_ex(md_chunk_ctx, chunk_hash, &chunk_hash_len);
    f.hashes.push_back(hash_to_hex(chunk_hash, chunk_hash_len));
    EVP_DigestUpdate(md_total_ctx, buffer, (size_t)n_bytes);
  }
  if (n_bytes < 0) {
    log_error("Error reading file:", strerror(errno));
    return false;
  }
  unsigned char total_hash[EVP_MAX_MD_SIZE];
  unsigned int total_hash_len = 0;
  EVP_DigestFinal_ex(md_total_ctx, total_hash, &total_hash_len);
  f.hash = hash_to_hex(total_hash, total_hash_len);
  return true;
}

void download_file(string groupId, string file_name, int tracker_sock) {
  string file_id = groupId + "::" + file_name;
  File &f = groupFiles[file_id];
  log_info("increasing file size to", f.size, "for writing");
  if (lseek(f.fd, f.size - 1, SEEK_SET) == -1) {
    log_error("Could not seek file:", strerror(errno));
    close(f.fd);
    groupFiles.erase(file_id);
    return;
  }
  if (write(f.fd, "", 1) != 1) {
    log_error("error writing file:", strerror(errno));
    close(f.fd);
    groupFiles.erase(file_id);
    return;
  }
  while (f.rem) {
    send_msg(tracker_sock, sprint("get_rarest_piece_info", groupId, file_name));
    string msg = recv_msg(tracker_sock);
    if (msg == "" || msg == "quit") {
      log_error("maybe tracker disconnected");
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }
    vector<string> info = split(msg, '\n');
    if (info[0] != "Success") {
      log_error("Server:", msg);
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }
    if (info.size() < 3) {
      log_error("something unexpected happened (info)", msg);
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }
    size_t piece = strtoul(info[1].c_str(), nullptr, 10);
    if (piece == 0) {
      log_error("something unexpected happened (count)", msg);
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }

    int peer = socket(AF_INET, SOCK_STREAM, 0);
    if (peer < 0) {
      log_error("could not create socket", strerror(errno));
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }

    bool success = false;
    size_t i = 2;
    while (not success and info.size() - i >= 2) {
      print("connecting to peer", i);
      vector<string> tmp = split(info[info.size() - i], ':');
      if (tmp.size() < 3) {
        log_error("something unexpected happened", msg);
        close(peer);
        close(f.fd);
        groupFiles.erase(file_id);
        return;
      }
      PortAddress peer_info = parse_port_address(tmp[0] + ":" + tmp[1]);

      struct sockaddr_in peer_addr;
      peer_addr.sin_addr.s_addr = peer_info.ip;
      peer_addr.sin_port = htons(peer_info.port); // convert from host to network byte order
      peer_addr.sin_family = AF_INET;

      if (connect(peer, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        log_error("Could not connect to peer:", strerror(errno));
        i++;
      } else {
        success = true;
      }
    }
    if (not success) {
      log_error("could not connect to any peer");
      close(peer);
      close(f.fd);
      groupFiles.erase(file_id);
      return;
    }
    log_info("Connected to peer");
    send_msg(peer, sprint("request_file_piece", file_id, piece));
    msg = recv_msg(peer);
    if (msg != "Success") {
      log_error("something unexpected happened", msg);
      return;
    }
    size_t msg_size = 0;
    ssize_t n_bytes = 0;
    if ((n_bytes = read(peer, &msg_size, sizeof(msg_size))) < 0) {
      log_error("Could not read from socket:", strerror(errno));
      return;
    }
    if (n_bytes == 0) {
      log_error(peer, "disconnected");
      return;
    }
    msg_size = ntohl(msg_size);
    log_info("Piece", piece, "size:", msg_size);
    // vector<char> msg_buf(msg_size);
    char msg_buf[PIECE_SIZE];
    ssize_t recieved = 0;
    while (recieved < msg_size) {
      if ((n_bytes = read(peer, msg_buf + recieved, msg_size - recieved)) < 0) {
        log_error("Could not read from socket (here):", strerror(errno));
        return;
      }
      if (n_bytes == 0) return;
      recieved += n_bytes;
    }
    piece--;
    if (lseek(f.fd, piece * PIECE_SIZE, SEEK_SET) < 0) {
      log_error("could not seek file", strerror(errno));
      return;
    }
    if (write(f.fd, &msg_buf, msg_size) < 0) {
      log_error("error writing file", strerror(errno));
      return;
    }
    f.rem--;
  }
  log_info("file downloaded");
  log_info("closing", f.path);
  close(f.fd);
}

int main(int argc, char *argv[]) {
  if (argc < 3) panic("Invalid usage, 2 arguments are required");

  signal(SIGINT, [](int sig) {
    (void)sig;
    cout << endl;
    panic("Caught interrupt signal!! Exiting...");
  });

  PortAddress self_info = parse_port_address(string(argv[1]));
  // int listen_sock;
  thread tl(listen_for_peers, self_info, handle_peer);
  tl.detach();

  int tracker_sock;
  connect_to_tracker(string(argv[2]), tracker_sock);

  string input;
  while (true) {
    cout << "> ";
    getline(cin, input);
    if (input == "") continue;

    vector<string> tokens = split(input, ' ');

    if (tokens[0] == "quit") {
      send_msg(tracker_sock, "quit");
      break;

    } else if (tokens[0] == "login") {
      if (tokens.size() != 3) {
        log_error("Invalid command, login needs 2 arguments");
        continue;
      }
      send_msg(tracker_sock, sprint(input, self_info.sprint()));

    } else if (tokens[0] == "upload_file") {
      if (tokens.size() != 3) {
        log_error("Invalid command, upload_file requires 2 arguments");
        continue;
      }
      File f;
      f.fd = open(tokens[1].c_str(), O_RDONLY);
      if (f.fd < 0) {
        log_error("Could not open file:", strerror(errno));
        continue;
      }
      f.path = tokens[1];
      if (!get_file_hashes(f)) {
        close(f.fd);
        continue;
      }
      f.rem = 0;
      struct stat file_stat;
      if (fstat(f.fd, &file_stat) < 0) {
        log_error("could not stat file", strerror(errno));
        close(f.fd);
        continue;
      }
      f.size = file_stat.st_size;
      close(f.fd);
      if (f.size == 0) {
        log_error("empty file; not uploading");
        continue;
      }
      send_msg(tracker_sock, sprint(input, f.hash, f.size, f.hashes.size()));
      string msg = recv_msg(tracker_sock);
      if (msg == "" || msg == "quit") {
        log_error("may be server disconnected");
        continue;
      }
      if (msg != "Success") {
        log_error("Server:", msg);
        continue;
      }
      for (const auto &hash : f.hashes) send_msg(tracker_sock, hash);
      msg = recv_msg(tracker_sock);
      if (msg == "" || msg == "quit") {
        log_error("may be server disconnected");
        continue;
      }
      if (msg != "file uploaded") {
        log_error("something unexpected happened:", msg);
        continue;
      }
      string file_name = basename(tokens[1].data());
      groupFiles[tokens[2] + "::" + file_name] = f;
      print("Server:", msg);
      continue;

    } else if (tokens[0] == "download_file") {
      if (tokens.size() < 4) {
        log_error("Invalid command, download_file requires 3 arguments");
        continue;
      }
      send_msg(tracker_sock, input);
      string msg = recv_msg(tracker_sock);
      if (msg.size() == 0 || msg == "quit") {
        log_error("some error occured, may be tracker disconnected");
        break;
      }
      vector<string> info = split(msg, '\n');
      if (info[0] != "Success" || info.size() < 3) {
        print("Server:", msg);
        continue;
      }
      vector<string> file_info = split(info[1], ' '); // grpId filename size hash chunkCount
      if (file_info.size() < 5) {
        log_error("invalid data", msg);
        continue;
      }
      File f;
      f.size = strtoul(file_info[2].c_str(), nullptr, 10);
      if (f.size == 0) {
        log_error("something unexpected happened (f.size):", msg);
        continue;
      }
      f.hash = file_info[3];
      size_t count = strtoul(file_info[4].c_str(), nullptr, 10);
      if (count == 0 || count != info.size() - 3) {
        print("Count:", count);
        print("info.size() - 2:", info.size() - 2);
        log_error("something unexpected happened (count):", msg);
        continue;
      }
      log_info("opening file:", tokens[3], "for writing");
      f.fd = open(tokens[3].c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (f.fd < 0) {
        log_error("could not open file:", strerror(errno));
        continue;
      }
      f.path = tokens[3];
      log_info("opened", f.path, "for writing");
      f.hashes.resize(count);
      for (size_t i = 0; i < count; i++) f.hashes[i] = info[i + 2];
      f.rem = count;
      string file_id = file_info[0] + "::" + file_info[1];
      groupFiles[file_id] = f;
      thread t(download_file, file_info[0], file_info[1], tracker_sock);
      t.detach();
      continue;

    } else {
      send_msg(tracker_sock, input);
    }
    string msg = recv_msg(tracker_sock);
    if (msg == "quit") {
      log_error("Server disconnected");
      break;
    } else if (msg == "") break;
    print("Server:", msg);
  }

  close(tracker_sock);
  return 0;
}
