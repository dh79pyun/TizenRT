/****************************************************************************
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>
#include <errno.h>
#include <debug.h>
#include <unistd.h>
#include <time.h>
#include <tinyara/fs/fs.h>
#include <tinyara/testcase_drv.h>
#include <tinyara/sched.h>
#include "clock/clock.h"
#include "signal/signal.h"
#include "timer/timer.h"
#if (defined CONFIG_SCHED_HAVE_PARENT) && (defined CONFIG_SCHED_CHILD_STATUS)
#include "task/task.h"
#include "group/group.h"
#endif
/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int kernel_test_drv_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static ssize_t kernel_test_drv_read(FAR struct file *filep, FAR char *buffer, size_t len);
static ssize_t kernel_test_drv_write(FAR struct file *filep, FAR const char *buffer, size_t len);

#if (defined CONFIG_SCHED_HAVE_PARENT) && (defined CONFIG_SCHED_CHILD_STATUS)
static int group_exitchild_func(int argc, char *argv[])
{
	task_delete(0);
	return ERROR;
}
#endif
/****************************************************************************
 * Private Data
 ****************************************************************************/

#define TASK_STACKSIZE 2048

static const struct file_operations kernel_test_drv_fops = {
	0,                                                   /* open */
	0,                                                   /* close */
	kernel_test_drv_read,                                /* read */
	kernel_test_drv_write,                               /* write */
	0,                                                   /* seek */
	kernel_test_drv_ioctl                                /* ioctl */
#ifndef CONFIG_DISABLE_POLL
	, 0                                                  /* poll */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/************************************************************************************
 * Name: kernel_test_drv_ioctl
 *
 * Description:  The standard ioctl method.
 *
 ************************************************************************************/

static int kernel_test_drv_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
	int ret = -EINVAL;
	int ret_chk;
	sem_t sem;
	struct timespec cur_time;
	struct timespec base_time;
	struct tcb_s *tcb;
	FAR sigactq_t *sigact;
	/* Handle built-in ioctl commands */

	switch (cmd) {
	/* TESTFWIOC_DRIVER_ANALOG - Run the test case for /os/driver/analog module
	 *
	 *   ioctl argument:  An integer value indicating the particular test to be run
	 */

	case TESTIOC_ANALOG: {

	}
	break;

	case TESTIOC_GET_SELF_PID: {
		tcb =  sched_self();
		if (tcb == NULL) {
			ret = ERROR;
			break;
		}
		ret = tcb->pid;
	}
	break;

	case TESTIOC_GET_SIG_FINDACTION_ADD: {
		sigact = sig_findaction(sched_self(), (int)arg);
		ret = (int)sigact;
	}
	break;

	case TESTIOC_IS_ALIVE_THREAD: {
		tcb = sched_gettcb((pid_t)arg);
		if (tcb == NULL) {
			ret = ERROR;
			break;
		}
		ret = OK;
	}
	break;

	case TESTIOC_GET_TCB_SIGPROCMASK: {
		tcb = sched_gettcb((pid_t)arg);
		if (tcb == NULL) {
			ret = ERROR;
			break;
		}
		ret = tcb->sigprocmask;
	}
	break;

	case TESTIOC_GET_TCB_ADJ_STACK_SIZE: {
		tcb = sched_gettcb((pid_t)arg);
		if (tcb == NULL) {
			ret = ERROR;
			break;
		}
		ret = tcb->adj_stack_size;
	}
	break;

#ifdef CONFIG_TC_KERNEL_ROUNDROBIN
	case TESTIOC_GET_TCB_TIMESLICE: {
		tcb = sched_gettcb((pid_t)arg);
		if (tcb == NULL) {
			ret = ERROR;
			break;
		}
		ret = tcb->timeslice;
	}
	break;
#endif

	case TESTIOC_SCHED_FOREACH: {
		sched_foreach((void *)arg, NULL);
	}
	break;

	case TESTIOC_SIGNAL_PAUSE: {
		ret = pause();              /* pause() always return -1 */
		if (ret == ERROR && get_errno() == EINTR) {
			ret = OK;
		} else {
			ret = ERROR;
		}
	}
	break;

	case TESTIOC_CLOCK_ABSTIME2TICKS_TEST: {
		int base_tick;
		int comparison_tick;
		struct timespec comparison_time;
		struct timespec result_time;

		ret_chk = clock_gettime(CLOCK_REALTIME, &cur_time);
		if (ret_chk != OK) {
			dbg("clock_gettime failed. errno : %d\n", get_errno());
			ret = ERROR;
			break;
		}

		base_time.tv_sec = cur_time.tv_sec + 101;
		base_time.tv_nsec = cur_time.tv_nsec;

		comparison_time.tv_sec = cur_time.tv_sec + 102;
		comparison_time.tv_nsec = cur_time.tv_nsec;
		ret_chk = clock_abstime2ticks(CLOCK_REALTIME, &base_time, &base_tick);
		if (ret_chk == ERROR) {
			dbg("clock_abstime2ticks failed. ret : %d\n", ret_chk);
			ret = ERROR;
			break;
		}

		ret_chk = clock_abstime2ticks(CLOCK_REALTIME, &comparison_time, &comparison_tick);
		if (ret_chk != OK) {
			dbg("clock_abstime2ticks failed. ret : %d\n", ret_chk);
			ret = ERROR;
			break;
		}

		clock_ticks2time(comparison_tick - base_tick, &result_time);
		if (result_time.tv_sec != 1) {
			dbg("clock_abstime2ticks failed. %d.%ld sec is not 1 sec.\n", result_time.tv_sec, result_time.tv_nsec);
			ret = ERROR;
			break;
		}
		ret = OK;
	}
	break;

	case TESTIOC_TIMER_INITIALIZE_TEST: {
		timer_t timer_id;
		struct sigevent st_sigevent;
		FAR struct posix_timer_s *timer;
		FAR struct posix_timer_s *next;

		int initalloc_cnt = 0;
		int initfree_cnt = 0;
		int createalloc_cnt = 0;
		int createfree_cnt = 0;
		int finalalloc_cnt = 0;
		int finalfree_cnt = 0;

		/* Set and enable alarm */
		st_sigevent.sigev_notify = SIGEV_SIGNAL;
		st_sigevent.sigev_signo = SIGRTMIN;
		st_sigevent.sigev_value.sival_ptr = &timer_id;

		/* check the count for g_alloctimers and g_freetimers after timer_initialize */
		timer_initialize();

		for (timer = (FAR struct posix_timer_s *)g_alloctimers.head; timer; timer = next) {
			next = timer->flink;
			initalloc_cnt++;
		}

		for (timer = (FAR struct posix_timer_s *)g_freetimers.head; timer; timer = next) {
			next = timer->flink;
			initfree_cnt++;
		}

		/* check the count for g_alloctimers and g_freetimers after create now they change */
		ret_chk = timer_create(CLOCK_REALTIME, &st_sigevent, &timer_id);
		if (ret_chk == ERROR) {
			dbg("timer_create failed.");
			ret = ERROR;
			break;
		}

		if (timer_id == NULL) {
			dbg("timer_create failed.");
			ret = ERROR;
			break;
		}

		for (timer = (FAR struct posix_timer_s *)g_alloctimers.head; timer; timer = next) {
			next = timer->flink;
			createalloc_cnt++;
		}

		for (timer = (FAR struct posix_timer_s *)g_freetimers.head; timer; timer = next) {
			next = timer->flink;
			createfree_cnt++;
		}

		/* check the count for g_alloctimers and g_freetimers after timer_initialize now they change to original value */
		timer_initialize();

		for (timer = (FAR struct posix_timer_s *)g_alloctimers.head; timer; timer = next) {
			next = timer->flink;
			finalalloc_cnt++;
		}

		for (timer = (FAR struct posix_timer_s *)g_freetimers.head; timer; timer = next) {
			next = timer->flink;
			finalfree_cnt++;
		}

		ret_chk = timer_delete(timer_id);

		if (ret_chk == ERROR) {
			dbg("timer_delete failed.");
			ret = ERROR;
			break;
		}

		if (initalloc_cnt != finalalloc_cnt) {
			dbg("timer_initialise failed.");
			ret = ERROR;
			break;
		}

		if (initfree_cnt != finalfree_cnt) {
			dbg("timer_initialise failed.");
			ret = ERROR;
			break;
		}

		if (createalloc_cnt == finalalloc_cnt) {
			dbg("timer_initialise failed.");
			ret = ERROR;
			break;
		}

		if (createfree_cnt == finalfree_cnt) {
			dbg("timer_initialise failed.");
			ret = ERROR;
			break;
		}

		ret = OK;
	}
	break;

	case TESTIOC_SEM_TICK_WAIT_TEST: {
		/* init sem count to 1 */

		ret_chk = sem_init(&sem, 0, 1);
		if (ret_chk != OK) {
			dbg("sem_init failed.");
			ret = ERROR;
			break;
		}

		/* success to get sem case test */

		ret_chk = clock_gettime(CLOCK_REALTIME, &base_time);
		if (ret_chk != OK) {
			dbg("clock_gettime failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_tickwait(&sem, clock(), 2);
		if (ret_chk != OK) {
			dbg("sem_tickwait failed.");
			ret = ERROR;
			break;
		}

		ret_chk = clock_gettime(CLOCK_REALTIME, &cur_time);
		if (ret_chk != OK) {
			dbg("clock_gettime failed.");
			ret = ERROR;
			break;
		}
		if (base_time.tv_sec + 2 == cur_time.tv_sec) {
			dbg("sem_timedwait failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_post(&sem);
		if (ret_chk != OK) {
			dbg("sem_post failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_destroy(&sem);
		if (ret_chk != OK) {
			dbg("sem_destroy failed.");
			ret = ERROR;
			break;
		}

		/* init sem count to 0 */

		ret_chk = sem_init(&sem, 0, 0);
		if (ret_chk != OK) {
			dbg("sem_init failed.");
			ret = ERROR;
			break;
		}

		/* expired time test */

		ret_chk = sem_tickwait(&sem, clock() - 2, 0);
		if (ret_chk != ERROR) {
			dbg("sem_tickwait failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_tickwait(&sem, clock() - 2, 1);
		if (ret_chk != ERROR) {
			dbg("sem_tickwait failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_tickwait(&sem, clock() - 2, 3);
		if (ret_chk != ERROR) {
			dbg("sem_tickwait failed.");
			ret = ERROR;
			break;
		}

		ret_chk = sem_destroy(&sem);
		if (ret_chk != OK) {
			dbg("sem_destroy failed.");
			ret = ERROR;
			break;
		}
		ret = OK;
	}
	break;
#if (defined CONFIG_SCHED_HAVE_PARENT) && (defined CONFIG_SCHED_CHILD_STATUS)
	case TESTIOC_GROUP_ADD_FINED_REMOVE_TEST: {
		struct tcb_s *st_tcb;
		struct task_group_s *group;
		struct child_status_s *child;
		struct child_status_s *child_returned;
		pid_t child_pid;

		st_tcb = sched_self();
		if (st_tcb == NULL) {
			dbg("sched_self failed.");
			ret = ERROR;
			break;
		}

		group = st_tcb->group;
		if (group == NULL) {
			dbg("group is null.");
			ret = ERROR;
			break;
		}

		child = group_allocchild();
		if (child == NULL) {
			dbg("group_allocchild failed.");
			ret = ERROR;
			break;
		}

		child_pid = -1;
		child->ch_flags = TCB_FLAG_TTYPE_TASK;
		child->ch_pid = child_pid;
		child->ch_status = 0;
		/* Add the entry into the TCB list of children */
		group_addchild(group, child);

		/* cross check starts */
		child_returned = group_findchild(group, child_pid);
		if (child != child_returned) {
			dbg("group_findchild failed.");
			ret = ERROR;
			break;
		}

		child_returned = group_removechild(group, child_pid);
		if (child != child_returned) {
			dbg("group_removechild failed.");
			ret = ERROR;
			break;
		}

		child_returned = group_findchild(group, child_pid);
		if (child_returned != NULL) {
			dbg("group_removechild failed.");
			group_removechild(group, child_pid);
			group_freechild(child);
			ret = ERROR;
			break;
		}
		group_freechild(child);
		ret = OK;
	}
	break;
	case TESTIOC_GROUP_ALLOC_FREE_TEST: {
		struct tcb_s *st_tcb;
		struct task_group_s *group;
		struct child_status_s *child;
		struct child_status_s child_dummy;

		st_tcb = sched_self();
		if (st_tcb == NULL) {
			dbg("sched_self failed.");
			ret = ERROR;
			break;
		}

		group = st_tcb->group;
		if (group == NULL) {
			dbg("group is null.");
			ret = ERROR;
			break;
		}

		child = group_allocchild();
		if (child == NULL) {
			dbg("group_allocchild failed.");
			ret = ERROR;
			break;
		}
		if (child->flink != NULL) {
			dbg("group_allocchild failed.");
			ret = ERROR;
			break;
		}

		child->flink = &child_dummy;
		group_freechild(child);
		if (child->flink == &child_dummy) {
			dbg("group_freechild failed.");
			ret = ERROR;
			break;
		}
		ret = OK;
	}
	break;
	case TESTIOC_GROUP_EXIT_CHILD_TEST: {
		struct tcb_s *st_tcb;
		struct task_group_s *group;
		struct child_status_s *child;
		struct child_status_s *child_returned;
		pid_t child_pid;

		st_tcb = sched_self();
		if (st_tcb == NULL) {
			dbg("sched_self failed.");
			ret = ERROR;
			break;
		}

		group = st_tcb->group;
		if (group == NULL) {
			dbg("group is null.");
			ret = ERROR;
			break;
		}

		child_pid = kernel_thread("group", SCHED_PRIORITY_DEFAULT, TASK_STACKSIZE, group_exitchild_func, (char *const *)NULL);
		if (child_pid < 0) {
			dbg("task_create failed.");
			ret = ERROR;
			break;
		}

		child = group_findchild(group, child_pid);
		if (child == NULL) {
			dbg("child is null.");
			ret = ERROR;
			break;
		}

		sleep(3);

		child_returned = group_exitchild(group);
		if (child != child_returned) {
			dbg("group_exitchild failed.");
			ret = ERROR;
			break;
		}

		child_returned = group_removechild(group, child_pid);
		if (child != child_returned) {
			dbg("group_removechild failed.");
			ret = ERROR;
			break;
		}

		group_freechild(child);
		ret = OK;
	}
	break;
	case TESTIOC_GROUP_REMOVECHILDREN_TEST: {
		struct tcb_s *st_tcb;
		struct task_group_s *group;
		struct child_status_s *child;
		struct child_status_s *child_returned;
		pid_t child_pid;

		st_tcb = sched_self();
		if (st_tcb == NULL) {
			dbg("sched_self failed.");
			ret = ERROR;
			break;
		}

		group = st_tcb->group;
		if (group == NULL) {
			dbg("group is null.");
			ret = ERROR;
			break;
		}

		child = group_allocchild();
		if (child == NULL) {
			dbg("group_allocchild failed.");
			ret = ERROR;
			break;
		}

		child_pid = -1;
		child->ch_flags = TCB_FLAG_TTYPE_TASK;
		child->ch_pid = child_pid;
		child->ch_status = 0;
		/* Add the entry into the TCB list of children */
		group_addchild(group, child);

		/* cross check starts */
		child_returned = group_findchild(group, child_pid);
		if (child != child_returned) {
			dbg("group_findchild failed.");
			ret = ERROR;
			break;
		}

		group_removechildren(group);
		if (group->tg_children != NULL) {
			dbg("group_removechildren failed.");
			ret = ERROR;
			break;
		}
		ret = OK;
	}
	break;
#endif
	default: {
		vdbg("Unrecognized cmd: %d arg: %ld\n", cmd, arg);
	}
	break;

	}

	return ret;
}


static ssize_t kernel_test_drv_read(FAR struct file *filep, FAR char *buffer, size_t len)
{
	return 0;                                       /* Return EOF */
}

static ssize_t kernel_test_drv_write(FAR struct file *filep, FAR const char *buffer, size_t len)
{
	return len;                                     /* Say that everything was written */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: kernel_test_drv_register
 *
 * Description:
 *   Register /dev/testcase
 *
 ****************************************************************************/

void kernel_test_drv_register(void)
{
	(void)register_driver(KERNEL_TC_DRVPATH, &kernel_test_drv_fops, 0666, NULL);
}
