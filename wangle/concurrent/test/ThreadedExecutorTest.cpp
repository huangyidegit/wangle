/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/concurrent/ThreadedExecutor.h>

#include <gtest/gtest.h>

#include <folly/Conv.h>
#include <folly/futures/Future.h>
#include <folly/gen/Base.h>

namespace {

class ThreadedExecutorTest : public testing::Test {};

}

TEST_F(ThreadedExecutorTest, example) {
  wangle::ThreadedExecutor x;
  auto ret = folly::via(&x)
    .then([&] { return 42; })
    .then([&](int n) { return folly::to<std::string>(n); })
    .get();

  EXPECT_EQ("42", ret);
}

TEST_F(ThreadedExecutorTest, dtor_waits) {
  constexpr auto kDelay = std::chrono::milliseconds(100);
  auto x = std::make_unique<wangle::ThreadedExecutor>();
  auto fut = folly::via(&*x, [&] {
    /* sleep override */ std::this_thread::sleep_for(kDelay);
  });
  x = nullptr;

  EXPECT_TRUE(fut.isReady());
}

TEST_F(ThreadedExecutorTest, many) {
  constexpr auto kNumTasks = 1024;
  wangle::ThreadedExecutor x;
  auto rets = folly::collect(
      folly::gen::range<size_t>(0, kNumTasks)
      | folly::gen::map([&](size_t i) {
          return folly::via(&x)
            .then([=] { return i; })
            .then([](size_t k) { return folly::to<std::string>(k); });
        })
      | folly::gen::as<std::vector>())
    .get();

  EXPECT_EQ("42", rets[42]);
}

TEST_F(ThreadedExecutorTest, many_sleeping_constant_time) {
  constexpr auto kNumTasks = 256;
  constexpr auto kDelay = std::chrono::milliseconds(100);
  wangle::ThreadedExecutor x;
  auto rets = folly::collect(
      folly::gen::range<size_t>(0, kNumTasks)
      | folly::gen::map([&](size_t i) {
          return folly::via(&x)
            .then([=] {
              /* sleep override */ std::this_thread::sleep_for(kDelay);
            })
            .then([=] { return i; })
            .then([](size_t k) { return folly::to<std::string>(k); });
        })
      | folly::gen::as<std::vector>())
    .get();

  EXPECT_EQ("42", rets[42]);
}

TEST_F(ThreadedExecutorTest, many_sleeping_decreasing_time) {
  constexpr auto kNumTasks = 256;
  constexpr auto kDelay = std::chrono::milliseconds(100);
  wangle::ThreadedExecutor x;
  auto rets = folly::collect(
      folly::gen::range<size_t>(0, kNumTasks)
      | folly::gen::map([&](size_t i) {
          return folly::via(&x)
            .then([=] {
              auto delay = kDelay * (kNumTasks - i) / kNumTasks;
              /* sleep override */ std::this_thread::sleep_for(delay);
            })
            .then([=] { return i; })
            .then([](size_t k) { return folly::to<std::string>(k); });
        })
      | folly::gen::as<std::vector>())
    .get();

  EXPECT_EQ("42", rets[42]);
}
