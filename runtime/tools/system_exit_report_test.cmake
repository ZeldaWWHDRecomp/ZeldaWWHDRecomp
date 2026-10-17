file(MAKE_DIRECTORY "${WORK}")
set(sources "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd")
file(GLOB_RECURSE stubs "${SOURCE}/runtime/tools/android_exit_stubs/*.java")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  ${stubs} "${sources}/SystemExitReport.java" "${sources}/CrashReportFiles.java" "${sources}/SystemExitReports.java"
  "${SOURCE}/runtime/tools/SystemExitReportTest.java" "${SOURCE}/runtime/tools/SystemExitCollectorTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "System-exit Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.SystemExitReportTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "System-exit Java tests failed")
endif()

execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.SystemExitCollectorTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "System-exit collector tests failed")
endif()
