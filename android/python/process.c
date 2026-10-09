#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static double monotonic(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

/* No shell, Python fork, or writable executable is involved. By default stdout
   and stderr share one bounded pipe. Extraction requests separate streaming
   pipes, private stdin bytes and cooperative cancellation. */
static PyObject *run(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;
    PyObject *command, *environment = Py_None, *input = Py_None;
    PyObject *on_output = Py_None, *cancel = Py_None;
    double timeout = 0;
    int separate = 0;
    static char *names[] = {"command", "env", "timeout", "input", "on_output", "separate", "cancel", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OdOOpO", names,
            &command, &environment, &timeout, &input, &on_output, &separate, &cancel)) return NULL;
    if (timeout < 0 || !isfinite(timeout)) { PyErr_SetString(PyExc_ValueError, "invalid process timeout"); return NULL; }
    if ((on_output != Py_None && !PyCallable_Check(on_output)) || (cancel != Py_None && !PyCallable_Check(cancel))) {
        PyErr_SetString(PyExc_TypeError, "output/cancellation handlers must be callable"); return NULL;
    }
    char *input_bytes = NULL;
    Py_ssize_t input_size = 0;
    if (input != Py_None && PyBytes_AsStringAndSize(input, &input_bytes, &input_size) < 0) return NULL;
    if (input_size > 1024 * 1024) { PyErr_SetString(PyExc_ValueError, "native stdin exceeds 1 MiB"); return NULL; }
    PyObject *sequence = PySequence_Fast(command, "command must be a sequence of strings");
    if (!sequence) return NULL;
    Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    char **argv = calloc((size_t)count + 1, sizeof(char *));
    char **envp = NULL;
    PyObject *envstrings = NULL;
    PyObject *result = NULL;
    if (!argv) { PyErr_NoMemory(); goto cleanup; }
    for (Py_ssize_t i = 0; i < count; i++) {
        Py_ssize_t length;
        argv[i] = (char *)PyUnicode_AsUTF8AndSize(PySequence_Fast_GET_ITEM(sequence, i), &length);
        if (!argv[i]) goto cleanup;
        if ((Py_ssize_t)strlen(argv[i]) != length) { PyErr_SetString(PyExc_ValueError, "NUL in command"); goto cleanup; }
    }
    if (!count || argv[0][0] != '/') { PyErr_SetString(PyExc_ValueError, "absolute executable path required"); goto cleanup; }
    // Bionic can report a child's exec failure as status 127 rather than a
    // posix_spawn error. Catch missing/non-executable tools before spawning.
    if (access(argv[0], X_OK) < 0) { PyErr_SetFromErrno(PyExc_OSError); goto cleanup; }
    if (environment != Py_None) {
        if (!PyDict_Check(environment)) { PyErr_SetString(PyExc_TypeError, "env must be a dict"); goto cleanup; }
        envstrings = PyList_New(0);
        envp = calloc((size_t)PyDict_Size(environment) + 1, sizeof(char *));
        if (!envstrings || !envp) { PyErr_NoMemory(); goto cleanup; }
        Py_ssize_t position = 0, index = 0;
        PyObject *key, *value;
        while (PyDict_Next(environment, &position, &key, &value)) {
            const char *k = PyUnicode_AsUTF8(key), *v = PyUnicode_AsUTF8(value);
            if (!k || !v) goto cleanup;
            if (!*k || strchr(k, '=') || PyUnicode_FindChar(key, 0, 0, PY_SSIZE_T_MAX, 1) >= 0 ||
                PyUnicode_FindChar(value, 0, 0, PY_SSIZE_T_MAX, 1) >= 0) {
                PyErr_SetString(PyExc_ValueError, "invalid environment entry"); goto cleanup;
            }
            PyObject *entry = PyUnicode_FromFormat("%s=%s", k, v);
            if (!entry) goto cleanup;
            int added = PyList_Append(envstrings, entry);
            Py_DECREF(entry);
            if (added < 0) goto cleanup;
            envp[index] = (char *)PyUnicode_AsUTF8(PyList_GET_ITEM(envstrings, index));
            index++;
        }
    }
    const size_t limit = 4 * 1024 * 1024;
    int pipes[2][2] = {{-1, -1}, {-1, -1}}, incoming[2] = {-1, -1};
    char *output[2] = {NULL, NULL};
    size_t used[2] = {0, 0};
    int streams = separate ? 2 : 1;
    for (int stream = 0; stream < streams; stream++) {
        output[stream] = malloc(limit);
        if (!output[stream]) { PyErr_NoMemory(); goto descriptors; }
        if (pipe2(pipes[stream], O_CLOEXEC) < 0) { PyErr_SetFromErrno(PyExc_OSError); goto descriptors; }
    }
    if (input_size) {
        // A socket stdin supports MSG_NOSIGNAL: a tool closing stdin early
        // must never deliver SIGPIPE to the Android app's process.
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, incoming) < 0 ||
                fcntl(incoming[1], F_SETFL, O_NONBLOCK) < 0) {
            PyErr_SetFromErrno(PyExc_OSError); goto descriptors;
        }
    } else {
        incoming[0] = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (incoming[0] < 0) { PyErr_SetFromErrno(PyExc_OSError); goto descriptors; }
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_adddup2(&actions, incoming[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipes[0][1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipes[separate ? 1 : 0][1], STDERR_FILENO);
    for (int stream = 0; stream < streams; stream++) {
        posix_spawn_file_actions_addclose(&actions, pipes[stream][0]);
        posix_spawn_file_actions_addclose(&actions, pipes[stream][1]);
    }
    posix_spawn_file_actions_addclose(&actions, incoming[0]);
    if (incoming[1] >= 0) posix_spawn_file_actions_addclose(&actions, incoming[1]);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid;
    int error = posix_spawn(&pid, argv[0], &actions, &attributes, argv, envp ? envp : environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    for (int stream = 0; stream < streams; stream++) {
        close(pipes[stream][1]); pipes[stream][1] = -1;
    }
    close(incoming[0]); incoming[0] = -1;
    if (error) { errno = error; PyErr_SetFromErrno(PyExc_OSError); goto descriptors; }
    int status = 0, timed_out = 0, io_error = 0, callback_failed = 0, cancelled = 0;
    Py_ssize_t written = 0;
    double deadline = timeout ? monotonic() + timeout : 0;
    Py_BEGIN_ALLOW_THREADS
    int open_streams = streams, reaped = 0;
    while (open_streams || !reaped) {
        if (!timed_out && deadline && monotonic() >= deadline) { timed_out = 1; kill(-pid, SIGKILL); }
        if (cancel != Py_None && !cancelled && !callback_failed) {
            PyEval_RestoreThread(_save);
            PyObject *requested = PyObject_CallNoArgs(cancel);
            int stop = requested ? PyObject_IsTrue(requested) : -1;
            Py_XDECREF(requested);
            if (stop < 0) callback_failed = 1;
            else if (stop) cancelled = 1;
            _save = PyEval_SaveThread();
            if (cancelled || callback_failed) kill(-pid, SIGKILL);
        }
        struct pollfd ready[3] = {{pipes[0][0], POLLIN, 0}, {pipes[1][0], POLLIN, 0}, {incoming[1], POLLOUT, 0}};
        int polled = poll(ready, 3, open_streams ? 100 : 10);
        if (polled < 0 && errno != EINTR) { io_error = errno; kill(-pid, SIGKILL); break; }
        if (polled > 0 && incoming[1] >= 0 && ready[2].revents) {
            ssize_t bytes = send(incoming[1], input_bytes + written, (size_t)(input_size - written), MSG_NOSIGNAL);
            if (bytes > 0) written += bytes;
            else if (bytes < 0 && errno != EINTR && errno != EAGAIN && errno != EPIPE && errno != ECONNRESET) {
                io_error = errno; kill(-pid, SIGKILL); break;
            }
            if (written == input_size || (bytes < 0 && (errno == EPIPE || errno == ECONNRESET))) {
                close(incoming[1]); incoming[1] = -1;
            }
        }
        for (int stream = 0; stream < streams && polled > 0; stream++) {
            if (pipes[stream][0] < 0 || !ready[stream].revents) continue;
            char chunk[8192];
            ssize_t bytes = read(pipes[stream][0], chunk, sizeof(chunk));
            if (bytes == 0) { close(pipes[stream][0]); pipes[stream][0] = -1; open_streams--; }
            else if (bytes > 0) {
                size_t keep = (size_t)bytes < limit - used[stream] ? (size_t)bytes : limit - used[stream];
                memcpy(output[stream] + used[stream], chunk, keep); used[stream] += keep;
                if (on_output != Py_None && !callback_failed && !cancelled) {
                    PyEval_RestoreThread(_save);
                    PyObject *value = PyObject_CallFunction(on_output, "sy#",
                        separate ? (stream ? "stderr" : "stdout") : "combined", chunk, (Py_ssize_t)bytes);
                    if (!value) callback_failed = 1;
                    Py_XDECREF(value);
                    _save = PyEval_SaveThread();
                    if (callback_failed) kill(-pid, SIGKILL);
                }
            } else if (errno != EINTR) { io_error = errno; kill(-pid, SIGKILL); break; }
        }
        if (io_error) break;
        if (!reaped) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) reaped = 1;
            else if (waited < 0 && errno != EINTR) { io_error = errno; break; }
        }
    }
    if (!reaped) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    Py_END_ALLOW_THREADS
    if (callback_failed) { /* Preserve the Python callback's exception. */ }
    else if (cancelled) PyErr_SetString(PyExc_InterruptedError, "native tool was cancelled");
    else if (timed_out) PyErr_SetString(PyExc_TimeoutError, "native tool timed out");
    else if (io_error) { errno = io_error; PyErr_SetFromErrno(PyExc_OSError); }
    else {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
        if (separate) result = Py_BuildValue("iy#y#", code, output[0], (Py_ssize_t)used[0], output[1], (Py_ssize_t)used[1]);
        else result = Py_BuildValue("iy#", code, output[0], (Py_ssize_t)used[0]);
    }
descriptors:
    for (int stream = 0; stream < 2; stream++) {
        free(output[stream]);
        for (int end = 0; end < 2; end++) if (pipes[stream][end] >= 0) close(pipes[stream][end]);
    }
    for (int end = 0; end < 2; end++) if (incoming[end] >= 0) close(incoming[end]);
cleanup:
    free(argv); free(envp); Py_XDECREF(envstrings); Py_DECREF(sequence);
    return result;
}

static PyMethodDef methods[] = {
    {"run", (PyCFunction)(void (*)(void))run, METH_VARARGS | METH_KEYWORDS, "Run an absolute native tool; return (status, combined output)."},
    {NULL, NULL, 0, NULL}
};
static struct PyModuleDef module = {PyModuleDef_HEAD_INIT, "wwhd_native", NULL, -1, methods, NULL, NULL, NULL, NULL};
PyMODINIT_FUNC PyInit_wwhd_native(void) { return PyModule_Create(&module); }
