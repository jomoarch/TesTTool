#include "compiler.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

CompileResult compile(const std::filesystem::path &source_path,
                      const std::filesystem::path &output_path, bool enableO2) {
  CompileResult result;
  result.success = false;

  if (!std::filesystem::exists(source_path)) {
    result.message = "Source file does not exist: " + source_path.string();
    return result;
  }
  if (!std::filesystem::is_regular_file(source_path)) {
    result.message =
        "Source file is not a regular file: " + source_path.string();
    return result;
  }

  if (std::filesystem::exists(output_path)) {
    try {
      std::filesystem::remove(output_path);
    } catch (const std::filesystem::filesystem_error &e) {
      result.message =
          "Failed to remove existing output file: " + std::string(e.what());
      return result;
    }
    if (std::filesystem::exists(output_path)) {
      result.message =
          "Cannot remove existing output file: " + output_path.string();
      return result;
    }
  }

  std::string command = "g++ -std=c++14";
  if (enableO2) {
    command += " -O2";
  }
  command += " \"" + source_path.string() + "\"";
  command += " -o \"" + output_path.string() + "\"";
  command += " 2>&1";

  std::string compile_output;
  FILE *pipe = POPEN(command.c_str(), "r");
  if (!pipe) {
    result.message = "Failed to execute compiler command (popen failed)";
    return result;
  }

  char buf[256];
  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    compile_output += buf;
  }
  int status = PCLOSE(pipe);

  if (status == 0) {
    result.success = true;
    result.message = "Compilation succeeded";
  } else {
    result.success = false;
    if (compile_output.empty()) {
      compile_output =
          "Compiler returned non-zero exit code " + std::to_string(status);
    }
    result.message = compile_output;
  }

  return result;
}