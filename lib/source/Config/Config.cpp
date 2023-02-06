#include <Syncme/Config/Config.h>

using namespace Syncme;

bool Config::GetBool(const std::string& option, bool def)
{
  return GetBool(option.c_str(), def);
}

bool Config::GetBool(const char* option, bool def)
{
  return def;
}

int Config::GetInt(const std::string& option, int def)
{
  return GetInt(option.c_str(), def);
}

int Config::GetInt(const char* option, int def)
{
  return def;
}

std::string Config::GetString(const std::string& option, const std::string& def)
{
  return GetString(option.c_str(), def.c_str());
}

std::string Config::GetString(const char* option, const char* def)
{
  return def;
}
