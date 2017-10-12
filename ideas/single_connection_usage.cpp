boost::asio::io_service io_service;

boost::asio::spawn(io_service, [&] (boost::asio::yield_context yield) {
    auto connection_provider = apq::make_connection_provider(io_service);
    apq::cursor cursor;
    apq::text_protocol::request(connection_provider, cursor, "SELECT pg_is_in_recovery()", yield);
    std::cout << cursor.next(yield).at(0) << '\n';
});

io_service.run();