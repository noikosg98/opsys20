#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"

/**
  @brief New start thread, for creating new thread in the current process
  */
void start_new_thread()
{
  int exitval;

  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/** 
  @brief Create a new thread in the current process. DONE
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  // Aquire current process global system variable
  PCB * curproc_pcb = CURPROC;
  // TCB main thread
  TCB* new_tcb = spawn_thread(curproc_pcb, start_new_thread);
  //Acquiring a new PTCB, by allocating in parallel mem for it
  PTCB* new_ptcb = (PTCB*)xmalloc(sizeof(PTCB));
  //Making connections with PCB, TCB
  new_ptcb->task = task;
  new_ptcb->argl = argl;
  new_ptcb->args = args;
  new_ptcb->exited = 0;
  new_ptcb->detached = 0;
  new_ptcb->exit_cv = COND_INIT;
  new_ptcb->refcount = 0;

  rlnode_init (&(new_ptcb->ptcb_list_node), new_ptcb);
  new_ptcb->tcb = new_tcb;
  new_ptcb->tcb->ptcb = new_ptcb;
  rlist_push_back(&(curproc_pcb->ptcb_list), &(new_ptcb->ptcb_list_node));
  curproc_pcb->thread_count += 1;
  wakeup(new_ptcb->tcb);
  
  return (Tid_t)new_ptcb;
}

/**
  @brief Return the Tid of the current thread. DONE
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* given_ptcb = (PTCB*)tid;

  if((given_ptcb->detached != 1) && (given_ptcb->tcb != NULL))  {
    given_ptcb->refcount += 1;
    
    // Loop for checking exited or detached condition
    while((given_ptcb->exited == 0) && (given_ptcb->detached == 0)){
      kernel_wait(&(given_ptcb->exit_cv), SCHED_USER);
    }

    given_ptcb->refcount -= 1;

    if (exitval != NULL)
      *exitval = given_ptcb->exitval;

    if (given_ptcb->refcount <= 0){
        rlist_remove(&(given_ptcb->ptcb_list_node));
        free(given_ptcb);
    }
    return 0;

  } else {
    if(given_ptcb-> exited != 1) {
      return -1;
    }else {
      return 0;
    }
  }
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* curptcb = (PTCB*) tid;

  if(curptcb->tcb != NULL && curptcb->tcb->state != EXITED) {
    curptcb->detached = 1;
    kernel_broadcast(&(curptcb->exit_cv));
    return 0;
  }

	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  PCB* curproc = CURPROC;  /* cache for efficiency */
  PTCB* ptcb = cur_thread()->ptcb;

  if(curproc->thread_count == 0) {
    /* Reparent any children of the exiting process to the 
       initial task */
    PCB* initpcb = get_pcb(1);
    while(!is_rlist_empty(& curproc->children_list)) {
      rlnode* child = rlist_pop_front(& curproc->children_list);
      child->pcb->parent = initpcb;
      rlist_push_front(& initpcb->children_list, child);
    }

    /* Add exited children to the initial task's exited list 
       and signal the initial task */
    if(!is_rlist_empty(& curproc->exited_list)) {
      rlist_append(& initpcb->exited_list, &curproc->exited_list);
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);


    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
      Do all the other cleanup we want here, close files etc. 
    */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
    curproc->exitval = exitval;
  } else {
    ptcb->exitval = exitval;
    ptcb->exited = 1;
    kernel_broadcast(&(ptcb->exit_cv));
    curproc->thread_count -= 1;
  }

  kernel_sleep(EXITED, SCHED_USER);
}

