# Writes a C++ include with every game object recipe (runtime/assets/game_objects/*.recipe.json) as text:
#   cmake -DDIR=dir -DOUT=out.inc -P EmbedGameObjectRecipes.cmake
# defines `story_game_objects::kRecipes[]` (story_game_objects_catalogue.h). It runs when cmake configures
# (CMakeLists.txt re-runs it when a recipe changes), so every target that includes the catalogue header
# has it without a generated source of its own. A recipe holds our own words and references into the game
# only; the objects load from the player's game files. A new group of objects is a new recipe.
file(GLOB tables "${DIR}/*.recipe.json")
list(SORT tables)
set(body "")
foreach(table ${tables})
  file(READ "${table}" text)
  string(FIND "${text}" ")wwhd\"" clash)
  if(NOT clash EQUAL -1)
    message(FATAL_ERROR "${table} contains the raw string delimiter")
  endif()
  get_filename_component(name "${table}" NAME)
  string(APPEND body "    // ${name}\n    R\"wwhd(${text})wwhd\",\n")
endforeach()
string(APPEND body "    \"\",\n")
file(WRITE "${OUT}.tmp" "// generated from ${DIR} by cmake/EmbedGameObjectRecipes.cmake\nnamespace story_game_objects {\ninline const char* const kRecipes[] = {\n${body}};\n}  // namespace story_game_objects\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
