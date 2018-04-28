#pragma once

#include <ozo/connection.h>
#include <yamail/resource_pool/async/pool.hpp>

namespace ozo::impl {

template <typename Provider>
struct get_connection_pool {
    using type = yamail::resource_pool::async::pool<
        std::decay_t<decltype(unwrap_connection(std::declval<connectable_type<Provider>&>()))>
    >;
};

template <typename Provider>
using connection_pool = typename get_connection_pool<Provider>::type;

template <typename Provider>
struct pooled_connection {
    using handle_type = typename connection_pool<Provider>::handle;
    using underlying_type = typename handle_type::value_type;

    handle_type handle_;

    pooled_connection(handle_type&& handle) : handle_(std::move(handle)) {}

    bool empty() const {return handle_.empty();}

    void reset(underlying_type&& v) {
        handle_.reset(std::move(v));
    }

    ~pooled_connection() {
        if (!empty() && connection_bad(*this)) {
            handle_.waste();
        }
    }

    friend auto& unwrap_connection(pooled_connection& conn) noexcept {
        return unwrap_connection(*(conn.handle_));
    }
    friend const auto& unwrap_connection(const pooled_connection& conn) noexcept {
        return unwrap_connection(*(conn.handle_));
    }
};
template <typename Provider>
using pooled_connection_ptr = std::shared_ptr<pooled_connection<Provider>>;

template <typename Provider, typename Handler>
struct pooled_connection_wrapper {
    Provider provider_;
    Handler handler_;

    using connection = pooled_connection<Provider>;
    using connection_ptr = pooled_connection_ptr<Provider>;

    struct wrapper {
        Handler handler_;
        connection_ptr conn_;

        template <typename Conn>
        void operator () (error_code ec, Conn&& conn) {
            static_assert(std::is_same_v<connectable_type<Provider>, std::decay_t<Conn>>,
                "Conn must connectiable type of Provider");
            if (!ec) {
                conn_->reset(std::move(unwrap_connection(conn)));
            }
            handler_(std::move(ec), std::move(conn_));
        }

        template <typename Func>
        friend void asio_handler_invoke(Func&& f, wrapper* ctx) {
            using ::boost::asio::asio_handler_invoke;
            asio_handler_invoke(std::forward<Func>(f), std::addressof(ctx->handler_));
        }
    };

    template <typename Handle>
    void operator ()(error_code ec, Handle&& handle) {
        if (ec) {
            return handler_(std::move(ec), connection_ptr{});
        }

        auto conn = std::make_shared<connection>(std::forward<Handle>(handle));
        if (!conn->empty() && connection_good(conn)) {
            return handler_(std::move(ec), std::move(conn));
        }

        async_get_connection(provider_, wrapper{std::move(handler_), std::move(conn)});
    }

    template <typename Func>
    friend void asio_handler_invoke(Func&& f, pooled_connection_wrapper* ctx) {
        using ::boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Func>(f), std::addressof(ctx->handler_));
    }
};

template <typename P, typename Handler>
auto wrap_pooled_connection_handler(P&& provider, Handler&& handler) {

    static_assert(ConnectionProvider<P>, "is not a ConnectionProvider");

    return pooled_connection_wrapper<std::decay_t<P>, std::decay_t<Handler>> {
        std::forward<P>(provider), std::forward<Handler>(handler)
    };
}

static_assert(Connectable<pooled_connection_ptr<connection<empty_oid_map, no_statistics>>>,
    "pooled_connection_ptr is not a Connectable concept");

static_assert(ConnectionProvider<pooled_connection_ptr<connection<empty_oid_map, no_statistics>>>,
    "pooled_connection_ptr is not a ConnectionProvider concept");

} // namespace ozo::impl