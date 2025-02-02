/**************************************************************************
   Copyright (c) 2021 sewenew

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 *************************************************************************/

#ifndef SEWENEW_REDISPLUSPLUS_ASYNC_CONNECTION_POOL_H
#define SEWENEW_REDISPLUSPLUS_ASYNC_CONNECTION_POOL_H

#include <cassert>
#include <chrono>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <deque>
#include "connection.h"
#include "connection_pool.h"
#include "async_connection.h"

namespace sw {

namespace redis {

class AsyncConnectionPool {
public:
    AsyncConnectionPool(const EventLoopSPtr &loop,
                    const ConnectionPoolOptions &pool_opts,
                    const ConnectionOptions &connection_opts);

    //AsyncConnectionPool(SimpleSentinel sentinel,
    //                const ConnectionPoolOptions &pool_opts,
    //                const ConnectionOptions &connection_opts);

    AsyncConnectionPool() = default;

    AsyncConnectionPool(AsyncConnectionPool &&that);
    AsyncConnectionPool& operator=(AsyncConnectionPool &&that);

    AsyncConnectionPool(const AsyncConnectionPool &) = delete;
    AsyncConnectionPool& operator=(const AsyncConnectionPool &) = delete;

    ~AsyncConnectionPool();

    // Fetch a connection from pool.
    AsyncConnectionSPtr fetch();

    ConnectionOptions connection_options();

    void release(AsyncConnectionSPtr connection);

    // Create a new connection.
    AsyncConnectionSPtr create();

    AsyncConnectionPool clone();

private:
    void _move(AsyncConnectionPool &&that);

    // NOT thread-safe
    AsyncConnectionSPtr _create();

    //Connection _create(SimpleSentinel &sentinel, const ConnectionOptions &opts, bool locked);

    AsyncConnectionSPtr _fetch();

    void _wait_for_connection(std::unique_lock<std::mutex> &lock);

    bool _need_reconnect(const AsyncConnection &connection,
                            const std::chrono::milliseconds &connection_lifetime) const;

    /*
    void _update_connection_opts(const std::string &host, int port) {
        _opts.host = host;
        _opts.port = port;
    }

    bool _role_changed(const ConnectionOptions &opts) const {
        return opts.port != _opts.port || opts.host != _opts.host;
    }
    */

    EventLoopSPtr _loop;

    ConnectionOptions _opts;

    ConnectionPoolOptions _pool_opts;

    std::deque<AsyncConnectionSPtr> _pool;

    std::size_t _used_connections = 0;

    std::mutex _mutex;

    std::condition_variable _cv;

    //SimpleSentinel _sentinel;
};

using AsyncConnectionPoolSPtr = std::shared_ptr<AsyncConnectionPool>;

class SafeAsyncConnection {
public:
    explicit SafeAsyncConnection(AsyncConnectionPool &pool) : _pool(pool), _connection(_pool.fetch()) {
        assert(_connection);
    }

    SafeAsyncConnection(const SafeAsyncConnection &) = delete;
    SafeAsyncConnection& operator=(const SafeAsyncConnection &) = delete;

    SafeAsyncConnection(SafeAsyncConnection &&) = delete;
    SafeAsyncConnection& operator=(SafeAsyncConnection &&) = delete;

    ~SafeAsyncConnection() {
        _pool.release(std::move(_connection));
    }

    AsyncConnection& connection() {
        return *_connection;
    }

private:
    AsyncConnectionPool &_pool;
    AsyncConnectionSPtr _connection;
};

// NOTE: This class is similar to `SafeAsyncConnection`.
// The difference is that `SafeAsyncConnection` tries to avoid copying a std::shared_ptr.
class GuardedAsyncConnection {
public:
    explicit GuardedAsyncConnection(const AsyncConnectionPoolSPtr &pool) : _pool(pool),
                                                        _connection(_pool->fetch()) {
        assert(!_connection->broken());
    }

    GuardedAsyncConnection(const GuardedAsyncConnection &) = delete;
    GuardedAsyncConnection& operator=(const GuardedAsyncConnection &) = delete;

    GuardedAsyncConnection(GuardedAsyncConnection &&) = default;
    GuardedAsyncConnection& operator=(GuardedAsyncConnection &&) = default;

    ~GuardedAsyncConnection() {
        // If `GuardedAsyncConnection` has been moved, `_pool` will be nullptr.
        if (_pool) {
            _pool->release(std::move(_connection));
        }
    }

    AsyncConnectionSPtr& connection() {
        return _connection;
    }

private:
    AsyncConnectionPoolSPtr _pool;
    AsyncConnectionSPtr _connection;
};

using GuardedAsyncConnectionSPtr = std::shared_ptr<GuardedAsyncConnection>;

}

}

#endif // end SEWENEW_REDISPLUSPLUS_ASYNC_CONNECTION_POOL_H
