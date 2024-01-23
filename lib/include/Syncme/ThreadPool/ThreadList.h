#pragma once

#include <vector>

#include <Syncme/Sync.h>

class ThreadsList
{
  std::vector<HEvent> List;

public:
  ThreadsList();
  ~ThreadsList();

  void Add(HEvent h);
  size_t Update();

private:
  void Wait();
};
