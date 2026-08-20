#include "test_result_viewer.hpp"

#include "test_result_file.hpp"
#include "test_runner.hpp"
#include "terminal.hpp"
#include "text.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace fp;

namespace {

// 一行内容（纯文本；cyan_bg
// 标记整行使用青色背景色，仅此一种样式，不含其他颜色）
struct RenderLine {
  std::string text;
  bool cyan_bg = false;
};

int clamp_int(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// 行：text 补齐到 cols（按显示宽度）
std::string pad_line(const std::string &text, int cols) {
  std::string out = text;
  int pad = cols - string_display_width(text);
  if (pad > 0)
    out.append(static_cast<size_t>(pad), ' ');
  return out;
}

// 按显示宽度取 [start_col, start_col+width) 的字符；不劈开多字节/宽字符
// 超界部分不显示（硬裁剪，绝不换行）
std::string slice_by_width(const std::string &s, int start_col, int width) {
  if (width <= 0)
    return std::string();
  if (start_col <= 0 && string_display_width(s) <= width)
    return s;
  std::string out;
  int col = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    char32_t cp = 0;
    size_t consumed = 0;
    if (!utf8_decode(s, pos, cp, consumed)) {
      ++col;
      pos += 1;
      continue;
    }
    int cw = display_width(cp);
    if (col + cw > start_col + width)
      break;
    if (col >= start_col)
      out.append(s, pos, consumed);
    col += cw;
    pos += consumed;
  }
  return out;
}

// 按终端宽度折行
std::vector<std::string> wrap_text(const std::string &text, int width) {
  std::vector<std::string> out;
  if (width < 1)
    width = 1;
  std::string cur;
  size_t pos = 0;
  while (pos < text.size()) {
    char ch = text[pos];
    if (ch == '\n') {
      out.push_back(cur);
      cur.clear();
      ++pos;
      continue;
    }
    if (ch == ' ' || ch == '\t') {
      if (!cur.empty() && string_display_width(cur) < width)
        cur.push_back(' ');
      ++pos;
      continue;
    }
    size_t w_end = pos;
    while (w_end < text.size() && text[w_end] != ' ' && text[w_end] != '\t' &&
           text[w_end] != '\n')
      ++w_end;
    std::string word = text.substr(pos, w_end - pos);
    pos = w_end;
    int ww = string_display_width(word);
    int cur_w = string_display_width(cur);
    if (cur_w + ww <= width) {
      cur += word;
    } else if (cur.empty()) {
      std::string rest = word;
      while (string_display_width(rest) > width) {
        out.push_back(slice_by_width(rest, 0, width));
        rest = slice_by_width(rest, width, 1 << 20);
      }
      cur = rest;
    } else {
      out.push_back(cur);
      cur = word;
      while (string_display_width(cur) > width) {
        out.push_back(slice_by_width(cur, 0, width));
        cur = slice_by_width(cur, width, 1 << 20);
      }
    }
  }
  if (!cur.empty())
    out.push_back(cur);
  while (!out.empty() && out.back().empty())
    out.pop_back(); // 去掉末尾空行
  return out;
}

// ---- 各视图的帧渲染 ----

std::string render_error_frame(const std::vector<std::string> &info, Size sz) {
  int cols = std::max(sz.cols, 8);
  int rows = std::max(sz.rows, 5);
  int body_rows = rows - 2;
  std::string out = "\033[H";
  out += pad_line("INVALID TEST RESULT FILE", cols);
  out += "\r\n";
  for (int i = 0; i < body_rows; ++i) {
    std::string line;
    if (static_cast<size_t>(i) < info.size())
      line = truncate_to_width(info[i], cols);
    out += pad_line(line, cols);
    if (i + 1 < body_rows)
      out += "\r\n";
  }
  out += "\r\n";
  out += pad_line("press any key to exit", cols);
  return out;
}

std::string render_ce_frame(const std::vector<std::string> &body, int scroll_x,
                            int scroll_y, Size sz) {
  int cols = std::max(sz.cols, 8);
  int rows = std::max(sz.rows, 5);
  int body_rows = rows - 3; // [CE] 行 + "===" 行 + 底栏
  std::string out = "\033[H";
  // 第一行：左侧 [CE]
  out += pad_line("[CE]", cols);
  out += "\r\n";
  // "===" 分割线
  out += pad_line(std::string(static_cast<size_t>(cols), '='), cols);
  out += "\r\n";
  // 编译错误正文（硬裁剪，不换行；[CE] 头不随滚动移动）
  for (int i = 0; i < body_rows; ++i) {
    size_t li = static_cast<size_t>(scroll_y) + static_cast<size_t>(i);
    std::string line;
    if (li < body.size())
      line = slice_by_width(body[li], scroll_x, cols);
    out += pad_line(line, cols);
    if (i + 1 < body_rows)
      out += "\r\n";
  }
  out += "\r\n";
  std::string footer =
      "CE  x=" + std::to_string(scroll_x) + " y=" + std::to_string(scroll_y) +
      "  [\xE2\x86\x90/\xE2\x86\x92] horizontal  "
      "j/k/\xE2\x86\x91/\xE2\x86\x93/wheel: vertical  q/Esc: quit";
  out += pad_line(truncate_to_width(footer, cols), cols);
  return out;
}

// 逐测试点内容（随终端宽度变化，每帧重建）
std::vector<RenderLine> build_normal_content(const AssessmentResult &r,
                                             int cols) {
  std::vector<RenderLine> out;
  if (r.cases.empty()) {
    out.push_back({"(no test cases)"});
    return out;
  }
  int max_id_w = 1;
  for (const auto &c : r.cases)
    max_id_w =
        std::max(max_id_w, static_cast<int>(std::to_string(c.id).size()));
  for (size_t i = 0; i < r.cases.size(); ++i) {
    const CaseResult &c = r.cases[i];
    // 首行：#编号#（右对齐到最大编号宽度） [状态] ====...==== 用时ms 内存MB
    // （=== 分隔线合并进首行，填满 [状态] 与用时/内存之间的空隙）
    std::string id_s = std::to_string(c.id);
    std::string head =
        "#" +
        std::string(
            static_cast<size_t>(max_id_w - static_cast<int>(id_s.size())),
            ' ') +
        id_s + "#";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", c.memory_mb);
    std::string left = head + " [" + verdict_name(c.verdict) + "]";
    std::string right = std::to_string(c.time_ms) + "ms " + buf + "MB";
    int fill =
        cols - string_display_width(left) - string_display_width(right) - 2;
    if (fill < 0)
      fill = 0; // 终端过窄时不填充
    std::string line =
        left + " " + std::string(static_cast<size_t>(fill), '=') + " " + right;
    out.push_back({truncate_to_width(line, cols), /*cyan_bg=*/true});
    if (c.verdict != Verdict::AC) {
      // 提示信息直接跟在首行下面（无 "---" 分割线；AC 无提示信息）
      // 每行前加 "| "（| 后空一格）；按 cols-2 折行，保证 "| " + 行 = cols
      // 极窄终端（cols<3）时行内容退化为 1 列并整体截断
      int msg_width = std::max(1, cols - 2);
      auto msg = wrap_text(c.message, msg_width);
      for (auto &m : msg) {
        out.push_back({truncate_to_width("| " + m, cols)});
      }
    }
  }
  return out;
}

std::string render_normal_frame(const std::vector<RenderLine> &content,
                                int scroll_y, Size sz) {
  int cols = std::max(sz.cols, 8);
  int rows = std::max(sz.rows, 5);
  int body_rows = rows - 1; // 底栏
  std::string out = "\033[H";
  for (int i = 0; i < body_rows; ++i) {
    size_t ci = static_cast<size_t>(scroll_y) + static_cast<size_t>(i);
    std::string line;
    bool cyan_bg = false;
    if (ci < content.size()) {
      line = content[ci].text;
      cyan_bg = content[ci].cyan_bg;
    }
    // 先补齐整行，再对整行（含填充的空格）应用青色背景（黑字），贯穿整行
    line = pad_line(line, cols);
    if (cyan_bg)
      line = "\033[30;106m" + line + "\033[0m";
    out += line;
    if (i + 1 < body_rows)
      out += "\r\n";
  }
  out += "\r\n";
  std::string footer =
      "j/k/\xE2\x86\x91/\xE2\x86\x93/wheel: scroll  q/Esc: quit";
  out += pad_line(truncate_to_width(footer, cols), cols);
  return out;
}

// ---- 各视图的主循环 ----

bool is_quit_key(const KeyEvent &ev) {
  return ev.kind == KeyKind::Esc || ev.kind == KeyKind::CtrlC ||
         (ev.kind == KeyKind::Char && ev.ch == 'q');
}

int run_error_screen(Terminal &term, const std::filesystem::path &path,
                     const std::string &error) {
  std::vector<std::string> info = {
      "Path: " + path_to_utf8(path),
      "",
      error,
      "",
      "The file is not a valid (lossless) test result file.",
  };
  Size sz = term.size();
  term.write(render_error_frame(info, sz));
  while (true) {
    KeyEvent ev = term.read_key(100);
    if (ev.kind == KeyKind::None) {
      Size nsz = term.size();
      if (nsz.cols != sz.cols || nsz.rows != sz.rows) {
        sz = nsz;
        term.clear_screen();
        term.write(render_error_frame(info, sz));
      }
      continue;
    }
    return 0; // 任意键退出
  }
}

int run_ce_view(Terminal &term, const std::string &compile_message) {
  // 拆行：严格保留换行，去掉每行末尾 '\r' 与末尾多余空行
  std::vector<std::string> body;
  size_t start = 0;
  for (size_t i = 0; i <= compile_message.size(); ++i) {
    if (i == compile_message.size() || compile_message[i] == '\n') {
      size_t end = i;
      if (end > start && compile_message[end - 1] == '\r')
        --end;
      body.emplace_back(compile_message, start, end - start);
      start = i + 1;
    }
  }
  while (!body.empty() && body.back().empty())
    body.pop_back();

  int max_w = 0;
  for (const auto &l : body)
    max_w = std::max(max_w, string_display_width(l));
  // 横向最多滚到“最长行宽-1”（至少留下一个字符）；纵向最多到“总行数-1”
  const int x_max = std::max(0, max_w - 1);
  const int y_max = std::max(0, static_cast<int>(body.size()) - 1);
  int scroll_x = 0, scroll_y = 0;

  Size sz = term.size();
  term.write(render_ce_frame(body, scroll_x, scroll_y, sz));
  while (true) {
    KeyEvent ev = term.read_key(100);
    if (ev.kind == KeyKind::None) {
      Size nsz = term.size();
      if (nsz.cols != sz.cols || nsz.rows != sz.rows) {
        sz = nsz;
        term.clear_screen();
        term.write(render_ce_frame(body, scroll_x, scroll_y, sz));
      }
      continue;
    }
    switch (ev.kind) {
    case KeyKind::Left:
      --scroll_x;
      break;
    case KeyKind::Right:
      ++scroll_x;
      break;
    case KeyKind::Up:
      --scroll_y;
      break;
    case KeyKind::Down:
      ++scroll_y;
      break;
    case KeyKind::WheelUp:
      --scroll_y;
      break;
    case KeyKind::WheelDown:
      ++scroll_y;
      break;
    case KeyKind::Home:
      scroll_y = 0;
      break;
    case KeyKind::End:
      scroll_y = y_max;
      break;
    case KeyKind::PgUp:
      scroll_y -= std::max(1, sz.rows - 4);
      break;
    case KeyKind::PgDn:
      scroll_y += std::max(1, sz.rows - 4);
      break;
    case KeyKind::Char:
      if (ev.ch == 'j')
        ++scroll_y;
      else if (ev.ch == 'k')
        --scroll_y;
      else if (ev.ch == 'h')
        --scroll_x;
      else if (ev.ch == 'l')
        ++scroll_x;
      break;
    default:
      break;
    }
    if (is_quit_key(ev))
      break;
    scroll_x = clamp_int(scroll_x, 0, x_max);
    scroll_y = clamp_int(scroll_y, 0, y_max);
    sz = term.size();
    term.write(render_ce_frame(body, scroll_x, scroll_y, sz));
  }
  return 0;
}

int run_normal_view(Terminal &term, const AssessmentResult &result) {
  Size sz = term.size();
  int scroll_y = 0;
  auto content = build_normal_content(result, sz.cols);
  term.write(render_normal_frame(content, scroll_y, sz));
  while (true) {
    KeyEvent ev = term.read_key(100);
    if (ev.kind == KeyKind::None) {
      Size nsz = term.size();
      if (nsz.cols != sz.cols || nsz.rows != sz.rows) {
        sz = nsz;
        term.clear_screen();
        content = build_normal_content(result, sz.cols); // 宽度变化需重建
        term.write(render_normal_frame(content, scroll_y, sz));
      }
      continue;
    }
    int delta = 0;
    switch (ev.kind) {
    case KeyKind::Up:
      delta = -1;
      break;
    case KeyKind::Down:
      delta = 1;
      break;
    case KeyKind::WheelUp:
      delta = -1;
      break;
    case KeyKind::WheelDown:
      delta = 1;
      break;
    case KeyKind::Home:
      scroll_y = 0;
      break;
    case KeyKind::End:
      scroll_y = std::max(0, static_cast<int>(content.size()) - 1);
      break;
    case KeyKind::PgUp:
      scroll_y -= std::max(1, sz.rows - 2);
      break;
    case KeyKind::PgDn:
      scroll_y += std::max(1, sz.rows - 2);
      break;
    case KeyKind::Char:
      if (ev.ch == 'j')
        delta = 1;
      else if (ev.ch == 'k')
        delta = -1;
      break;
    default:
      break;
    }
    if (is_quit_key(ev))
      break;
    if (delta != 0)
      scroll_y += delta;
    int y_max = std::max(0, static_cast<int>(content.size()) - 1);
    scroll_y = clamp_int(scroll_y, 0, y_max);
    sz = term.size();
    term.write(render_normal_frame(content, scroll_y, sz));
  }
  return 0;
}

} // namespace

int view_test_result(const std::filesystem::path &file_path) {
  AssessmentResult result;
  std::string error;
  const bool ok = load_test_result(file_path, result, error);

  Terminal term;
  if (!term.init()) {
    std::fprintf(stderr, "test_result_viewer: cannot enter interactive mode "
                         "(stdin/stdout is not a terminal)\n");
    return 1;
  }
  int rc = 0;
  try {
    if (!ok)
      rc = run_error_screen(term, file_path, error);
    else if (!result.compile_ok)
      rc = run_ce_view(term, result.compile_message);
    else
      rc = run_normal_view(term, result);
  } catch (...) {
    term.restore();
    return 1;
  }
  term.restore();
  return rc;
}
