#include "../common/utils.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <libgen.h>
#include <netinet/in.h>
#include <pthread.h>
#include <set>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using namespace std;

struct File {
  size_t size;
  string hash;
  vector<string> hashes;
  vector<set<string>> locs;         // piece -> clients
  unordered_map<string, string> mp; // client -> file-path
  string get_rarest_piece_info(string curr_client_addr) {
    size_t minn = SIZE_MAX;
    string res;
    for (size_t i = 0; i < hashes.size(); i++) {
      if (locs[i].size() < minn and locs[i].find(curr_client_addr) == locs[i].end()) {
        minn = locs[i].size();
        res.clear();
        res = "Success\n";
        res += to_string(i + 1) + "\n";
        for (auto x : locs[i]) {
          res += x + ":" + mp[x] + "\n";
        }
      }
    }
    return res;
  }
  void stop_share(string curr_client_addr) {
    for (auto loc : locs)
      if (loc.find(curr_client_addr) != loc.end()) loc.erase(curr_client_addr);
    if (mp.find(curr_client_addr) != mp.end()) mp.erase(curr_client_addr);
  }
  string get_file_info(string groupId, string file_name) {
    string res = "Success\n";
    res += sprint(groupId, file_name, size, hash, hashes.size());
    res += "\n";
    for (auto h: hashes) res += h + "\n";
    return res;
  }
  void update_piece_info(size_t piece, string curr_client_addr, string file_path) {
    locs[piece].insert(curr_client_addr);
    mp[curr_client_addr] = file_path;
  }
};

struct Group {
  string owner;
  set<string> members;
  set<string> requests;
  unordered_map<string, File> filesMap; // file-name -> File
};

unordered_map<int, pair<string, string>> activeUsers; // sock -> username, port-adddress
unordered_map<string, string> userIdMap;              // username -> password
unordered_map<string, Group> groupsMap;               // group-name -> Group

bool is_logged_in(int sock) { return activeUsers.find(sock) != activeUsers.end(); }
bool is_registered(string userId) { return userIdMap.find(userId) != userIdMap.end(); }
bool group_exists(string groupId) { return groupsMap.find(groupId) != groupsMap.end(); }
bool is_member(string userId, string groupId) {
  return groupsMap[groupId].members.find(userId) != groupsMap[groupId].members.end();
}
bool is_membership_requested(string userId, string groupId) {
  return groupsMap[groupId].requests.find(userId) != groupsMap[groupId].requests.end();
}
bool file_exists(string groupId, string filename) {
  return groupsMap[groupId].filesMap.find(filename) != groupsMap[groupId].filesMap.end();
}

void logout(int sock) {
  activeUsers.erase(sock);
  for (auto group : groupsMap) {
    if (is_member(activeUsers[sock].first, group.first)) {
      for (auto file : group.second.filesMap) file.second.stop_share(activeUsers[sock].second);
    }
  }
}

void handle_command(int sock, string s) {
  vector<string> cmd = split(s, ' ');
  if (cmd.size() == 0) return;

  if (cmd[0] == "create_user") {
    if (cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    if (is_logged_in(sock)) return send_msg(sock, "already logged in");
    if (is_registered(cmd[1])) return send_msg(sock, "user already exists");
    userIdMap[cmd[1]] = cmd[2];
    send_msg(sock, "user created");

  } else if (cmd[0] == "login") {
    if (cmd.size() < 4) return send_msg(sock, "INVALID COMMAND");
    if (is_logged_in(sock)) return send_msg(sock, "already logged in");
    if (not is_registered(cmd[1])) return send_msg(sock, "Invalid user id");
    if (userIdMap[cmd[1]] != cmd[2]) return send_msg(sock, "Invalid password");
    send_msg(sock, "logged in");
    activeUsers[sock] = {cmd[1], cmd[3]};

  } else if (cmd[0] == "create_group") {
    if (cmd.size() < 2) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (group_exists(cmd[1])) return send_msg(sock, "group already exists");
    Group g;
    g.owner = activeUsers[sock].first;
    g.members.insert(activeUsers[sock].first);
    groupsMap[cmd[1]] = g;
    send_msg(sock, "group created");

  } else if (cmd[0] == "join_group") {
    if (cmd.size() < 2) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (is_member(activeUsers[sock].first, cmd[1])) return send_msg(sock, "already a member");
    if (is_membership_requested(activeUsers[sock].first, cmd[1])) return send_msg(sock, "already requested");
    groupsMap[cmd[1]].requests.insert(activeUsers[sock].first);
    send_msg(sock, "request sent");

  } else if (cmd[0] == "leave_group") {
    if (cmd.size() < 2) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (not is_member(activeUsers[sock].first, cmd[1])) return send_msg(sock, "not a member");
    groupsMap[cmd[1]].members.erase(activeUsers[sock].first);
    if (groupsMap[cmd[1]].owner == activeUsers[sock].first)
      groupsMap[cmd[1]].owner = *groupsMap[cmd[1]].members.begin(); // owner left; change owner
    if (groupsMap[cmd[1]].members.size() == 0) {
      send_msg(sock, "last member. deleting group");
      groupsMap.erase(cmd[1]);
    }

  } else if (cmd[0] == "list_requests") {
    if (cmd.size() < 2) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (groupsMap[cmd[1]].owner != activeUsers[sock].first) return send_msg(sock, "unauthorized");
    string resp;
    for (auto req : groupsMap[cmd[1]].requests) resp += "\n" + req;
    send_msg(sock, resp);

  } else if (cmd[0] == "accept_request") {
    if (cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (groupsMap[cmd[1]].owner != activeUsers[sock].first) return send_msg(sock, "unauthorized");
    if (not is_registered(cmd[2])) return send_msg(sock, "user does not exist");
    if (not is_membership_requested(cmd[2], cmd[1])) return send_msg(sock, "not requested");
    groupsMap[cmd[1]].requests.erase(cmd[2]);
    groupsMap[cmd[1]].members.insert(cmd[2]);
    send_msg(sock, "request accepted");

  } else if (cmd[0] == "list_groups") {
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    string resp;
    for (auto group : groupsMap) resp += "\n" + group.first + "\t" + group.second.owner;
    send_msg(sock, resp);

  } else if (cmd[0] == "upload_file") { // filePath GrpId fileHash fileSize chunkCount
    print("uploading...");
    if (cmd.size() < 4) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[2])) return send_msg(sock, "group does not exist");
    if (not is_member(activeUsers[sock].first, cmd[2])) return send_msg(sock, "not a member of the group");
    string file_path = cmd[1];
    string file_name = string(basename(cmd[1].data()));
    if (file_exists(cmd[2], file_name)) return send_msg(sock, "file with same name already exists");
    size_t count = strtoul(cmd[5].c_str(), nullptr, 10);
    if (count == 0) return send_msg(sock, "invalid argument");
    File f;
    f.hash = cmd[3];
    f.size = strtoul(cmd[4].c_str(), nullptr, 10);
    f.hashes.reserve(count);
    f.locs.resize(count, set<string>{activeUsers[sock].second});
    f.mp[activeUsers[sock].second] = file_path;
    send_msg(sock, "Success");
    while (count--) {
      print("reading hash", count);
      string hash = recv_msg(sock);
      if (hash == "") return;
      f.hashes.push_back(hash);
    }
    groupsMap[cmd[2]].filesMap[file_name] = f;
    send_msg(sock, "file uploaded");

  } else if (cmd[0] == "list_files") {
    if (cmd.size() < 2) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exit");
    if (not is_member(activeUsers[sock].first, cmd[1])) return send_msg(sock, "not a member of the group");
    string response;
    for (auto f : groupsMap[cmd[1]].filesMap) response += f.first + "\t" + to_string(f.second.size) + "\n";
    send_msg(sock, response);

  } else if (cmd[0] == "stop_share") {
    if (cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "not logged in");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (not file_exists(cmd[1], cmd[2])) return send_msg(sock, "file does not exist");
    groupsMap[cmd[1]].filesMap[cmd[2]].stop_share(activeUsers[sock].second);
    send_msg(sock, "stopped sharing");

  } else if (cmd[0] == "logout") {
    if (not is_logged_in(sock)) return send_msg(sock, "not logged in");
    logout(sock);
    send_msg(sock, "logged out");

  } else if (cmd[0] == "download_file") {
    if (cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (not file_exists(cmd[1], cmd[2])) return send_msg(sock, "file does not exist");
    string response = groupsMap[cmd[1]].filesMap[cmd[2]].get_file_info(cmd[1], cmd[2]);
    send_msg(sock, response);

  } else if (cmd[0] == "get_rarest_piece_info") {
    if (cmd.size() < 3) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (not is_member(activeUsers[sock].first, cmd[1])) return send_msg(sock, "not a member of the group");
    if (not file_exists(cmd[1], cmd[2])) return send_msg(sock, "file does not exist");
    string rare_piece_info = groupsMap[cmd[1]].filesMap[cmd[2]].get_rarest_piece_info(activeUsers[sock].second);
    send_msg(sock, rare_piece_info);

  } else if (cmd[0] == "update_piece_info") {  // grpId filename file-path piece
    if (cmd.size() < 5) return send_msg(sock, "INVALID COMMAND");
    if (not is_logged_in(sock)) return send_msg(sock, "login first");
    if (not group_exists(cmd[1])) return send_msg(sock, "group does not exist");
    if (not file_exists(cmd[1], cmd[2])) return send_msg(sock, "file does not exist");
    size_t piece = strtoul(cmd[4].c_str(), nullptr, 10);
    if (piece == 0) return send_msg(sock, "INVALID INPUT; peice number should be positive");
    groupsMap[cmd[1]].filesMap[cmd[2]].update_piece_info(piece, activeUsers[sock].second, cmd[3]);
    send_msg(sock, "updated");

  } else {
    send_msg(sock, "unknown command: " + cmd[0]);
  }
}

void handle_client(int sock) {
  log_info("Client connected:", sock);

  while (true) {
    string msg = recv_msg(sock);
    if (msg == "") {
      close(sock);
      return;
    }
    if (msg == "quit") {
      log_info("Client disconnected:", sock);
      if (is_logged_in(sock)) logout(sock);
      close(sock);
      return;
    }
    log_info("Client", sock, msg);
    handle_command(sock, msg);
  }
  close(sock);
}

int main(int argc, char *argv[]) {
  if (argc < 3) panic("Invalid usage, 2 arguments are required");

  signal(SIGINT, [](int sig) {
    (void)sig;
    cout << endl;
    panic("Caught interrupt signal!! Exiting...");
  });

  size_t tracker_count = strtoul(argv[2], nullptr, 10);
  if (tracker_count <= 0 or tracker_count > TRACKERS) panic("invalid tracker number");
  PortAddress tracker_info = parse_port_address(read_n_file_lines(string(argv[1]), TRACKERS)[tracker_count - 1]);

  // int server_sock;
  listen_for_peers(tracker_info, handle_client);

  // close(server_sock);
  return 0;
}
