// 测评结果查看器演示：全屏渲染测评结果文件（支持滚动与鼠标滚轮）
// usage: view_result_demo <result_file>
#include "test_result_viewer.hpp"

#include <cstdio>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <result_file>\n", argv[0]);
    return 2;
  }
  return view_test_result(argv[1]);
}
