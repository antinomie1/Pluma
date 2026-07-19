/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <string>

/** Resolve java.exe. Prefer explicit path, then JAVA_HOME, PATH, common install dirs. */
std::string find_java(const std::string& preferred = {});
