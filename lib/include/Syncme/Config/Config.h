#pragma once

#include <memory>
#include <string>

namespace Syncme
{
  struct Config
  {
    virtual bool GetBool(const std::string& option, bool def);
    virtual bool GetBool(const char* option, bool def);

    virtual int GetInt(const std::string& option, int def);
    virtual int GetInt(const char* option, int def);

    virtual std::string GetString(const std::string& option, const std::string& def);
    virtual std::string GetString(const char* option, const char* def);
  };

  typedef std::shared_ptr<Config> ConfigPtr;
}