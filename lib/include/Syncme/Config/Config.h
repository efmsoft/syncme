#pragma once

#include <memory>
#include <string>

#include <Syncme/Api.h>

namespace Syncme
{
  constexpr static const char* IFID_IUNKNOWN = "iunknown";

  struct IUnknown
  {
    virtual int AddRef() = 0;
    virtual int Release() = 0;
    virtual IUnknown* QueryInterface(const char* name) = 0;
  };

  struct Config
  {
    SINCMELNK virtual bool GetBool(const std::string& option, bool def);
    SINCMELNK virtual bool GetBool(const char* option, bool def);

    SINCMELNK virtual int GetInt(const std::string& option, int def);
    SINCMELNK virtual int GetInt(const char* option, int def);

    SINCMELNK virtual std::string GetString(const std::string& option, const std::string& def);
    SINCMELNK virtual std::string GetString(const char* option, const char* def);

    SINCMELNK virtual int GetByteSize(const std::string& option, int def);
    SINCMELNK virtual int GetByteSize(const char* option, int def);

    SINCMELNK virtual int GetTimeInMilliseconds(const std::string& option, const char* def);
    SINCMELNK virtual int GetTimeInMilliseconds(const char* option, const char* def);

    SINCMELNK virtual int GetTimeInMilliseconds(const std::string& option, int def);
    SINCMELNK virtual int GetTimeInMilliseconds(const char* option, int def);

    SINCMELNK virtual void RegisterCachedOption(const char* option, IUnknown* unk);
    SINCMELNK virtual void UnregisterCachedOption(IUnknown* unk);
    SINCMELNK virtual bool DynamicUpdate();
  };

  typedef std::shared_ptr<Config> ConfigPtr;
}