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
#include "dxmt_shader_cache.hpp"
#include "sha1/sha1_util.hpp"
#include <cstdint>
#include <cstring>
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

/* Shader cache: airconv's output for a shader, keyed by its bytecode and every compilation
 * argument that affects the result, so later runs skip the conversion. */
inline bool
HashCompileArguments(Sha1HashState &h, const void *pArgs, const char *function_name) {
  // bump when the converter's output changes
  h.update("vodka-d3d12-1", 13);
  h.update(function_name, strlen(function_name));
  for (auto arg = (const SM50_SHADER_COMPILATION_ARGUMENT_DATA *)pArgs; arg;
       arg = (const SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next) {
    h.update(arg->type);
    switch (arg->type) {
    case SM50_SHADER_COMMON: {
      auto data = (const SM50_SHADER_COMMON_DATA *)arg;
      h.update(data->metal_version);
      h.update(data->flags);
      break;
    }
    case SM50_SHADER_PSO_PIXEL_SHADER: {
      auto data = (const SM50_SHADER_PSO_PIXEL_SHADER_DATA *)arg;
      h.update(data->sample_mask);
      h.update(data->dual_source_blending);
      h.update(data->disable_depth_output);
      h.update(data->unorm_output_reg_mask);
      h.update(data->pixel_formats);
      break;
    }
    case SM50_SHADER_IA_INPUT_LAYOUT: {
      auto data = (const SM50_SHADER_IA_INPUT_LAYOUT_DATA *)arg;
      h.update(data->index_buffer_format);
      h.update(data->slot_mask);
      h.update(data->num_elements);
      for (unsigned i = 0; i < data->num_elements; i++) {
        auto &element = data->elements[i];
        uint32_t fields[] = {element.reg, element.slot, element.aligned_byte_offset, element.format,
                             element.step_function, element.step_rate};
        h.update(fields);
      }
      break;
    }
    case SM50_SHADER_ROOT_SIGNATURE: {
      auto data = (const SM50_SHADER_ROOT_SIGNATURE_DATA *)arg;
      h.update(data->bytecode_length);
      h.update(data->bytecode, data->bytecode_length);
      break;
    }
    default:
      return false;
    }
  }
  return true;
}

inline ShaderCache &
D3D12ShaderCache() {
  return ShaderCache::getInstance(WMTMetal310);
}

inline WMT::Reference<WMT::DispatchData>
LoadCachedShader(const void *bytecode, size_t length, const void *pArgs, const char *function_name) {
  Sha1HashState h;
  if (!HashCompileArguments(h, pArgs, function_name))
    return {};
  auto key = std::make_pair(Sha1HashState::compute(bytecode, length), h.final());
  auto reader = D3D12ShaderCache().getReader();
  if (!reader)
    return {};
  return reader->get(key);
}

inline void
StoreCachedShader(
    const void *bytecode, size_t length, const void *pArgs, const char *function_name, WMT::DispatchData data
) {
  Sha1HashState h;
  if (!HashCompileArguments(h, pArgs, function_name))
    return;
  auto key = std::make_pair(Sha1HashState::compute(bytecode, length), h.final());
  auto writer = D3D12ShaderCache().getWriter();
  if (writer)
    writer->set(key, data);
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