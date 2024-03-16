/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#pragma once

#include <string>
#include <sstream>

class Version {
private:
    int year, month, day;

public:
    // Parameterized constructor that extracts year, month, and day from the version string.
    explicit Version(const std::string& versionStr) : year(0), month(0), day(0) {
        std::istringstream versionStream(versionStr);
        char dot; // Used to ignore the dots in the version string.

        versionStream >> year >> dot >> month >> dot >> day;

        // Ensure all version components are non-negative.
        if (year < 0) year = 0;
        if (month < 0) month = 0;
        if (day < 0) day = 0;
    }

    // Overload the greater than (>) operator to compare two version objects.
    bool operator>(const Version& other) const {
        if (year != other.year) 
            return year > other.year;
        if (month != other.month) 
            return month > other.month;
        return day > other.day;
    }

    // Overload the equal to (==) operator to compare two version objects.
    bool operator==(const Version& other) const {
        return year == other.year && month == other.month && day == other.day;
    }

    // Overload the less than (<) operator for completeness (optional).
    bool operator<(const Version& other) const {
        return !(*this > other) && !(*this == other);
    }
};
