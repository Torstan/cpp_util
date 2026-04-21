#ifndef COMPUTATIONAL_GEOMETRY_TEST_UTILS_H_
#define COMPUTATIONAL_GEOMETRY_TEST_UTILS_H_

#include <string>

void Assert(bool condition, const std::string& message);
void AssertEqual(double a, double b, double epsilon, const std::string& message);
void AssertEqual(double a, double b, const std::string& message);
void AssertTrue(bool condition, const std::string& message = "");
void AssertFalse(bool condition, const std::string& message = "");

#endif  // COMPUTATIONAL_GEOMETRY_TEST_UTILS_H_
