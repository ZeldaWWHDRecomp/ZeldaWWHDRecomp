// Windows: the setup runs tools\installer\setup.py with the official embeddable Python the release ships in
// tools\python (unmodified, signed by the PSF; tools/release/package.py). No download, no script host.
#pragma once
#ifdef _WIN32

#include <string>
#include <vector>

// <release>\tools\python\python.exe. pkg: the release folder, ending with a separator.
std::string bundled_python(const std::string& pkg);
bool have_bundled_python(const std::string& pkg);

// "Wind Waker HD.exe --console-setup ARGS" (tools\Setup in a console window.bat): runs setup.py ARGS with the
// bundled Python in the console the program was started from and returns setup.py's exit code (1 when the
// release folder or its Python is missing).
int console_setup(const std::string& pkg, const std::vector<std::string>& args);

#endif
