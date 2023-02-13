#include <stdio.h>
#include <thread>

#include <Syncme/CritSection.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/Sleep.h>

using namespace Syncme;

// Both Syncme::CS and CritSection names can be used
// CritSection is an alias of Syncme::CS
CritSection Sec;

int Counter = 0;
const int LOOPS = 100;

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

  // On multi cpu computers next lines can be changed on "return Counter++;"
  // We are using Sleep(1) to ensure that the error occurs immediatelly
  auto c = Counter + 1;
  Sleep(1);
  Counter = c;
  return c;
}

void EntryPoint()
{
  for (int i = 0; i < LOOPS; i++)
    IncreaseCounter();
}

int main()
{
  for (int i = 0; i < 5; i++)
  {
    Counter = 0;

    std::thread thread1(&EntryPoint);
    std::thread thread2(&EntryPoint);

    thread1.join();
    thread2.join();

    if (Counter != 2 * LOOPS)
    {
      printf("Detected race condition!!!\n");
      exit(1);
    }
  }

  printf("Everything is OK\n");
  return 0;
}
