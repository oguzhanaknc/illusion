# The game module showcase. USES gives it the headers and Components of engine libraries; their code runs in the editor
# and is reached through the host's services (see the scripting API reference).
illusion_add_module(showcase SHARED SOURCES showcase.cpp USES physics audio particles input render runtime)
