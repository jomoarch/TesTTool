#ifndef COMPILER_HPP
#define COMPILER_HPP

#include <filesystem>
#include <string>

struct CompileResult {
  bool success = false;
  std::string message;
};

CompileResult compile(const std::filesystem::path &source_path,
                      const std::filesystem::path &output_path,
                      bool enableO2 = true);

#endif // COMPILER_HPP