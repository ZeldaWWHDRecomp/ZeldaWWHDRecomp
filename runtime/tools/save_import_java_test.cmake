file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/SessionLease.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/SafeSaveArchive.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/TransactionalSaveStore.java"
  "${SOURCE}/runtime/tools/SaveImportTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Save import Java test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -ea -cp "${WORK}" org.wwhdrecomp.wwhd.SaveImportTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Save import test failed")
endif()
