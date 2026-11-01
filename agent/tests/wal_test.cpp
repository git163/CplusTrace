#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "tests/test_util.h"
#include "wal.h"

namespace {

using vmon::agent::Wal;

TEST(Wal, RejectsEmptyDirectory) {
  Wal wal;
  EXPECT_FALSE(wal.open("", 100, 1000));
  wal.append("ignored");
  EXPECT_TRUE(wal.sealed_segments().empty());
}

TEST(Wal, RotatesSegmentsAndTracksSealedFiles) {
  vmon::test::TempDir dir("vmon-wal");
  Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 120, 10000));

  const std::string line(60, 'a');
  wal.append(line);
  EXPECT_TRUE(wal.sealed_segments().empty());

  wal.append(line);  // 当前段超过 120 字节，触发轮转
  ASSERT_EQ(1u, wal.sealed_segments().size());
  EXPECT_NE(wal.current_segment(), wal.sealed_segments()[0]);

  wal.append(line);
  EXPECT_EQ(1u, wal.sealed_segments().size());
  EXPECT_GT(wal.total_bytes(), 120u);

  const std::string sealed = wal.sealed_segments()[0];
  wal.remove_segment(sealed);
  EXPECT_TRUE(wal.sealed_segments().empty());
  wal.remove_segment(wal.current_segment());  // 当前段不可删除
  EXPECT_FALSE(wal.current_segment().empty());
}

TEST(Wal, ReopenSealsPreviousSegmentAndKeepsData) {
  vmon::test::TempDir dir("vmon-wal-reopen");
  {
    Wal wal;
    ASSERT_TRUE(wal.open(dir.path(), 1024, 10000));
    wal.append("first");
    wal.append("second");
  }

  Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 1024, 10000));
  const auto sealed = wal.sealed_segments();
  ASSERT_EQ(1u, sealed.size());
  const std::string content = vmon::test::read_file(sealed[0]);
  EXPECT_NE(std::string::npos, content.find("first\n"));
  EXPECT_NE(std::string::npos, content.find("second\n"));
}

TEST(Wal, SealIfOlderThanFlushesCurrentSegment) {
  vmon::test::TempDir dir("vmon-wal-seal");
  Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 1024, 10000));

  wal.append("first");
  // 年龄阈值很大时不封存。
  wal.seal_if_older_than(3600ull * 1000000000ull);
  EXPECT_TRUE(wal.sealed_segments().empty());

  // 阈值为 0 时立即封存当前非空段。
  wal.seal_if_older_than(0);
  ASSERT_EQ(1u, wal.sealed_segments().size());
  EXPECT_NE(std::string::npos,
            vmon::test::read_file(wal.sealed_segments()[0]).find("first\n"));

  // 当前段为空时不产生新的空段。
  wal.seal_if_older_than(0);
  EXPECT_EQ(1u, wal.sealed_segments().size());
}

TEST(Wal, EnforcesMaxBytesByDroppingOldestSegments) {
  vmon::test::TempDir dir("vmon-wal-limit");
  Wal wal;
  ASSERT_TRUE(wal.open(dir.path(), 51, 100));

  const std::string line(50, 'x');
  for (int i = 0; i < 10; ++i) wal.append(line);

  EXPECT_LE(wal.total_bytes(), 100u);
  EXPECT_LE(wal.sealed_segments().size(), 1u);
}

}  // namespace
