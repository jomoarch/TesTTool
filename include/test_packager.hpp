#ifndef TEST_PACKAGER_HPP
#define TEST_PACKAGER_HPP

#include <filesystem>
#include <vector>
#include <string>

struct TestPoint {
  int id;
  std::filesystem::path input_path;
  std::filesystem::path expected_path;
  int time_limit_ms;
  float memory_limit_mb;
};

struct TestPackage {
  std::string name;
  int test_count = 0;
  bool success = false;
  std::vector<TestPoint> tests;
  std::string message;
};

TestPackage pack_tests(const std::string &name,
                       const std::filesystem::path &tests_path,
                       int time_limit_ms, float memory_limit_mb);

#endif // TEST_PACKAGER_HPP