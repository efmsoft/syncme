# CriticalSection example

This example demonstrates the use of a critical section - **CritSection** (or **Syncme::CS**). In the Windows system, this object corresponds to CRITICAL_SECTION. For other operating systems, this object is implemented via **std::recursive_mutex**.

Critical sections are used to quickly synchronize access to data. In the example, if the line acquiring CritSection is not commented out, the program exits successfully. If you comment out the line (a line for commenting is specially marked in the source), then an error occurs. The reason for the error is described in one of example comment.

