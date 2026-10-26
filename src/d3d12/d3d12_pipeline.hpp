/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#pragma once
#include "airconv_public.h"
#include "util_env.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

namespace dxmt {

/* Writes a shader that failed to compile, and the root signature it was compiled
 * against, to DXMT_SHADER_DUMP_PATH (a Windows path, e.g. Z:\tmp\dumps) so the
 * airconv CLI can reproduce the failure offline. With DXMT_SHADER_DUMP_ALL=1,
 * DumpShader writes every shader the same way. */
inline void
DumpFailedShader(const char *stage, const void *bytecode, size_t length, const void *rootsig, size_t rootsig_length) {
  static const std::string dir = env::getEnvVar("DXMT_SHADER_DUMP_PATH");
  if (dir.empty() || !bytecode)
    return;
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < length; i++)
    hash = (hash ^ static_cast<const uint8_t *>(bytecode)[i]) * 0x100000001b3ull;
  char name[64];
  auto write = [&](const char *suffix, const void *data, size_t size) {
    snprintf(name, sizeof(name), "\\%016llx.%s", (unsigned long long)hash, suffix);
    if (FILE *f = fopen((dir + name).c_str(), "wb")) {
      fwrite(data, 1, size, f);
      fclose(f);
    }
  };
  write(stage, bytecode, length);
  if (rootsig && rootsig != bytecode)
    write("rootsig", rootsig, rootsig_length);
}

/* DXMT_SKIP_SHADERS is a comma-separated list of shader hashes (as named by the
 * dumps); pipelines using one of them don't draw or dispatch. A debugging aid for
 * bisecting which pass renders wrongly. */
inline bool
IsShaderSkipped(const void *bytecode, size_t length) {
  static const std::string list = env::getEnvVar("DXMT_SKIP_SHADERS");
  if (list.empty() || !bytecode)
    return false;
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < length; i++)
    hash = (hash ^ static_cast<const uint8_t *>(bytecode)[i]) * 0x100000001b3ull;
  char name[17];
  snprintf(name, sizeof(name), "%016llx", (unsigned long long)hash);
  return list.find(name) != std::string::npos;
}

inline uint64_t
ShaderHash(const void *bytecode, size_t length) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (size_t i = 0; bytecode && i < length; i++)
    hash = (hash ^ static_cast<const uint8_t *>(bytecode)[i]) * 0x100000001b3ull;
  return hash;
}

inline void
DumpShader(const char *stage, const void *bytecode, size_t length, const void *rootsig, size_t rootsig_length) {
  static const bool all = env::getEnvVar("DXMT_SHADER_DUMP_ALL") == "1";
  if (all)
    DumpFailedShader(stage, bytecode, length, rootsig, rootsig_length);
}

class SM50Shader {
  sm50_shader_t sm50_shader_{};

public:
  sm50_shader_t *
  operator&() {
    return &sm50_shader_;
  }

  operator sm50_shader_t() {
    return sm50_shader_;
  }

  ~SM50Shader() {
    if (sm50_shader_)
      SM50Destroy(sm50_shader_);
  }
};

class SM50ShaderBitcode {
  sm50_bitcode_t sm50_bitcode_{};

public:
  sm50_bitcode_t *
  operator&() {
    return &sm50_bitcode_;
  }

  operator sm50_bitcode_t() {
    return sm50_bitcode_;
  }

  ~SM50ShaderBitcode() {
    if (sm50_bitcode_)
      SM50DestroyBitcode(sm50_bitcode_);
  }
};

class SM50Error {
  sm50_error_t sm50_error_{};

public:
  sm50_error_t *
  operator&() {
    return &sm50_error_;
  }

  operator sm50_error_t() {
    return sm50_error_;
  }

  ~SM50Error() {
    if (sm50_error_)
      SM50FreeError(sm50_error_);
  }
};

} // namespace dxmt