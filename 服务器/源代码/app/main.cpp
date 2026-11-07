#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "Server.h"
#include "TicketService.h"

namespace {

volatile std::sig_atomic_t g_exit_requested = 0;

void on_signal(int) {
    g_exit_requested = 1;
}

}  // namespace

int main() {
    auto service = std::make_shared<TicketService>();

    Server server(
        [service](const std::string& message) -> std::function<void()> {
            return [service, message]() { service->handle(1, message); };
        },
        9101,   // ticket server port
        8,      // parser threads
        16,     // business threads
        4);     // reactor threads

    server.mark_fast_prefix("ping|");
    server.mark_fast_prefix("whoami|");
    server.mark_fast_prefix("logout|");
    server.mark_fast_prefix("notice.push|");
    // Reservation first passes the Redis admission gate inside TicketService.
    // Database work starts only after Redis has atomically reserved stock.
    server.mark_fast_prefix("order.reserve|");
    server.mark_fast_prefix("refund.submit|");
    server.mark_db_prefix("register|");
    server.mark_db_prefix("login|");
    server.mark_db_prefix("login.challenge|");
    server.mark_db_prefix("login.proof|");
    server.mark_db_prefix("catalog.");
    server.mark_db_prefix("order.status|");
    server.mark_db_prefix("order.my|");

    Server::DBConfig db;
    db.connections = 20;
    db.db_workers = 20;
    db.host = "127.0.0.1";
    db.user = "chat";
    db.password = "chat123";
    db.database = "chat";
    db.port = 3306;
    server.set_db_config(db);

    server.set_error_handler(
        [](std::shared_ptr<Internalconnection> conn,
           const std::string& stage,
           const std::string& err) {
            std::cerr << "[" << stage << "] " << err << std::endl;
            if (conn)
                conn->send_function("request|error|INTERNAL|internal error");
        });

    if (!server.start()) {
        std::cerr << "ticket server failed to start" << std::endl;
        return 1;
    }

    std::cout << "ticket server listening on 0.0.0.0:9101" << std::endl;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    while (!g_exit_requested)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    server.stop();
    std::cout << "ticket server stopped" << std::endl;
    return 0;
}
