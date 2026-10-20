#include <Syncme/Sockets/Async/AsyncStream.h>

using namespace Syncme::Sockets::Async;

Result::Result()
  : Context(nullptr)
  , Op(Operation::None)
  , Bytes(0)
  , Error(0)
  , Origin(ErrorOrigin::None)
{
}

AsyncStream::~AsyncStream()
{
}

bool AsyncStream::CanTrackWriteProgress() const
{
  return false;
}

uint64_t AsyncStream::GetLastWriteProgressTime() const
{
  return 0;
}

AsyncEngine::~AsyncEngine()
{
}

bool AsyncEngine::TryPopPendingResult(Result& result)
{
  result = Result();
  return false;
}
