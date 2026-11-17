# The game module of the tech demo: first-person walking, the day cycle and night effects, the HUD and the
# pause menu with Save and Load.
illusion_add_module(techdemo SHARED SOURCES techdemo.cpp USES physics audio particles input render runtime ui save)
