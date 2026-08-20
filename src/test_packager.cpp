#include "test_packager.hpp"
#include <regex>
#include <map>
#include <algorithm>
#include <cctype>

TestPackage pack_tests(const std::string &name,
                       const std::filesystem::path &tests_path,
                       int time_limit_ms, float memory_limit_mb) {
  TestPackage result;
  result.name = name;
  result.success = false;
  result.test_count = 0;

  if (!std::filesystem::exists(tests_path)) {
    result.message = "Path does not exist: " + tests_path.string();
    return result;
  }
  if (!std::filesystem::is_directory(tests_path)) {
    result.message = "Path is not a directory: " + tests_path.string();
    return result;
  }
  if (std::filesystem::is_empty(tests_path)) {
    result.message = "Directory is empty";
    return result;
  }

  std::map<int, std::filesystem::path> in_files;
  std::map<int, std::filesystem::path> out_files;
  std::regex number_regex(R"(\d+)");

  try {
    for (const auto &entry : std::filesystem::directory_iterator(tests_path)) {
      if (!std::filesystem::is_regular_file(entry.status())) {
        result.message =
            "Directory contains non-regular item: " + entry.path().string();
        return result;
      }

      const auto &path = entry.path();
      std::string ext = path.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext != ".in" && ext != ".out") {
        result.message = "Unexpected file type: " + path.string();
        return result;
      }

      std::string stem = path.stem().string();
      std::smatch match;
      if (!std::regex_search(stem, match, number_regex)) {
        result.message = "No numeric ID found in filename: " + path.string();
        return result;
      }

      std::string remaining = match.suffix().str();
      if (std::regex_search(remaining, number_regex)) {
        result.message =
            "Multiple numeric sequences in filename: " + path.string();
        return result;
      }

      int id = std::stoi(match.str());
      if (ext == ".in") {
        if (in_files.find(id) != in_files.end()) {
          result.message = "Duplicate .in file for ID " + std::to_string(id) +
                           ": " + path.string();
          return result;
        }
        in_files[id] = path;
      } else {
        if (out_files.find(id) != out_files.end()) {
          result.message = "Duplicate .out file for ID " + std::to_string(id) +
                           ": " + path.string();
          return result;
        }
        out_files[id] = path;
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    result.message = "Filesystem error: " + std::string(e.what());
    return result;
  }

  if (in_files.size() != out_files.size()) {
    result.message = "Mismatch in number of .in and .out files: in=" +
                     std::to_string(in_files.size()) +
                     ", out=" + std::to_string(out_files.size());
    return result;
  }

  for (const auto &p : in_files) {
    if (out_files.find(p.first) == out_files.end()) {
      result.message = "Missing .out for ID " + std::to_string(p.first);
      return result;
    }
  }

  int count = static_cast<int>(in_files.size());
  for (int i = 1; i <= count; ++i) {
    if (in_files.find(i) == in_files.end()) {
      result.message = "Missing .in for ID " + std::to_string(i);
      return result;
    }
    if (out_files.find(i) == out_files.end()) {
      result.message = "Missing .out for ID" + std::to_string(i);
      return result;
    }
  }

  std::vector<TestPoint> tests;
  tests.reserve(count);
  for (int i = 1; i <= count; ++i) {
    TestPoint tp;
    tp.id = i;
    tp.input_path = in_files[i];
    tp.expected_path = out_files[i];
    tp.time_limit_ms = time_limit_ms;
    tp.memory_limit_mb = memory_limit_mb;
    tests.push_back(std::move(tp));
  }

  result.tests = std::move(tests);
  result.test_count = count;
  result.success = true;
  result.message = "Success";
  return result;
}