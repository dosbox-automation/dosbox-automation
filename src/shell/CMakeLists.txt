target_sources(dosboxcommon PRIVATE
  autoexec.cpp
  autoexec_archive.cpp
  command_line.cpp
  file_reader.cpp
  path_command.cpp
  prompt_command.cpp
  shell.cpp
  shell_args.cpp
  shell_batch.cpp
  shell_history.cpp
  shell_misc.cpp)

target_sources(dosbox PRIVATE shell_cmds.cpp)
