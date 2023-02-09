#include <stdio.h>

#include <Syncme/Sync.h>
#include <Syncme/TimePoint.h>

using namespace Syncme;

int main()
{
  auto timer1 = CreateManualResetTimer();
  auto timer2 = CreateAutoResetTimer();
  auto event3 = CreateNotificationEvent(STATE::SIGNALLED);

  TimePoint t0;
  SetWaitableTimer(timer1, 3000, 0, [t0](HEvent h) { printf("timer1 signalled after %llu ms\n", t0.ElapsedSince()); });
  SetWaitableTimer(timer2, 7000, 0, [t0](HEvent h) { printf("timer2 signalled after %llu ms\n", t0.ElapsedSince()); });

  EventArray ev(timer1, timer2, event3);
  auto rc = WaitForMultipleObjects(ev, true);
  printf("all objects are signalled after %llu ms\n", t0.ElapsedSince());

  printf("timer1 is %s\n", GetEventState(timer1) == STATE::SIGNALLED ? "signalled" : "not signalled");
  printf("timer2 is %s\n", GetEventState(timer2) == STATE::SIGNALLED ? "signalled" : "not signalled");

  // timer3 will be signalled first time after 1 sec, then once per 1/2 sec till 
  // CancelWaitableTimer call
  auto timer3 = CreateAutoResetTimer();

  TimePoint t1;
  SetWaitableTimer(timer3, 1000, 500, [t1](HEvent h) { printf("timer3 signalled after %llu ms\n", t1.ElapsedSince()); });
  for (int i = 0; i < 4; i++)
    WaitForSingleObject(timer3);

  // The state of object is non-signalled because it is auto reset timer.
  // It means that WaitForSingleObject/WaitForMultipleObjects resets state to
  // non-signalled if wait was completed due to state transition of the timer
  CancelWaitableTimer(timer3);

  // The timer will never signalled any more becase it was cancelled
  bool f = WaitForSingleObject(timer3, 2000) == WAIT_RESULT::TIMEOUT;
  printf("timer3 is %s\n", f ? "not signalled because it was cancelled" : "signalled. Library error!!!");

  return 0;
}