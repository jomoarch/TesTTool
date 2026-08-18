#ifndef FILE_COMPARATOR_HPP
#define FILE_COMPARATOR_HPP

#include <string>
#include <filesystem>

struct CompareResult {
  bool matched = false;
  std::string message = "";
};

CompareResult compare_files(const std::filesystem::path &output_path,
                            const std::filesystem::path &expected_path);

#endif // FILE_COMPARATOR_HPP