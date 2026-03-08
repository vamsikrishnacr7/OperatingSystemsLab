#include "syscall.h"

int
main()
{   

    int i, j;

    for(i = 0; i<5; i++){
        char *msg = "33333\n";
        Write(msg, 6, 1);   // 1 = ConsoleOutput
    }
    Exit(0);
}

