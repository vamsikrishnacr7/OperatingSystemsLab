#include "syscall.h"

int
main()
{
    int pid1, pid2, pid3;

    pid1 = Exec("../test/childprog");
    pid2 = Exec("../test/pachild");
    pid3 = Exec("../test/up");

    Join(pid1);
    Join(pid2);
    Join(pid3);

    Exit(0);
}

