// scheduler.cc
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would
//	end up calling FindNextToRun(), and that would put us in an
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

// callback for waking up blocked or sleeping threads

class WakeUpCallback : public CallBackObj {
    public:
        void CallBack() {
            kernel->scheduler->checkBlockedList(kernel->stats->totalTicks, false);
        }
};

// Defining the implementation of the class SleepingThread

SleepingThread::SleepingThread(Thread* thread, int time){
    sleptThread = thread;
    whenToWake = time;
}


int ThreadPriorityCompare(Thread *t1, Thread *t2){
    if(t1->getPriority() > t2->getPriority())
        return -1;
    else if(t1->getPriority() < t2->getPriority())
        return 1;
    else return 0;
}

Scheduler::Scheduler() {
    readyList = new SortedList<Thread *>(ThreadPriorityCompare);
    blockedList = new List<SleepingThread*>;
    toBeDestroyed = NULL;
}

// Scheduler::pushIntoBlockedList
// Adds a new thread into the blockedList indicating they are sleeping

void Scheduler::pushIntoBlockedList(Thread* thread, int time){
    if(thread == NULL){
        cout << "Thread passed for pushing into blockedList is NULL"<<endl;
        ASSERT(thread != NULL);
    }

    thread->setStatus(BLOCKED);
    IntStatus oldLevel = kernel->interrupt->SetLevel(IntOff);

    SleepingThread* goingToSleep = new SleepingThread(thread, time);
    blockedList->Append(goingToSleep);

    WakeUpCallback* wcb = new WakeUpCallback();

    kernel->interrupt->Schedule(wcb, time, TimerInt);
    Thread* nextThread;
    while((nextThread = this->FindNextToRun()) == NULL){
        // this->checkBlockedList(kernel->stats->totalTicks, true);
        kernel->interrupt->Idle();
    }
    this->Run(nextThread, FALSE);
    readyList->Remove(thread);
    kernel->interrupt->SetLevel(oldLevel);
}
// Scheduler::checkBlockedList
//Iterates through the blocked list and releases those threads from BLOCKED state to READY state

bool Scheduler::checkBlockedList(int currentTime, bool advanceTime){
    if(advanceTime){
        kernel->stats->totalTicks += 1;
        //cout<<"Advancing the clock"<<endl;
    }
    //cout<<"calling check inside blocked list"<<endl;
    ListElement<SleepingThread*> *ptr;
    bool someThreadWoken = false;

    for(ptr = blockedList->first; ptr != NULL; ptr = ptr->next){
        cout<<"Thread to wake up at:"<<ptr->item->whenToWake<<"while current is at:"<<kernel->stats->totalTicks<<endl;
        volatile int currentTicks = kernel->stats->totalTicks;

        if(ptr->item->whenToWake <= currentTicks){
            SleepingThread* toBeWoken = ptr->item;
            blockedList->Remove(toBeWoken);
            readyList->Insert(toBeWoken->sleptThread);
            toBeWoken->sleptThread->setStatus(READY);
            //cout<<"Woken up a thread with name: << toBeWoken->sleptThread->getName()<<endl;
            someThreadWoken = true;
        }
            
    }

    return someThreadWoken;
}

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler() { delete readyList; }

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void Scheduler::ReadyToRun(Thread *thread) {
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());

    thread->setStatus(READY);
    readyList->Append(thread);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *Scheduler::FindNextToRun() {
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (readyList->IsEmpty()) {
        return NULL;
    } else {
        return readyList->RemoveFront();
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void Scheduler::Run(Thread *nextThread, bool finishing) {
    Thread *oldThread = kernel->currentThread;

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {  // mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL) {  // if this thread is a user program,
        oldThread->SaveUserState();  // save the user's CPU registers
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow();  // check if the old thread
                                 // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running

    DEBUG(dbgThread, "Switching from: " << oldThread->getName()
                                        << " to: " << nextThread->getName());

    // This is a machine-dependent assembly language routine defined
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread

    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();  // check if thread we were running
                           // before this one has finished
                           // and needs to be cleaned up

    if (oldThread->space != NULL) {     // if there is an address space
        oldThread->RestoreUserState();  // to restore, do it.
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void Scheduler::CheckToBeDestroyed() {
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void Scheduler::Print() {
    cout << "Ready list contents:\n";
    readyList->Apply(ThreadPrint);
}
