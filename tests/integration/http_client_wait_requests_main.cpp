#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>
#include <server_http.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

namespace {

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

void background_process(std::atomic<bool>& stop) {
    while (!stop.load()) {
        kurlyk::process();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

struct ProcessorGuard {
    std::atomic<bool> stop{false};
    std::thread thread;

    ProcessorGuard() : thread([this]() { background_process(stop); }) {}
    ~ProcessorGuard() {
        stop.store(true);
        if (thread.joinable()) thread.join();
    }
};

} // namespace

int main() {
    kurlyk::init(false);

    HttpServer server;
    server.config.port = 0;
    server.config.thread_pool_size = 2;

    server.resource["^/fast$"]["GET"] = [](std::shared_ptr<HttpServer::Response> response,
                                           std::shared_ptr<HttpServer::Request> request) {
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nfast";
    };

    server.resource["^/slow$"]["GET"] = [](std::shared_ptr<HttpServer::Response> response,
                                           std::shared_ptr<HttpServer::Request> request) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nslow";
    };

    std::atomic<int> flaky_counter{0};
    server.resource["^/flaky$"]["GET"] = [&flaky_counter](std::shared_ptr<HttpServer::Response> response,
                                                          std::shared_ptr<HttpServer::Request> request) {
        if (flaky_counter.fetch_add(1) == 0) {
            *response << "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 5\r\n\r\nerror";
        } else {
            *response << "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
        }
    };

    std::promise<unsigned short> port_promise;
    std::thread server_thread([&server, &port_promise]() {
        server.start([&port_promise](unsigned short port) {
            try {
                port_promise.set_value(port);
            } catch (...) {
            }
        });
    });

    const unsigned short port = port_promise.get_future().get();
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port);

    // --- Test 1: wait_requests() waits for callback ---
    {
        ProcessorGuard pg;

        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        std::atomic<int> callback_count{0};

        bool ok = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) {
                    ++callback_count;
                }
            });
        require(ok, "request should be accepted");

        client->wait_requests();
        require(callback_count.load() == 1, "wait_requests() must wait until callback is delivered");
        require(client->in_flight_requests() == 0, "client group must be idle after wait_requests()");

        client.reset();
    }

    // --- Test 2: wait_requests() waits only current client group ---
    {
        ProcessorGuard pg;

        auto client_a = std::make_unique<kurlyk::HttpClient>(base_url);
        auto client_b = std::make_unique<kurlyk::HttpClient>(base_url);
        std::atomic<int> callback_a{0};
        std::atomic<int> callback_b{0};

        bool ok_a = client_a->get("/fast", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_a;
            });
        bool ok_b = client_b->get("/slow", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_b;
            });
        require(ok_a, "client_a request should be accepted");
        require(ok_b, "client_b request should be accepted");

        auto t0 = std::chrono::steady_clock::now();
        client_a->wait_requests();
        auto dt = std::chrono::steady_clock::now() - t0;

        require(callback_a.load() == 1, "client_a.wait_requests() must wait until client_a callback is delivered");
        require(client_a->in_flight_requests() == 0, "client_a group must be idle after wait_requests()");
        require(callback_b.load() == 0 || dt < std::chrono::milliseconds(200),
                "client_a.wait_requests() must not wait for client_b's slow request");

        client_a.reset();
        client_b.reset();
    }

    // --- Test 3: wait_requests_for() timeout ---
    {
        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        std::atomic<int> callback_count{0};

        bool ok = client->get("/slow", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_count;
            });
        require(ok, "request should be accepted");

        // No background processor running: request is pending but never processed.
        bool done = client->wait_requests_for(std::chrono::milliseconds(10));
        require(!done, "wait_requests_for() should return false on timeout when no processor is running");

        ProcessorGuard pg;

        bool done_long = client->wait_requests_for(std::chrono::seconds(2));
        require(done_long, "wait_requests_for() should return true before long timeout when processor runs");
        require(callback_count.load() == 1, "callback must be delivered after wait_requests_for succeeds");

        client.reset();
    }

    // --- Test 4: per-client max_in_flight ---
    {
        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        client->set_max_in_flight(1);
        std::atomic<int> callback_count{0};

        bool first = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_count;
            });
        bool second = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_count;
            });

        require(first, "first request should be accepted");
        require(!second, "second request should be rejected by per-client in-flight limit");

        ProcessorGuard pg;
        client->wait_requests();
        require(callback_count.load() == 1, "only first callback should be delivered");

        bool third = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) ++callback_count;
            });
        require(third, "request after group idle should be accepted");

        client->wait_requests();
        require(callback_count.load() == 2, "third callback should be delivered");

        client.reset();
    }

    // --- Test 5: future-based API with max_in_flight ---
    {
        ProcessorGuard pg;

        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        client->set_max_in_flight(1);

        auto f1 = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers());
        auto f2 = client->get("/fast", kurlyk::QueryParams(), kurlyk::Headers());

        auto rejected = f2.get();
        require(rejected && rejected->ready, "rejected future must be ready");
        require(rejected->error_code == kurlyk::utils::make_error_code(kurlyk::utils::ClientError::QueueLimitExceeded),
                "rejected future must carry QueueLimitExceeded error code");

        auto completed = f1.get();
        require(completed && completed->ready, "first future must complete");

        client.reset();
    }

    // --- Test 6: wait_requests() waits through retry chain ---
    {
        ProcessorGuard pg;
        flaky_counter.store(0);

        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        client->set_retry_attempts(2, 50);
        std::atomic<int> callback_count{0};
        std::atomic<int> final_status{0};

        bool ok = client->get("/flaky", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                ++callback_count;
                if (response && response->ready) {
                    final_status.store(static_cast<int>(response->status_code));
                }
            });
        require(ok, "flaky request should be accepted");

        client->wait_requests();
        // /flaky returns 500 then 200; with 2 retry attempts we get
        // one intermediate callback (500, should_retry) and one final (200).
        require(callback_count.load() == 2, "retry chain should deliver intermediate and final callbacks");
        require(final_status.load() == 200, "final status after retry must be 200");
        require(client->in_flight_requests() == 0, "client group must be idle after wait_requests()");

        client.reset();
    }

    // --- Test 7: sequential rate limit does not self-block retry ---
    {
        ProcessorGuard pg;
        flaky_counter.store(0);

        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        client->set_rate_limit(3, 60000, kurlyk::RateLimitType::RL_GENERAL, true);
        client->set_retry_attempts(2, 50);
        std::atomic<int> final_status{0};

        bool ok = client->get("/flaky", kurlyk::QueryParams(), kurlyk::Headers(),
            [&](kurlyk::HttpResponsePtr response) {
                if (response && response->ready) {
                    final_status.store(static_cast<int>(response->status_code));
                }
            });
        require(ok, "flaky request with sequential limit should be accepted");

        bool done = client->wait_requests_for(std::chrono::seconds(2));

        require(done, "wait_requests_for(2s) must complete before timeout");
        require(final_status.load() == 200, "final status after sequential-limit retry must be 200");
        require(client->in_flight_requests() == 0, "client group must be idle after wait_requests_for()");

        client.reset();
    }

    // --- Test 8: concurrent same-client max_in_flight submission ---
    {
        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        client->set_max_in_flight(1);
        std::atomic<int> callback_count{0};

        std::atomic<bool> start{false};
        std::atomic<int> accepted{0};
        std::atomic<int> rejected{0};

        auto submit_fn = [&start, &client, &callback_count, &accepted, &rejected]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            bool ok = client->get("/slow", kurlyk::QueryParams(), kurlyk::Headers(),
                [&callback_count](kurlyk::HttpResponsePtr response) {
                    if (response && response->ready) ++callback_count;
                });

            if (ok) {
                ++accepted;
            } else {
                ++rejected;
            }
        };

        std::thread t1(submit_fn);
        std::thread t2(submit_fn);

        start.store(true, std::memory_order_release);

        t1.join();
        t2.join();

        require(accepted.load() == 1,
                "only one request should be accepted through one HttpClient with max_in_flight=1");
        require(rejected.load() == 1,
                "one request should be rejected by client-side max_in_flight");

        ProcessorGuard pg;
        client->wait_requests();
        require(callback_count.load() == 1, "only accepted request callback should be delivered");
        require(client->in_flight_requests() == 0, "client group must be idle after wait_requests()");

        client.reset();
    }

    // --- Test 9: wait_requests() on empty group returns immediately ---
    {
        ProcessorGuard pg;
        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        auto t0 = std::chrono::steady_clock::now();
        client->wait_requests();
        auto dt = std::chrono::steady_clock::now() - t0;
        require(dt < std::chrono::milliseconds(50),
                "wait_requests() on empty group must return immediately");
        client.reset();
    }

    // --- Test 10: wait_requests_for() on empty group returns true immediately ---
    {
        auto client = std::make_unique<kurlyk::HttpClient>(base_url);
        bool done = client->wait_requests_for(std::chrono::milliseconds(10));
        require(done, "wait_requests_for() on empty group must return true immediately");
        client.reset();
    }

    server.stop();
    server_thread.join();

    kurlyk::deinit();
    std::cout << "HttpClient wait_requests and max_in_flight integration test passed" << std::endl;
    return 0;
}
