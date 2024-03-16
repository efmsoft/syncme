#pragma once

#include <vector>

#include <Syncme/Api.h>
#include <Syncme/Sync.h>

class ThreadsList
{
  std::vector<HEvent> List;

public:
  SINCMELNK ThreadsList();
  SINCMELNK ~ThreadsList();

  SINCMELNK void Add(HEvent h);
  SINCMELNK size_t Update();

private:
  void Wait();
};
