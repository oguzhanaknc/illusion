# The game module of the tech demo: first-person walking, the day cycle and night effects, the HUD and the
# pause menu.
illusion_add_module(techdemo SHARED SOURCES techdemo.cpp USES physics audio particles input render runtime ui)
