#pragma once

#include <memory>
#include <string>

#include <Syncme/Api.h>

namespace Syncme
{
  struct Config
  {
    SINCMELNK virtual bool GetBool(const std::string& option, bool def);
    SINCMELNK virtual bool GetBool(const char* option, bool def);

    SINCMELNK virtual int GetInt(const std::string& option, int def);
    SINCMELNK virtual int GetInt(const char* option, int def);

    SINCMELNK virtual std::string GetString(const std::string& option, const std::string& def);
    SINCMELNK virtual std::string GetString(const char* option, const char* def);
  };

  typedef std::shared_ptr<Config> ConfigPtr;
}