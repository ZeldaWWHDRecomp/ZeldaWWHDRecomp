# The decomp build's function-list check (Decomp.cmake): GEN_DIR's decomp_hooks.txt (the list recomp.py translated
# the game code with) against this build's. Both come from tools/decomp_build/gen.py, whose header names the decomp
# commit ("# (canonical addresses; ...) from decomp <hash>"); a new commit with the same function list changes only
# that line, and the translated code does not depend on it. So the digest leaves out that header line; every other
# line (each function address, its name, any other comment) counts.
function(wwhd_decomp_hooks_digest path out)
  file(STRINGS "${path}" lines)
  set(body "")
  foreach(line IN LISTS lines)
    if(line MATCHES "^# .* from decomp [0-9a-f]+$")
      continue()
    endif()
    string(APPEND body "${line}\n")
  endforeach()
  string(SHA256 digest "${body}")
  set(${out} "${digest}" PARENT_SCOPE)
endfunction()
