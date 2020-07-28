#include "gtest/gtest.h"

// The main function to run GTests

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}