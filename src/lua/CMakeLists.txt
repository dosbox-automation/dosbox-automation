target_sources(dosboxcommon PRIVATE
  lua_engine.cpp
  script_validator.cpp
)

target_link_libraries(dosboxcommon PRIVATE
  unofficial::lua::lua
  sol2::sol2
)
