#include <Syncme/Sockets/Async/AsyncStream.h>

using namespace Syncme::Sockets::Async;

Result::Result()
  : Context(nullptr)
  , Op(Operation::None)
  , Bytes(0)
  , Error(0)
{
}

AsyncStream::~AsyncStream()
{
}

AsyncEngine::~AsyncEngine()
{
}
