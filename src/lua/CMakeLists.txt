target_sources(dosboxcommon PRIVATE
  lua_bridge_commands.cpp
  lua_coroutine.cpp
  lua_debug_log.cpp
  lua_engine.cpp
  script_validator.cpp
)

target_link_libraries(dosboxcommon PRIVATE
  unofficial::lua::lua
  sol2::sol2
)
