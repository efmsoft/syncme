#pragma once

namespace Syncme
{
  void Uninitialize();

  namespace Implementation
  {
    typedef void (*PUNINIT)();

    struct UninitializeEntry
    {
      UninitializeEntry* Next;
      PUNINIT Callback;
    };

    void CallOnUninitialize(UninitializeEntry* entry);

    #define ON_SYNCME_UNINITIALIZE(callback) \
      struct UnintHelper : public UninitializeEntry \
      { \
        UnintHelper(PUNINIT c) \
        { \
          Next = nullptr; \
          Callback = c; \
          Syncme::Implementation::CallOnUninitialize(this); \
        } \
      } Helper(callback); 
  }
}