file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/TouchLayout.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/ContextPack.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/StartupTouch.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/ContextAssets.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/GoldenRings.java"
  "${SOURCE}/runtime/tools/StartupTouchTest.java"
  "${SOURCE}/tools/android/ContextButtonsTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Startup touch Java test compilation failed")
endif()
foreach(test StartupTouchTest ContextButtonsTest)
  execute_process(COMMAND "${JAVA}" -ea -cp "${WORK}" "org.wwhdrecomp.wwhd.${test}" RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${test} failed")
  endif()
endforeach()
