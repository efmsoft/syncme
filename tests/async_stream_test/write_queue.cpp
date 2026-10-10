#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Syncme/Sockets/Async/AsyncWriteQueue.h>
#include <Syncme/Sockets/Async/BufferChain.h>
#include <Syncme/Sockets/Async/Counter.h>

#include "fake_stream.h"

using namespace Syncme::Sockets;
using namespace Syncme::Sockets::Async;

// AsyncWriteQueue and BufferChain: a chain pushed by move reaches the stream,
// and then OnWriteCompleted()'s `completed`, as the very same buffers, and a
// chain that was moved from is an empty one.

namespace
{
  IO::BufferPtr Make(size_t size, char fill)
  {
    return std::make_shared<IO::Buffer>(size, fill);
  }

  BufferChain ChainOf(const std::vector<IO::BufferPtr>& buffers)
  {
    BufferChain chain;
    for (const auto& buffer : buffers)
    {
      EXPECT_TRUE(chain.Add(buffer));
    }

    return chain;
  }

  std::vector<const void*> Addresses(const BufferChain& chain)
  {
    std::vector<const void*> addresses;
    for (const auto& view : chain.GetViews())
    {
      addresses.push_back(view.Buffer.get());
    }

    return addresses;
  }
}

TEST(buffer_chain, a_moved_from_chain_is_empty)
{
  BufferChain a;
  ASSERT_TRUE(a.Add(Make(10, 'a')));
  ASSERT_TRUE(a.Add(Make(20, 'b')));
  ASSERT_EQ(a.Size(), 30u);

  const auto addresses = Addresses(a);

  BufferChain b(std::move(a));
  EXPECT_EQ(b.Size(), 30u);
  EXPECT_EQ(Addresses(b), addresses);

  EXPECT_TRUE(a.IsEmpty());
  EXPECT_EQ(a.Size(), 0u);
  EXPECT_TRUE(a.GetViews().empty());

  BufferChain c;
  ASSERT_TRUE(c.Add(Make(5, 'c')));
  c = std::move(b);
  EXPECT_EQ(c.Size(), 30u);
  EXPECT_EQ(Addresses(c), addresses);
  EXPECT_TRUE(b.IsEmpty());
  EXPECT_EQ(b.Size(), 0u);
  EXPECT_TRUE(b.GetViews().empty());

  // a moved-from chain is usable again
  ASSERT_TRUE(b.Add(Make(7, 'd')));
  EXPECT_EQ(b.Size(), 7u);
}

TEST(buffer_chain, assigning_a_chain_to_itself_by_move_keeps_it)
{
  BufferChain chain;
  ASSERT_TRUE(chain.Add(Make(10, 'a')));

  BufferChain& same = chain;
  chain = std::move(same);

  EXPECT_EQ(chain.Size(), 10u);
  EXPECT_EQ(chain.GetViews().size(), 1u);
}

TEST(buffer_chain, a_copy_is_independent_of_the_original)
{
  BufferChain a;
  ASSERT_TRUE(a.Add(Make(10, 'a')));

  BufferChain b(a);
  EXPECT_EQ(b.Size(), 10u);
  EXPECT_EQ(Addresses(b), Addresses(a));

  a.Clear();
  EXPECT_EQ(b.Size(), 10u);
  EXPECT_EQ(b.GetViews().size(), 1u);

  BufferChain c;
  c = b;
  EXPECT_EQ(c.Size(), 10u);
  EXPECT_EQ(Addresses(c), Addresses(b));
}

TEST(buffer_chain, adding_a_buffer_takes_it_whole)
{
  auto buffer = Make(64, 'x');
  const IO::Buffer* address = buffer.get();

  BufferChain chain;
  ASSERT_TRUE(chain.Add(std::move(buffer)));

  ASSERT_EQ(chain.GetViews().size(), 1u);
  EXPECT_EQ(chain.GetViews()[0].Buffer.get(), address);
  EXPECT_EQ(chain.GetViews()[0].Offset, 0u);
  EXPECT_EQ(chain.GetViews()[0].Size, 64u);
  EXPECT_EQ(chain.Size(), 64u);

  // nothing to add
  EXPECT_FALSE(chain.Add(IO::BufferPtr()));
  EXPECT_FALSE(chain.Add(Make(0, 'x')));
  EXPECT_EQ(chain.Size(), 64u);
}

TEST(async_write_queue, a_pushed_chain_reaches_the_stream_and_comes_back_as_the_same_buffers)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  auto first = Make(100, 'a');
  auto second = Make(50, 'b');
  BufferChain chain = ChainOf({ first, second });
  const auto addresses = Addresses(chain);

  ASSERT_TRUE(queue.Push(std::move(chain)));

  // taken over: the pushed chain is empty now
  EXPECT_TRUE(chain.IsEmpty());
  EXPECT_EQ(chain.Size(), 0u);
  EXPECT_TRUE(chain.GetViews().empty());

  ASSERT_EQ(stream->Writes.size(), 1u);
  EXPECT_EQ(stream->Writes[0].Buffers, addresses);
  EXPECT_EQ(stream->Writes[0].Sizes, (std::vector<size_t>{ 100, 50 }));
  EXPECT_EQ(stream->Writes[0].Bytes, std::string(100, 'a') + std::string(50, 'b'));

  EXPECT_EQ(queue.Size(), 150u);
  EXPECT_EQ(queue.Count(), 1u);
  EXPECT_TRUE(queue.IsWriting());
  EXPECT_FALSE(queue.IsIdle());

  BufferChain completed;
  ASSERT_TRUE(queue.OnWriteCompleted(150, &completed));

  EXPECT_EQ(Addresses(completed), addresses);
  EXPECT_EQ(completed.Size(), 150u);
  EXPECT_EQ(completed.GetViews()[0].Buffer, first);
  EXPECT_EQ(completed.GetViews()[1].Buffer, second);

  EXPECT_TRUE(queue.IsIdle());
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_EQ(queue.Count(), 0u);
}

TEST(async_write_queue, chains_are_written_one_at_a_time_in_the_order_they_were_pushed)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  auto a = Make(10, 'a');
  auto b = Make(20, 'b');
  auto c = Make(30, 'c');

  ASSERT_TRUE(queue.Push(ChainOf({ a })));
  ASSERT_TRUE(queue.Push(ChainOf({ b })));
  ASSERT_TRUE(queue.Push(ChainOf({ c })));

  // only the first has been handed over, the others wait
  ASSERT_EQ(stream->Writes.size(), 1u);
  EXPECT_EQ(queue.Count(), 3u);
  EXPECT_EQ(queue.Size(), 60u);

  BufferChain completed;
  ASSERT_TRUE(queue.OnWriteCompleted(10, &completed));
  EXPECT_EQ(completed.GetViews()[0].Buffer, a);
  ASSERT_EQ(stream->Writes.size(), 2u);
  EXPECT_EQ(stream->Writes[1].Buffers[0], b.get());

  completed = BufferChain();
  ASSERT_TRUE(queue.OnWriteCompleted(20, &completed));
  EXPECT_EQ(completed.GetViews()[0].Buffer, b);
  ASSERT_EQ(stream->Writes.size(), 3u);
  EXPECT_EQ(stream->Writes[2].Buffers[0], c.get());

  completed = BufferChain();
  ASSERT_TRUE(queue.OnWriteCompleted(30, &completed));
  EXPECT_EQ(completed.GetViews()[0].Buffer, c);

  EXPECT_TRUE(queue.IsIdle());
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_EQ(stream->Writes.size(), 3u);
}

TEST(async_write_queue, a_pushed_buffer_is_one_view_of_the_whole_buffer)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  auto buffer = Make(40, 'z');
  const IO::Buffer* address = buffer.get();

  ASSERT_TRUE(queue.Push(buffer));

  ASSERT_EQ(stream->Writes.size(), 1u);
  EXPECT_EQ(stream->Writes[0].Buffers, (std::vector<const void*>{ address }));
  EXPECT_EQ(stream->Writes[0].Offsets, (std::vector<size_t>{ 0 }));
  EXPECT_EQ(stream->Writes[0].Sizes, (std::vector<size_t>{ 40 }));

  BufferChain completed;
  ASSERT_TRUE(queue.OnWriteCompleted(40, &completed));
  ASSERT_EQ(completed.GetViews().size(), 1u);
  EXPECT_EQ(completed.GetViews()[0].Buffer, buffer);

  // the caller's own buffer is still the caller's: no extra owner is left
  completed.Clear();
  EXPECT_EQ(buffer.use_count(), 1);
}

TEST(async_write_queue, no_buffer_outlives_its_write)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  std::weak_ptr<IO::Buffer> weak;
  {
    auto buffer = Make(32, 'q');
    weak = buffer;

    ASSERT_TRUE(queue.Push(std::move(buffer)));
    EXPECT_FALSE(weak.expired());

    BufferChain completed;
    ASSERT_TRUE(queue.OnWriteCompleted(32, &completed));
    EXPECT_FALSE(weak.expired());
  }

  // `completed` and every local are gone, the queue and the stream kept nothing
  EXPECT_TRUE(weak.expired());

  // a completion nobody collects does not keep it either
  std::weak_ptr<IO::Buffer> weak2;
  {
    auto buffer = Make(32, 'r');
    weak2 = buffer;

    ASSERT_TRUE(queue.Push(std::move(buffer)));
    ASSERT_TRUE(queue.OnWriteCompleted(32));
  }

  EXPECT_TRUE(weak2.expired());
}

TEST(async_write_queue, a_chain_pushed_by_reference_is_copied)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  const BufferChain chain = ChainOf({ Make(10, 'a'), Make(20, 'b') });
  const auto addresses = Addresses(chain);

  ASSERT_TRUE(queue.Push(chain));

  // the original is left as it was
  EXPECT_EQ(chain.Size(), 30u);
  EXPECT_EQ(Addresses(chain), addresses);

  ASSERT_EQ(stream->Writes.size(), 1u);
  EXPECT_EQ(stream->Writes[0].Buffers, addresses);
  EXPECT_EQ(queue.Size(), 30u);
}

TEST(async_write_queue, nothing_to_push_is_not_an_error_and_starts_nothing)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  EXPECT_TRUE(queue.Push(BufferChain()));

  BufferChain empty;
  EXPECT_TRUE(queue.Push(empty));
  EXPECT_TRUE(queue.Push(std::move(empty)));

  EXPECT_TRUE(stream->Writes.empty());
  EXPECT_TRUE(queue.IsIdle());
  EXPECT_EQ(queue.Size(), 0u);

  // a missing or empty buffer is
  EXPECT_FALSE(queue.Push(IO::BufferPtr()));
  EXPECT_FALSE(queue.Push(Make(0, 'x')));
  EXPECT_TRUE(stream->Writes.empty());
  EXPECT_TRUE(queue.IsIdle());
}

TEST(async_write_queue, a_completion_must_be_for_the_whole_front_chain)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  ASSERT_TRUE(queue.Push(ChainOf({ Make(10, 'a') })));
  ASSERT_TRUE(queue.Push(ChainOf({ Make(20, 'b') })));

  BufferChain completed;
  EXPECT_FALSE(queue.OnWriteCompleted(9, &completed));
  EXPECT_TRUE(completed.IsEmpty());
  EXPECT_EQ(queue.Count(), 2u);
  EXPECT_EQ(queue.Size(), 30u);
  EXPECT_EQ(stream->Writes.size(), 1u);

  EXPECT_TRUE(queue.OnWriteCompleted(10, &completed));
  EXPECT_EQ(completed.Size(), 10u);
  EXPECT_EQ(queue.Count(), 1u);
}

TEST(async_write_queue, clear_forgets_what_is_queued)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  std::weak_ptr<IO::Buffer> weak;
  {
    auto buffer = Make(10, 'a');
    weak = buffer;

    ASSERT_TRUE(queue.Push(std::move(buffer)));
    ASSERT_TRUE(queue.Push(Make(20, 'b')));
  }

  queue.Clear();

  EXPECT_TRUE(queue.IsIdle());
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_EQ(queue.Count(), 0u);
  EXPECT_TRUE(weak.expired());
}

TEST(async_write_queue, byte_counter_tracks_queued_data_and_peak)
{
  auto stream = std::make_shared<AsyncTest::FakeStream>();
  AsyncWriteQueue queue;
  queue.Attach(stream);

  const uint64_t before = GetAsyncWriteQueueBytes();
  const uint64_t peakBefore = GetAsyncWriteQueueBytesPeak();

  ASSERT_TRUE(queue.Push(ChainOf({ Make(100, 'a') })));
  ASSERT_TRUE(queue.Push(ChainOf({ Make(50, 'b') })));

  EXPECT_EQ(GetAsyncWriteQueueBytes(), before + 150);
  EXPECT_GE(GetAsyncWriteQueueBytesPeak(), before + 150);
  EXPECT_GE(GetAsyncWriteQueueBytesPeak(), peakBefore);

  ASSERT_TRUE(queue.OnWriteCompleted(100));
  EXPECT_EQ(GetAsyncWriteQueueBytes(), before + 50);

  queue.Clear();
  EXPECT_EQ(GetAsyncWriteQueueBytes(), before);
}

TEST(async_buffer_counter, capacity_counter_tracks_live_io_buffers)
{
  const uint64_t before = GetAsyncIoBufferCapacityBytes();
  const uint64_t peakBefore = GetAsyncIoBufferCapacityBytesPeak();

  size_t capacity = 0;
  {
    auto buffer = std::make_shared<IO::Buffer>();
    buffer->resize(100 * 1024);
    capacity = buffer->capacity();

    EXPECT_EQ(GetAsyncIoBufferCapacityBytes(), before + capacity);
    EXPECT_GE(GetAsyncIoBufferCapacityBytesPeak(), before + capacity);
    EXPECT_GE(GetAsyncIoBufferCapacityBytesPeak(), peakBefore);

    buffer->resize(1024);
    EXPECT_EQ(GetAsyncIoBufferCapacityBytes(), before + capacity);
  }

  EXPECT_EQ(GetAsyncIoBufferCapacityBytes(), before);
}
