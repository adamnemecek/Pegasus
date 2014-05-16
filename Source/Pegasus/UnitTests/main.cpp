/****************************************************************************************/
/*                                                                                      */
/*                                       Pegasus                                        */
/*                                                                                      */
/****************************************************************************************/

//! \file   main.cpp (unit tests entry point)
//! \author Kleber Garcia
//! \date   3/30/2014
//! \brief  Set of unit tests, used to prove soundness of 
//!         any data structure. To run, edit Utils project to generate an executable, and run

#include "Pegasus/UnitTests/UtilsTests.h"
#include <stdio.h>

typedef bool (*TestFunc)(void);


//! Utility function, presents and runs unit tests to tty
bool RunTests(TestFunc func, const char * testTitle, int& outSucceses, int& outTotals)
{
    printf("***********************\n");
    printf("RUNNING TEST: %s\n", testTitle);
    printf("***********************\n");
    bool result = func();
    outSucceses += result ? 1 : 0;
    ++outTotals;
    printf("Result: %s", result ? "SUCCESS" : "FAIL");
    printf("\n-------------------------\n\n");
    return result;
}

int main()
{
    int successes = 0;
    int total = 0;

#define RUN_TEST(name) RunTests(UNIT_TEST_##name, #name, successes, total)
    
    ///////////////////////////////////////////////////////////////////
    // UNIT TESTS - add here your UTILS package unit tests executions//
    ///////////////////////////////////////////////////////////////////
    
    //memcpy
    RUN_TEST(Memcpy1);
    RUN_TEST(Memcpy2);
    RUN_TEST(Memcpy3);

    //memset
    RUN_TEST(Memset1);
    RUN_TEST(Memset2);

    //strcmp
    RUN_TEST(Strcmp1);
    RUN_TEST(Strcmp2);
    RUN_TEST(Strcmp3);
    RUN_TEST(Strcmp4);

    //stricmp
    RUN_TEST(Stricmp1);
    RUN_TEST(Stricmp2);
    RUN_TEST(Stricmp3);

    //strlen
    RUN_TEST(Strlen1);
    RUN_TEST(Strlen2);
    RUN_TEST(Strlen3);

    //strchr
    RUN_TEST(Strchr1);
    RUN_TEST(Strchr2);
    RUN_TEST(Strchr3);

    //strrchr
    RUN_TEST(Strrchr1);
    RUN_TEST(Strrchr2);
    RUN_TEST(Strrchr3);

    ///////////////////////////////////////////////////////////

    printf("Final Results: %d out of %d succeeded\n", successes, total);
}
