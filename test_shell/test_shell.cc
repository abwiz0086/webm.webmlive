// webmtestshell_2008.cpp : Defines the entry point for the console application.
//

#include "test_shell.h"

#include <conio.h>
#include <stdio.h>
#include <tchar.h>

#include "gtest/gtest.h"


int _tmain(int argc, _TCHAR* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    RUN_ALL_TESTS();

    printf("press a key to exit...\n");

    while( !_kbhit() )
    {
        Sleep(1);
    }

    return 0;
}
