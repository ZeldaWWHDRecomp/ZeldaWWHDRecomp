# TWWHD Randomizer facts used by the port

[TWWHD Randomizer](https://github.com/SuperDude88/TWWHD-Randomizer) by SuperDude88 and contributors, MIT
(`LICENSE` here), commit cb7b1c9. No file of it is part of the port; Game progress uses these facts of its vanilla
data:

- the treasure charts' printed numbers and the Triforce Charts 1 to 3 by the chart's save number
  (`logic/data/item_data.yaml`, Game Item Id = 0xFF - save number): `runtime/src/mods/game_progress_names.h` kChartNo;
- as a check of the progress map's places (not shipped): the sea square of every Piece of Heart, chart and sunken
  treasure (`logic/data/location_data.yaml`, `area_names.yaml`, `logic/GameItem.cpp` roomNumToDefaultChart).
