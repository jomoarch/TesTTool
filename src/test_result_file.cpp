#include "test_result_file.hpp"

#include "text.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char *kMagic = "TESTOOL_RESULT v1";
const char *kCompileOkMessage = "Compilation succeeded";

// ---- base64（标准字母表 + '=' 填充）----

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(const std::string &data) {
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                 (static_cast<unsigned char>(data[i + 1]) << 8) |
                 static_cast<unsigned char>(data[i + 2]);
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]);
    out.push_back(kB64[v & 63]);
  }
  size_t rem = data.size() - i;
  if (rem == 1) {
    unsigned v = static_cast<unsigned char>(data[i]) << 16;
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                 (static_cast<unsigned char>(data[i + 1]) << 8);
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

int b64_val(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

bool b64_decode(const std::string &s, std::string &out) {
  out.clear();
  if (s.size() % 4 != 0)
    return false;
  size_t pad = 0;
  if (!s.empty() && s[s.size() - 1] == '=')
    ++pad;
  if (s.size() >= 2 && s[s.size() - 2] == '=')
    ++pad;
  if (pad > 2)
    return false;
  for (size_t i = 0; i < s.size() - pad; ++i) {
    if (b64_val(s[i]) < 0)
      return false;
  }
  for (size_t i = s.size() - pad; i < s.size(); ++i) {
    if (s[i] != '=')
      return false;
  }
  out.reserve(s.size() / 4 * 3);
  for (size_t i = 0; i < s.size(); i += 4) {
    int a = b64_val(s[i]);
    int b = (i + 1 < s.size()) ? b64_val(s[i + 1]) : 0;
    int c = (i + 2 < s.size() && s[i + 2] != '=') ? b64_val(s[i + 2]) : 0;
    int d = (i + 3 < s.size() && s[i + 3] != '=') ? b64_val(s[i + 3]) : 0;
    if (a < 0 || b < 0 || c < 0 || d < 0)
      return false;
    unsigned v = (static_cast<unsigned>(a) << 18) |
                 (static_cast<unsigned>(b) << 12) |
                 (static_cast<unsigned>(c) << 6) | static_cast<unsigned>(d);
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    if (i + 2 < s.size() && s[i + 2] != '=')
      out.push_back(static_cast<char>((v >> 8) & 0xFF));
    if (i + 3 < s.size() && s[i + 3] != '=')
      out.push_back(static_cast<char>(v & 0xFF));
  }
  return true;
}

// ---- 文件读取 ----

// 读取全部行（兼容 \r\n；中间空行保留，文件末尾 '\n' 不产生多余空行）
bool read_lines(const std::filesystem::path &path,
                std::vector<std::string> &lines, std::string &error) {
  lines.clear();
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open file: " + fp::path_to_utf8(path);
    return false;
  }
  std::string data((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  size_t start = 0;
  for (size_t i = 0; i <= data.size(); ++i) {
    if (i == data.size() || data[i] == '\n') {
      if (start < i || i < data.size()) {
        size_t end = i;
        if (end > start && data[end - 1] == '\r')
          --end;
        lines.emplace_back(data, start, end - start);
      }
      start = i + 1;
    }
  }
  return true;
}

bool parse_verdict(const std::string &s, Verdict &v) {
  if (s == "AC") {
    v = Verdict::AC;
    return true;
  }
  if (s == "WA") {
    v = Verdict::WA;
    return true;
  }
  if (s == "TLE") {
    v = Verdict::TLE;
    return true;
  }
  if (s == "MLE") {
    v = Verdict::MLE;
    return true;
  }
  if (s == "RE") {
    v = Verdict::RE;
    return true;
  }
  return false;
}

std::string verdict_str(Verdict v) {
  switch (v) {
  case Verdict::AC:
    return "AC";
  case Verdict::WA:
    return "WA";
  case Verdict::TLE:
    return "TLE";
  case Verdict::MLE:
    return "MLE";
  case Verdict::RE:
    return "RE";
  }
  return "?";
}

// 剩余行是否只含空行（允许文件末尾有空白行）
bool only_empty_rest(const std::vector<std::string> &lines, size_t idx) {
  for (size_t k = idx; k < lines.size(); ++k) {
    if (!lines[k].empty())
      return false;
  }
  return true;
}

} // namespace

bool save_test_result(const std::filesystem::path &file_path,
                      const AssessmentResult &result, std::string &error) {
  std::ofstream f(file_path, std::ios::binary | std::ios::trunc);
  if (!f) {
    error = "cannot open file for writing: " + fp::path_to_utf8(file_path);
    return false;
  }
  f << kMagic << "\n";
  if (!result.compile_ok) {
    f << "CE 1\n";
    f << b64_encode(result.compile_message) << "\n";
  } else {
    f << "CE 0\n";
    f << result.cases.size() << "\n";
    for (const auto &c : result.cases) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "CASE %d %s %d %.2f", c.id,
                    verdict_str(c.verdict).c_str(), c.time_ms, c.memory_mb);
      f << buf << "\n";
      f << b64_encode(c.message) << "\n";
    }
  }
  if (!f) {
    error = "write failed: " + fp::path_to_utf8(file_path);
    return false;
  }
  return true;
}

bool load_test_result(const std::filesystem::path &file_path,
                      AssessmentResult &result, std::string &error) {
  result = AssessmentResult{};
  std::vector<std::string> lines;
  if (!read_lines(file_path, lines, error))
    return false;

  size_t idx = 0;
  auto next = [&](std::string &out) -> bool {
    if (idx >= lines.size())
      return false;
    out = lines[idx++];
    return true;
  };

  std::string line;
  if (!next(line) || line != kMagic) {
    error = "not a test result file (bad magic)";
    return false;
  }
  if (!next(line)) {
    error = "truncated file (missing CE marker)";
    return false;
  }

  if (line == "CE 1") {
    // 编译失败：CE 标记 + 编译信息
    if (!next(line)) {
      error = "truncated file (missing compile message)";
      return false;
    }
    if (!b64_decode(line, result.compile_message)) {
      error = "invalid base64 in compile message";
      return false;
    }
    result.compile_ok = false;
    if (!only_empty_rest(lines, idx)) {
      error = "trailing content after CE block";
      return false;
    }
    return true;
  }

  if (line != "CE 0") {
    error = "invalid CE marker: " + line;
    return false;
  }
  result.compile_ok = true;
  result.compile_message = kCompileOkMessage; // 编译成功时固定文案

  if (!next(line)) {
    error = "truncated file (missing case count)";
    return false;
  }
  char *endp = nullptr;
  long n = std::strtol(line.c_str(), &endp, 10);
  if (endp == line.c_str() || *endp != '\0' || n < 0) {
    error = "invalid case count: " + line;
    return false;
  }

  for (long i = 0; i < n; ++i) {
    if (!next(line)) {
      error = "truncated file (missing CASE line)";
      return false;
    }
    std::istringstream iss(line);
    std::string kw, verdict_s, time_s, mem_s;
    int id = 0;
    if (!(iss >> kw >> id >> verdict_s >> time_s >> mem_s) || kw != "CASE") {
      error = "invalid CASE line: " + line;
      return false;
    }
    std::string extra;
    if (iss >> extra) {
      error = "extra fields in CASE line: " + line;
      return false;
    }

    CaseResult cs;
    cs.id = id;
    if (!parse_verdict(verdict_s, cs.verdict)) {
      error = "invalid verdict: " + verdict_s;
      return false;
    }
    char *t_end = nullptr;
    long t = std::strtol(time_s.c_str(), &t_end, 10);
    if (t_end == time_s.c_str() || *t_end != '\0' || t < 0) {
      error = "invalid time: " + time_s;
      return false;
    }
    cs.time_ms = static_cast<int>(t);
    char *m_end = nullptr;
    float m = std::strtof(mem_s.c_str(), &m_end);
    if (m_end == mem_s.c_str() || *m_end != '\0' || m < 0) {
      error = "invalid memory: " + mem_s;
      return false;
    }
    cs.memory_mb = m;

    if (!next(line)) {
      error = "truncated file (missing case message)";
      return false;
    }
    if (!b64_decode(line, cs.message)) {
      error = "invalid base64 in case message";
      return false;
    }
    if (cs.id != static_cast<int>(i) + 1) {
      error = "case id out of order: expected " + std::to_string(i + 1) +
              ", got " + std::to_string(cs.id);
      return false;
    }
    result.cases.push_back(std::move(cs));
  }

  if (!only_empty_rest(lines, idx)) {
    error = "trailing content after cases";
    return false;
  }
  return true;
}
