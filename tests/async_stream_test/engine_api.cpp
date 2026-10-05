#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include <Syncme/Sockets/Async/AsyncStream.h>

using namespace Syncme::Sockets;
using namespace Syncme::Sockets::Async;

// AsyncEngine::TryPopPendingResult(): the results an engine already holds, for
// a caller that wants them without another Wait(). The default -- an engine
// with no queue of its own -- has none.

namespace
{
  // An engine that implements only what the interface forces on it
  class BareEngine : public AsyncEngine
  {
  public:
    bool IsValid() const override
    {
      return true;
    }

    bool Add(Syncme::Socket*, void*, AsyncStreamPtr&) override
    {
      return false;
    }

    bool RebindSocket(AsyncStream*, Syncme::Socket*) override
    {
      return false;
    }

    bool Remove(AsyncStream*) override
    {
      return false;
    }

    bool Wait(Result& result, int) override
    {
      result = Result();
      return true;
    }

    void Wake() override
    {
    }

    void Stop() override
    {
    }
  };
}

TEST(async_engine, the_default_has_no_ready_result_and_leaves_the_result_empty)
{
  BareEngine engine;

  Result result;
  result.Op = Operation::Read;
  result.Bytes = 77;
  result.Error = 5;

  AsyncEngine& base = engine;
  EXPECT_FALSE(base.TryPopPendingResult(result));

  EXPECT_EQ(result.Op, Operation::None);
  EXPECT_EQ(result.Bytes, 0u);
  EXPECT_EQ(result.Error, 0);
  EXPECT_EQ(result.Stream, nullptr);
  EXPECT_EQ(result.Buffer, nullptr);
  EXPECT_EQ(result.Context, nullptr);
}

TEST(async_engine, a_real_engine_with_nothing_queued_has_nothing_ready_and_does_not_wait)
{
  auto engine = AsyncEngine::Create();
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->IsValid());

  const auto begin = std::chrono::steady_clock::now();

  for (int i = 0; i < 100; ++i)
  {
    Result result;
    result.Op = Operation::Write;

    EXPECT_FALSE(engine->TryPopPendingResult(result));
    EXPECT_EQ(result.Op, Operation::None);
  }

  const auto elapsed = std::chrono::steady_clock::now() - begin;
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);

  engine->Stop();
}
