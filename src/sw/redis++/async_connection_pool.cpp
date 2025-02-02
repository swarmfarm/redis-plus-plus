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

#include "async_connection_pool.h"
#include <cassert>
#include "errors.h"

namespace sw {

namespace redis {

AsyncConnectionPool::AsyncConnectionPool(const EventLoopSPtr &loop,
        const ConnectionPoolOptions &pool_opts,
        const ConnectionOptions &connection_opts) :
            _loop(loop),
            _opts(connection_opts),
            _pool_opts(pool_opts) {
    if (_pool_opts.size == 0) {
        throw Error("CANNOT create an empty pool");
    }

    // Lazily create connections.
}

/*
AsyncConnectionPool::AsyncConnectionPool(SimpleSentinel sentinel,
                                const ConnectionPoolOptions &pool_opts,
                                const ConnectionOptions &connection_opts) :
                                    _opts(connection_opts),
                                    _pool_opts(pool_opts),
                                    _sentinel(std::move(sentinel)) {
    // In this case, the connection must be of TCP type.
    if (_opts.type != ConnectionType::TCP) {
        throw Error("Sentinel only supports TCP connection");
    }

    if (_opts.connect_timeout == std::chrono::milliseconds(0)
            || _opts.socket_timeout == std::chrono::milliseconds(0)) {
        throw Error("With sentinel, connection timeout and socket timeout cannot be 0");
    }

    // Cleanup connection options.
    _update_connection_opts("", -1);

    assert(_sentinel);
}
*/

AsyncConnectionPool::AsyncConnectionPool(AsyncConnectionPool &&that) {
    std::lock_guard<std::mutex> lock(that._mutex);

    _move(std::move(that));
}

AsyncConnectionPool& AsyncConnectionPool::operator=(AsyncConnectionPool &&that) {
    if (this != &that) {
        std::lock(_mutex, that._mutex);
        std::lock_guard<std::mutex> lock_this(_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> lock_that(that._mutex, std::adopt_lock);

        _move(std::move(that));
    }

    return *this;
}

AsyncConnectionPool::~AsyncConnectionPool() {
    assert(_loop);
    for (auto &connection : _pool) {
        _loop->unwatch(std::move(connection));
    }
}

AsyncConnectionSPtr AsyncConnectionPool::fetch() {
    std::unique_lock<std::mutex> lock(_mutex);

    if (_pool.empty()) {
        if (_used_connections == _pool_opts.size) {
            _wait_for_connection(lock);
        } else {
            // Lazily create a new connection.
            auto connection = _create();

            ++_used_connections;

            return connection;
        }
    }

    // _pool is NOT empty.
    auto connection = _fetch();

    //auto connection_lifetime = _pool_opts.connection_lifetime;

    /*
    if (_sentinel) {
        auto opts = _opts;
        auto role_changed = _role_changed(connection.options());
        auto sentinel = _sentinel;

        lock.unlock();

        if (role_changed || _need_reconnect(connection, connection_lifetime)) {
            try {
                connection = _create(sentinel, opts, false);
            } catch (const Error &e) {
                // Failed to reconnect, return it to the pool, and retry latter.
                release(std::move(connection));
                throw;
            }
        }

        return connection;
    }
    */

    lock.unlock();

    assert(connection);

    /*
    if (_need_reconnect(*connection, connection_lifetime)) {
        try {
            connection->reconnect();
        } catch (const Error &e) {
            // Failed to reconnect, return it to the pool, and retry latter.
            release(std::move(connection));
            throw;
        }
    }
    */

    return connection;
}

ConnectionOptions AsyncConnectionPool::connection_options() {
    std::lock_guard<std::mutex> lock(_mutex);

    return _opts;
}

void AsyncConnectionPool::release(AsyncConnectionSPtr connection) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        _pool.push_back(std::move(connection));
    }

    _cv.notify_one();
}

AsyncConnectionSPtr AsyncConnectionPool::create() {
    std::unique_lock<std::mutex> lock(_mutex);

    auto opts = _opts;

    /*
    if (_sentinel) {
        auto sentinel = _sentinel;

        lock.unlock();

        return _create(sentinel, opts, false);
    } else {
    */
        lock.unlock();

        return std::make_shared<AsyncConnection>(opts, _loop.get());
    //}
}

AsyncConnectionPool AsyncConnectionPool::clone() {
    std::unique_lock<std::mutex> lock(_mutex);

    auto opts = _opts;
    auto pool_opts = _pool_opts;

    /*
    if (_sentinel) {
        auto sentinel = _sentinel;

        lock.unlock();

        return ConnectionPool(sentinel, pool_opts, opts);
    } else {
    */
        lock.unlock();

        return AsyncConnectionPool(_loop, pool_opts, opts);
    //}
}

void AsyncConnectionPool::_move(AsyncConnectionPool &&that) {
    _loop = std::move(that._loop);
    _opts = std::move(that._opts);
    _pool_opts = std::move(that._pool_opts);
    _pool = std::move(that._pool);
    _used_connections = that._used_connections;
    //_sentinel = std::move(that._sentinel);
}

AsyncConnectionSPtr AsyncConnectionPool::_create() {
    /*
    if (_sentinel) {
        // Get Redis host and port info from sentinel.
        return _create(_sentinel, _opts, true);
    }
    */

    return std::make_shared<AsyncConnection>(_opts, _loop.get());
}

/*
Connection ConnectionPool::_create(SimpleSentinel &sentinel,
                                    const ConnectionOptions &opts,
                                    bool locked) {
    try {
        auto connection = sentinel.create(opts);

        std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
        if (!locked) {
            lock.lock();
        }

        const auto &connection_opts = connection.options();
        if (_role_changed(connection_opts)) {
            // Master/Slave has been changed, reconnect all connections.
            _update_connection_opts(connection_opts.host, connection_opts.port);
        }

        return connection;
    } catch (const StopIterError &e) {
        throw Error("Failed to create connection with sentinel");
    }
}
*/

AsyncConnectionSPtr AsyncConnectionPool::_fetch() {
    assert(!_pool.empty());

    auto connection = std::move(_pool.front());
    _pool.pop_front();

    return connection;
}

void AsyncConnectionPool::_wait_for_connection(std::unique_lock<std::mutex> &lock) {
    auto timeout = _pool_opts.wait_timeout;
    if (timeout > std::chrono::milliseconds(0)) {
        // Wait until _pool is no longer empty or timeout.
        if (!_cv.wait_for(lock,
                    timeout,
                    [this] { return !(this->_pool).empty(); })) {
            throw Error("Failed to fetch a connection in "
                    + std::to_string(timeout.count()) + " milliseconds");
        }
    } else {
        // Wait forever.
        _cv.wait(lock, [this] { return !(this->_pool).empty(); });
    }
}

bool AsyncConnectionPool::_need_reconnect(const AsyncConnection &connection,
                                    const std::chrono::milliseconds &connection_lifetime) const {
    if (connection.broken()) {
        return true;
    }

    if (connection_lifetime > std::chrono::milliseconds(0)) {
        auto now = std::chrono::steady_clock::now();
        if (now - connection.last_active() > connection_lifetime) {
            return true;
        }
    }

    return false;
}

}

}
