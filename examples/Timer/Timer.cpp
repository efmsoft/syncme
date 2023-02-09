#include <stdio.h>

#include <Syncme/Sync.h>
#include <Syncme/TimePoint.h>

using namespace Syncme;

int main()
{
  auto timer1 = CreateManualResetTimer();
  auto timer2 = CreateManualResetTimer();
  auto event3 = CreateNotificationEvent(STATE::SIGNALLED);

  TimePoint t0;
  SetWaitableTimer(timer1, 3000, 0, [t0](HEvent h) { printf("timer1 signalled after %llu ms\n", t0.ElapsedSince()); });
  SetWaitableTimer(timer2, 7000, 0, [t0](HEvent h) { printf("timer2 signalled after %llu ms\n", t0.ElapsedSince()); });

  EventArray ev(timer1, timer2, event3);
  auto rc = WaitForMultipleObjects(ev, true);
  printf("all objects are signalled after %llu ms\n", t0.ElapsedSince());

  return 0;
}