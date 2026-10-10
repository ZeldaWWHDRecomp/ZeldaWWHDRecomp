#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <dlfcn.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
PyMODINIT_FUNC PyInit_wwhd_native(void);

static void fail(JNIEnv *env, const char *message) {
    jclass type = (*env)->FindClass(env, "java/lang/IllegalStateException");
    if (type) (*env)->ThrowNew(env, type, message);
}

JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_AndroidGame_validate(
    JNIEnv *env, jclass cls, jstring library) {
    (void)cls;
    const char *path = (*env)->GetStringUTFChars(env, library, NULL);
    if (!path) return;
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    (*env)->ReleaseStringUTFChars(env, library, path);
    if (!handle) {
        fail(env, "Completed game library could not load with its runtime dependencies");
        return;
    }
    if (!dlsym(handle, "SDL_main")) fail(env, "Completed game library has no SDL entry point");
    dlclose(handle);
}

JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_EmbeddedPython_run(
    JNIEnv *env, jclass cls, jstring home, jstring source, jstring module, jstring arguments) {
    (void)cls;
    const char *h = (*env)->GetStringUTFChars(env, home, NULL);
    const char *s = (*env)->GetStringUTFChars(env, source, NULL);
    const char *m = (*env)->GetStringUTFChars(env, module, NULL);
    const char *a = (*env)->GetStringUTFChars(env, arguments, NULL);
    if (!h || !s || !m || !a) goto cleanup;
    pthread_mutex_lock(&lock);
    if (!initialized) {
        if (PyImport_AppendInittab("wwhd_native", PyInit_wwhd_native) < 0) {
            fail(env, "Cannot register native setup process bridge");
            pthread_mutex_unlock(&lock);
            goto cleanup;
        }
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.site_import = 0;
        config.write_bytecode = 0;
        config.install_signal_handlers = 0;
        PyStatus status = PyConfig_SetBytesString(&config, &config.home, h);
        if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
        if (PyStatus_Exception(status)) {
            fail(env, status.err_msg ? status.err_msg : "Python initialization failed");
            PyConfig_Clear(&config);
            pthread_mutex_unlock(&lock);
            goto cleanup;
        }
        PyConfig_Clear(&config);
        initialized = 1;
        PyEval_SaveThread();
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *path = PySys_GetObject("path");
    const char *suffixes[] = {"/tools/android", "/tools/recomp"};
    int ok = 1;
    for (unsigned i = 0; i < 2; i++) {
        PyObject *entry = PyUnicode_FromFormat("%s%s", s, suffixes[i]);
        if (!entry || PyList_Insert(path, 0, entry) < 0) ok = 0;
        Py_XDECREF(entry);
    }
    PyObject *json = ok ? PyImport_ImportModule("json") : NULL;
    PyObject *argv = json ? PyObject_CallMethod(json, "loads", "s", a) : NULL;
    PyObject *target = argv ? PyImport_ImportModule(m) : NULL;
    PyObject *result = target ? PyObject_CallMethod(target, "main", "O", argv) : NULL;
    if (!result) {
        PyErr_Print();
        fail(env, "Embedded Python task failed; inspect python.stderr in logcat");
    }
    Py_XDECREF(result);
    Py_XDECREF(target);
    Py_XDECREF(argv);
    Py_XDECREF(json);
    PyGILState_Release(gil);
    pthread_mutex_unlock(&lock);
cleanup:
    if (h) (*env)->ReleaseStringUTFChars(env, home, h);
    if (s) (*env)->ReleaseStringUTFChars(env, source, s);
    if (m) (*env)->ReleaseStringUTFChars(env, module, m);
    if (a) (*env)->ReleaseStringUTFChars(env, arguments, a);
}
