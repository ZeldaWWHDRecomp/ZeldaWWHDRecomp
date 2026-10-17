file(MAKE_DIRECTORY "${WORK}")
file(GLOB_RECURSE stubs "${SOURCE}/runtime/tools/phone_rumble_java_stubs/*.java")
execute_process(COMMAND "${JAVAC}" -d "${WORK}" ${stubs}
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/PhoneRumble.java"
  "${SOURCE}/runtime/tools/PhoneRumbleTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Phone rumble test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.PhoneRumbleTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Phone rumble behavior test failed")
endif()
