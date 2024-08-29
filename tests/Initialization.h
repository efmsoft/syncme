#pragma once

typedef bool (*PINITROUTINE)();
typedef void (*PEXITROUTINE)();

struct InitializationItem
{
  InitializationItem* Next;
  PINITROUTINE InitRoutine;
  PEXITROUTINE ExitRoutine;
};

extern InitializationItem* InitializationListHead;

struct InitalizationListAppender
{
  InitalizationListAppender(InitializationItem* item)
  {
    item->Next = InitializationListHead;
    InitializationListHead = item;
  }
};

#define INITIALIZATION_ITEM(ir, er) \
  InitializationItem IItem { \
    .Next = nullptr \
    , .InitRoutine = ir \
    , .ExitRoutine = er \
  }; \
  InitalizationListAppender IAppender(&IItem)

