/*
Copyright (C) 2018-2019 Tom Schoonjans and Laszlo Vincze

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "xmi_batch.h"
#include "xmi_aux.h"
#include <gio/gio.h>

struct _XmiMsimBatchAbstractPrivate {
	XmiMsimJob *active_job;
	GMutex batch_mutex;
	gboolean running;
	gboolean paused;
	gboolean finished;
	gboolean successful;
	gboolean do_not_emit;
	gboolean killed;
	gboolean send_all_stdout_events;
	gchar *executable;
	gchar **extra_options;
	gchar *last_finished_message;
	GMainLoop *main_loop;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(XmiMsimBatchAbstract, xmi_msim_batch_abstract, G_TYPE_OBJECT)

static XmiMsimJob* xmi_msim_batch_abstract_real_get_job(XmiMsimBatchAbstract *batch, guint job_index, GError **error);

static guint xmi_msim_batch_abstract_real_get_number_of_jobs(XmiMsimBatchAbstract *batch);

static void xmi_msim_batch_abstract_dispose(GObject *object);

static void xmi_msim_batch_abstract_finalize(GObject *object);

static void xmi_msim_batch_abstract_set_property(GObject          *object,
                                                 guint             prop_id,
                                                 const GValue     *value,
                                                 GParamSpec       *pspec);

static void xmi_msim_batch_abstract_get_property(GObject          *object,
                                                 guint             prop_id,
                                                 GValue     *value,
                                                 GParamSpec       *pspec);

enum {
	STDOUT_EVENT,
	STDERR_EVENT,
	FINISHED_EVENT,
	//SPECIAL_EVENT,
	PROGRESS_EVENT,
	LAST_SIGNAL
};

enum {
	PROP_ABSTRACT_0,
	PROP_ABSTRACT_ACTIVE_JOB,
	PROP_ABSTRACT_EXECUTABLE,
	PROP_ABSTRACT_EXTRA_OPTIONS,
	N_ABSTRACT_PROPERTIES
};

static guint signals[LAST_SIGNAL];
static GParamSpec *abstract_props[N_ABSTRACT_PROPERTIES] = {NULL, };

static void xmi_msim_batch_abstract_class_init(XmiMsimBatchAbstractClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = xmi_msim_batch_abstract_dispose;
	object_class->finalize = xmi_msim_batch_abstract_finalize;
	object_class->set_property = xmi_msim_batch_abstract_set_property;
	object_class->get_property = xmi_msim_batch_abstract_get_property;

	klass->get_job = xmi_msim_batch_abstract_real_get_job;
	klass->get_number_of_jobs = xmi_msim_batch_abstract_real_get_number_of_jobs;
	
	/**
	 * XmiMsimBatchAbstract::stdout-event:
	 * @batch: The #XmiMsimBatch object emitting the signal
	 * @message: The %stdout line produced by the %xmimsim executable
	 *
	 * Emitted whenever a regular %stdout message was produced by the %xmimsim executable.
	 * Special messages can also be emitted, if #xmi_msim_batch_abstract_send_all_stdout_events() is set to %TRUE
	 */
	signals[STDOUT_EVENT] = g_signal_new(
		"stdout-event",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, // no default handler
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING // gchar*
	);

	/**
	 * XmiMsimBatchAbstract::stderr-event:
	 * @batch: The #XmiMsimBatchAbstract object emitting the signal
	 * @message: The %stderr line produced by the %xmimsim executable
	 *
	 * Emitted whenever a %stderr message was produced by the %xmimsim executable.
	 */
	signals[STDERR_EVENT] = g_signal_new(
		"stderr-event",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, // no default handler
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING // gchar*
	);

	/**
	 * XmiMsimBatchAbstract::finished-event:
	 * @batch: The #XmiMsimBatchAbstract object emitting the signal
	 * @status: The exit status of the finished job. %TRUE if successful, %FALSE otherwise
	 * @message: An appropriate message marking that the job is now finished.
	 *
	 * Emitted when the #XmiMsimBatchAbstract has finished.
	 */
	signals[FINISHED_EVENT] = g_signal_new(
		"finished-event",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, // no default handler
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		2,
		G_TYPE_BOOLEAN, // gboolean 
		G_TYPE_STRING // gchar*
	);

	/*
	 * XmiMsimJob::special-event:
	 * @job: The #XmiMsimJob object emitting the signal
	 * @event_type: The type of special event that occurred.
	 * @message: A message containing a description of the event.
	 *
	 * Emitted whenever a 'special' event has occurred.
	 */
	/*
	signals[SPECIAL_EVENT] = g_signal_new(
		"special-event",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, // no default handler
		NULL,
		NULL,
		xmi_VOID__ENUM_STRING,
		G_TYPE_NONE,
		2,
		XMI_MSIM_TYPE_JOB_SPECIAL_EVENT, // XmiMsimJobSpecialEvent
		G_TYPE_STRING // gchar*
	);*/

	/**
	 * XmiMsimBatchAbstract::progress-event:
	 * @batch: The #XmiMsimBatchAbstract object emitting the signal
	 * @fraction: the progress fraction that has been reached when this event was emitted
	 *
	 * Emitted whenever a job has been completed.
	 */
	signals[PROGRESS_EVENT] = g_signal_new(
		"progress-event",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		0, // no default handler
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		G_TYPE_DOUBLE // double (between 0 and 1)
	);

	abstract_props[PROP_ABSTRACT_ACTIVE_JOB] = g_param_spec_object(
		"active-job",
		"Active job",
		"Active job",
		XMI_MSIM_TYPE_JOB,
    		G_PARAM_READABLE
	);

	abstract_props[PROP_ABSTRACT_EXECUTABLE] = g_param_spec_string(
		"executable",
		"XMI-MSIM main executable",
		"Full path to the XMI-MSIM main executable",
		NULL,
    		G_PARAM_READWRITE
	);

	abstract_props[PROP_ABSTRACT_EXTRA_OPTIONS] = g_param_spec_boxed(
		"extra-options",
		"XMI-MSIM extra options",
		"Extra options XMI-MSIM main executable",
		G_TYPE_STRV,
    		G_PARAM_READWRITE
	);

	g_object_class_install_properties(object_class, N_ABSTRACT_PROPERTIES, abstract_props);
}

static void xmi_msim_batch_abstract_init(XmiMsimBatchAbstract *self) {
	self->priv = xmi_msim_batch_abstract_get_instance_private(self);
	g_mutex_init(&self->priv->batch_mutex);
	self->priv->send_all_stdout_events = TRUE;
}

static void xmi_msim_batch_abstract_dispose(GObject *object) {
	XmiMsimBatchAbstract *batch = XMI_MSIM_BATCH_ABSTRACT(object);

	G_OBJECT_CLASS(xmi_msim_batch_abstract_parent_class)->dispose(object);
}

static void xmi_msim_batch_abstract_finalize(GObject *object) {
	XmiMsimBatchAbstract *batch = XMI_MSIM_BATCH_ABSTRACT(object);

	g_mutex_lock(&batch->priv->batch_mutex);
	if (batch->priv->active_job)
		g_clear_object(&batch->priv->active_job);
	g_mutex_unlock(&batch->priv->batch_mutex);

	g_mutex_clear(&batch->priv->batch_mutex);

	g_free(batch->priv->executable);
	g_strfreev(batch->priv->extra_options);
	g_free(batch->priv->last_finished_message);
	if (batch->priv->main_loop)
		g_main_loop_unref(batch->priv->main_loop);

	G_OBJECT_CLASS(xmi_msim_batch_abstract_parent_class)->finalize(object);
}

static XmiMsimJob* xmi_msim_batch_abstract_real_get_job(XmiMsimBatchAbstract *batch, guint job_index, GError **error) {
	g_warning("XmiMsimBatchAbstract::get_job not implemented for '%s'", g_type_name(G_TYPE_FROM_INSTANCE(batch)));
	g_set_error(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_METHOD_UNDEFINED, "XmiMsimBatchAbstract::get_job not implemented for '%s'", g_type_name(G_TYPE_FROM_INSTANCE(batch)));

	return NULL;
}

static guint xmi_msim_batch_abstract_real_get_number_of_jobs(XmiMsimBatchAbstract *batch) {
	g_warning("XmiMsimBatchAbstract::get_number_of_jobs not implemented for '%s'", g_type_name(G_TYPE_FROM_INSTANCE(batch)));

	return 0;
}

static void xmi_msim_batch_abstract_set_property(GObject *object, guint prop_id, const GValue *value,  GParamSpec *pspec) {

	XmiMsimBatchAbstract *batch = XMI_MSIM_BATCH_ABSTRACT(object);

	switch (prop_id) {
		case PROP_ABSTRACT_EXECUTABLE:
			if (batch->priv->running || batch->priv->finished) {
				g_critical("cannot set executable after batch has started");
				break;
			}
			g_free(batch->priv->executable);
 			batch->priv->executable = g_value_dup_string(value);
			break;
    		case PROP_ABSTRACT_EXTRA_OPTIONS:
			if (batch->priv->running || batch->priv->finished) {
				g_critical("cannot set extra_options after batch has started");
				break;
			}
			g_strfreev(batch->priv->extra_options);
 			batch->priv->extra_options = g_value_dup_boxed(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void xmi_msim_batch_abstract_get_property(GObject *object, guint prop_id, GValue *value,  GParamSpec *pspec) {

	XmiMsimBatchAbstract *batch = XMI_MSIM_BATCH_ABSTRACT(object);

	switch (prop_id) {
		case PROP_ABSTRACT_ACTIVE_JOB:
			g_value_set_object(value, batch->priv->active_job);
			break;
		case PROP_ABSTRACT_EXECUTABLE:
			g_value_set_string(value, batch->priv->executable);
			break;
		case PROP_ABSTRACT_EXTRA_OPTIONS:
			g_value_set_boxed(value, batch->priv->extra_options);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void batch_finished_cb(XmiMsimBatchAbstract *batch, GAsyncResult *result, gpointer data) {
	gchar *message = g_task_propagate_pointer(G_TASK(result), NULL);
	g_mutex_lock(&batch->priv->batch_mutex);
	if (!batch->priv->do_not_emit) /* necessary to avoid blunt kills from emitting useless signals  */
		g_signal_emit(batch, signals[FINISHED_EVENT], 0, batch->priv->successful, message);
	g_free(message);
	g_mutex_unlock(&batch->priv->batch_mutex);
}

static void batch_process_stdout_event(XmiMsimBatchAbstract *batch, gchar *buffer, XmiMsimJob *job) {
	gchar *buffer_copy = g_strdup(buffer);
	g_signal_emit(batch, signals[STDOUT_EVENT], 0, buffer_copy);
	g_free(buffer_copy);
}

static void batch_process_stderr_event(XmiMsimBatchAbstract *batch, gchar *buffer, XmiMsimJob *job) {
	gchar *buffer_copy = g_strdup(buffer);
	g_signal_emit(batch, signals[STDERR_EVENT], 0, buffer_copy);
	g_free(buffer_copy);
}

static void batch_process_finished_event(XmiMsimJob *job, gboolean success, gchar *message, XmiMsimBatchAbstract *batch) {
	g_free(batch->priv->last_finished_message);
	batch->priv->last_finished_message = g_strdup(message);
	g_main_loop_quit(batch->priv->main_loop);
}

static void batch_thread(GTask *task, XmiMsimBatchAbstract *batch, gpointer task_data, GCancellable *cancellable) {
	GMainContext *main_context = g_main_context_get_thread_default();

	const guint n_jobs = XMI_MSIM_BATCH_ABSTRACT_GET_CLASS(batch)->get_number_of_jobs(batch);

	if (n_jobs == 0) {
		batch->priv->finished = TRUE;
		g_task_return_pointer(task, g_strdup("Batch could not be started as number of jobs is 0"), g_free);
		return;
	}

	guint job_i;

	batch->priv->running = TRUE;

	for (job_i = 0 ; job_i < n_jobs ; job_i++) {
		GError *error = NULL;
		XmiMsimJob *job = XMI_MSIM_BATCH_ABSTRACT_GET_CLASS(batch)->get_job(batch, job_i, &error);
		xmi_msim_job_send_all_stdout_events(job, batch->priv->send_all_stdout_events);

		if (error != NULL) {
			batch->priv->running = FALSE;
			batch->priv->finished = TRUE;
			g_task_return_pointer(task, g_strdup(error->message), g_free);
			g_clear_error(&error);
			return;
		}
		
		if (batch->priv->main_loop)
			g_main_loop_unref(batch->priv->main_loop);
		batch->priv->main_loop = g_main_loop_new(main_context, FALSE);

		// hook up all signals
		gulong sig1 = g_signal_connect_swapped(job, "stdout-event", G_CALLBACK(batch_process_stdout_event), batch);
		gulong sig2 = g_signal_connect_swapped(job, "stderr-event", G_CALLBACK(batch_process_stderr_event), batch);
		gulong sig3 = g_signal_connect(job, "finished-event", G_CALLBACK(batch_process_finished_event), batch);
		
		// start job
		xmi_msim_job_start(job, &error);

		if (error != NULL) {
			g_signal_handler_disconnect(job, sig1);
			g_signal_handler_disconnect(job, sig2);
			g_signal_handler_disconnect(job, sig3);
			batch->priv->running = FALSE;
			batch->priv->finished = TRUE;
			g_object_unref(job);
			g_task_return_pointer(task, g_strdup(error->message), g_free);
			g_clear_error(&error);
			return;
		}

		// make this the active job -> mutex??
		batch->priv->active_job = job;
		g_object_notify_by_pspec(G_OBJECT(batch), abstract_props[PROP_ABSTRACT_ACTIVE_JOB]);

		// start blocking
		g_main_loop_run(batch->priv->main_loop);

		// handler_disconnect signal handlers
		g_signal_handler_disconnect(job, sig1);
		g_signal_handler_disconnect(job, sig2);
		g_signal_handler_disconnect(job, sig3);

		// check job status!
		if (!xmi_msim_job_was_successful(job)) {
			batch->priv->running = FALSE;
			batch->priv->paused = FALSE;
			batch->priv->finished = TRUE;
			g_clear_object(&batch->priv->active_job);
			g_object_notify_by_pspec(G_OBJECT(batch), abstract_props[PROP_ABSTRACT_ACTIVE_JOB]);
			if (batch->priv->last_finished_message)
				g_task_return_pointer(task, g_strdup_printf("Job finished with error %s", batch->priv->last_finished_message), g_free);
			else
				g_task_return_pointer(task, g_strdup("Job finished with an unknown error"), g_free);
			return;
		}
		double progress = (double) job_i / n_jobs;
		g_signal_emit(batch, signals[PROGRESS_EVENT], 0, progress);
		g_clear_object(&batch->priv->active_job);
	}

	// we are done!
	g_object_notify_by_pspec(G_OBJECT(batch), abstract_props[PROP_ABSTRACT_ACTIVE_JOB]);
	batch->priv->running = FALSE;
	batch->priv->finished = TRUE;
	batch->priv->successful = TRUE;
	g_task_return_pointer(task, g_strdup("Batch has been terminated successfully"), g_free);
}

gboolean xmi_msim_batch_abstract_start(XmiMsimBatchAbstract *batch, GError **error) {
	if (!XMI_MSIM_IS_BATCH_ABSTRACT(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchAbstract");
		return FALSE;
	}

	g_mutex_lock(&batch->priv->batch_mutex);

	if (batch->priv->finished) {
		// do not allow old finished batch jobs to be restarted!
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "this batch has finished already");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;
	}

	GTask *task = g_task_new(batch, NULL, (GAsyncReadyCallback) batch_finished_cb, NULL);
	g_task_run_in_thread(task, (GTaskThreadFunc) batch_thread);
	g_object_unref(task);

	g_mutex_unlock(&batch->priv->batch_mutex);
	return TRUE;
}

gboolean xmi_msim_batch_abstract_stop(XmiMsimBatchAbstract *batch, GError **error) {
	if (!XMI_MSIM_IS_BATCH_ABSTRACT(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchAbstract");
		return FALSE;
	}

	g_mutex_lock(&batch->priv->batch_mutex);
	if (!batch->priv->running) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "batch is not running");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;	
	
	}
	if (batch->priv->killed) {
		g_mutex_unlock(&batch->priv->batch_mutex);
		return TRUE;
	}
	XmiMsimJob *active_job = g_object_ref(batch->priv->active_job);
	batch->priv->killed = TRUE;
	gboolean rv = xmi_msim_job_stop(active_job, error);
	g_object_unref(active_job);
	g_mutex_unlock(&batch->priv->batch_mutex);
	return rv;
}

gboolean xmi_msim_batch_abstract_kill(XmiMsimBatchAbstract *batch, GError **error) {
	if (!XMI_MSIM_IS_BATCH_ABSTRACT(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchAbstract");
		return FALSE;
	}
	batch->priv->do_not_emit = TRUE;
	return xmi_msim_batch_abstract_stop(batch, error);
}

gboolean xmi_msim_batch_abstract_suspend(XmiMsimBatchAbstract *batch, GError **error) {
	if (!XMI_MSIM_IS_BATCH_ABSTRACT(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchAbstract");
		return FALSE;
	}

	g_mutex_lock(&batch->priv->batch_mutex);
	if (!batch->priv->running) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "batch is not running");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;	
	
	}
	if (batch->priv->paused) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "batch is already suspended");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;	
	
	}
	XmiMsimJob *active_job = g_object_ref(batch->priv->active_job);
	batch->priv->paused = xmi_msim_job_suspend(active_job, error);
	g_object_unref(active_job);
	g_mutex_unlock(&batch->priv->batch_mutex);
	return batch->priv->paused;
}

gboolean xmi_msim_batch_abstract_resume(XmiMsimBatchAbstract *batch, GError **error) {
	if (!XMI_MSIM_IS_BATCH_ABSTRACT(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchAbstract");
		return FALSE;
	}

	g_mutex_lock(&batch->priv->batch_mutex);
	if (!batch->priv->running) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "batch is not running");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;	
	
	}
	if (!batch->priv->paused) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_UNAVAILABLE, "batch is not suspended");
		g_mutex_unlock(&batch->priv->batch_mutex);
		return FALSE;	
	
	}
	XmiMsimJob *active_job = g_object_ref(batch->priv->active_job);
	batch->priv->paused = !xmi_msim_job_resume(active_job, error);
	g_object_unref(active_job);
	g_mutex_unlock(&batch->priv->batch_mutex);
	return !batch->priv->paused;
}

gboolean xmi_msim_batch_abstract_is_running(XmiMsimBatchAbstract *batch) {
	g_return_val_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch), FALSE);

	g_mutex_lock(&batch->priv->batch_mutex);
	gboolean rv = batch->priv->running;
	g_mutex_unlock(&batch->priv->batch_mutex);
	return rv;
}
	
gboolean xmi_msim_batch_abstract_is_suspended(XmiMsimBatchAbstract *batch) {
	g_return_val_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch), FALSE);

	g_mutex_lock(&batch->priv->batch_mutex);
	gboolean rv = batch->priv->paused;
	g_mutex_unlock(&batch->priv->batch_mutex);
	return rv;
}

gboolean xmi_msim_batch_abstract_has_finished(XmiMsimBatchAbstract *batch) {
	g_return_val_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch), FALSE);

	g_mutex_lock(&batch->priv->batch_mutex);
	gboolean rv = batch->priv->finished;
	g_mutex_unlock(&batch->priv->batch_mutex);
	return rv;
}

gboolean xmi_msim_batch_abstract_was_successful(XmiMsimBatchAbstract *batch) {
	g_return_val_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch), FALSE);

	g_mutex_lock(&batch->priv->batch_mutex);
	gboolean rv = batch->priv->successful;
	g_mutex_unlock(&batch->priv->batch_mutex);
	return rv;
}

void xmi_msim_batch_abstract_send_all_stdout_events(XmiMsimBatchAbstract *batch, gboolean setting) {
	g_return_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch));
	g_return_if_fail(!batch->priv->running && !batch->priv->finished);

	batch->priv->send_all_stdout_events = setting;
}

void xmi_msim_batch_abstract_set_executable(XmiMsimBatchAbstract *batch, const gchar *executable) {
	g_return_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch));

	g_object_set(batch, "executable", executable, NULL);
}

void xmi_msim_batch_abstract_set_extra_options(XmiMsimBatchAbstract *batch, gchar **extra_options) {
	g_return_if_fail(XMI_MSIM_IS_BATCH_ABSTRACT(batch));

	g_object_set(batch, "extra-options", extra_options, NULL);
}

GQuark xmi_msim_batch_error_quark(void) {
	return g_quark_from_static_string("xmi-msim-batch-error-quark");
}

struct _XmiMsimBatchMulti {
	XmiMsimBatchAbstract parent_instance;
	GPtrArray *xmsi_files;
	GPtrArray *options;
};

struct _XmiMsimBatchMultiClass {
	XmiMsimBatchAbstractClass parent_class;
};

G_DEFINE_TYPE(XmiMsimBatchMulti, xmi_msim_batch_multi, XMI_MSIM_TYPE_BATCH_ABSTRACT)

static XmiMsimJob* xmi_msim_batch_multi_real_get_job(XmiMsimBatchAbstract *batch, guint job_index, GError **error);

static guint xmi_msim_batch_multi_real_get_number_of_jobs(XmiMsimBatchAbstract *batch);

static void xmi_msim_batch_multi_dispose(GObject *object);

static void xmi_msim_batch_multi_finalize(GObject *object);

static void xmi_msim_batch_multi_set_property(GObject          *object,
                                              guint             prop_id,
                                              const GValue     *value,
                                              GParamSpec       *pspec);

static void xmi_msim_batch_multi_get_property(GObject          *object,
                                              guint             prop_id,
                                              GValue     *value,
                                              GParamSpec       *pspec);

enum {
	PROP_MULTI_0,
	PROP_MULTI_XMSI_FILES,
	PROP_MULTI_OPTIONS,
	N_MULTI_PROPERTIES
};

static GParamSpec *multi_props[N_MULTI_PROPERTIES] = {NULL, };

static void xmi_msim_batch_multi_class_init(XmiMsimBatchMultiClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	XmiMsimBatchAbstractClass *abstract_class = XMI_MSIM_BATCH_ABSTRACT_CLASS(klass);

	object_class->dispose = xmi_msim_batch_multi_dispose;
	object_class->finalize = xmi_msim_batch_multi_finalize;
	object_class->set_property = xmi_msim_batch_multi_set_property;
	object_class->get_property = xmi_msim_batch_multi_get_property;

	abstract_class->get_job = xmi_msim_batch_multi_real_get_job;
	abstract_class->get_number_of_jobs = xmi_msim_batch_multi_real_get_number_of_jobs;
	

	multi_props[PROP_MULTI_XMSI_FILES] = g_param_spec_boxed(
		"xmsi-files",
		"XMSI input-files",
		"XMSI input-files",
		G_TYPE_PTR_ARRAY,
    		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
	);

	multi_props[PROP_MULTI_OPTIONS] = g_param_spec_boxed(
		"options",
		"Job Main Options",
		"Job Main Options",
		G_TYPE_PTR_ARRAY,
    		G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
	);

	g_object_class_install_properties(object_class, N_MULTI_PROPERTIES, multi_props);
}

static void xmi_msim_batch_multi_init(XmiMsimBatchMulti *self) {
}

static void xmi_msim_batch_multi_dispose(GObject *object) {
	XmiMsimBatchMulti *batch = XMI_MSIM_BATCH_MULTI(object);

	G_OBJECT_CLASS(xmi_msim_batch_multi_parent_class)->dispose(object);
}

static void xmi_msim_batch_multi_finalize(GObject *object) {
	XmiMsimBatchMulti *batch = XMI_MSIM_BATCH_MULTI(object);

	if (batch->xmsi_files)
		g_ptr_array_unref(batch->xmsi_files);
	if (batch->options)
		g_ptr_array_unref(batch->options);

	G_OBJECT_CLASS(xmi_msim_batch_multi_parent_class)->finalize(object);
}

static XmiMsimJob* xmi_msim_batch_multi_real_get_job(XmiMsimBatchAbstract *batch, guint job_index, GError **error) {
	if (!XMI_MSIM_IS_BATCH_MULTI(batch)) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "batch must be an instance of XmiMsimBatchMulti");
		return NULL;
	}

	XmiMsimBatchMulti *multi = XMI_MSIM_BATCH_MULTI(batch);
	if (job_index >= multi->xmsi_files->len) {
		g_set_error_literal(error, XMI_MSIM_BATCH_ERROR, XMI_MSIM_BATCH_ERROR_INVALID_INPUT, "job_index out of range");
		return NULL;
	}

	gchar *xmsi_file = g_ptr_array_index(multi->xmsi_files, job_index);
	xmi_main_options *options = multi->options->len == 1 ? g_ptr_array_index(multi->options, 0) : g_ptr_array_index(multi->options, job_index);

	gchar *executable = batch->priv->executable != NULL ? g_strdup(batch->priv->executable) : xmi_get_xmimsim_path();
	gchar **extra_options = batch->priv->extra_options != NULL ? g_strdupv(batch->priv->extra_options) : NULL;

	XmiMsimJob *job = xmi_msim_job_new(executable, xmsi_file, options, NULL, NULL, NULL, NULL, extra_options, error);
	g_free(executable);
	g_strfreev(extra_options);
	
	return job;
}

static guint xmi_msim_batch_multi_real_get_number_of_jobs(XmiMsimBatchAbstract *batch) {
	g_return_val_if_fail(XMI_MSIM_IS_BATCH_MULTI(batch), 0);

	return XMI_MSIM_BATCH_MULTI(batch)->xmsi_files->len;
}

static void xmi_msim_batch_multi_set_property(GObject *object, guint prop_id, const GValue *value,  GParamSpec *pspec) {

	XmiMsimBatchMulti *batch = XMI_MSIM_BATCH_MULTI(object);

	switch (prop_id) {
		case PROP_MULTI_XMSI_FILES:
			if (batch->xmsi_files)
				g_ptr_array_unref(batch->xmsi_files);
			batch->xmsi_files = g_value_dup_boxed(value);
      			break;
		case PROP_MULTI_OPTIONS:
			if (batch->options)
				g_ptr_array_unref(batch->options);
			batch->options = g_value_dup_boxed(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  	}
}

static void xmi_msim_batch_multi_get_property(GObject *object, guint prop_id, GValue *value,  GParamSpec *pspec) {

  XmiMsimBatchMulti *batch = XMI_MSIM_BATCH_MULTI(object);

  switch (prop_id) {
    case PROP_MULTI_XMSI_FILES:
      g_value_set_boxed(value, batch->xmsi_files);
      break;
    case PROP_MULTI_OPTIONS:
      g_value_set_boxed(value, batch->options);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
  }
}

XmiMsimBatchAbstract* xmi_msim_batch_multi_new(GPtrArray* xmsi_files, GPtrArray* options) {
	g_return_val_if_fail(xmsi_files != NULL && xmsi_files->len >= 1, NULL);
	g_return_val_if_fail(options != NULL && (options->len == 1 || options->len == xmsi_files->len), NULL);
	return g_object_new(XMI_MSIM_TYPE_BATCH_MULTI, "xmsi-files", xmsi_files, "options", options, NULL);
}