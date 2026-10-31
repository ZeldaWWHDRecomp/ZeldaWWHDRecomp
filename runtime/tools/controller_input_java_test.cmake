file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/ControllerInputPolicy.java"
  "${SOURCE}/runtime/tools/ControllerInputPolicyTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Controller input policy Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -ea -cp "${WORK}" org.wwhdrecomp.wwhd.ControllerInputPolicyTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Controller input policy test failed")
endif()
