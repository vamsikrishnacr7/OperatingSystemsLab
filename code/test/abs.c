/* add.c
 *	Simple program to test whether the systemcall interface works.
 *
 *
 */

#include "syscall.h"

int main() {
    int x = -18;

    int result = Abs(x);
    PrintNum(result);
//  Halt();
    /* not reached */
}
