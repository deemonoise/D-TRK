#include "ble_midi_pipewire.h"

#ifdef __linux__
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "Arduino.h"

extern char** environ;

namespace hw::ble::pipewire {
namespace {
constexpr char kHelper[] = "./ble_midi_bridge.py";

pid_t spawn(std::initializer_list<const char*> args, int output = -1) {
  if (access(kHelper, R_OK) != 0) return -1;
  std::vector<char*> argv{const_cast<char*>("python3"), const_cast<char*>(kHelper)};
  for (const auto* arg : args) argv.push_back(const_cast<char*>(arg));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) return -1;
  pid_t child = -1;
  if (output >= 0 && posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    return -1;
  }
  const int status = posix_spawnp(&child, "python3", &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  return status == 0 ? child : -1;
}

bool midiAddress(const char* text) {
  if (strlen(text) != 17) return false;
  for (int i = 0; i < 17; ++i)
    if (i % 3 == 2 ? text[i] != ':' : !std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
  return true;
}

void reap(pid_t child) {
  while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}
}  // namespace

int scan(Found* out, int max) {
  int stream[2];
  if (access(kHelper, R_OK) != 0 || pipe2(stream, O_CLOEXEC) != 0) return 0;
  pid_t child = spawn({"scan"}, stream[1]);
  close(stream[1]);
  if (child < 0) {
    close(stream[0]);
    return 0;
  }
  FILE* rows = fdopen(stream[0], "r");
  if (!rows) {
    close(stream[0]);
    stop(child);
    return 0;
  }
  int count = 0;
  char line[256];
  while (fgets(line, sizeof line, rows)) {
    auto* separator = strchr(line, '\t');
    auto* newline = strchr(line, '\n');
    if (count == max || !separator || !newline) continue;
    *separator = *newline = '\0';
    if (!midiAddress(line)) continue;
    auto& found = out[count++];
    snprintf(found.addr, sizeof found.addr, "%s", line);
    snprintf(found.name, sizeof found.name, "%s", separator + 1);
    found.type = kDeviceType;
  }
  fclose(rows);
  reap(child);
  return count;
}

pid_t start(int client, const char* in, int inPort, const char* out, int outPort) {
  if ((*in && !midiAddress(in)) || (*out && !midiAddress(out))) return -1;
  const auto address = std::to_string(client);
  const auto input = std::to_string(inPort);
  const auto output = std::to_string(outPort);
  return spawn({"connect", address.c_str(), in, input.c_str(), out, output.c_str()});
}

bool running(pid_t& child) {
  if (child < 0) return false;
  const auto status = waitpid(child, nullptr, WNOHANG);
  if (status == 0 || (status < 0 && errno == EINTR)) return true;
  child = -1;
  return false;
}

void stop(pid_t& child) {
  if (!running(child)) return;
  kill(child, SIGTERM);
  for (int wait = 0; wait < 10 && running(child); ++wait) delay(10);
  if (!running(child)) return;
  kill(child, SIGKILL);
  reap(child);
  child = -1;
}
}  // namespace hw::ble::pipewire
#endif
