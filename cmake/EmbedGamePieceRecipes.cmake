# Writes a C++ include with every game piece set recipe (runtime/assets/game_pieces/*.recipe.json) as text:
#   cmake -DDIR=dir -DOUT=out.inc -P EmbedGamePieceRecipes.cmake
# defines `story_game_pieces::kRecipes[]` (story_game_pieces_catalogue.h). It runs when cmake configures
# (CMakeLists.txt re-runs it when a recipe changes), so every target that includes the catalogue header
# has it without a generated source of its own. A recipe holds our own words and references into the game
# only; the pieces are built from the player's game files. A new set of pieces is a new recipe.
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
file(WRITE "${OUT}.tmp" "// generated from ${DIR} by cmake/EmbedGamePieceRecipes.cmake\nnamespace story_game_pieces {\ninline const char* const kRecipes[] = {\n${body}};\n}  // namespace story_game_pieces\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
