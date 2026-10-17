file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/TouchLayout.java"
  "${SOURCE}/runtime/tools/TouchLayoutTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Touch layout Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.TouchLayoutTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Touch layout bounds/persistence/editor test failed")
endif()
