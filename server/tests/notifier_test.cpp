#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "notifier.h"

namespace {

using vmon::server::EventRecord;
using vmon::server::Notifier;
using vmon::server::NotifierConfig;

TEST(Notifier, PostsEventJsonToWebhook) {
  httplib::Server server;
  std::mutex mutex;
  std::condition_variable cv;
  std::string received;
  server.Post("/hook", [&](const httplib::Request& req,
                           httplib::Response& res) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      received = req.body;
    }
    cv.notify_all();
    res.status = 200;
    res.set_content("{\"ok\":true}", "application/json");
  });
  const int port = server.bind_to_any_port("127.0.0.1");
  ASSERT_GT(port, 0);
  std::thread listener([&]() { server.listen_after_bind(); });

  NotifierConfig config;
  config.enabled = true;
  config.url = "http://127.0.0.1:" + std::to_string(port) + "/hook";
  config.timeout_ms = 2000;
  Notifier notifier(config);
  notifier.start();

  EventRecord event;
  event.id = "evt-webhook-1";
  event.type = "alert";
  event.severity = "critical";
  event.title = "慢调用";
  event.service = "order";
  event.status = "open";
  event.time_ns = 123456789;
  notifier.enqueue(event);

  bool got = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    got = cv.wait_for(lock, std::chrono::seconds(5),
                      [&]() { return !received.empty(); });
  }
  notifier.stop();
  server.stop();
  if (listener.joinable()) listener.join();

  ASSERT_TRUE(got) << "Webhook 未在超时内收到事件";
  nlohmann::json payload = nlohmann::json::parse(received);
  EXPECT_EQ("vmon", payload.value("source", std::string()));
  ASSERT_TRUE(payload.contains("event"));
  EXPECT_EQ("evt-webhook-1", payload["event"].value("id", std::string()));
  EXPECT_EQ("慢调用", payload["event"].value("title", std::string()));
  EXPECT_EQ(1u, notifier.sent());
}

TEST(Notifier, DisabledDoesNothing) {
  NotifierConfig config;
  config.enabled = false;
  Notifier notifier(config);
  notifier.start();  // 未启用时不应创建线程
  EventRecord event;
  event.id = "ignored";
  notifier.enqueue(event);
  notifier.stop();
  EXPECT_EQ(0u, notifier.sent());
}

}  // namespace
