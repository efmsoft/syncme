#include <windows.h>
#include <gtest/gtest.h>

#include <Syncme/File/Mapping.h>

using namespace Syncme;

TEST(File, Mapping)
{
  const char* section_name = "test";
  uint64_t size1 = uint64_t(32) * 1024;
  uint64_t size2 = uint64_t(64) * 1024;

  File::Mapping map1(false);
  EXPECT_EQ(map1.CreateSection(section_name, size1), S_OK);
  EXPECT_EQ(map1.Resize(size2, section_name), S_OK);

  const char* t = "test data";
  char* data1 = (char*)map1.Map(0, size2);
  EXPECT_NE(data1, nullptr);

  strcpy(data1, t);
  map1.Unmap(data1);

  File::Mapping map2(true);
  EXPECT_EQ(map2.OpenSection(section_name, size2), S_OK);

  char* data2 = (char*)map2.Map(0, size2);
  EXPECT_NE(data2, nullptr);

  EXPECT_EQ(strcmp(data2, t), 0);

  map2.Unmap(data2);
}