#define OPENSSL_SUPPRESS_DEPRECATED

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Syncme/Sockets/OsslCompat.h>

#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <Syncme/Sockets/Async/AsyncTlsStream.h>
#include <Syncme/Sockets/Async/Counter.h>

#include "fake_stream.h"

using namespace Syncme::Sockets;
using namespace Syncme::Sockets::Async;

// AsyncTlsStream over a fake lower stream, against a real TLS peer talking
// through memory BIOs: what the stream queues for its owner (handshake, read,
// write, close, error results) has to come out of PopPendingResult() exactly
// as it went in -- the buffer object, the operation, the byte count, the
// error and the context -- in the order it was queued.

namespace
{
  struct Identity
  {
    EVP_PKEY* Key;
    X509* Cert;

    Identity()
      : Key(nullptr)
      , Cert(nullptr)
    {
    }

    ~Identity()
    {
      if (Cert != nullptr)
        X509_free(Cert);

      if (Key != nullptr)
        EVP_PKEY_free(Key);
    }

    Identity(const Identity&) = delete;
    Identity& operator=(const Identity&) = delete;
  };

  bool MakeIdentity(Identity& identity)
  {
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ec == nullptr || EC_KEY_generate_key(ec) != 1)
    {
      if (ec != nullptr)
        EC_KEY_free(ec);

      return false;
    }

    identity.Key = EVP_PKEY_new();
    if (identity.Key == nullptr || EVP_PKEY_assign_EC_KEY(identity.Key, ec) != 1)
    {
      EC_KEY_free(ec);
      return false;
    }

    identity.Cert = X509_new();
    if (identity.Cert == nullptr)
      return false;

    X509_set_version(identity.Cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(identity.Cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(identity.Cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(identity.Cert), 3600);
    X509_set_pubkey(identity.Cert, identity.Key);

    X509_NAME* name = X509_get_subject_name(identity.Cert);
    X509_NAME_add_entry_by_txt(
      name
      , "CN"
      , MBSTRING_ASC
      , reinterpret_cast<const unsigned char*>("localhost")
      , -1
      , -1
      , 0
    );
    X509_set_issuer_name(identity.Cert, name);

    return X509_sign(identity.Cert, identity.Key, EVP_sha256()) > 0;
  }

  // The client is the AsyncTlsStream under test, over a FakeStream; the server
  // is a plain SSL object on memory BIOs. Pump() is the network.
  class Rig
  {
  public:
    SSL_CTX* ClientCtx;
    SSL_CTX* ServerCtx;
    SSL* Server;
    BIO* ServerIn;
    BIO* ServerOut;

    // what the server has read from the client so far
    std::string ServerReceived;

    int Marker;
    std::shared_ptr<AsyncTest::FakeStream> Lower;
    AsyncTlsStreamPtr Tls;

    size_t Delivered;

    Rig()
      : ClientCtx(nullptr)
      , ServerCtx(nullptr)
      , Server(nullptr)
      , ServerIn(nullptr)
      , ServerOut(nullptr)
      , Marker(0)
      , Delivered(0)
    {
      Identity identity;
      EXPECT_TRUE(MakeIdentity(identity));

      ServerCtx = SSL_CTX_new(TLS_server_method());
      ClientCtx = SSL_CTX_new(TLS_client_method());
      EXPECT_NE(ServerCtx, nullptr);
      EXPECT_NE(ClientCtx, nullptr);

      EXPECT_EQ(SSL_CTX_use_certificate(ServerCtx, identity.Cert), 1);
      EXPECT_EQ(SSL_CTX_use_PrivateKey(ServerCtx, identity.Key), 1);
      SSL_CTX_set_verify(ClientCtx, SSL_VERIFY_NONE, nullptr);

      Server = SSL_new(ServerCtx);
      ServerIn = BIO_new(BIO_s_mem());
      ServerOut = BIO_new(BIO_s_mem());
      BIO_set_mem_eof_return(ServerIn, -1);
      BIO_set_mem_eof_return(ServerOut, -1);
      SSL_set_bio(Server, ServerIn, ServerOut);
      SSL_set_accept_state(Server);

      SSL* client = SSL_new(ClientCtx);
      SSL_set_connect_state(client);

      Lower = std::make_shared<AsyncTest::FakeStream>(&Marker);
      Tls = std::make_shared<AsyncTlsStream>(Lower, client, true, &Marker);
    }

    ~Rig()
    {
      Tls.reset();
      Lower.reset();

      if (Server != nullptr)
        SSL_free(Server);

      if (ServerCtx != nullptr)
        SSL_CTX_free(ServerCtx);

      if (ClientCtx != nullptr)
        SSL_CTX_free(ClientCtx);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The oldest lower write the client has started and the network has not
    // yet carried: its bytes go to the server, then the write completes (which
    // is when the client goes on with what it has left to send). False when
    // there is none.
    bool DeliverOneWrite()
    {
      if (Delivered >= Lower->Writes.size())
        return false;

      const AsyncTest::WriteRecord record = Lower->Writes[Delivered++];

      EXPECT_EQ(
        BIO_write(ServerIn, record.Bytes.data(), int(record.Bytes.size()))
        , int(record.Bytes.size())
      );

      ServerStep();

      Result result;
      result.Stream = Lower;
      result.Context = &Marker;
      result.Op = Operation::Write;
      result.Bytes = record.Bytes.size();
      EXPECT_TRUE(Tls->ProcessLowerResult(result));

      return true;
    }

    // Hands the client's ciphertext to the server and completes the lower
    // writes, hands the server's to the client whenever it has a read
    // pending, until nothing moves any more.
    void Pump()
    {
      for (int round = 0; round < 1000; ++round)
      {
        bool progress = false;

        while (DeliverOneWrite())
        {
          progress = true;
        }

        const size_t pending = size_t(BIO_ctrl_pending(ServerOut));
        if (Lower->PendingRead != nullptr && pending > 0)
        {
          IO::BufferPtr buffer = std::move(Lower->PendingRead);
          Lower->PendingRead.reset();

          const size_t size = (std::min)(pending, buffer->size());
          const int rc = BIO_read(ServerOut, buffer->data(), int(size));
          EXPECT_EQ(rc, int(size));

          Result result;
          result.Stream = Lower;
          result.Context = &Marker;
          result.Op = Operation::Read;
          result.Buffer = buffer;
          result.Bytes = size_t(rc);
          EXPECT_TRUE(Tls->ProcessLowerResult(result));

          progress = true;
        }

        if (!progress)
          return;
      }

      ADD_FAILURE() << "the pump did not come to rest";
    }

    // the server's side of whatever the client has sent
    void ServerStep()
    {
      ERR_clear_error();

      if (!SSL_is_init_finished(Server))
      {
        SSL_do_handshake(Server);
        return;
      }

      char buffer[4096];
      for (;;)
      {
        ERR_clear_error();
        const int rc = SSL_read(Server, buffer, int(sizeof(buffer)));
        if (rc <= 0)
          return;

        ServerReceived.append(buffer, size_t(rc));
      }
    }

    void Handshake()
    {
      ASSERT_TRUE(Tls->StartHandshake());
      Pump();
      ASSERT_TRUE(Tls->IsHandshakeCompleted());
      ASSERT_TRUE(SSL_is_init_finished(Server));
    }

    void ServerWrite(const std::string& text)
    {
      ASSERT_EQ(SSL_write(Server, text.data(), int(text.size())), int(text.size()));
    }

    // Puts bytes in front of the client as if they came from the network;
    // the client must have a lower read pending.
    bool Deliver(const std::string& bytes)
    {
      if (Lower->PendingRead == nullptr)
        return false;

      IO::BufferPtr buffer = std::move(Lower->PendingRead);
      Lower->PendingRead.reset();

      if (bytes.size() > buffer->size())
        return false;

      std::memcpy(buffer->data(), bytes.data(), bytes.size());

      Result result;
      result.Stream = Lower;
      result.Context = &Marker;
      result.Op = Operation::Read;
      result.Buffer = buffer;
      result.Bytes = bytes.size();
      return Tls->ProcessLowerResult(result);
    }
  };

  std::string Text(const IO::BufferPtr& buffer, size_t size)
  {
    return std::string(buffer->data(), size);
  }
}

TEST(async_tls_stream, the_handshake_result_comes_out_as_it_went_in)
{
  Rig rig;
  rig.Handshake();

  ASSERT_TRUE(rig.Tls->HasPendingResult());

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));

  EXPECT_EQ(result.Op, Operation::Handshake);
  EXPECT_EQ(result.Stream, rig.Tls);
  EXPECT_EQ(result.Context, &rig.Marker);
  EXPECT_EQ(result.Bytes, 0u);
  EXPECT_EQ(result.Error, 0);
  EXPECT_EQ(result.Buffer, nullptr);

  EXPECT_FALSE(rig.Tls->HasPendingResult());
  EXPECT_FALSE(rig.Tls->PopPendingResult(result));
}

TEST(async_tls_stream, a_read_result_carries_the_buffer_the_read_was_started_with)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  ASSERT_EQ(result.Op, Operation::Handshake);

  auto buffer = std::make_shared<IO::Buffer>(4096, '.');
  const IO::Buffer* address = buffer.get();
  ASSERT_TRUE(rig.Tls->StartRead(buffer));

  rig.ServerWrite("hello over tls");
  rig.Pump();

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Read);
  EXPECT_EQ(result.Buffer.get(), address);
  EXPECT_EQ(result.Bytes, 14u);
  EXPECT_EQ(result.Error, 0);
  EXPECT_EQ(result.Context, &rig.Marker);
  EXPECT_EQ(result.Stream, rig.Tls);

  // the buffer is cut down to what was read, and holds it
  EXPECT_EQ(result.Buffer->size(), 14u);
  EXPECT_EQ(Text(result.Buffer, 14), "hello over tls");

  EXPECT_FALSE(rig.Tls->HasPendingResult());
}

TEST(async_tls_stream, results_come_out_in_the_order_they_were_queued)
{
  Rig rig;
  rig.Handshake();

  auto first = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(first));

  rig.ServerWrite("one");
  rig.Pump();

  // not collected yet: the next read can be started and answered as well
  auto second = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(second));

  rig.ServerWrite("two!");
  rig.Pump();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Handshake);

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Read);
  EXPECT_EQ(result.Buffer, first);
  EXPECT_EQ(Text(result.Buffer, result.Bytes), "one");

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Read);
  EXPECT_EQ(result.Buffer, second);
  EXPECT_EQ(Text(result.Buffer, result.Bytes), "two!");

  EXPECT_FALSE(rig.Tls->PopPendingResult(result));
}

TEST(async_tls_stream, a_write_result_arrives_once_the_ciphertext_is_written)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  ASSERT_EQ(result.Op, Operation::Handshake);

  BufferChain chain;
  ASSERT_TRUE(chain.Add(std::make_shared<IO::Buffer>(100, 'a')));
  ASSERT_TRUE(chain.Add(std::make_shared<IO::Buffer>(28, 'b')));
  ASSERT_TRUE(rig.Tls->StartWrite(chain));

  // the plaintext is encrypted, but the lower write has not completed yet
  EXPECT_FALSE(rig.Tls->HasPendingResult());

  rig.Pump();

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, 128u);
  EXPECT_EQ(result.Error, 0);
  EXPECT_EQ(result.Buffer, nullptr);
  EXPECT_EQ(result.Context, &rig.Marker);
  EXPECT_EQ(result.Stream, rig.Tls);

  EXPECT_EQ(rig.ServerReceived, std::string(100, 'a') + std::string(28, 'b'));
}

TEST(async_tls_stream, the_peer_closing_is_a_read_closed_result)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));

  auto buffer = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(buffer));

  ERR_clear_error();
  SSL_shutdown(rig.Server);
  rig.Pump();

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::ReadClosed);
  EXPECT_EQ(result.Bytes, 0u);
  EXPECT_EQ(result.Error, 0);
  EXPECT_EQ(result.Buffer, nullptr);
  EXPECT_EQ(result.Context, &rig.Marker);
  EXPECT_EQ(result.Stream, rig.Tls);
}

TEST(async_tls_stream, a_protocol_error_is_an_error_result)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));

  auto buffer = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(buffer));

  // not a TLS record
  const std::string garbage(64, '\x17');
  rig.Deliver(garbage);

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Error);
  EXPECT_EQ(result.Error, SSL_ERROR_SSL);
  EXPECT_EQ(result.Bytes, 0u);
  EXPECT_EQ(result.Buffer, nullptr);
  EXPECT_EQ(result.Context, &rig.Marker);
  EXPECT_EQ(result.Stream, rig.Tls);
}

TEST(async_tls_stream, the_owner_is_told_once_per_drain)
{
  Rig rig;

  int notified = 0;
  rig.Tls->SetResultSink([&] { ++notified; });

  rig.Handshake();
  EXPECT_EQ(notified, 1);

  auto buffer = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(buffer));
  rig.ServerWrite("x");
  rig.Pump();

  // a second result queued behind the first does not tell it again
  EXPECT_EQ(notified, 1);

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_FALSE(rig.Tls->PopPendingResult(result));

  // drained: the next one does
  auto next = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(next));
  rig.ServerWrite("y");
  rig.Pump();

  EXPECT_EQ(notified, 2);

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Read);
  EXPECT_EQ(result.Buffer, next);
}

TEST(async_tls_stream, a_result_nobody_collects_does_not_keep_its_buffer_after_close)
{
  Rig rig;
  rig.Handshake();

  std::weak_ptr<IO::Buffer> weak;
  {
    auto buffer = std::make_shared<IO::Buffer>(64, '.');
    weak = buffer;
    ASSERT_TRUE(rig.Tls->StartRead(buffer));
  }

  rig.ServerWrite("data");
  rig.Pump();

  // the result holds the buffer until it is collected or the stream closes
  EXPECT_FALSE(weak.expired());

  rig.Tls->Close();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(rig.Tls->HasPendingResult());
}

TEST(async_tls_stream, many_writes_arrive_intact_and_in_order_and_each_is_answered)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  ASSERT_EQ(result.Op, Operation::Handshake);

  std::string expected;
  for (int i = 0; i < 12; ++i)
  {
    const size_t size = 50 + size_t(i) * 37;
    const char fill = char('a' + i);

    BufferChain chain;
    ASSERT_TRUE(chain.Add(std::make_shared<IO::Buffer>(size, fill)));
    ASSERT_TRUE(rig.Tls->StartWrite(chain));
    expected.append(size, fill);

    rig.Pump();

    ASSERT_TRUE(rig.Tls->PopPendingResult(result)) << "write " << i;
    EXPECT_EQ(result.Op, Operation::Write);
    EXPECT_EQ(result.Bytes, size);
    EXPECT_EQ(result.Error, 0);
    EXPECT_FALSE(rig.Tls->HasPendingResult());
  }

  EXPECT_EQ(rig.ServerReceived, expected);
}

TEST(async_tls_stream, a_write_larger_than_one_record_is_delivered_whole)
{
  Rig rig;
  rig.Handshake();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  ASSERT_EQ(result.Op, Operation::Handshake);

  std::string payload(100 * 1024, '\0');
  for (size_t i = 0; i < payload.size(); ++i)
  {
    payload[i] = char('A' + (i * 7 + i / 251) % 26);
  }

  BufferChain chain;
  ASSERT_TRUE(chain.Add(std::make_shared<IO::Buffer>(payload.begin(), payload.end())));
  ASSERT_TRUE(rig.Tls->StartWrite(chain));

  rig.Pump();

  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, payload.size());
  EXPECT_EQ(result.Error, 0);

  EXPECT_EQ(rig.ServerReceived.size(), payload.size());
  EXPECT_TRUE(rig.ServerReceived == payload);
  EXPECT_FALSE(rig.Tls->HasPendingResult());
}

namespace
{
  // The stream's limit on ciphertext that is not yet written to the lower
  // stream, and the most one lower write can hold: the limit and one more
  // record (16 KB of plaintext and the TLS framing around it).
  constexpr size_t BatchLimit = 64 * 1024;
  constexpr size_t BiggestChunk = BatchLimit + 16 * 1024 + 512;

  std::string Pattern(size_t size, size_t seed = 0)
  {
    std::string text(size, '\0');
    for (size_t i = 0; i < size; ++i)
    {
      text[i] = char('A' + ((i + seed) * 7 + (i + seed) / 251) % 26);
    }

    return text;
  }

  IO::BufferPtr BufferOf(const std::string& text)
  {
    return std::make_shared<IO::Buffer>(text.begin(), text.end());
  }

  // Past the handshake, and its result collected
  void Established(Rig& rig)
  {
    rig.Handshake();

    Result result;
    ASSERT_TRUE(rig.Tls->PopPendingResult(result));
    ASSERT_EQ(result.Op, Operation::Handshake);
  }
}

TEST(async_tls_stream_batching, a_large_write_goes_down_in_a_few_big_chunks)
{
  Rig rig;
  Established(rig);

  const std::string payload = Pattern(1024 * 1024);

  BufferChain chain;
  ASSERT_TRUE(chain.Add(BufferOf(payload)));

  const size_t before = rig.Lower->Writes.size();
  ASSERT_TRUE(rig.Tls->StartWrite(chain));
  rig.Pump();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, payload.size());
  EXPECT_EQ(result.Error, 0);

  EXPECT_EQ(rig.ServerReceived.size(), payload.size());
  EXPECT_TRUE(rig.ServerReceived == payload);

  // 64 records of 16 KB: one lower write each when a record was written as soon
  // as it was made, about a quarter of that in batches of the limit
  const size_t writes = rig.Lower->Writes.size() - before;
  EXPECT_GE(writes, 12u);
  EXPECT_LE(writes, 20u);

  for (size_t i = before; i < rig.Lower->Writes.size(); ++i)
  {
    const size_t size = rig.Lower->Writes[i].Bytes.size();
    EXPECT_LE(size, BiggestChunk) << "write " << i;

    // every chunk but the last is a full batch
    if (i + 1 < rig.Lower->Writes.size())
      EXPECT_GE(size, BatchLimit) << "write " << i;

    // and a batch is one buffer
    EXPECT_EQ(rig.Lower->Writes[i].Buffers.size(), 1u) << "write " << i;
  }
}

TEST(async_tls_stream_batching, ciphertext_not_yet_written_stays_bounded_while_the_lower_stream_is_slow)
{
  Rig rig;
  Established(rig);

  const std::string payload = Pattern(1024 * 1024, 5);

  BufferChain chain;
  ASSERT_TRUE(chain.Add(BufferOf(payload)));

  const size_t before = rig.Lower->Writes.size();
  ASSERT_TRUE(rig.Tls->StartWrite(chain));

  // the lower stream is not completing anything: one batch has been handed
  // over, and that is all
  ASSERT_EQ(rig.Lower->Writes.size() - before, 1u);

  const size_t first = rig.Lower->Writes.back().Bytes.size();
  EXPECT_GE(first, BatchLimit);
  EXPECT_LE(first, BiggestChunk);

  auto diagnostics = rig.Tls->GetDiagnostics();
  EXPECT_TRUE(diagnostics.LowerWritePending);
  EXPECT_EQ(diagnostics.LowerWriteBytes, first);
  EXPECT_EQ(diagnostics.LowerWriteCount, 1u);
  EXPECT_TRUE(diagnostics.PlainWritePending);

  // another event drives the stream again (a read is started): it adds no
  // ciphertext behind the chunk
  auto read = std::make_shared<IO::Buffer>(64, '.');
  ASSERT_TRUE(rig.Tls->StartRead(read));
  EXPECT_EQ(rig.Lower->Writes.size() - before, 1u);
  EXPECT_EQ(rig.Tls->GetDiagnostics().LowerWriteBytes, first);

  EXPECT_FALSE(rig.Tls->HasPendingResult());

  // the lower stream catches up: one batch per completion, never more than a
  // batch outstanding, and the write is answered only after the last of them
  size_t completions = 0;
  while (rig.DeliverOneWrite())
  {
    ++completions;

    EXPECT_LE(rig.Tls->GetDiagnostics().LowerWriteBytes, BiggestChunk);

    const bool more = rig.Delivered < rig.Lower->Writes.size();
    EXPECT_EQ(rig.Tls->HasPendingResult(), !more) << "after completion " << completions;
  }

  EXPECT_GE(completions, 12u);

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, payload.size());
  EXPECT_FALSE(rig.Tls->HasPendingResult());

  EXPECT_TRUE(rig.ServerReceived == payload);
}

TEST(async_tls_stream_batching, many_small_views_are_written_together)
{
  Rig rig;
  Established(rig);

  std::string expected;
  BufferChain chain;
  for (int i = 0; i < 50; ++i)
  {
    const std::string piece = Pattern(100, size_t(i) * 13);
    ASSERT_TRUE(chain.Add(BufferOf(piece)));
    expected += piece;
  }

  const size_t before = rig.Lower->Writes.size();
  ASSERT_TRUE(rig.Tls->StartWrite(chain));
  rig.Pump();

  // fifty records, one lower write
  EXPECT_EQ(rig.Lower->Writes.size() - before, 1u);

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, expected.size());

  EXPECT_TRUE(rig.ServerReceived == expected);
}

TEST(async_tls_stream_batching, views_of_all_sizes_cross_the_batches_without_losing_a_byte)
{
  Rig rig;
  Established(rig);

  const std::vector<size_t> sizes = {
    10 * 1024, 70 * 1024, 3, 40 * 1024 + 7, 100, 130 * 1024, 1, 16 * 1024, 16 * 1024 - 1, 17 * 1024
  };

  std::string expected;
  BufferChain chain;
  for (size_t i = 0; i < sizes.size(); ++i)
  {
    const std::string piece = Pattern(sizes[i], i * 1000);
    ASSERT_TRUE(chain.Add(BufferOf(piece)));
    expected += piece;
  }

  ASSERT_TRUE(rig.Tls->StartWrite(chain));
  rig.Pump();

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, expected.size());
  EXPECT_FALSE(rig.Tls->HasPendingResult());

  EXPECT_EQ(rig.ServerReceived.size(), expected.size());
  EXPECT_TRUE(rig.ServerReceived == expected);
}

TEST(async_tls_stream_batching, a_small_write_is_still_one_lower_write_and_is_answered_after_it)
{
  Rig rig;
  Established(rig);

  const std::string payload = Pattern(5000);

  BufferChain chain;
  ASSERT_TRUE(chain.Add(BufferOf(payload)));

  const size_t before = rig.Lower->Writes.size();
  ASSERT_TRUE(rig.Tls->StartWrite(chain));

  ASSERT_EQ(rig.Lower->Writes.size() - before, 1u);
  EXPECT_FALSE(rig.Tls->HasPendingResult());

  ASSERT_TRUE(rig.DeliverOneWrite());

  Result result;
  ASSERT_TRUE(rig.Tls->PopPendingResult(result));
  EXPECT_EQ(result.Op, Operation::Write);
  EXPECT_EQ(result.Bytes, payload.size());
  EXPECT_TRUE(rig.ServerReceived == payload);
}

TEST(async_tls_stream_batching, random_chains_always_arrive_whole_and_are_always_answered)
{
  Rig rig;
  Established(rig);

  // sizes around the record size (16 KB), the limit (64 KB) and their multiples
  const std::vector<size_t> edges = {
    1, 2, 100, 16 * 1024 - 1, 16 * 1024, 16 * 1024 + 1
    , 32 * 1024, 48 * 1024 + 5, 64 * 1024 - 1, 64 * 1024, 64 * 1024 + 1
    , 65624, 80 * 1024, 128 * 1024, 128 * 1024 + 1, 200 * 1024
  };

  unsigned state = 12345;
  auto next = [&state](unsigned bound) {
    state = state * 1103515245u + 12345u;
    return (state >> 8) % bound;
  };

  std::string expected;
  for (int round = 0; round < 120; ++round)
  {
    BufferChain chain;
    size_t total = 0;

    const unsigned views = 1 + next(5);
    for (unsigned v = 0; v < views; ++v)
    {
      size_t size = edges[next(unsigned(edges.size()))];
      if (next(3) == 0)
        size = 1 + next(40 * 1024);

      const std::string piece = Pattern(size, size_t(round) * 31 + v);
      ASSERT_TRUE(chain.Add(BufferOf(piece)));
      expected += piece;
      total += size;
    }

    ASSERT_TRUE(rig.Tls->StartWrite(chain)) << "round " << round;

    // now and then another event drives the stream in the middle of the write
    if (next(4) == 0)
    {
      auto read = std::make_shared<IO::Buffer>(64, '.');
      if (rig.Tls->StartRead(read))
      {
        // nothing is coming from the server: the read just stays pending
      }
    }

    rig.Pump();

    Result result;
    ASSERT_TRUE(rig.Tls->PopPendingResult(result)) << "round " << round;
    ASSERT_EQ(result.Op, Operation::Write) << "round " << round;
    ASSERT_EQ(result.Bytes, total) << "round " << round;
    ASSERT_FALSE(rig.Tls->HasPendingResult());

    ASSERT_EQ(rig.ServerReceived.size(), expected.size()) << "round " << round;

    const auto diagnostics = rig.Tls->GetDiagnostics();
    ASSERT_FALSE(diagnostics.PlainWritePending) << "round " << round;
    ASSERT_EQ(diagnostics.LowerWriteBytes, 0u) << "round " << round;
  }

  EXPECT_TRUE(rig.ServerReceived == expected);

  for (const auto& write : rig.Lower->Writes)
  {
    EXPECT_LE(write.Bytes.size(), BiggestChunk);
  }
}

TEST(async_tls_stream_batching, writes_one_after_another_each_see_a_clean_start)
{
  Rig rig;
  Established(rig);

  std::string expected;
  for (int i = 0; i < 5; ++i)
  {
    // sizes on both sides of a batch
    const size_t size = (i % 2 == 0) ? 90 * 1024 + size_t(i) : 3 * 1024;
    const std::string piece = Pattern(size, size_t(i) * 77);
    expected += piece;

    BufferChain chain;
    ASSERT_TRUE(chain.Add(BufferOf(piece)));
    ASSERT_TRUE(rig.Tls->StartWrite(chain));
    rig.Pump();

    Result result;
    ASSERT_TRUE(rig.Tls->PopPendingResult(result)) << "write " << i;
    EXPECT_EQ(result.Op, Operation::Write);
    EXPECT_EQ(result.Bytes, size);
    EXPECT_FALSE(rig.Tls->HasPendingResult());
  }

  EXPECT_TRUE(rig.ServerReceived == expected);
}

TEST(async_tls_stream, stream_counter_tracks_lifetime)
{
  const uint64_t before = GetAsyncTlsStreams();

  {
    auto lower = std::make_shared<AsyncTest::FakeStream>();
    auto tls = std::make_shared<AsyncTlsStream>(lower, nullptr, false, nullptr);
    EXPECT_EQ(GetAsyncTlsStreams(), before + 1);
  }

  EXPECT_EQ(GetAsyncTlsStreams(), before);
}
