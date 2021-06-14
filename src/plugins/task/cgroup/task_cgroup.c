/*****************************************************************************\
 *  task_cgroup.c - Library for task pre-launch and post_termination functions
 *		    for containment using linux cgroup subsystems
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <signal.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "task_cgroup.h"
#include "task_cgroup_cpuset.h"
#include "task_cgroup_memory.h"
#include "task_cgroup_devices.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "Tasks containment cgroup plugin";
const char plugin_type[]        = "task/cgroup";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static bool use_cpuset  = false;
static bool use_memory  = false;
static bool use_devices = false;
static bool do_task_affinity = false;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	int rc = SLURM_SUCCESS;
	cgroup_conf_t *cg_conf;

	if (!running_in_slurmstepd())
		goto end;

	/* read cgroup configuration */
	cg_conf = cgroup_get_conf();

	/* enable subsystems based on conf */
	if (cg_conf->constrain_cores)
		use_cpuset = true;
	if (cg_conf->constrain_ram_space ||
	    cg_conf->constrain_swap_space)
		use_memory = true;
	if (cg_conf->constrain_devices)
		use_devices = true;
	if (cg_conf->task_affinity)
		do_task_affinity = true;

	cgroup_free_conf(cg_conf);

	/* enable subsystems based on conf */
	if (use_cpuset) {
		if ((rc = task_cgroup_cpuset_init())) {
			error("failure enabling core enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else {
			debug("core enforcement enabled");
		}
	}

	if (use_memory) {
		if ((rc = task_cgroup_memory_init())) {
			error("failure enabling memory enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else {
			debug("memory enforcement enabled");
		}
	}

	if (use_devices) {
		if ((rc = task_cgroup_devices_init())) {
			error("failure enabling device enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else {
			debug("device enforcement enabled");
		}
	}
end:
	debug("%s loaded", plugin_name);
	return rc;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{
	int rc[3] = {0};

	if (use_cpuset) {
		rc[0] = task_cgroup_cpuset_fini();
	}

	if (use_memory) {
		rc[1] = task_cgroup_memory_fini();
	}

	if (use_devices) {
		rc[2] = task_cgroup_devices_fini();
	}

	debug("%s unloaded", plugin_name);
	return MAX(rc[0], MAX(rc[1], rc[2]));
}

/*
 * task_p_slurmd_batch_request()
 */
extern int task_p_slurmd_batch_request (batch_job_launch_msg_t *req)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_launch_request()
 */
extern int task_p_slurmd_launch_request (launch_tasks_request_msg_t *req,
					 uint32_t node_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_suspend_job()
 */
extern int task_p_slurmd_suspend_job (uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_resume_job()
 */
extern int task_p_slurmd_resume_job (uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_p_pre_setuid (stepd_step_rec_t *job)
{
	int rc[3] = {0};

	if (use_cpuset) {
		/* we create the cpuset container as we are still root */
		rc[0] = task_cgroup_cpuset_create(job);
	}

	if (use_memory) {
		/* we create the memory container as we are still root */
		rc[1] = task_cgroup_memory_create(job);
	}

	if (use_devices) {
		rc[2] = task_cgroup_devices_create(job);
		/* here we should create the devices container as we are root */
	}

	return MAX(rc[0], MAX(rc[1], rc[2]));
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv(stepd_step_rec_t *job, pid_t pid)
{
	int rc[3] = {0};

	if (use_cpuset) {
		/* attach the task to the cpuset cgroup */
		rc[0] = task_cgroup_cpuset_attach_task(job);
	}

	if (use_memory) {
		/* attach the task to the memory cgroup */
		rc[1] = task_cgroup_memory_attach_task(job, pid);
	}

	if (use_devices) {
		/* attach the task to the devices cgroup */
		rc[2] = task_cgroup_devices_attach_task(job);
	}

	return MAX(rc[0], MAX(rc[1], rc[2]));
}

/*
 * task_p_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_p_pre_launch (stepd_step_rec_t *job)
{
	if (use_cpuset && do_task_affinity)
		return task_cgroup_cpuset_set_task_affinity(job);

	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
	static bool ran = false;
	int rc = SLURM_SUCCESS;

	/* Only run this on the first call since this will run for
	 * every task on the node.
	 */
	if (use_memory && !ran) {
		rc = task_cgroup_memory_check_oom(job);
		ran = true;
	}
	return rc;
}

/*
 * task_p_post_step() is called after termination of the step
 * (all the task)
 */
extern int task_p_post_step (stepd_step_rec_t *job)
{
	return fini();
}

/*
 * Add pid to specific cgroup.
 */
extern int task_p_add_pid (pid_t pid)
{
	int rc[3] = {0};

	if (use_cpuset) {
		rc[0] = task_cgroup_cpuset_add_pid(pid);
	}

	if (use_memory) {
		rc[1] = task_cgroup_memory_add_pid(pid);
	}

	if (use_devices) {
		rc[2] = task_cgroup_devices_add_pid(pid);
	}

	return MAX(rc[0], MAX(rc[1], rc[2]));
}
