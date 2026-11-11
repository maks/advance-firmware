/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Copyright (c) 2026 xiphonics, inc.
 */

#include "Application/Model/Config.h"
#include "boundary_test_utils.h"
#include "doctest/doctest.h"
#include <string>

TEST_CASE("Config imports themes from an absolute path without cwd changes") {
  BoundaryTestEnvironment env;
  env.fs.AddDirectory("/themes");
  // Legacy font elements must not prevent colours from loading.
  env.fs.AddFile("/themes/Neon.ptt",
                 "<THEME><Font value=\"2\"/>"
                 "<Color name=\"BACKGROUND\" value=\"#123456\"/>"
                 "</THEME>");
  env.fs.ClearOperationLog();

  Config *config = Config::GetInstance();

  env.fs.ClearOperationLog();
  const bool imported = config->ImportTheme("/themes/Neon.ptt");

  REQUIRE(imported);
  CHECK(config->FindVariable(FourCC::VarThemeName) != nullptr);
  CHECK(config->FindVariable(FourCC::VarThemeName)->GetString() == "Neon");
  CHECK(config->FindVariable(FourCC::VarBGColor)->GetInt() == 0x123456);
  CHECK(operationLogHasNoChdir(env.fs));

  // Export retains the imported colours without propagating legacy settings.
  {
    auto file = env.fs.Open("/themes/RoundTrip.ptt", "w");
    REQUIRE(static_cast<bool>(file));
    tinyxml2::XMLPrinter printer(file.get());
    REQUIRE(config->SaveTheme(&printer, "Neon"));
  }
  std::string exported;
  {
    auto file = env.fs.Open("/themes/RoundTrip.ptt", "r");
    REQUIRE(static_cast<bool>(file));
    int c;
    while ((c = file->GetC()) != EOF)
      exported.push_back(static_cast<char>(c));
  }
  CHECK(exported.find("<Font") == std::string::npos);
  config->FindVariable(FourCC::VarBGColor)->SetInt(0);
  REQUIRE(config->ImportTheme("/themes/RoundTrip.ptt"));
  CHECK(config->FindVariable(FourCC::VarBGColor)->GetInt() == 0x123456);

  const auto usedAbsoluteThemePath = std::count_if(
      env.fs.operationLog().begin(), env.fs.operationLog().end(),
      [](const FakeFileSystemOperation &entry) {
        return (entry.type == FakeFileSystemOperationType::Exists ||
                entry.type == FakeFileSystemOperationType::Open) &&
               entry.resolvedPath == "/themes/Neon.ptt" && entry.success;
      });
  CHECK(usedAbsoluteThemePath >= 2);
}

TEST_CASE("Config still resolves legacy theme names through the themes dir") {
  BoundaryTestEnvironment env;
  env.fs.AddDirectory("/themes");
  env.fs.AddFile("/themes/Retro.ptt", "<THEME><Font value=\"1\"/></THEME>");
  env.fs.ClearOperationLog();

  Config *config = Config::GetInstance();

  env.fs.ClearOperationLog();
  const bool imported = config->ImportTheme("Retro");

  REQUIRE(imported);
  CHECK(config->FindVariable(FourCC::VarThemeName)->GetString() == "Retro");
  CHECK(operationLogHasNoChdir(env.fs));

  CHECK(std::any_of(env.fs.operationLog().begin(), env.fs.operationLog().end(),
                    [](const FakeFileSystemOperation &entry) {
                      return entry.type == FakeFileSystemOperationType::Open &&
                             entry.resolvedPath == "/themes/Retro.ptt" &&
                             entry.success;
                    }));
}

TEST_CASE("Config loads invalid import resampler values as the default") {
  struct ImportResamplerMapping {
    int savedValue;
    int expectedValue;
    const char *expectedLabel;
  };

  const ImportResamplerMapping mappings[] = {
      {0, 2, "Sinc"},    {1, 1, "Linear"}, {2, 2, "Sinc"},
      {3, 3, "Sinc HQ"}, {99, 2, "Sinc"},
  };

  for (const auto &mapping : mappings) {
    BoundaryTestEnvironment env;
    std::string configXml = "<CONFIG><IMPORTRESAMP VALUE=\"";
    configXml.append(std::to_string(mapping.savedValue));
    configXml.append("\"/></CONFIG>");
    env.fs.AddFile("/.config.xml", configXml.c_str());

    Config *config = Config::GetInstance();
    Variable *var = config->FindVariable(FourCC::VarImportResampler);

    REQUIRE(var != nullptr);
    CHECK(var->GetInt() == mapping.expectedValue);
    CHECK(var->GetString() == mapping.expectedLabel);
  }
}
