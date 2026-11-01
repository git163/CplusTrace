#include "symbolizer.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace vmon {
namespace symbolizer {
namespace {

// 通过 fork/exec 执行命令并捕获 stdout。不经过 shell。
bool run_command(const std::vector<std::string>& args, std::string* output,
                 std::string* error) {
  if (args.empty()) {
    if (error) *error = "空命令";
    return false;
  }

  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) {
    if (error) *error = std::string("pipe 失败: ") + std::strerror(errno);
    return false;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    if (error) *error = std::string("fork 失败: ") + std::strerror(errno);
    return false;
  }

  if (pid == 0) {
    ::close(pipe_fds[0]);
    ::dup2(pipe_fds[1], STDOUT_FILENO);
    ::close(pipe_fds[1]);
    const int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      ::dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    ::_exit(127);
  }

  ::close(pipe_fds[1]);
  output->clear();
  char buffer[4096];
  while (true) {
    const ssize_t n = ::read(pipe_fds[0], buffer, sizeof(buffer));
    if (n > 0) {
      output->append(buffer, static_cast<size_t>(n));
      if (output->size() > 8 * 1024 * 1024) break;
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
  ::close(pipe_fds[0]);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (error) *error = "命令执行失败: " + args[0];
    return false;
  }
  return true;
}

bool file_exists(const std::string& path) {
  return ::access(path.c_str(), X_OK) == 0;
}

std::string trim_text(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}  // namespace

std::string Symbolizer::tool() const {
  if (!tool_.empty()) return tool_;
  const char* candidates[] = {"/usr/bin/addr2line", "addr2line",
                              "/usr/bin/llvm-symbolizer",
                              "llvm-symbolizer"};
  for (const char* candidate : candidates) {
    if (file_exists(candidate)) return candidate;
  }
  return "addr2line";
}

std::string Symbolizer::extract_address(const std::string& frame_text) {
  const size_t pos = frame_text.find("0x");
  if (pos == std::string::npos) {
    return trim_text(frame_text);
  }
  size_t end = pos + 2;
  while (end < frame_text.size()) {
    const char c = frame_text[end];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) break;
    ++end;
  }
  if (end == pos + 2) return std::string();
  return frame_text.substr(pos, end - pos);
}

BuildId Symbolizer::build_id_of(const std::string& binary) const {
  BuildId out;
  if (binary.empty()) return out;
  std::string text;
  std::string error;

#if defined(__APPLE__)
  if (run_command({"dwarfdump", "--uuid", binary}, &text, &error)) {
    // 输出形如: UUID: 1234-... (arm64) binary
    const size_t pos = text.find("UUID:");
    if (pos != std::string::npos) {
      size_t begin = pos + 5;
      while (begin < text.size() && text[begin] == ' ') ++begin;
      size_t end = begin;
      while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
      }
      out.value = text.substr(begin, end - begin);
      out.kind = "macho";
      out.valid = !out.value.empty();
      return out;
    }
  }
#else
  if (run_command({"readelf", "-n", binary}, &text, &error)) {
    const size_t pos = text.find("Build ID:");
    if (pos != std::string::npos) {
      size_t begin = pos + 9;
      while (begin < text.size() && text[begin] == ' ') ++begin;
      size_t end = begin;
      while (end < text.size() &&
             !std::isspace(static_cast<unsigned char>(text[end]))) {
        ++end;
      }
      out.value = text.substr(begin, end - begin);
      out.kind = "elf";
      out.valid = !out.value.empty();
      return out;
    }
  }
#endif
  (void)error;
  return out;
}

bool Symbolizer::symbolize(const std::string& binary,
                           const std::vector<std::string>& addresses,
                           std::vector<Frame>* frames,
                           std::string* error) const {
  if (frames == nullptr) return false;
  frames->clear();
  if (binary.empty() || addresses.empty()) {
    if (error) *error = "缺少二进制或地址";
    return false;
  }

  const std::string resolved_tool = tool();
  std::vector<std::string> args;
  const bool is_llvm = resolved_tool.find("llvm-symbolizer") != std::string::npos;
  if (is_llvm) {
    args = {resolved_tool, "--obj=" + binary, "--demangle", "--no-inlines"};
  } else {
    args = {resolved_tool, "-f", "-C", "-e", binary};
  }
  for (const std::string& address : addresses) {
    args.push_back(address.compare(0, 2, "0x") == 0 ? address : "0x" + address);
  }

  std::string output;
  std::string run_error;
  if (!run_command(args, &output, &run_error)) {
    if (error) *error = run_error;
    return false;
  }

  std::istringstream lines(output);
  std::string line;
  std::vector<std::string> collected;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;  // llvm-symbolizer 用空行分隔地址
    collected.push_back(line);
  }

  // addr2line 与 llvm-symbolizer(--no-inlines) 都是每个地址两行：
  // 第一行函数名，第二行 file:line。
  for (size_t i = 0; i < addresses.size(); ++i) {
    Frame frame;
    frame.address = addresses[i];
    if (i * 2 + 1 < collected.size()) {
      frame.function = collected[i * 2];
      const std::string location = collected[i * 2 + 1];
      const size_t colon = location.rfind(':');
      if (colon != std::string::npos) {
        frame.file = location.substr(0, colon);
        frame.line = static_cast<int>(std::strtol(location.c_str() + colon + 1,
                                                  nullptr, 10));
      } else {
        frame.file = location;
      }
      frame.resolved = frame.function != "??" && frame.file != "??";
    }
    frames->push_back(frame);
  }
  return true;
}

}  // namespace symbolizer
}  // namespace vmon
