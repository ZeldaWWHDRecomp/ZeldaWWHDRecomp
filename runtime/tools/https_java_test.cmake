file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/HttpsDownload.java"
  "${SOURCE}/runtime/tools/HttpsDownloadTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Java HTTPS test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" HttpsDownloadTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Java HTTPS transport test failed")
endif()
