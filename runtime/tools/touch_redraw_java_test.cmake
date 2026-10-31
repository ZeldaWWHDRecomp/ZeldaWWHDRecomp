file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/TouchRedraw.java"
  "${SOURCE}/runtime/tools/TouchRedrawTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Touch redraw Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -ea -cp "${WORK}" org.wwhdrecomp.wwhd.TouchRedrawTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Touch redraw test failed")
endif()
