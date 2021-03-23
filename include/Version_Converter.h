/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

struct Version {
 private:
  // Define four member variables major, minor, revision and build.
  // Here, we are saying it as version-tag
  int major, minor, revision, build, commitCount;
  char *branch, *commit;

 public:
  // Parametarized constructor. Pass string to it and it will
  // extract version-tag from it.
  //
  // Use initializer list to assign version-tag variables
  // Assign it by zero, otherwise std::scanf() will assign
  // garbage value to the version-tag, if number of version-tag
  // will be less than four.
  explicit Version(const std::string& version) : major(0), minor(0), revision(0), build(0), commitCount(0), branch(const_cast<char*>("master")), commit(const_cast<char*>("")) {
    if (sscanf(version.c_str(), "v%d.%d.%d.%d-%s-%d-%s", &major, &minor, &revision, &build, branch, &commitCount, commit) != 7) {
      if (sscanf(version.c_str(), "v%d.%d.%d.%d", &major, &minor, &revision, &build) != 4) {
        sscanf(version.c_str(), "%d.%d.%d.%d", &major, &minor, &revision, &build);
      }
    }

    // version-tag must be >=0, if it is less than zero, then make it zero.
    if (major < 0) major = 0;
    if (minor < 0) minor = 0;
    if (revision < 0) revision = 0;
    if (build < 0) build = 0;
    if (commitCount < 0) commitCount = 0;
  }

  // Overload greater than(>) operator to compare two version objects
  bool operator>(const Version& other) {
    // Start comparing version tag from left most.
    // There are three relation between two number. It could be >, < or ==
    // While comparing the version tag, if they are equal, then move to the
    // next version-tag. If it could be greater then return true otherwise
    // return false.

    if (branch != other.branch) return false;

    // Compare major version-tag
    if (major > other.major)
      return true;
    else if (major < other.major)
      return false;

    // Compare moinor version-tag
    // If control came here it means that above version-tag(s) are equal
    if (minor > other.minor)
      return true;
    else if (minor < other.minor)
      return false;

    // Compare revision version-tag
    // If control came here it means that above version-tag(s) are equal
    if (revision > other.revision)
      return true;
    else if (revision < other.revision)
      return false;

    // Compare build version-tag
    // If control came here it means that above version-tag(s) are equal
    if (build > other.build)
      return true;
    else if (build < other.build)
      return false;

    if (commitCount > other.commitCount)
      return true;
    else if (commitCount < other.commitCount)
      return false;

    return false;
  }

  // Overload equal to(==) operator to compare two version
  bool operator==(const Version& other) {
    return major == other.major && minor == other.minor && revision == other.revision && build == other.build && branch == other.branch && commitCount == other.commitCount &&
           commit == other.commit;
  }
};
