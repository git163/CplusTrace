#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "log_tail.h"
#include "tests/test_util.h"

namespace {

using vmon::agent::LogTailer;

TEST(LogTail, ReadsOnlyCompleteLinesAndKeepsRemainder) {
  vmon::test::TempDir dir("vmon-tail");
  const std::string path = dir.file("app.log");
  ASSERT_TRUE(vmon::test::write_file(path, "line1\nline2\npart"));

  LogTailer tailer;
  tailer.add(path, "order");
  EXPECT_EQ(1u, tailer.file_count());

  auto lines = tailer.poll(100);
  ASSERT_EQ(2u, lines.size());
  EXPECT_EQ("line1", lines[0].msg);
  EXPECT_EQ("line2", lines[1].msg);
  EXPECT_EQ("order", lines[0].service);
  EXPECT_EQ(path, lines[0].path);
  EXPECT_GT(lines[0].time_ns, 0u);

  // 半行补齐后再读到两行（partial + line3）。
  ASSERT_TRUE(vmon::test::write_file(path, "ial\nline3\n", true));
  lines = tailer.poll(100);
  ASSERT_EQ(2u, lines.size());
  EXPECT_EQ("partial", lines[0].msg);
  EXPECT_EQ("line3", lines[1].msg);

  EXPECT_TRUE(tailer.poll(100).empty());
}

TEST(LogTail, HonorsMaxLinesPerPoll) {
  vmon::test::TempDir dir("vmon-tail-max");
  const std::string path = dir.file("app.log");
  ASSERT_TRUE(vmon::test::write_file(path, "a\nb\nc\n"));

  LogTailer tailer;
  tailer.add(path, "svc");
  auto lines = tailer.poll(1);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("a", lines[0].msg);

  lines = tailer.poll(10);
  ASSERT_EQ(2u, lines.size());
  EXPECT_EQ("b", lines[0].msg);
  EXPECT_EQ("c", lines[1].msg);
}

TEST(LogTail, ReopensAfterRotation) {
  vmon::test::TempDir dir("vmon-tail-rotate");
  const std::string path = dir.file("app.log");
  ASSERT_TRUE(vmon::test::write_file(path, "old\n"));

  LogTailer tailer;
  tailer.add(path, "svc");
  ASSERT_EQ(1u, tailer.poll(10).size());

  ASSERT_EQ(0, std::rename(path.c_str(), (path + ".1").c_str()));
  ASSERT_TRUE(vmon::test::write_file(path, "new\n"));

  const auto lines = tailer.poll(10);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("new", lines[0].msg);
}

TEST(LogTail, ReopensAfterTruncation) {
  vmon::test::TempDir dir("vmon-tail-truncate");
  const std::string path = dir.file("app.log");
  ASSERT_TRUE(vmon::test::write_file(path, "before\n"));

  LogTailer tailer;
  tailer.add(path, "svc");
  ASSERT_EQ(1u, tailer.poll(10).size());

  ASSERT_TRUE(vmon::test::write_file(path, "after\n"));  // truncate
  const auto lines = tailer.poll(10);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ("after", lines[0].msg);
}

TEST(LogTail, DuplicateAddIsIgnored) {
  vmon::test::TempDir dir("vmon-tail-dup");
  LogTailer tailer;
  tailer.add(dir.file("a.log"), "svc");
  tailer.add(dir.file("a.log"), "other");
  EXPECT_EQ(1u, tailer.file_count());
}

TEST(LogTail, MissingFileIsSkipped) {
  LogTailer tailer;
  tailer.add("/tmp/vmon-definitely-missing-file.log", "svc");
  EXPECT_TRUE(tailer.poll(10).empty());
}

}  // namespace
