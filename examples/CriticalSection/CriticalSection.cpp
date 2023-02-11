#include <stdio.h>
#include <thread>

#include <Syncme/CritSection.h>
#include <Syncme/ProcessThreadId.h>

using namespace Syncme;

// Both Syncme::CS and CritSection names can be used
// CritSection is an alias of Syncme::CS
CritSection Sec;

volatile int Counter = 0;
const int LOOPS = 100000;

int IncreaseCounter()
{
  // Comment out the next code line to reproduce the race condition:
  // [Thread1]                               [Thread2]
  // read Counter to a register
  //                                         read Counter to a register
  // inc reg
  // write reg to Counter variable in memory
  //                                         inc reg
  //                                         write reg to Counter variable in memory
  // -------------------------------------------------------------------------------
  // Result: val is increased only once

  auto guard = Sec.Lock();   // <-- comment out this line
  return Counter++;
}

void EntryPoint()
{
  for (int i = 0; i < LOOPS; i++)
    IncreaseCounter();
}

int main()
{
  for (int i = 0; i < 100; i++)
  {
    Counter = 0;

    std::thread thread1(&EntryPoint);
    std::thread thread2(&EntryPoint);

    thread1.join();
    thread2.join();

    if (Counter != 2 * LOOPS)
    {
      printf("Detected race condition!!!");
      exit(1);
    }
  }

  printf("Everything is OK");
  return 0;
}