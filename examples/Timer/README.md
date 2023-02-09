# Timer example

This example demonstrates the use of watable timers and functions to wait for a signalled state of objects (**WaitForMultipleObjects**).

Three objects are created: a timer that goes into the signaled state after 3 seconds, a timer that goes into the signaled state after 7 seconds, and an event that is already initially in the signaled state. The **WaitForMultipleObjects** function waits for the signaled state of all objects (because the second parameter **waitAll**=true). Thus, the wait ends after 7 seconds. The example also shows the use of the **TimePoint** object

Since the second timer is an autoreset timer it goes to a non-signalled state after completion of **WaitForMultipleObjects**. The first timer remains in the signaled state.

On the next stage the example creates a periodic timer. First time it goes to a signalled state after 1 sec, then each 1/2 sec. 