#include "file_comparator.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>

namespace {

std::string char_repr(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  if (std::isprint(uc)) {
    return "'" + std::string(1, c) + "'";
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "'\\x%02x'", uc);
  return buf;
}

bool read_file(const std::filesystem::path &path, std::string &content) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return false;
  std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  content.resize(static_cast<size_t>(size));
  if (size > 0) {
    f.read(content.data(), size);
  }
  return f.good() || f.eof();
}

class LineReader {
public:
  explicit LineReader(const std::string &data)
      : data_(data), pos_(0), line_no_(0) {}

  struct LineView {
    const char *start;
    size_t len;
    int line_no;
    bool has_line;
  };

  LineView next() {
    if (pos_ >= data_.size()) {
      return {nullptr, 0, line_no_, false};
    }
    size_t line_start = pos_;
    size_t line_end = pos_;
    while (line_end < data_.size() && data_[line_end] != '\n')
      ++line_end;
    size_t content_end = line_end;
    if (content_end > line_start && data_[content_end - 1] == '\r')
      --content_end;
    while (content_end > line_start && data_[content_end - 1] == ' ')
      --content_end;
    if (line_end < data_.size()) {
      pos_ = line_end + 1;
    } else {
      pos_ = data_.size();
    }
    ++line_no_;
    return {data_.data() + line_start, content_end - line_start, line_no_,
            true};
  }

private:
  const std::string &data_;
  size_t pos_;
  int line_no_;
};

} // namespace

CompareResult compare_files(const std::filesystem::path &output_path,
                            const std::filesystem::path &expected_path) {
  std::string output;
  std::string expected;

  if (!read_file(output_path, output)) {
    return {false, "Cannot read output file"};
  }
  if (!read_file(expected_path, expected)) {
    return {false, "Cannot read expected file"};
  }

  {
    LineReader tmp(output);
    bool has_content = false;
    while (true) {
      auto line = tmp.next();
      if (!line.has_line)
        break;
      if (line.len > 0) {
        has_content = true;
        break;
      }
    }
    if (!has_content) {
      return {false, "Output file is empty"};
    }
  }

  LineReader reader_out(output);
  LineReader reader_exp(expected);

  while (true) {
    auto line_out = reader_out.next();
    auto line_exp = reader_exp.next();

    if (!line_out.has_line && !line_exp.has_line) {
      return {true, "Files matched"};
    }

    if (!line_out.has_line || !line_exp.has_line) {
      if (!line_out.has_line) {
        while (line_exp.has_line) {
          if (line_exp.len > 0) {
            return {false, "Line " + std::to_string(line_exp.line_no) +
                               " is too short"};
          }
          line_exp = reader_exp.next();
        }
      } else {
        while (line_out.has_line) {
          if (line_out.len > 0) {
            return {false, "Line " + std::to_string(line_out.line_no) +
                               " is too long"};
          }
          line_out = reader_out.next();
        }
      }
      return {true, "Files matched"};
    }

    if (line_out.len != line_exp.len) {
      if (line_out.len < line_exp.len) {
        return {false,
                "Line " + std::to_string(line_out.line_no) + " is too short"};
      } else {
        return {false,
                "Line " + std::to_string(line_out.line_no) + " is too long"};
      }
    }

    for (size_t i = 0; i < line_out.len; ++i) {
      if (line_out.start[i] != line_exp.start[i]) {
        char got = line_out.start[i];
        char expected = line_exp.start[i];
        return {false, "Line " + std::to_string(line_out.line_no) +
                           ", character " + std::to_string(i + 1) + ": got " +
                           char_repr(got) + ", expected " +
                           char_repr(expected)};
      }
    }
  }
}