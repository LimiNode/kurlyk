#define KURLYK_AUTO_INIT 0
#include <kurlyk.hpp>
#include <server_http.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

struct CallbackStats {
    int final_count = 0;
    int retry_count = 0;
    std::string streamed;
    bool prefix_signalled = false;
    kurlyk::HttpResponsePtr final;
};

kurlyk::HttpResponseCallback callback_for(
        CallbackStats& stats,
        std::promise<void>& done,
        std::promise<void>* prefix = nullptr) {
    return [&stats, &done, prefix](kurlyk::HttpResponsePtr response) {
        require(static_cast<bool>(response), "Response callback received null");
        if (response->stream_chunk) {
            require(!response->ready, "Intermediate stream chunk was marked ready");
            stats.streamed += response->content;
            if (prefix && stats.streamed.size() >= 8 && !stats.prefix_signalled) {
                stats.prefix_signalled = true;
                prefix->set_value();
            }
        } else if (response->ready) {
            ++stats.final_count;
            if (stats.final_count == 1) {
                stats.final = std::move(response);
                done.set_value();
            }
        } else {
            ++stats.retry_count;
        }
    };
}

void await_completion(kurlyk::HttpClient& client, std::future<void>& done, CallbackStats& stats) {
    require(done.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
            "Timed out waiting for final response");
    require(client.wait_requests_for(std::chrono::seconds(10)),
            "Completed request did not release its client group");
    require(stats.final_count == 1, "Request did not deliver exactly one final callback");
    require(stats.retry_count == 0, "Body-limit request unexpectedly retried");
}

void run_request(kurlyk::HttpClient& client, const std::string& path, CallbackStats& stats,
                 std::promise<void>* prefix = nullptr) {
    std::promise<void> done;
    std::future<void> future = done.get_future();
    require(client.get(path, kurlyk::QueryParams(), kurlyk::Headers(),
                       callback_for(stats, done, prefix)), "Request was not admitted");
    await_completion(client, future, stats);
}

void check_too_large(const CallbackStats& stats, std::size_t limit) {
    require(stats.final->error_code ==
                kurlyk::utils::make_error_code(kurlyk::utils::ClientError::ResponseTooLarge),
            "Oversized body did not report ResponseTooLarge");
    require(!stats.final->error_message.empty(), "Body-limit error has no diagnostic");
    require(stats.final->content.size() <= limit, "Oversized chunk was retained");
    require(stats.streamed.size() <= limit, "Oversized chunk was emitted to streaming callback");
    require(stats.final->retry_attempt == 1, "Body-limit error was automatically retried");
}

} // namespace

int main() {
    kurlyk::HttpRequest config;
    require(config.max_response_body_bytes == 0, "Default body limit must preserve unlimited requests");
    config.set_max_response_body_bytes(8);
    require(config.max_response_body_bytes == 8, "Request body-limit setter did not store its value");
    require(kurlyk::HttpRequestHandler::write_http_response_body(
                nullptr, static_cast<std::size_t>(-1), 2, nullptr) == 0,
            "Body callback did not reject an overflowing byte count");

    HttpServer server;
    server.config.address = "127.0.0.1";
    server.config.port = 0;
    server.config.thread_pool_size = 2;
    std::atomic<int> content_hits(0);
    std::atomic<int> stream_hits(0);
    std::promise<void> stream_prefix;
    std::shared_future<void> stream_prefix_future = stream_prefix.get_future().share();
    std::mutex held_mutex;
    std::shared_ptr<HttpServer::Response> held_response;

    server.resource["^/content$"]["GET"] = [&content_hits](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        ++content_hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabcdefghij";
    };
    server.resource["^/exact$"]["GET"] = [](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nabcdefgh";
    };
    server.resource["^/chunked$"]["GET"] = [](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                     "3\r\nabc\r\n7\r\ndefghij\r\n0\r\n\r\n";
    };
    server.resource["^/close$"]["GET"] = [](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabcdefghij";
    };
    server.resource["^/stream$"]["GET"] = [&stream_hits, stream_prefix_future](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        ++stream_hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabcdefgh";
        response->send([response, stream_prefix_future](const SimpleWeb::error_code& error) {
            require(!error, "Could not send streaming prefix");
            // Wait for actual callback delivery so TCP coalescing cannot hide cumulative accounting.
            require(stream_prefix_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
                    "Streaming prefix was not delivered before the suffix");
            *response << "ij";
        });
    };
    server.resource["^/cancel$"]["GET"] = [&held_mutex, &held_response](
            std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request>) {
        response->close_connection_after_response = true;
        {
            std::lock_guard<std::mutex> lock(held_mutex);
            held_response = response;
        }
        *response << "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabcdefgh";
        response->send();
    };

    std::promise<unsigned short> port_promise;
    std::future<unsigned short> port_future = port_promise.get_future();
    std::thread server_thread([&server, &port_promise]() {
        server.start([&port_promise](unsigned short port) { port_promise.set_value(port); });
    });
    require(port_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
            "Timed out starting loopback HTTP server");
    const std::string host = "http://127.0.0.1:" + std::to_string(port_future.get());

    kurlyk::init(true);
    {
        kurlyk::HttpClient client(host);
        client.set_max_in_flight(1);
        client.set_rate_limit(1000, 1000, kurlyk::RateLimitType::RL_GENERAL, true);
        client.set_retry_attempts(3, 0);
        CallbackStats unlimited;
        run_request(client, "/content", unlimited);
        require(!unlimited.final->error_code && unlimited.final->content == "abcdefghij",
                "Unlimited request no longer accepts its full body");

        client.set_max_response_body_bytes(8);
        CallbackStats exact;
        run_request(client, "/exact", exact);
        require(!exact.final->error_code && exact.final->content == "abcdefgh",
                "Body exactly at the limit was rejected");
        const int hits_before = content_hits.load();
        CallbackStats oversized;
        run_request(client, "/content", oversized);
        check_too_large(oversized, 8);
        require(content_hits.load() == hits_before + 1, "Oversized Content-Length response was retried");

        const char* paths[] = {"/chunked", "/close"};
        for (std::size_t i = 0; i != 2; ++i) {
            CallbackStats failed;
            run_request(client, paths[i], failed);
            check_too_large(failed, 8);
            client.set_max_response_body_bytes(10);
            CallbackStats accepted;
            run_request(client, paths[i], accepted);
            require(!accepted.final->error_code && accepted.final->content == "abcdefghij",
                    "Chunked or unknown-length body exactly at the limit was rejected");
            client.set_max_response_body_bytes(8);
        }

        client.set_streaming(true);
        CallbackStats streamed;
        run_request(client, "/stream", streamed, &stream_prefix);
        check_too_large(streamed, 8);
        require(streamed.streamed == "abcdefgh" && streamed.final->content == "abcdefgh",
                "Cumulative streaming limit did not preserve only its accepted prefix");
        require(stream_hits.load() == 1, "Oversized streaming response was retried");

        CallbackStats after_limit;
        run_request(client, "/exact", after_limit);
        require(!after_limit.final->error_code && after_limit.final->content == "abcdefgh",
                "Body-limit abort did not release resources for the next request");

        client.set_max_response_body_bytes(16);
        CallbackStats cancelled;
        std::promise<void> cancelled_done;
        std::future<void> cancelled_future = cancelled_done.get_future();
        std::promise<void> cancel_prefix;
        std::future<void> cancel_prefix_future = cancel_prefix.get_future();
        require(client.get("/cancel", kurlyk::QueryParams(), kurlyk::Headers(),
                           callback_for(cancelled, cancelled_done, &cancel_prefix)),
                "Bounded cancellation request was not admitted");
        require(cancel_prefix_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
                "Cancellation test did not receive a body prefix");
        client.cancel_requests();
        await_completion(client, cancelled_future, cancelled);
        require(cancelled.final->error_code ==
                    kurlyk::utils::make_error_code(kurlyk::utils::ClientError::CancelledByUser),
                "Cancelled bounded request did not report CancelledByUser");
        require(cancelled.final->content == "abcdefgh", "Cancellation retained an unexpected body");
        {
            std::lock_guard<std::mutex> lock(held_mutex);
            held_response.reset();
        }
        CallbackStats after_cancel;
        run_request(client, "/exact", after_cancel);
        require(!after_cancel.final->error_code && after_cancel.final->content == "abcdefgh",
                "Cancelled request did not release resources for the next request");
    }
    kurlyk::deinit();
    server.stop();
    server_thread.join();
    std::cout << "HTTP response body limit integration test passed" << std::endl;
}
