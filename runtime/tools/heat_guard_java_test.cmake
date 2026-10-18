file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/HeatGuardDevice.java"
  "${SOURCE}/runtime/tools/HeatGuardDeviceTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Heat guard device Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.HeatGuardDeviceTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Heat guard device test failed")
endif()
