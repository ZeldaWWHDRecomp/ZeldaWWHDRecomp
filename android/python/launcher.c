/* The APK's Python as a program, for code mods (guest mods) on the phone.
 *
 * Android runs programs only from the app's native library folder, so this is installed there as
 * lib<name>.so like the compiler. The game starts it (through the code-mod build tools that
 * AndroidGuestTools writes) to translate a guest mod and run its preparation tools: the same
 * builder and arguments as on a computer. Its standard library is the extracted python-home
 * folder, named by WWHD_PYTHON_HOME; other PYTHON* variables are ignored. libpython3.14.so lies
 * next to this file ($ORIGIN). */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *home = getenv("WWHD_PYTHON_HOME");
    if (!home || home[0] != '/') {
        fputs("WWHD_PYTHON_HOME must name the extracted Python folder\n", stderr);
        return 2;
    }
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.use_environment = 0;
    config.user_site_directory = 0;
    config.write_bytecode = 0;  /* the extracted folders are compared with the APK's copies */
    PyStatus status = PyConfig_SetBytesString(&config, &config.home, home);
    if (!PyStatus_Exception(status)) status = PyConfig_SetBytesArgv(&config, argc, argv);
    if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) Py_ExitStatusException(status);
    return Py_RunMain();
}
