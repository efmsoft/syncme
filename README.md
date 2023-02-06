# Syncme library

This library contains a cross-platform implementation of such synchronization objects as Notification Event, Synchronization Event, Waitable Timer, Socket Event, Thread, as well as functions for waiting for the transition of objects to a signaled state. To wait for the signaled state of objects, analogues of the functions used in Windows are used - WaitForSingleObject and WaitForMultipleObjects.

```
#include <Syncme/Sync.h>

using namespace Syncme;
...
HEvent timer = CreateManualResetTimer();
SetWaitableTimer(htimer, 1500, 0, nullptr);

auto rc = WaitForSingleObject(timer, FOREVER);
assert(rc == WAIT_RESULT::OBJECT_0);
```
